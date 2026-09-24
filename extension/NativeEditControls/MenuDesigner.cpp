#include "MenuDesigner.h"
#include "PaintUtils.h"
#include "SelectionOverlay.h"
#include "TextEncoding.h"

#include <newui/fontmanager.h>
#include <newui/keyboard_constants.h>
#include <newui/mouse_constants.h>
#include <newui/runloop.h>
#include <newui/rootview.h>
#include <newui/uicolormanager.h>

#include <algorithm>

namespace CodeToolsVsix
{
    namespace
    {
        constexpr float kTextLeft = 28.0f;      // check-mark gutter
        constexpr float kShortcutGap = 32.0f;
        constexpr float kArrowWidth = 16.0f;
        constexpr float kRightPadding = 10.0f;
        constexpr float kMinColumnWidth = 140.0f;
        constexpr float kBarPlaceholderWidth = 80.0f;
        const char* const kTypeHere = "Type Here";

        newui::Color roleColor(newui::UIColorRole role)
        {
            return newui::UIColorManager::colorFor(role);
        }

        BLRgba32 withAlpha(const newui::Color& color, std::uint32_t alpha)
        {
            BLRgba32 c = color.toBLRgba32();
            return BLRgba32(c.r(), c.g(), c.b(), alpha);
        }

        double textWidth(const std::string& text)
        {
            newui::Font font = newui::FontManager::getSystemFont(newui::SystemUIFont::Menu);
            BLFont* blFont = font.blFont();
            return blFont != nullptr && blFont->is_valid() ? measureTextWidth(*blFont, text) : 7.0 * text.size();
        }

        void strokeDashedRect(BLContext& ctx, const newui::Rect& r, BLRgba32 color)
        {
            ctx.save();
            ctx.set_stroke_style(color);
            ctx.set_stroke_width(1.0);
            const double dash = 3.0;
            const double left = r.left() + 0.5, top = r.top() + 0.5, right = r.right() - 0.5, bottom = r.bottom() - 0.5;
            for (double x = left; x < right; x += dash * 2) {
                const double end = x + dash < right ? x + dash : right;
                ctx.stroke_line(x, top, end, top);
                ctx.stroke_line(x, bottom, end, bottom);
            }
            for (double y = top; y < bottom; y += dash * 2) {
                const double end = y + dash < bottom ? y + dash : bottom;
                ctx.stroke_line(left, y, left, end);
                ctx.stroke_line(right, y, right, end);
            }
            ctx.restore();
        }

        std::vector<newui::MenuItem*> pathFromTopLevel(newui::MenuBar* bar, newui::MenuItem* item)
        {
            std::vector<newui::MenuItem*> path;
            for (newui::MenuItem* it = item; it != nullptr && it != &bar->root(); it = it->parent()) {
                path.push_back(it);
            }
            std::reverse(path.begin(), path.end());
            return path;
        }
    }

    // --- MenuColumnView ---------------------------------------------------------------------

    MenuColumnView::MenuColumnView(MenuDesigner& designer) : designer_(designer)
    {
        setName("menuDesignerColumn");
        onMouseDown.add(this, &MenuColumnView::handleMouseDown);
        onMouseDblClick.add(this, &MenuColumnView::handleMouseDblClick);
    }

    void MenuColumnView::setMenu(newui::MenuItem* parent, newui::MenuItem* highlighted)
    {
        parent_ = parent;
        highlighted_ = highlighted;
        style().markDirty();
    }

    float MenuColumnView::rowHeight(std::size_t index) const
    {
        const auto& items = parent_->children();
        return index < items.size() && items[index]->isSeparator() ? kSeparatorHeight : kRowHeight;
    }

    float MenuColumnView::rowTop(std::size_t index) const
    {
        float top = 2.0f;
        for (std::size_t i = 0; i < index; ++i) {
            top += rowHeight(i);
        }
        return top;
    }

    std::size_t MenuColumnView::rowAt(const newui::Point& localPt) const
    {
        if (parent_ == nullptr) {
            return npos;
        }
        const std::size_t count = parent_->children().size();
        for (std::size_t i = 0; i <= count; ++i) {
            const float top = rowTop(i);
            if (localPt.y >= top && localPt.y < top + rowHeight(i)) {
                return i;
            }
        }
        return npos;
    }

    newui::Size MenuColumnView::preferredSize() const
    {
        if (parent_ == nullptr) {
            return newui::Size();
        }
        double textMax = textWidth(kTypeHere);
        double shortcutMax = 0.0;
        for (newui::MenuItem* item : parent_->children()) {
            const double w = textWidth(newui::stripMnemonics(item->text()));
            textMax = w > textMax ? w : textMax;
            const double s = item->shortcutText().empty() ? 0.0 : textWidth(item->shortcutText());
            shortcutMax = s > shortcutMax ? s : shortcutMax;
        }
        float width = kTextLeft + static_cast<float>(textMax)
            + (shortcutMax > 0.0 ? kShortcutGap + static_cast<float>(shortcutMax) : 0.0f) + kArrowWidth + kRightPadding;
        width = width < kMinColumnWidth ? kMinColumnWidth : width;
        return newui::Size(width, rowTop(parent_->children().size()) + kRowHeight + 2.0f);
    }

    void MenuColumnView::paint(BLContext& ctx)
    {
        if (parent_ == nullptr) {
            return;
        }
        const newui::Size size = bounds().size();
        const newui::Color text = roleColor(newui::UIColorRole::WindowText);
        const newui::Color dim = roleColor(newui::UIColorRole::DisabledText);
        const newui::Color highlight = roleColor(newui::UIColorRole::HighlightBackground);
        const newui::Color highlightText = roleColor(newui::UIColorRole::HighlightText);

        ctx.save();
        ctx.set_fill_style(roleColor(newui::UIColorRole::WindowBackground).toBLRgba32());
        ctx.fill_rect(0.0, 0.0, size.width, size.height);
        ctx.set_stroke_style(withAlpha(dim, 160));
        ctx.stroke_rect(0.5, 0.5, size.width - 1.0, size.height - 1.0);

        const auto& items = parent_->children();
        for (std::size_t i = 0; i < items.size(); ++i) {
            newui::MenuItem* item = items[i];
            const newui::Rect row(2.0f, rowTop(i), size.width - 4.0f, rowHeight(i));
            if (item->isSeparator()) {
                const double y = row.top() + row.size().height * 0.5;
                ctx.set_stroke_style(withAlpha(dim, 140));
                ctx.stroke_line(row.left() + kTextLeft - 4.0, y, row.right() - 4.0, y);
                continue;
            }

            const bool selected = item == designer_.selectedItem();
            const bool onPath = item == highlighted_;
            if (selected) {
                ctx.set_fill_style(highlight.toBLRgba32());
                ctx.fill_rect(row.left(), row.top(), row.size().width, row.size().height);
            } else if (onPath) {
                ctx.set_fill_style(withAlpha(highlight, 70));
                ctx.fill_rect(row.left(), row.top(), row.size().width, row.size().height);
            }
            const newui::Color rowText = selected ? highlightText : text;

            if (item->isChecked()) {
                const double cx = row.left() + 10.0, cy = row.top() + row.size().height * 0.5;
                ctx.set_stroke_style(rowText.toBLRgba32());
                ctx.set_stroke_width(1.6);
                ctx.stroke_line(cx - 4.0, cy, cx - 1.0, cy + 3.5);
                ctx.stroke_line(cx - 1.0, cy + 3.5, cx + 5.0, cy - 4.0);
                ctx.set_stroke_width(1.0);
            }

            const float textRight = row.right() - kArrowWidth;
            paintText(ctx, newui::Rect(row.left() + kTextLeft, row.top(), textRight - row.left() - kTextLeft, row.size().height),
                newui::stripMnemonics(item->text()), rowText, newui::SystemUIFont::Menu);
            if (!item->shortcutText().empty()) {
                const float w = static_cast<float>(textWidth(item->shortcutText()));
                paintText(ctx, newui::Rect(textRight - w, row.top(), w + 1.0f, row.size().height),
                    item->shortcutText(), selected ? highlightText : dim, newui::SystemUIFont::Menu);
            }
            if (item->hasChildren()) {
                const double ax = row.right() - 9.0, ay = row.top() + row.size().height * 0.5;
                BLPath arrow;
                arrow.move_to(ax - 3.0, ay - 4.0);
                arrow.line_to(ax + 1.0, ay);
                arrow.line_to(ax - 3.0, ay + 4.0);
                arrow.close();
                ctx.set_fill_style(rowText.toBLRgba32());
                ctx.fill_path(arrow);
            }
        }

        const newui::Rect placeholder(4.0f, rowTop(items.size()) + 2.0f, size.width - 8.0f, kRowHeight - 4.0f);
        strokeDashedRect(ctx, placeholder, withAlpha(dim, 200));
        paintText(ctx, newui::Rect(placeholder.left() + kTextLeft - 4.0f, placeholder.top(),
            placeholder.size().width - kTextLeft, placeholder.size().height), kTypeHere, dim, newui::SystemUIFont::Menu);
        ctx.restore();
    }

    newui::SyncReturn MenuColumnView::handleMouseDown(newui::View& /*sender*/, const newui::Point& pt,
        std::uint32_t btnMask, std::uint32_t /*keyMask*/)
    {
        const std::size_t row = rowAt(pt);
        if (parent_ == nullptr || row == npos) {
            return newui::SyncReturn::Handled;
        }
        if ((btnMask & newui::mbmRightButton) != 0) {
            if (row < parent_->children().size()) {
                newui::MenuItem* item = parent_->children()[row];
                designer_.select(item);
                const newui::Rect own = SelectionOverlay::boundsInRootView(this);
                designer_.showContextMenu(item, newui::Point(own.left() + pt.x, own.top() + pt.y));
            }
            return newui::SyncReturn::Handled;
        }
        if (row == parent_->children().size()) {
            designer_.beginEdit(parent_, row);   // Type Here
        } else {
            newui::MenuItem* item = parent_->children()[row];
            if (!item->isSeparator()) {
                designer_.select(item);
            }
            const newui::Rect own = SelectionOverlay::boundsInRootView(this);
            designer_.armDrag(item, newui::Point(own.left() + pt.x, own.top() + pt.y));
        }
        return newui::SyncReturn::Handled;
    }

    newui::SyncReturn MenuColumnView::handleMouseDblClick(newui::View& /*sender*/, const newui::Point& pt,
        std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/)
    {
        const std::size_t row = rowAt(pt);
        if (parent_ != nullptr && row != npos) {
            designer_.beginEdit(parent_, row);   // rename, or Type Here
        }
        return newui::SyncReturn::Handled;
    }

    // --- MenuBarPlaceholderView -------------------------------------------------------------

    MenuBarPlaceholderView::MenuBarPlaceholderView(MenuDesigner& designer) : designer_(designer)
    {
        setName("menuDesignerBarPlaceholder");
        onMouseDown.add(this, &MenuBarPlaceholderView::handleMouseDown);
    }

    newui::SyncReturn MenuBarPlaceholderView::handleMouseDown(newui::View& /*sender*/, const newui::Point& /*pt*/,
        std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/)
    {
        if (newui::MenuBar* bar = designer_.menuBar()) {
            designer_.beginEdit(&bar->root(), bar->menus().size());
        }
        return newui::SyncReturn::Handled;
    }

    void MenuBarPlaceholderView::paint(BLContext& ctx)
    {
        const newui::Size size = bounds().size();
        const newui::Color dim = roleColor(newui::UIColorRole::DisabledText);
        strokeDashedRect(ctx, newui::Rect(0.0f, 0.0f, size.width, size.height), withAlpha(dim, 200));
        paintText(ctx, newui::Rect(8.0f, 0.0f, size.width - 12.0f, size.height), kTypeHere, dim, newui::SystemUIFont::Menu);
    }

    // --- MenuDesigner -----------------------------------------------------------------------

    MenuDesigner::MenuDesigner(newui::View* host) : host_(host) {}

    MenuDesigner::~MenuDesigner()
    {
        *alive_ = false;
        // Not close(): that commits an open edit, which would call back into the editor.
        dragItem_ = nullptr;
        editParent_ = nullptr;
        bar_ = nullptr;
        selected_ = nullptr;
        std::vector<newui::SubView*> views(columns_.begin(), columns_.end());
        if (barPlaceholder_ != nullptr) {
            views.push_back(barPlaceholder_);
        }
        if (editField_ != nullptr) {
            views.push_back(editField_);
        }
        if (dropMark_ != nullptr) {
            views.push_back(dropMark_);
        }
        if (barHighlight_ != nullptr) {
            views.push_back(barHighlight_);
        }
        for (newui::SubView* view : views) {
            if (view->parent() != nullptr) {
                view->parent()->removeChild(view);
            }
            view->destroy();
            delete view;
        }
    }

    void MenuDesigner::open(newui::MenuBar* bar, newui::MenuItem* selected)
    {
        bar_ = bar;
        selected_ = bar != nullptr ? selected : nullptr;
        refresh();
    }

    void MenuDesigner::close()
    {
        cancelDrag();
        if (isEditing()) {
            commitEdit(false);
        }
        bar_ = nullptr;
        selected_ = nullptr;
        refresh();
    }

    void MenuDesigner::select(newui::MenuItem* item)
    {
        if (bar_ == nullptr) {
            return;
        }
        selected_ = item;
        refresh();
        if (selectionChangedHandler_) {
            selectionChangedHandler_(item);
        }
    }

    MenuColumnView* MenuDesigner::columnAt(std::size_t index)
    {
        while (columns_.size() <= index) {
            auto* column = new MenuColumnView(*this);
            host_->addChild(column);
            columns_.push_back(column);
        }
        return columns_[index];
    }

    newui::Rect MenuDesigner::toHost(const newui::Rect& rootRect) const
    {
        const newui::Rect hostRoot = SelectionOverlay::boundsInRootView(host_);
        return newui::Rect(rootRect.left() - hostRoot.left() + host_->origin().x,
            rootRect.top() - hostRoot.top() + host_->origin().y, rootRect.size().width, rootRect.size().height);
    }

    void MenuDesigner::refresh()
    {
        std::size_t shown = 0;
        bool showPlaceholder = false;
        bool showHighlight = false;

        // The selection may have been taken out of the tree (an undo) - drop it then.
        bool selectionDropped = false;
        if (bar_ != nullptr && selected_ != nullptr) {
            newui::MenuItem* top = selected_;
            while (top->parent() != nullptr) {
                top = top->parent();
            }
            if (top != &bar_->root()) {
                selected_ = nullptr;
                selectionDropped = true;
            }
        }

        if (bar_ != nullptr && host_ != nullptr) {
            const newui::Rect barRoot = SelectionOverlay::boundsInRootView(bar_);
            const auto& buttons = bar_->childViews();

            showPlaceholder = true;
            if (barPlaceholder_ == nullptr) {
                barPlaceholder_ = new MenuBarPlaceholderView(*this);
                host_->addChild(barPlaceholder_);
            }
            const float x = buttons.empty() ? barRoot.left() + 2.0f : SelectionOverlay::boundsInRootView(buttons.back()).right() + 2.0f;
            barPlaceholder_->setBounds(toHost(newui::Rect(x, barRoot.top() + 3.0f, kBarPlaceholderWidth, barRoot.size().height - 6.0f)));

            const std::vector<newui::MenuItem*> path = pathFromTopLevel(bar_, selected_);
            if (!path.empty()) {
                const auto& menus = bar_->menus();
                const std::size_t index = static_cast<std::size_t>(std::find(menus.begin(), menus.end(), path.front()) - menus.begin());
                if (index < buttons.size()) {
                    if (barHighlight_ == nullptr) {
                        barHighlight_ = new MenuMarkView("menuDesignerBarHighlight", 60);
                        host_->addChild(barHighlight_);
                    }
                    barHighlight_->setBounds(toHost(SelectionOverlay::boundsInRootView(buttons[index])));
                    showHighlight = true;
                }
            }
            newui::Rect previous;
            for (std::size_t i = 0; i < path.size(); ++i) {
                newui::MenuItem* menu = path[i];
                const bool last = i + 1 == path.size();
                if (last && i > 0 && !menu->hasChildren()) {
                    break;   // a leaf selection opens no submenu
                }
                MenuColumnView* column = columnAt(shown);
                column->setMenu(menu, last ? nullptr : path[i + 1]);
                const newui::Size size = column->preferredSize();

                newui::Rect rootRect;
                if (i == 0) {
                    const auto& menus = bar_->menus();
                    const auto it = std::find(menus.begin(), menus.end(), menu);
                    const std::size_t index = static_cast<std::size_t>(it - menus.begin());
                    const float left = index < buttons.size() ? SelectionOverlay::boundsInRootView(buttons[index]).left() : barRoot.left();
                    rootRect = newui::Rect(left, barRoot.bottom(), size.width, size.height);
                } else {
                    // Beside the previous column, level with the row that opened it.
                    MenuColumnView* opener = columns_[shown - 1];
                    const auto& siblings = opener->menu()->children();
                    const auto it = std::find(siblings.begin(), siblings.end(), menu);
                    const float rowTop = opener->rowTop(static_cast<std::size_t>(it - siblings.begin()));
                    rootRect = newui::Rect(previous.right() - 2.0f, previous.top() + rowTop - 2.0f, size.width, size.height);
                }
                column->setBounds(toHost(rootRect));
                column->setVisible(true);
                previous = rootRect;
                ++shown;
            }
        }

        for (std::size_t i = shown; i < columns_.size(); ++i) {
            columns_[i]->setVisible(false);
            columns_[i]->setMenu(nullptr, nullptr);
        }
        if (barPlaceholder_ != nullptr) {
            barPlaceholder_->setVisible(showPlaceholder);
        }
        if (barHighlight_ != nullptr) {
            barHighlight_->setVisible(showHighlight);
        }
        if (isEditing()) {
            positionEditField();
        }
        if (host_ != nullptr) {
            host_->style().markDirty();
        }
        if (selectionDropped && selectionChangedHandler_) {
            selectionChangedHandler_(nullptr);
        }
    }

    newui::MenuItem* MenuDesigner::topLevelMenuAt(const newui::Point& rootPt) const
    {
        if (bar_ == nullptr) {
            return nullptr;
        }
        const auto& buttons = bar_->childViews();
        const auto& menus = bar_->menus();
        for (std::size_t i = 0; i < buttons.size() && i < menus.size(); ++i) {
            if (SelectionOverlay::boundsInRootView(buttons[i]).contains(rootPt)) {
                return menus[i];
            }
        }
        return nullptr;
    }

    bool MenuDesigner::contains(const newui::Point& rootPt) const
    {
        for (MenuColumnView* column : columns_) {
            if (column->isVisible() && SelectionOverlay::boundsInRootView(column).contains(rootPt)) {
                return true;
            }
        }
        return barPlaceholder_ != nullptr && barPlaceholder_->isVisible()
            && SelectionOverlay::boundsInRootView(barPlaceholder_).contains(rootPt);
    }

    std::vector<MenuColumnView*> MenuDesigner::visibleColumns() const
    {
        std::vector<MenuColumnView*> visible;
        for (MenuColumnView* column : columns_) {
            if (column->isVisible()) {
                visible.push_back(column);
            }
        }
        return visible;
    }

    // --- Editing ----------------------------------------------------------------------------

    void MenuDesigner::runAction(newui::UndoableAction action)
    {
        std::function<void()> doIt = std::move(action.doIt);
        std::function<void()> undoIt = std::move(action.undoIt);
        action.doIt = [this, doIt] { doIt(); notifyChanged(); };
        action.undoIt = [this, undoIt] { undoIt(); notifyChanged(); };
        if (undoStack_ != nullptr) {
            undoStack_->push(std::move(action));   // runs doIt()
        } else {
            action.doIt();
        }
    }

    void MenuDesigner::notifyChanged()
    {
        refresh();
        if (changedHandler_) {
            changedHandler_();
        }
    }

    newui::MenuItem* MenuDesigner::insertItem(newui::MenuItem* parent, std::size_t index, const std::string& text)
    {
        if (parent == nullptr) {
            return nullptr;
        }
        auto* item = new newui::MenuItem(text == "-" ? std::string() : text);
        item->setSeparator(text == "-");
        newui::MenuBar* bar = isBarRoot(parent) ? bar_ : nullptr;

        newui::UndoableAction action;
        action.description = "Add Menu Item";
        action.doIt = [parent, item, index, bar] {
            parent->addChild(item);
            parent->reorderChild(item, index);
            if (bar != nullptr) {
                bar->rebuildButtons();
            }
        };
        action.undoIt = [parent, item, bar] {
            parent->removeChild(item);   // kept alive for redo
            if (bar != nullptr) {
                bar->rebuildButtons();
            }
        };
        runAction(std::move(action));
        return item;
    }

    void MenuDesigner::renameItem(newui::MenuItem* item, const std::string& text)
    {
        if (item == nullptr || item->text() == text) {
            return;
        }
        const std::string before = item->text();
        newui::MenuBar* bar = isBarRoot(item->parent()) ? bar_ : nullptr;
        auto apply = [item, bar](const std::string& value) {
            item->setText(value);
            if (bar != nullptr) {
                bar->rebuildButtons();
            }
        };

        newui::UndoableAction action;
        action.description = "Rename Menu Item";
        action.doIt = [apply, text] { apply(text); };
        action.undoIt = [apply, before] { apply(before); };
        runAction(std::move(action));
    }

    std::optional<newui::Rect> MenuDesigner::editRect() const
    {
        if (!isEditing() || bar_ == nullptr) {
            return std::nullopt;
        }
        if (isBarRoot(editParent_)) {
            const auto& buttons = bar_->childViews();
            if (editIndex_ < buttons.size()) {
                return toHost(SelectionOverlay::boundsInRootView(buttons[editIndex_]));
            }
            if (barPlaceholder_ != nullptr && barPlaceholder_->isVisible()) {
                return barPlaceholder_->bounds();
            }
            return std::nullopt;
        }
        for (MenuColumnView* column : columns_) {
            if (column->isVisible() && column->menu() == editParent_) {
                const newui::Rect& c = column->bounds();
                return newui::Rect(c.left() + 2.0f, c.top() + column->rowTop(editIndex_),
                    c.size().width - 4.0f, MenuColumnView::kRowHeight);
            }
        }
        return std::nullopt;
    }

    void MenuDesigner::positionEditField()
    {
        std::optional<newui::Rect> rect = editRect();
        if (!rect.has_value()) {
            cancelEdit();
            return;
        }
        editField_->setBounds(*rect);
        editField_->setVisible(true);
    }

    void MenuDesigner::beginEdit(newui::MenuItem* parent, std::size_t index)
    {
        if (bar_ == nullptr || parent == nullptr || host_ == nullptr) {
            return;
        }
        if (isEditing()) {
            commitEdit(false);
        }
        const auto& children = parent->children();
        if (index > children.size()) {
            return;
        }

        if (editField_ == nullptr) {
            editField_ = new newui::TextField();
            editField_->setName("menuDesignerEditField");
            editField_->onReturnPressed.add(this, &MenuDesigner::handleEditReturn);
            editField_->onKeyDown.add(this, &MenuDesigner::handleEditKeyDown);
            editField_->onLostFocus.add(this, &MenuDesigner::handleEditLostFocus);
            host_->addChild(editField_);
        }
        host_->reorderChild(editField_, host_->childViews().size());   // above the columns

        // Renaming selects the item itself; typing into a submenu's placeholder keeps it open.
        if (index < children.size()) {
            if (selected_ != children[index]) {
                select(children[index]);
            }
        } else if (!isBarRoot(parent) && selected_ != parent && (selected_ == nullptr || selected_->parent() != parent)) {
            select(parent);
        }

        editParent_ = parent;
        editIndex_ = index;
        editField_->setText(utf8ToWide(index < children.size() ? children[index]->text() : std::string()));
        positionEditField();
        if (!isEditing()) {
            return;   // nowhere to show it
        }
        editField_->controller().selectAll();
        if (newui::RootView* root = host_->rootView()) {
            root->setFocusedSubView(editField_);
        }
        host_->style().markDirty();
    }

    void MenuDesigner::commitEdit(bool continueEditing)
    {
        if (!isEditing()) {
            return;
        }
        newui::MenuItem* parent = editParent_;
        const std::size_t index = editIndex_;
        const std::string text = wideToUtf8(editField_->text());
        editParent_ = nullptr;   // before anything below can re-enter through lost focus

        newui::MenuItem* created = nullptr;
        if (index < parent->children().size()) {
            if (!text.empty()) {
                renameItem(parent->children()[index], text);
            }
        } else if (!text.empty()) {
            created = insertItem(parent, index, text);
            select(created);
        }

        if (continueEditing && created != nullptr) {
            // The next placeholder: below the new item, or - for a new top-level menu - its first item.
            if (isBarRoot(parent)) {
                beginEdit(created, 0);
            } else {
                beginEdit(parent, index + 1);
            }
            return;
        }
        endEditField();
    }

    void MenuDesigner::cancelEdit()
    {
        if (!isEditing()) {
            return;
        }
        editParent_ = nullptr;
        endEditField();
    }

    void MenuDesigner::endEditField()
    {
        editField_->setVisible(false);
        newui::RootView* root = host_->rootView();
        if (root != nullptr && root->focusedSubView() == editField_) {
            root->setFocusedSubView(nullptr);
        }
        host_->style().markDirty();
    }

    newui::SyncReturn MenuDesigner::handleEditReturn(newui::TextField& /*sender*/)
    {
        commitEdit(true);
        return newui::SyncReturn::Handled;
    }

    newui::SyncReturn MenuDesigner::handleEditKeyDown(newui::View& /*sender*/, std::uint32_t /*keyMask*/,
        int /*keyCharVal*/, int /*repeatCount*/, std::uint32_t VKeyCode)
    {
        if (VKeyCode == static_cast<std::uint32_t>(newui::vkEscape)) {
            cancelEdit();
            return newui::SyncReturn::Handled;
        }
        return newui::SyncReturn::Ignored;
    }

    newui::SyncReturn MenuDesigner::handleEditLostFocus(newui::View& /*sender*/)
    {
        commitEdit(false);
        return newui::SyncReturn::Ignored;
    }

    // --- Structure and navigation -----------------------------------------------------------

    namespace
    {
        std::size_t indexIn(const newui::MenuItem* parent, const newui::MenuItem* item)
        {
            const auto& children = parent->children();
            return static_cast<std::size_t>(std::find(children.begin(), children.end(), item) - children.begin());
        }

        // The next non-separator child after (step +1) or before (step -1) index, wrapping; nullptr if none.
        newui::MenuItem* nextSelectable(const newui::MenuItem* parent, std::size_t index, int step)
        {
            const auto& children = parent->children();
            const std::size_t count = children.size();
            for (std::size_t n = 1; n <= count; ++n) {
                const std::size_t i = (index + count + static_cast<std::size_t>(step) * n) % count;
                if (!children[i]->isSeparator()) {
                    return children[i];
                }
            }
            return nullptr;
        }

        newui::MenuItem* firstSelectable(const newui::MenuItem* parent)
        {
            for (newui::MenuItem* child : parent->children()) {
                if (!child->isSeparator()) {
                    return child;
                }
            }
            return nullptr;
        }
    }

    void MenuDesigner::deleteItem(newui::MenuItem* item)
    {
        newui::MenuItem* parent = item != nullptr ? item->parent() : nullptr;
        if (bar_ == nullptr || parent == nullptr) {
            return;
        }
        cancelEdit();
        const std::size_t index = indexIn(parent, item);
        newui::MenuBar* bar = isBarRoot(parent) ? bar_ : nullptr;

        const auto& siblings = parent->children();
        newui::MenuItem* next = index + 1 < siblings.size() ? siblings[index + 1]
            : index > 0 ? siblings[index - 1]
            : bar != nullptr ? nullptr : parent;

        newui::UndoableAction action;
        action.description = "Delete Menu Item";
        action.doIt = [parent, item, bar] {
            parent->removeChild(item);   // kept alive for undo
            if (bar != nullptr) {
                bar->rebuildButtons();
            }
        };
        action.undoIt = [parent, item, index, bar] {
            parent->addChild(item);
            parent->reorderChild(item, index);
            if (bar != nullptr) {
                bar->rebuildButtons();
            }
        };
        runAction(std::move(action));
        select(next);
    }

    newui::MenuItem* MenuDesigner::insertBefore(newui::MenuItem* item, bool separator)
    {
        newui::MenuItem* parent = item != nullptr ? item->parent() : nullptr;
        if (bar_ == nullptr || parent == nullptr) {
            return nullptr;
        }
        cancelEdit();
        const std::size_t index = indexIn(parent, item);
        const std::string text = separator ? "-" : isBarRoot(parent) ? "New Menu" : "New Item";
        newui::MenuItem* created = insertItem(parent, index, text);
        if (!separator) {
            select(created);
            beginEdit(parent, index);
        }
        return created;
    }

    void MenuDesigner::createSubmenu(newui::MenuItem* item)
    {
        if (bar_ == nullptr || item == nullptr || item->isSeparator()) {
            return;
        }
        cancelEdit();
        if (newui::MenuItem* first = firstSelectable(item)) {
            select(first);
            return;
        }
        newui::MenuItem* created = insertItem(item, item->children().size(), "New Item");
        select(created);
        beginEdit(item, indexIn(item, created));
    }

    void MenuDesigner::moveSelection(Direction direction)
    {
        newui::MenuItem* item = selected_;
        newui::MenuItem* parent = item != nullptr ? item->parent() : nullptr;
        if (bar_ == nullptr || parent == nullptr) {
            return;
        }
        const bool topLevel = isBarRoot(parent);
        // The top-level menu this item's column hangs from.
        newui::MenuItem* top = item;
        while (top->parent() != nullptr && !isBarRoot(top->parent())) {
            top = top->parent();
        }
        auto adjacentMenu = [this, top](int step) {
            return nextSelectable(&bar_->root(), indexIn(&bar_->root(), top), step);
        };

        newui::MenuItem* target = nullptr;
        switch (direction) {
        case Direction::Up:
            target = topLevel ? nullptr : nextSelectable(parent, indexIn(parent, item), -1);
            break;
        case Direction::Down:
            target = topLevel ? firstSelectable(item) : nextSelectable(parent, indexIn(parent, item), 1);
            break;
        case Direction::Right:
            target = !topLevel && firstSelectable(item) != nullptr ? firstSelectable(item) : adjacentMenu(1);
            break;
        case Direction::Left:
            target = topLevel || isBarRoot(parent->parent()) ? adjacentMenu(-1) : parent;
            break;
        }
        if (target != nullptr) {
            select(target);
        }
    }

    bool MenuDesigner::handleKeyDown(std::uint32_t keyMask, std::uint32_t vkCode)
    {
        if (selected_ == nullptr || isEditing()) {
            return false;
        }
        newui::MenuItem* item = selected_;
        const bool ctrl = (keyMask & newui::kmCtrl) != 0;
        switch (vkCode) {
        case newui::vkDelete: deleteItem(item); return true;
        case newui::vkInsert: insertBefore(item); return true;
        case newui::vkRightArrow:
            if (ctrl) {
                createSubmenu(item);
            } else {
                moveSelection(Direction::Right);
            }
            return true;
        case newui::vkLeftArrow: moveSelection(Direction::Left); return true;
        case newui::vkUpArrow: moveSelection(Direction::Up); return true;
        case newui::vkDownArrow: moveSelection(Direction::Down); return true;
        case newui::vkReturn:
        case newui::vkF2:
            beginEdit(item->parent(), indexIn(item->parent(), item));
            return true;
        case newui::vkEscape: select(nullptr); return true;
        default: return false;
        }
    }

    void MenuDesigner::showContextMenu(newui::MenuItem* item, const newui::Point& rootPt)
    {
        newui::RootView* root = host_ != nullptr ? host_->rootView() : nullptr;
        if (item == nullptr || root == nullptr || root->windowHandle() == nullptr) {
            return;
        }

        // Each command is posted: this can run inside a root-level mouse handler, and RootView
        // resets focus after that - a rename started right here would end at once.
        std::shared_ptr<bool> alive = alive_;
        auto post = [alive](std::function<void()> command) {
            auto guarded = [alive, command] {
                if (*alive) {
                    command();
                }
            };
            if (newui::RunLoop::current()) {
                newui::RunLoop::current().post(std::move(guarded));
            } else {
                guarded();
            }
        };

        newui::MenuItem menu;
        auto add = [&menu, &post](const std::string& text, const std::string& shortcut, bool enabled,
                       std::function<void()> command) {
            newui::MenuItem* entry = menu.addChild(std::make_unique<newui::MenuItem>(text));
            entry->setShortcutText(shortcut);
            entry->state().setEnabled(enabled);
            entry->onClick.add([post, command](newui::MenuItem&) {
                post(command);
                return newui::SyncReturn::Handled;
            });
        };
        const bool real = !item->isSeparator();
        add("Insert", "Ins", true, [this, item] { insertBefore(item); });
        add("Insert Separator", "", true, [this, item] { insertBefore(item, true); });
        add("Delete", "Del", true, [this, item] { deleteItem(item); });
        menu.addChild(newui::MenuItem::Separator());
        add("Create Submenu", "Ctrl+Right", real, [this, item] { createSubmenu(item); });
        add("Rename", "F2", real, [this, item] {
            if (item->parent() != nullptr) {
                beginEdit(item->parent(), indexIn(item->parent(), item));
            }
        });

        const newui::Point screenPt = root->localToScreen(rootPt);
        newui::ContextMenu contextMenu;
        contextMenu.show(root->windowHandle(), menu, static_cast<int>(screenPt.x), static_cast<int>(screenPt.y));
    }

    // --- Drag and drop ----------------------------------------------------------------------

    MenuMarkView::MenuMarkView(const char* name, std::uint32_t alpha) : alpha_(alpha)
    {
        setName(name);
    }

    void MenuMarkView::paint(BLContext& ctx)
    {
        const newui::Size size = bounds().size();
        const newui::Color highlight = roleColor(newui::UIColorRole::HighlightBackground);
        ctx.save();
        ctx.set_fill_style(withAlpha(highlight, alpha_));
        ctx.fill_rect(0.0, 0.0, size.width, size.height);
        if (alpha_ < 255) {
            ctx.set_stroke_style(highlight.toBLRgba32());
            ctx.stroke_rect(0.5, 0.5, size.width - 1.0, size.height - 1.0);
        }
        ctx.restore();
    }

    void MenuDesigner::moveItem(newui::MenuItem* item, newui::MenuItem* newParent, std::size_t index)
    {
        newui::MenuItem* oldParent = item != nullptr ? item->parent() : nullptr;
        if (bar_ == nullptr || oldParent == nullptr || newParent == nullptr) {
            return;
        }
        for (newui::MenuItem* p = newParent; p != nullptr; p = p->parent()) {
            if (p == item) {
                return;   // into itself or its own submenu
            }
        }
        const std::size_t oldIndex = indexIn(oldParent, item);
        if (newParent == oldParent && (index == oldIndex || index == oldIndex + 1)) {
            return;   // already there
        }
        // index counts the item itself when it's moving down within the same menu.
        const std::size_t finalIndex = newParent == oldParent && index > oldIndex ? index - 1 : index;
        newui::MenuBar* bar = isBarRoot(oldParent) || isBarRoot(newParent) ? bar_ : nullptr;
        cancelEdit();

        newui::UndoableAction action;
        action.description = "Move Menu Item";
        action.doIt = [item, newParent, finalIndex, bar] {
            item->setParent(newParent);
            newParent->reorderChild(item, finalIndex);
            if (bar != nullptr) {
                bar->rebuildButtons();
            }
        };
        action.undoIt = [item, oldParent, oldIndex, bar] {
            item->setParent(oldParent);
            oldParent->reorderChild(item, oldIndex);
            if (bar != nullptr) {
                bar->rebuildButtons();
            }
        };
        runAction(std::move(action));
    }

    std::optional<MenuDesigner::DropTarget> MenuDesigner::dropTargetAt(const newui::Point& rootPt) const
    {
        if (bar_ == nullptr || dragItem_ == nullptr) {
            return std::nullopt;
        }
        auto acceptsDrop = [this](const newui::MenuItem* parent) {
            for (const newui::MenuItem* p = parent; p != nullptr; p = p->parent()) {
                if (p == dragItem_) {
                    return false;
                }
            }
            return true;
        };

        // The bar (and its placeholder): between, before or after the top-level menus.
        const newui::Rect barRoot = SelectionOverlay::boundsInRootView(bar_);
        const bool onPlaceholder = barPlaceholder_ != nullptr && barPlaceholder_->isVisible()
            && SelectionOverlay::boundsInRootView(barPlaceholder_).contains(rootPt);
        if (barRoot.contains(rootPt) || onPlaceholder) {
            const auto& buttons = bar_->childViews();
            std::size_t index = buttons.size();
            float markX = buttons.empty() ? barRoot.left() + 2.0f : SelectionOverlay::boundsInRootView(buttons.back()).right();
            for (std::size_t i = 0; i < buttons.size(); ++i) {
                const newui::Rect r = SelectionOverlay::boundsInRootView(buttons[i]);
                if (rootPt.x < r.left() + r.size().width * 0.5f) {
                    index = i;
                    markX = r.left();
                    break;
                }
            }
            DropTarget target;
            target.parent = &bar_->root();
            target.index = index;
            target.markRoot = newui::Rect(markX - 1.0f, barRoot.top() + 3.0f, 2.0f, barRoot.size().height - 6.0f);
            return target;
        }

        // The columns, topmost (deepest) first: above or below the row under the point.
        for (auto it = columns_.rbegin(); it != columns_.rend(); ++it) {
            MenuColumnView* column = *it;
            if (!column->isVisible() || column->menu() == nullptr) {
                continue;
            }
            const newui::Rect c = SelectionOverlay::boundsInRootView(column);
            if (!c.contains(rootPt)) {
                continue;
            }
            newui::MenuItem* parent = column->menu();
            if (!acceptsDrop(parent)) {
                return std::nullopt;
            }
            const float y = rootPt.y - c.top();
            const std::size_t count = parent->children().size();
            std::size_t index = count;
            for (std::size_t i = 0; i < count; ++i) {
                const float top = column->rowTop(i);
                if (y < top + (column->rowTop(i + 1) - top) * 0.5f) {
                    index = i;
                    break;
                }
            }
            DropTarget target;
            target.parent = parent;
            target.index = index;
            target.markRoot = newui::Rect(c.left() + 4.0f, c.top() + column->rowTop(index) - 1.0f, c.size().width - 8.0f, 2.0f);
            return target;
        }
        return std::nullopt;
    }

    void MenuDesigner::armDrag(newui::MenuItem* item, const newui::Point& rootPt)
    {
        dragItem_ = bar_ != nullptr ? item : nullptr;
        dragStart_ = rootPt;
        dragActive_ = false;
    }

    void MenuDesigner::dragTo(const newui::Point& rootPt)
    {
        if (dragItem_ == nullptr) {
            return;
        }
        if (!dragActive_) {
            const float dx = rootPt.x - dragStart_.x;
            const float dy = rootPt.y - dragStart_.y;
            if ((dx < 0.0f ? -dx : dx) < 4.0f && (dy < 0.0f ? -dy : dy) < 4.0f) {
                return;
            }
            dragActive_ = true;
            cancelEdit();
        }

        // Hovering a bar menu opens it, so an item can be carried into it.
        newui::MenuItem* hovered = topLevelMenuAt(rootPt);
        const std::vector<newui::MenuItem*> path = pathFromTopLevel(bar_, selected_);
        if (hovered != nullptr && hovered != dragItem_ && (path.empty() || path.front() != hovered)) {
            selected_ = hovered;
            refresh();
        }

        std::optional<DropTarget> target = dropTargetAt(rootPt);
        if (!target.has_value()) {
            if (dropMark_ != nullptr) {
                dropMark_->setVisible(false);
            }
        } else {
            if (dropMark_ == nullptr) {
                dropMark_ = new MenuMarkView("menuDesignerDropMark", 255);
                host_->addChild(dropMark_);
            }
            host_->reorderChild(dropMark_, host_->childViews().size());
            dropMark_->setBounds(toHost(target->markRoot));
            dropMark_->setVisible(true);
        }
        host_->style().markDirty();
    }

    void MenuDesigner::endDrag(const newui::Point& rootPt)
    {
        if (dragItem_ == nullptr) {
            return;
        }
        newui::MenuItem* item = dragItem_;
        const bool active = dragActive_;
        std::optional<DropTarget> target = active ? dropTargetAt(rootPt) : std::nullopt;
        dragItem_ = nullptr;
        dragActive_ = false;
        if (dropMark_ != nullptr) {
            dropMark_->setVisible(false);
        }
        if (active) {
            if (target.has_value()) {
                moveItem(item, target->parent, target->index);
            }
            select(item);
        }
        if (host_ != nullptr) {
            host_->style().markDirty();
        }
    }

    void MenuDesigner::cancelDrag()
    {
        newui::MenuItem* item = dragItem_;
        const bool active = dragActive_;
        dragItem_ = nullptr;
        dragActive_ = false;
        if (dropMark_ != nullptr) {
            dropMark_->setVisible(false);
        }
        if (active && item != nullptr && bar_ != nullptr) {
            select(item);
        }
    }
}
