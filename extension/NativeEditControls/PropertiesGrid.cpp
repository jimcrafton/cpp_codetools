#include "PropertiesGrid.h"
#include "TextEncoding.h"

#include <newui/rootview.h>
#include <newui/uicolormanager.h>

#include <cmath>
#include <typeindex>

namespace CodeToolsVsix
{
    std::any PropertiesGrid::StringListModel::value(const std::any& key)
    {
        if (const std::size_t* index = std::any_cast<std::size_t>(&key)) {
            if (*index < rows.size()) {
                return rows[*index];
            }
        }
        return std::any();
    }

    PropertiesGrid::PropertiesGrid()
    {
        setVisible(true);
        style().setBackgroundColor(newui::UIColorManager::colorFor(newui::UIColorRole::ControlBackground));

        treeView_ = new newui::TreeView();
        treeView_->setName("propertiesTreeView");
        treeView_->setVisible(true);
        treeView_->setController(std::make_unique<PropertiesTreeController>());
        treeView_->setModel(&model_);
        treeView_->onSelectionChanged.add(this, &PropertiesGrid::handleSelectionChanged);
        treeView_->onMouseDown.add(this, &PropertiesGrid::handleTreeMouseDown);
        treeView_->onMouseMove.add(this, &PropertiesGrid::handleTreeMouseMove);
        treeView_->onMouseUp.add(this, &PropertiesGrid::handleTreeMouseUp);

        // ScrollView::addChild() redirects into its own viewport - not a
        // second, separate wrapping layer, same convention Toolbox's own
        // constructor comment documents.
        addChild(treeView_);
    }

    void PropertiesGrid::setSelection(newui::SubView* selected)
    {
        treeView_->clearSelection();
        destroyLiveEditor();
        model_.setSelection(selected);
    }

    newui::SyncReturn PropertiesGrid::handleSelectionChanged(newui::TreeView& /*sender*/)
    {
        // Selecting a row no longer activates its editor by itself - only
        // a click that actually lands on the value column does (see
        // activateLiveEditorIfClickedOnValueColumn(), called from
        // handleTreeMouseDown() instead). Still tears down whatever was
        // being edited before, though - a newly selected row should never
        // show a stale previous row's live widget.
        destroyLiveEditor();
        return newui::SyncReturn::Handled;
    }

    void PropertiesGrid::destroyLiveEditor()
    {
        if (liveEditorView_ != nullptr) {
            treeView_->removeChild(liveEditorView_);
            delete liveEditorView_;
            liveEditorView_ = nullptr;
            // A real, live-debugged bug otherwise: removeChild() only
            // ever calls updateLayout(), never markDirty()/invalidate()
            // (same "nothing asks Windows to actually repaint" class of
            // bug this project has hit before - DesignerEditor's own
            // setupUI()/load() need the same explicit call, for the same
            // reason) - Escape/Enter both correctly destroyed the widget
            // internally, but the screen never redrew to show it gone,
            // reading as "the keypress had no effect" even though it did.
            treeView_->style().markDirty();
        }
        liveEditor_.reset();
        liveEditorSubIndex_.reset();
    }

    void PropertiesGrid::rebuildLiveEditor()
    {
        destroyLiveEditor();

        std::optional<std::vector<std::size_t>> path = treeView_->selectedPath();
        if (!path.has_value()) {
            return;
        }

        PropertiesModel::Node node = model_.nodeAt(*path);
        bool isSubProperty = node.kind == PropertiesModel::Kind::SubPropertyEntry;
        if (node.kind != PropertiesModel::Kind::PropertyLeaf && !isSubProperty) {
            return;
        }

        std::optional<newui::Rect> rowRect = treeView_->rectForPath(*path);
        if (!rowRect.has_value()) {
            return;
        }

        // For a SubPropertyEntry, node.property/ownerClass/ownerInstance
        // already describe the *parent* compound property (e.g.
        // "bounds") - same PropertyEditor either way, just read/written
        // through its subPropertyValueAsString()/
        // setSubPropertyValueFromString(node.subPropertyIndex, ...) below
        // instead of the whole-value valueAsString()/setValueFromString().
        liveEditor_ = PropertyEditorRegistry::instance().createEditor(node.property, node.ownerClass, node.ownerInstance);
        if (liveEditor_ == nullptr) {
            return;
        }
        liveEditor_->setUndoStack(undoStack_);
        liveEditorSubIndex_ = isSubProperty ? std::optional<std::size_t>(node.subPropertyIndex) : std::nullopt;

        auto* propsController = dynamic_cast<PropertiesTreeController*>(&treeView_->controller());
        float keyColumnFraction = propsController != nullptr
            ? propsController->keyColumnFraction() : PropertiesTreeController::kDefaultKeyColumnFraction;
        newui::Rect valueRect = PropertyItem::valueRectFor(*rowRect, *path, keyColumnFraction);

        std::string initialText = isSubProperty
            ? liveEditor_->subPropertyValueAsString(node.subPropertyIndex) : liveEditor_->valueAsString();

        // A SubPropertyEntry (a synthetic float component - x/y/width/
        // height) is always plain text, regardless of what EditStyle the
        // parent compound editor itself reports (SubProperties) - only
        // the *parent* row's own now-unused editStyle() would ever say
        // SubProperties, never reached here since this function already
        // returned above for anything but PropertyLeaf/SubPropertyEntry.
        if (!isSubProperty && node.property->type() == std::type_index(typeid(bool))) {
            auto* toggle = new newui::Toggle();
            toggle->setVisible(true);
            toggle->setChecked(initialText == "true");
            float box = PropertyItem::kCheckboxSize;
            toggle->setBounds(newui::Rect(valueRect.left(), valueRect.top() + (valueRect.size().height - box) * 0.5f, box, box));
            toggle->onCheckedChanged.add(this, &PropertiesGrid::handleLiveToggleChanged);
            toggle->onLostFocus.add(this, &PropertiesGrid::handleLiveEditorLostFocus);
            toggle->onKeyDown.add(this, &PropertiesGrid::handleLiveEditorKeyDown);
            treeView_->addChild(toggle);
            liveEditorView_ = toggle;
            focusLiveEditorView();
            return;
        }

        if (!isSubProperty && liveEditor_->editStyle() == PropertyEditor::EditStyle::Dropdown) {
            dropdownModel_.rows = liveEditor_->dropdownValues();

            auto* dropdown = new newui::DropDownList();
            dropdown->setVisible(true);
            dropdown->setModel(&dropdownModel_);

            for (std::size_t i = 0; i < dropdownModel_.rows.size(); ++i) {
                if (dropdownModel_.rows[i] == initialText) {
                    dropdown->setSelectedIndex(i);
                    break;
                }
            }

            dropdown->setBounds(valueRect);
            dropdown->onSelectionChanged.add(this, &PropertiesGrid::handleLiveDropdownChanged);
            dropdown->onLostFocus.add(this, &PropertiesGrid::handleLiveEditorLostFocus);
            dropdown->onKeyDown.add(this, &PropertiesGrid::handleLiveEditorKeyDown);
            treeView_->addChild(dropdown);
            liveEditorView_ = dropdown;
            focusLiveEditorView();
            return;
        }

        // EditStyle::None (Int/Float/String/Color today) or a
        // SubPropertyEntry - plain editable text, same fallback
        // PropertyRow::build() uses. A Color property leaves its swatch
        // preview to PropertyItem's own inactive painting underneath
        // (unobscured - the live TextField only covers the text portion
        // of valueRect, matching the same swatch+text offset
        // PropertyItem::paint() draws), not reimplemented here.
        newui::Rect textRect = valueRect;
        if (!isSubProperty && node.property->type() == std::type_index(typeid(newui::Color))) {
            textRect = newui::Rect(valueRect.left() + PropertyItem::kSwatchSize + 6.0f, valueRect.top(),
                valueRect.size().width - PropertyItem::kSwatchSize - 6.0f, valueRect.size().height);
        }

        auto* textField = new newui::TextField();
        textField->setVisible(true);
        textField->setText(utf8ToWide(initialText));
        textField->setBounds(textRect);
        textField->onLostFocus.add(this, &PropertiesGrid::handleLiveTextCommit);
        textField->onReturnPressed.add(this, &PropertiesGrid::handleLiveTextReturnPressed);
        textField->onKeyDown.add(this, &PropertiesGrid::handleLiveEditorKeyDown);
        treeView_->addChild(textField);
        liveEditorView_ = textField;
        focusLiveEditorView();
    }

    void PropertiesGrid::focusLiveEditorView()
    {
        // See this method's own declaration comment (PropertiesGrid.h) for
        // why this is needed at all. treeView_->rootView() is null in
        // tests that construct a bare PropertiesGrid with no real RootView
        // above it - a no-op there, same as every other rootView()-
        // dependent call elsewhere in this codebase.
        if (newui::RootView* rootView = treeView_->rootView()) {
            rootView->setFocusedSubView(liveEditorView_);
        }
    }

    void PropertiesGrid::activateLiveEditorIfClickedOnValueColumn(const newui::Point& pt)
    {
        // Runs after TreeView's own internal mouse-down handler (its own
        // constructor registers it before PropertiesGrid ever subscribes
        // its own handleTreeMouseDown() - see this class's own header
        // comment) already updated selectedPath() for this same click.
        std::optional<std::vector<std::size_t>> path = treeView_->selectedPath();
        if (!path.has_value()) {
            return;
        }

        PropertiesModel::Node node = model_.nodeAt(*path);
        bool isEditable = node.kind == PropertiesModel::Kind::PropertyLeaf
            || node.kind == PropertiesModel::Kind::SubPropertyEntry;
        if (!isEditable) {
            return;
        }

        std::optional<newui::Rect> rowRect = treeView_->rectForPath(*path);
        if (!rowRect.has_value()) {
            return;
        }

        auto* propsController = dynamic_cast<PropertiesTreeController*>(&treeView_->controller());
        float keyColumnFraction = propsController != nullptr
            ? propsController->keyColumnFraction() : PropertiesTreeController::kDefaultKeyColumnFraction;
        newui::Rect valueRect = PropertyItem::valueRectFor(*rowRect, *path, keyColumnFraction);
        if (!valueRect.contains(pt)) {
            return;
        }

        rebuildLiveEditor();
    }

    void PropertiesGrid::repositionLiveEditor()
    {
        if (liveEditorView_ == nullptr) {
            return;
        }

        std::optional<std::vector<std::size_t>> path = treeView_->selectedPath();
        std::optional<newui::Rect> rowRect = path.has_value() ? treeView_->rectForPath(*path) : std::nullopt;
        if (!rowRect.has_value()) {
            return;
        }

        auto* propsController = dynamic_cast<PropertiesTreeController*>(&treeView_->controller());
        float keyColumnFraction = propsController != nullptr
            ? propsController->keyColumnFraction() : PropertiesTreeController::kDefaultKeyColumnFraction;
        newui::Rect valueRect = PropertyItem::valueRectFor(*rowRect, *path, keyColumnFraction);

        if (auto* toggle = dynamic_cast<newui::Toggle*>(liveEditorView_)) {
            float box = PropertyItem::kCheckboxSize;
            toggle->setBounds(newui::Rect(valueRect.left(), valueRect.top() + (valueRect.size().height - box) * 0.5f, box, box));
            return;
        }
        if (auto* dropdown = dynamic_cast<newui::DropDownList*>(liveEditorView_)) {
            dropdown->setBounds(valueRect);
            return;
        }
        if (auto* textField = dynamic_cast<newui::TextField*>(liveEditorView_)) {
            newui::Rect textRect = valueRect;
            PropertiesModel::Node node = model_.nodeAt(*path);
            if (!liveEditorSubIndex_.has_value() && node.property != nullptr
                    && node.property->type() == std::type_index(typeid(newui::Color))) {
                textRect = newui::Rect(valueRect.left() + PropertyItem::kSwatchSize + 6.0f, valueRect.top(),
                    valueRect.size().width - PropertyItem::kSwatchSize - 6.0f, valueRect.size().height);
            }
            textField->setBounds(textRect);
        }
    }

    newui::SyncReturn PropertiesGrid::handleLiveTextCommit(newui::View& /*sender*/)
    {
        // Does NOT destroyLiveEditor() - see handleLiveEditorLostFocus()'s
        // own comment for why: this fires for far more than "the user
        // clicked something else in this app" (the only case that should
        // actually close the editor), and destroying it here was a real,
        // live-debugged bug (the editor silently vanishing on every
        // alt-tab away from the app, discovered when Escape/Enter
        // appeared to do nothing - by the time focus came back there was
        // nothing left to act on). handleSelectionChanged() already
        // closes it when a real click lands on a *different* row.
        auto* textField = dynamic_cast<newui::TextField*>(liveEditorView_);
        if (textField != nullptr && liveEditor_ != nullptr) {
            std::string text = wideToUtf8(textField->text());
            if (liveEditorSubIndex_.has_value()) {
                liveEditor_->setSubPropertyValueFromString(*liveEditorSubIndex_, text);
            } else {
                liveEditor_->setValueFromString(text);
            }
            treeView_->style().markDirty();
        }
        return newui::SyncReturn::Ignored;
    }

    newui::SyncReturn PropertiesGrid::handleLiveTextReturnPressed(newui::TextField& sender)
    {
        // Commits, same as handleLiveTextCommit(), *then* closes the
        // editor - unlike that method, safe to do here: onReturnPressed
        // only ever fires for a real, deliberate Enter keypress
        // (TextField::handleKeyDown()'s own vkReturn special case,
        // controls.cpp), never for the entire window losing OS-level
        // focus (RootView::lostFocus(), e.g. alt-tab) the way
        // View::onLostFocus does - that distinction is exactly why
        // handleLiveTextCommit() itself no longer closes anything.
        newui::SyncReturn result = handleLiveTextCommit(sender);
        destroyLiveEditor();
        return result;
    }

    newui::SyncReturn PropertiesGrid::handleLiveEditorLostFocus(newui::View& /*sender*/)
    {
        // Deliberately a no-op now - see handleLiveTextCommit()'s own
        // comment. View::onLostFocus fires for this widget losing focus
        // to *anything*, including the entire window losing real OS-level
        // focus (RootView::lostFocus(), fired on WM_KILLFOCUS - e.g.
        // alt-tabbing away, unrelated to this app at all), not just a
        // real click on another control within this app - only the
        // latter should ever close a live editor, and there's no way to
        // tell the two apart from this callback alone. Kept (rather than
        // unwired entirely) as the one place to revisit if a real,
        // narrower "did focus move to a different control in this same
        // app" signal is ever added.
        return newui::SyncReturn::Ignored;
    }

    newui::SyncReturn PropertiesGrid::handleLiveEditorKeyDown(newui::View& /*sender*/, std::uint32_t /*keyMask*/,
        int /*keyCharVal*/, int /*repeatCount*/, std::uint32_t VKeyCode)
    {
        if (VKeyCode != static_cast<std::uint32_t>(newui::vkEscape)) {
            return newui::SyncReturn::Ignored;
        }
        // Discards whatever was typed/toggled/picked - never touches
        // liveEditor_->setValueFromString()/setSubPropertyValueFromString()
        // at all, unlike every commit path above.
        destroyLiveEditor();
        return newui::SyncReturn::Handled;
    }

    newui::SyncReturn PropertiesGrid::handleLiveToggleChanged(newui::Toggle& sender)
    {
        // A SubPropertyEntry is always plain text (see rebuildLiveEditor())
        // - a Toggle widget only ever exists for a whole-value bool
        // property, so liveEditorSubIndex_ is never set here.
        if (liveEditor_ != nullptr) {
            liveEditor_->setValueFromString(sender.isChecked() ? "true" : "false");
        }
        return newui::SyncReturn::Handled;
    }

    newui::SyncReturn PropertiesGrid::handleLiveDropdownChanged(newui::DropDownList& sender)
    {
        // Same as handleLiveToggleChanged() above - a Dropdown widget only
        // ever exists for a whole-value property.
        if (!sender.selectedIndex().has_value() || liveEditor_ == nullptr) {
            return newui::SyncReturn::Ignored;
        }
        std::any value = sender.model()->value(*sender.selectedIndex());
        if (const std::string* text = std::any_cast<std::string>(&value)) {
            liveEditor_->setValueFromString(*text);
        }
        return newui::SyncReturn::Handled;
    }

    float PropertiesGrid::dividerX() const
    {
        auto* propsController = dynamic_cast<PropertiesTreeController*>(&treeView_->controller());
        float fraction = propsController != nullptr
            ? propsController->keyColumnFraction() : PropertiesTreeController::kDefaultKeyColumnFraction;
        newui::Rect clientBounds = treeView_->getClientBounds();
        return clientBounds.left() + clientBounds.size().width * fraction;
    }

    bool PropertiesGrid::isPointNearDivider(const newui::Point& localPt) const
    {
        newui::Rect clientBounds = treeView_->getClientBounds();
        if (clientBounds.size().width <= 0.0f) {
            return false;
        }
        return std::abs(localPt.x - dividerX()) <= kDividerHitSlop;
    }

    newui::SyncReturn PropertiesGrid::handleTreeMouseDown(newui::View& /*sender*/, const newui::Point& pt,
        std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/)
    {
        if (isPointNearDivider(pt)) {
            draggingDivider_ = true;
            return newui::SyncReturn::Handled;
        }
        activateLiveEditorIfClickedOnValueColumn(pt);
        return newui::SyncReturn::Ignored;
    }

    newui::SyncReturn PropertiesGrid::handleTreeMouseMove(newui::View& /*sender*/, const newui::Point& pt,
        std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/)
    {
        if (!draggingDivider_) {
            return newui::SyncReturn::Ignored;
        }

        newui::Rect clientBounds = treeView_->getClientBounds();
        if (clientBounds.size().width <= 0.0f) {
            return newui::SyncReturn::Ignored;
        }

        auto* propsController = dynamic_cast<PropertiesTreeController*>(&treeView_->controller());
        if (propsController == nullptr) {
            return newui::SyncReturn::Ignored;
        }

        float fraction = (pt.x - clientBounds.left()) / clientBounds.size().width;
        propsController->setKeyColumnFraction(fraction);
        // The live editor's own value-column rect depends on
        // keyColumnFraction too - reposition it in place (not
        // rebuildLiveEditor() - see that method's own doc comment for why
        // destroying/recreating it here would silently drop an in-progress
        // edit) so it tracks the divider live during the drag.
        repositionLiveEditor();
        return newui::SyncReturn::Handled;
    }

    newui::SyncReturn PropertiesGrid::handleTreeMouseUp(newui::View& /*sender*/, const newui::Point& /*pt*/,
        std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/)
    {
        if (!draggingDivider_) {
            return newui::SyncReturn::Ignored;
        }
        draggingDivider_ = false;
        return newui::SyncReturn::Handled;
    }
}
