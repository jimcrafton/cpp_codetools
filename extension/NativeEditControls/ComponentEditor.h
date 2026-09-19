#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

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
        virtual void edit() {}  // double-click default - see §7 step 3's scope note

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
        // What every mutating verb runs its change through: pushed onto undoStack() if one is
        // attached (push() calls doIt() immediately), otherwise doIt() is just called directly.
        void runAction(newui::UndoableAction action);

        newui::View* view_;

    private:
        newui::UndoStack* undoStack_ = nullptr;
        PostExecuteSync postExecuteSync_;
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

    private:
        void addTab();
        void removeLastTab();
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

        // Registers the editors this DLL ships (TabControlEditor). Needs reflection data already
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
