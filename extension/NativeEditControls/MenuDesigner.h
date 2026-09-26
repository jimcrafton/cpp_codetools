#pragma once

#include <newui/geometry.h>
#include <newui/controls.h>
#include <newui/menus.h>
#include <newui/namemanager.h>
#include <newui/subview.h>
#include <newui/undostack.h>

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_set>
#include <vector>

namespace CodeToolsVsix
{
    class MenuDesigner;

    // A name for a menu item that names doesn't hold yet, based on its caption, Delphi-style:
    // "Save &As..." -> "saveAs1", a separator -> "separator1", no usable letters -> "menuItem1".
    std::string uniqueMenuItemName(newui::NameManager& names, const std::string& caption, bool separator);
    // Reserves the name of every menu item in every MenuBar under view (view included).
    void reserveMenuItemNames(const newui::View& view, newui::NameManager& names);
    // Gives every item under parent that is unnamed, or whose name names already holds, a fresh
    // unique name, and reserves the rest - for a pasted copy of a menu.
    void uniquifyMenuItemNames(newui::MenuItem& parent, newui::NameManager& names);

    // One open menu on the design surface: parent's children plus a "Type Here" placeholder row,
    // drawn like a native dropdown. Clicking an item selects it (MenuDesigner::select()).
    class MenuColumnView : public newui::SubView
    {
    public:
        explicit MenuColumnView(MenuDesigner& designer);

        void setMenu(newui::MenuItem* parent, newui::MenuItem* highlighted);
        newui::MenuItem* menu() const { return parent_; }

        // Width/height the column needs for its items.
        newui::Size preferredSize() const;
        // Top of row index (index == item count is the placeholder), local space.
        float rowTop(std::size_t index) const;
        // Row under localPt: an item index, the item count for the placeholder, or npos.
        std::size_t rowAt(const newui::Point& localPt) const;

        void paint(BLContext& ctx) override;

        static constexpr std::size_t npos = static_cast<std::size_t>(-1);
        static constexpr float kRowHeight = 24.0f;
        static constexpr float kSeparatorHeight = 9.0f;

    private:
        float rowHeight(std::size_t index) const;
        newui::SyncReturn handleMouseDown(newui::View& sender, const newui::Point& pt,
            std::uint32_t btnMask, std::uint32_t keyMask);
        newui::SyncReturn handleMouseDblClick(newui::View& sender, const newui::Point& pt,
            std::uint32_t btnMask, std::uint32_t keyMask);

        MenuDesigner& designer_;
        newui::MenuItem* parent_ = nullptr;
        newui::MenuItem* highlighted_ = nullptr;
    };

    // The "Type Here" box after a MenuBar's last menu - a click starts a new top-level menu.
    class MenuBarPlaceholderView : public newui::SubView
    {
    public:
        explicit MenuBarPlaceholderView(MenuDesigner& designer);
        void paint(BLContext& ctx) override;

    private:
        newui::SyncReturn handleMouseDown(newui::View& sender, const newui::Point& pt,
            std::uint32_t btnMask, std::uint32_t keyMask);

        MenuDesigner& designer_;
    };

    // A plain filled rect - the drop-position line while dragging, and the translucent highlight on
    // the open menu's bar button.
    class MenuMarkView : public newui::SubView
    {
    public:
        MenuMarkView(const char* name, std::uint32_t alpha);
        void paint(BLContext& ctx) override;

    private:
        std::uint32_t alpha_;
    };

    // Delphi-style menu editing on the design surface. While a MenuBar is selected it shows a
    // placeholder after its last menu; once a menu item is selected, it shows that item's menu and
    // every open submenu on the way to it as columns. The views live in host (the canvas well, not
    // the document), so they are never saved and never appear in the outline.
    class MenuDesigner
    {
    public:
        explicit MenuDesigner(newui::View* host);
        ~MenuDesigner();

        // Shows bar's menus with selected (nullptr: none, just the bar) selected.
        void open(newui::MenuBar* bar, newui::MenuItem* selected = nullptr);
        void close();
        bool isOpen() const { return bar_ != nullptr; }
        newui::MenuBar* menuBar() const { return bar_; }
        newui::MenuItem* selectedItem() const { return selected_; }

        // Selects item (must belong to menuBar(), or nullptr) and notifies the handler.
        void select(newui::MenuItem* item);

        // Re-lays the columns after the menu tree, an item's text or the bar's position changed.
        void refresh();

        // The top-level menu whose bar button contains rootPt, or nullptr.
        newui::MenuItem* topLevelMenuAt(const newui::Point& rootPt) const;
        // Whether rootPt is on one of this designer's views.
        bool contains(const newui::Point& rootPt) const;

        using SelectionChangedHandler = std::function<void(newui::MenuItem* item)>;
        void setSelectionChangedHandler(SelectionChangedHandler handler) { selectionChangedHandler_ = std::move(handler); }

        // Edits go through undoStack (nullptr: applied directly); changedHandler runs after each
        // one and each undo/redo of it.
        void setUndoStack(newui::UndoStack* undoStack) { undoStack_ = undoStack; }
        void setChangedHandler(std::function<void()> handler) { changedHandler_ = std::move(handler); }

        // One undoable step each. insertItem() adds a new item at index under parent (bar root for a
        // top-level menu); text "-" makes a separator. The inserted item is detached, not deleted,
        // on undo. Each item gets a unique name from its caption ("Save As..." -> "saveAs1") through
        // the root's NameManager; a provisionalName one ("New Item" placeholders) is renamed from
        // the caption it's first given.
        newui::MenuItem* insertItem(newui::MenuItem* parent, std::size_t index, const std::string& text,
            bool provisionalName = false);
        void renameItem(newui::MenuItem* item, const std::string& text);

        // In-place text entry over parent's child at index (renaming it), or over parent's "Type
        // Here" placeholder when index is its child count (creating an item there). Enter commits
        // and moves on to the next placeholder, Esc cancels, losing focus commits.
        void beginEdit(newui::MenuItem* parent, std::size_t index);
        // continueEditing: move on to the next placeholder rather than closing the field.
        void commitEdit(bool continueEditing);
        void cancelEdit();
        bool isEditing() const { return editParent_ != nullptr; }

        // Structural edits on the selection's menu tree - each one undoable step.
        // deleteItem() removes item and its submenu (detached, not deleted, so undo can restore it)
        // and selects its next sibling, else the previous one, else its parent.
        void deleteItem(newui::MenuItem* item);
        // A "New Item" above item (a separator with separator), selected; a new item starts renaming.
        newui::MenuItem* insertBefore(newui::MenuItem* item, bool separator = false);
        // Selects item's first submenu item, creating one (renaming it) if item has no submenu.
        void createSubmenu(newui::MenuItem* item);

        // Arrow-key navigation from the selection: Up/Down within its menu (skipping separators,
        // wrapping), Right into its submenu, Left out to its parent - at the top level Left/Right
        // move between the bar's menus.
        enum class Direction { Up, Down, Left, Right };
        void moveSelection(Direction direction);

        // The designer's keys while an item is selected (Del, Ins, Ctrl+Right, arrows, Enter/F2,
        // Esc) - true if handled.
        bool handleKeyDown(std::uint32_t keyMask, std::uint32_t vkCode);
        // Blocks in a native popup of the item commands for item, at rootPt.
        void showContextMenu(newui::MenuItem* item, const newui::Point& rootPt);

        // One undoable step: item becomes newParent's child at index (counted before the move, so
        // "just before child i" is i). Refused for a drop into item itself or its own submenu.
        void moveItem(newui::MenuItem* item, newui::MenuItem* newParent, std::size_t index);

        // Drag and drop. armDrag() on a press; dragTo()/endDrag() with the root-space mouse
        // position (DesignerEditor forwards every move/release while isDragging()). A drag starts
        // after a few pixels; before that, the press was just a click. Hovering a bar menu while
        // dragging opens it.
        struct DropTarget
        {
            newui::MenuItem* parent = nullptr;
            std::size_t index = 0;
            newui::Rect markRoot;   // where the drop line goes, root space
        };
        void armDrag(newui::MenuItem* item, const newui::Point& rootPt);
        bool isDragging() const { return dragItem_ != nullptr; }
        bool isDragActive() const { return dragActive_; }
        void dragTo(const newui::Point& rootPt);
        void endDrag(const newui::Point& rootPt);
        void cancelDrag();
        // Where dropping the dragged item at rootPt would put it - nullopt if nowhere valid.
        std::optional<DropTarget> dropTargetAt(const newui::Point& rootPt) const;
        MenuMarkView* dropMark() const { return dropMark_; }
        MenuMarkView* barHighlight() const { return barHighlight_; }
        newui::TextField* editField() const { return editField_; }

        // The visible columns, outermost first - for tests.
        std::vector<MenuColumnView*> visibleColumns() const;
        MenuBarPlaceholderView* barPlaceholder() const { return barPlaceholder_; }

    private:
        MenuColumnView* columnAt(std::size_t index);
        newui::Rect toHost(const newui::Rect& rootRect) const;
        bool isBarRoot(const newui::MenuItem* item) const { return bar_ != nullptr && item == &bar_->root(); }
        // Where the edit field goes for editParent_/editIndex_, in host space - nullopt if not shown.
        std::optional<newui::Rect> editRect() const;
        void positionEditField();
        void endEditField();   // hides the field and drops its focus
        void runAction(newui::UndoableAction action);
        // A NameManager-unique name based on caption.
        std::string uniqueName(const std::string& caption, bool separator) const;
        // Reserves every name under parent and names the unnamed ones (a loaded file's items keep theirs).
        void claimNames(newui::MenuItem* parent);
        void notifyChanged();

        newui::SyncReturn handleEditReturn(newui::TextField& sender);
        newui::SyncReturn handleEditKeyDown(newui::View& sender, std::uint32_t keyMask,
            int keyCharVal, int repeatCount, std::uint32_t VKeyCode);
        newui::SyncReturn handleEditLostFocus(newui::View& sender);

        newui::View* host_;
        newui::MenuBar* bar_ = nullptr;
        newui::MenuItem* selected_ = nullptr;
        // Reused, never deleted while open: a column can be refreshed from inside its own click.
        std::vector<MenuColumnView*> columns_;
        MenuBarPlaceholderView* barPlaceholder_ = nullptr;
        SelectionChangedHandler selectionChangedHandler_;
        newui::UndoStack* undoStack_ = nullptr;
        std::function<void()> changedHandler_;

        // Reused and hidden rather than deleted - it can end an edit from inside its own handlers.
        newui::TextField* editField_ = nullptr;
        newui::MenuItem* editParent_ = nullptr;
        std::size_t editIndex_ = 0;
        std::unordered_set<newui::MenuItem*> provisionalNames_;
        newui::MenuItem* dragItem_ = nullptr;
        newui::Point dragStart_;
        bool dragActive_ = false;
        MenuMarkView* dropMark_ = nullptr;
        MenuMarkView* barHighlight_ = nullptr;

        // Cleared in the destructor; guards showContextMenu()'s posted commands.
        std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    };
}
