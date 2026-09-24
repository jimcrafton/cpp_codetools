#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <newui/layout.h>
#include <newui/models.h>
#include <newui/reflection.h>
#include <newui/undostack.h>
#include <newui/view.h>

namespace CodeToolsVsix
{
    // Design-time editor for one live newui::View - governs the design
    // surface's context menu (verbs) and double-click default action
    // (edit()). See bluesky/designer-plan.md §4.2 (same placement
    // reasoning as PropertyEditor.h - design-time-only, single consumer,
    // deliberately not in newui).
    class ComponentEditor
    {
    public:
        explicit ComponentEditor(newui::View* view) : view_(view) {}
        virtual ~ComponentEditor() = default;

        virtual std::size_t verbCount() const { return 0; }
        virtual std::string verb(std::size_t index) const { return {}; }
        virtual void executeVerb(std::size_t index) {}

        // Property names from view() down to the property a double-click edits, e.g. {"text"} or
        // {"model", "items"}. Empty means this editor has no default property.
        virtual std::vector<std::string> defaultPropertyPath() const { return {}; }

        // Whether a double-click has anything to open.
        virtual bool canEdit() const { return !defaultPropertyPath().empty(); }

        // Double-click default: hands defaultPropertyPath() to the handler (DesignerEditor opens it
        // in the Properties grid). Does nothing without a path or a handler.
        virtual void edit();

        // edit() with the double-click's root-local point, for editors whose target depends on
        // where the click landed (a TabControl's tab). Defaults to edit().
        virtual void editAt(const newui::Point& rootPt) { edit(); }

        using EditPropertyHandler = std::function<void(newui::View* view, const std::vector<std::string>& path)>;
        void setEditPropertyHandler(EditPropertyHandler handler) { editPropertyHandler_ = std::move(handler); }

        newui::View* view() const { return view_; }

        // Attaches the UndoStack executeVerb() pushes through - nullptr (the default) means "run
        // directly, no undo", same contract as PropertyEditor::setUndoStack().
        void setUndoStack(newui::UndoStack* undoStack) { undoStack_ = undoStack; }
        newui::UndoStack* undoStack() const { return undoStack_; }

        // Runs after a verb's action executes, in both doIt and undoIt (so it stays in lockstep
        // with undo/redo, like PropertyEditor::setPostCommitSync()). DesignerEditor uses it to
        // refresh the outline model, mark the document dirty and repaint.
        using PostExecuteSync = std::function<void()>;
        void setPostExecuteSync(PostExecuteSync sync) { postExecuteSync_ = std::move(sync); }

    protected:
        // Hands target and path to the edit handler, if any - target may be a child of view().
        void requestEditProperty(newui::View* target, const std::vector<std::string>& path);

        // What every mutating verb runs its change through: pushed onto undoStack() if one is
        // attached (push() calls doIt() immediately), otherwise doIt() is just called directly.
        void runAction(newui::UndoableAction action);

        newui::View* view_;

    private:
        newui::UndoStack* undoStack_ = nullptr;
        PostExecuteSync postExecuteSync_;
        EditPropertyHandler editPropertyHandler_;
    };

    // No verbs, only a double-click default property - for controls like Button ("text") or Image
    // ("imagePath") that have nothing to add to the context menu.
    class DefaultPropertyEditor : public ComponentEditor
    {
    public:
        DefaultPropertyEditor(newui::View* view, std::vector<std::string> path)
            : ComponentEditor(view), path_(std::move(path)) {}

        std::vector<std::string> defaultPropertyPath() const override { return path_; }

    private:
        std::vector<std::string> path_;
    };

    // "Add Tab" / "Remove Last Tab" verbs for a newui::TabControl (view() must be one). Both are
    // undoable through undoStack(). A tab removed or undone-away is detached, not destroyed - the
    // action holds it for redo, and leaks it if the action is discarded while detached (the same
    // accepted gap DesignerEditor's own Delete has).
    class TabControlEditor : public ComponentEditor
    {
    public:
        using ComponentEditor::ComponentEditor;

        std::size_t verbCount() const override;
        std::string verb(std::size_t index) const override;
        void executeVerb(std::size_t index) override;

        // Edits a tab page's "title": the tab under rootPt (switching to it), else the current one.
        bool canEdit() const override;
        void edit() override;
        void editAt(const newui::Point& rootPt) override;

    private:
        void addTab();
        void removeLastTab();
    };

    // "Add Button" / "Add Separator" / "Remove Last Item" verbs for a newui::Toolbar (view() must
    // be one). Each is undoable; a removed or undone-away item is detached, not destroyed (same
    // accepted leak-if-discarded gap as TabControlEditor).
    class ToolbarEditor : public ComponentEditor
    {
    public:
        using ComponentEditor::ComponentEditor;

        std::size_t verbCount() const override;
        std::string verb(std::size_t index) const override;
        void executeVerb(std::size_t index) override;

    private:
        void addButton();
        void addSeparator();
        void removeLastItem();
    };

    // "Add Pane" / "Remove Last Pane" / "Swap Panes" verbs for a newui::Splitter (view() must be
    // one). A Splitter arranges exactly two children: Add Pane shows only while it has fewer than
    // two, Swap Panes only while it has both. Each is undoable; panes are detached, not destroyed.
    class SplitterEditor : public ComponentEditor
    {
    public:
        using ComponentEditor::ComponentEditor;

        std::size_t verbCount() const override;
        std::string verb(std::size_t index) const override;
        void executeVerb(std::size_t index) override;

    private:
        enum class Verb { AddPane, RemovePane, SwapPanes };
        std::vector<Verb> applicableVerbs() const;
        void addPane();
        void removeLastPane();
        void swapPanes();
    };

    // "Add Item" / "Remove Last Item" verbs for a newui::ListView or newui::DropDownList (view() must
    // be one) whose model is a StringListModel, or has none yet - Add Item then attaches a new
    // StringListModel first, and its undo removes it again. A view showing some other kind of model
    // gets no verbs. The model is the view controller's, so items go through it (onChanged reaches
    // the view); each verb is one undoable step.
    class ListModelEditor : public ComponentEditor
    {
    public:
        using ComponentEditor::ComponentEditor;

        std::size_t verbCount() const override;
        std::string verb(std::size_t index) const override;
        void executeVerb(std::size_t index) override;
        std::vector<std::string> defaultPropertyPath() const override { return { "model", "items" }; }

    private:
        newui::StringListModel* stringModel() const;
        bool applicable() const;
        void addItem();
        void removeLastItem();
    };

    // "Add Item" / "Remove Last Item" verbs for a newui::TreeView (view() must be one) whose model is
    // a StringTreeModel, or has none yet - same shape as ListModelEditor, just newui::TreeView/
    // StringTreeModel in place of newui::ListView-or-DropDownList/StringListModel. Add Item always
    // appends a new root-level row (never a child of the current selection - there's no selection
    // concept at this level); the model's own "rows" text editor (Properties grid) is where nesting
    // is actually set. Remove Last Item drops the last row outright, regardless of its depth.
    class TreeModelEditor : public ComponentEditor
    {
    public:
        using ComponentEditor::ComponentEditor;

        std::size_t verbCount() const override;
        std::string verb(std::size_t index) const override;
        void executeVerb(std::size_t index) override;
        std::vector<std::string> defaultPropertyPath() const override { return { "model", "rows" }; }

    private:
        newui::StringTreeModel* stringModel() const;
        bool applicable() const;
        void addItem();
        void removeLastItem();
    };

    // Row / column verbs for a view whose Layout is a GridLayout (view() is the container, not the
    // layout - a layout is not a View). Add Row / Add Column append a star track; Remove Last Row /
    // Column appear only while there is one to remove. Each is one undoable step. Children keep
    // their cell numbers: one in a removed track is left where it was, as GridLayout already does
    // for a cell outside its tracks.
    class GridLayoutEditor : public ComponentEditor
    {
    public:
        using ComponentEditor::ComponentEditor;

        std::size_t verbCount() const override;
        std::string verb(std::size_t index) const override;
        void executeVerb(std::size_t index) override;
        std::vector<std::string> defaultPropertyPath() const override { return { "layout", "rows" }; }

    private:
        enum class Verb { AddRow, AddColumn, RemoveRow, RemoveColumn };
        std::vector<Verb> applicableVerbs() const;
        newui::GridLayout* grid() const;
        void changeTracks(bool rows, bool add);
    };

    // Keyed on const reflection::Class* alone - no generic/wildcard
    // ComponentEditor exists the way PropertyEditor has generic bool/int/
    // float/string editors, since there's no meaningful "default" verb set
    // for an arbitrary View. createEditor() walks owningClass's own
    // parentClass() chain (most-derived first) for the first exact match,
    // same order Class::allProperties() already uses - a control with no
    // editor of its own can inherit its base class's.
    class ComponentEditorRegistry
    {
    public:
        using Factory = std::function<std::unique_ptr<ComponentEditor>(newui::View*)>;

        static ComponentEditorRegistry& instance();

        void registerEditor(const newui::reflection::Class* owningClass, Factory factory);

        // Registers the editors this DLL ships (TabControl, Toolbar, Splitter). Needs reflection data already
        // registered (classinfo() lookups); guarded like PropertyEditorRegistry's, so calling it
        // twice on one instance is harmless.
        void registerBuiltinEditors();

        // nullptr if no class in owningClass's parentClass() chain has a
        // registered editor.
        std::unique_ptr<ComponentEditor> createEditor(const newui::reflection::Class* owningClass,
                                                        newui::View* view) const;

    private:
        std::vector<std::pair<const newui::reflection::Class*, Factory>> entries_;
        bool builtinsRegistered_ = false;
    };
}
