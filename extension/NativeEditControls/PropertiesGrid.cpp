#include "PropertiesGrid.h"
#include "TextEncoding.h"

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
        rebuildLiveEditor();
        return newui::SyncReturn::Handled;
    }

    void PropertiesGrid::destroyLiveEditor()
    {
        if (liveEditorWidget_ != nullptr) {
            treeView_->removeChild(liveEditorWidget_);
            delete liveEditorWidget_;
            liveEditorWidget_ = nullptr;
        }
        liveEditor_.reset();
    }

    void PropertiesGrid::rebuildLiveEditor()
    {
        destroyLiveEditor();

        std::optional<std::vector<std::size_t>> path = treeView_->selectedPath();
        if (!path.has_value()) {
            return;
        }

        PropertiesModel::Node node = model_.nodeAt(*path);
        if (node.kind != PropertiesModel::Kind::PropertyLeaf) {
            return;
        }

        std::optional<newui::Rect> rowRect = treeView_->rectForPath(*path);
        if (!rowRect.has_value()) {
            return;
        }

        liveEditor_ = PropertyEditorRegistry::instance().createEditor(node.property, node.ownerClass, node.ownerInstance);
        if (liveEditor_ == nullptr) {
            return;
        }
        liveEditor_->setUndoStack(undoStack_);

        auto* propsController = dynamic_cast<PropertiesTreeController*>(&treeView_->controller());
        float keyColumnFraction = propsController != nullptr
            ? propsController->keyColumnFraction() : PropertiesTreeController::kDefaultKeyColumnFraction;
        newui::Rect valueRect = PropertyItem::valueRectFor(*rowRect, *path, keyColumnFraction);

        if (node.property->type() == std::type_index(typeid(bool))) {
            auto* toggle = new newui::Toggle();
            toggle->setVisible(true);
            toggle->setChecked(liveEditor_->valueAsString() == "true");
            float box = PropertyItem::kCheckboxSize;
            toggle->setBounds(newui::Rect(valueRect.left(), valueRect.top() + (valueRect.size().height - box) * 0.5f, box, box));
            toggle->onCheckedChanged.add(this, &PropertiesGrid::handleLiveToggleChanged);
            treeView_->addChild(toggle);
            liveEditorWidget_ = toggle;
            return;
        }

        if (liveEditor_->editStyle() == PropertyEditor::EditStyle::Dropdown) {
            dropdownModel_.rows = liveEditor_->dropdownValues();

            auto* dropdown = new newui::DropDownList();
            dropdown->setVisible(true);
            dropdown->setModel(&dropdownModel_);

            std::string current = liveEditor_->valueAsString();
            for (std::size_t i = 0; i < dropdownModel_.rows.size(); ++i) {
                if (dropdownModel_.rows[i] == current) {
                    dropdown->setSelectedIndex(i);
                    break;
                }
            }

            dropdown->setBounds(valueRect);
            dropdown->onSelectionChanged.add(this, &PropertiesGrid::handleLiveDropdownChanged);
            treeView_->addChild(dropdown);
            liveEditorWidget_ = dropdown;
            return;
        }

        // EditStyle::None (Int/Float/String/Color today) - plain editable
        // text, same fallback PropertyRow::build() uses. A Color property
        // leaves its swatch preview to PropertyItem's own inactive
        // painting underneath (unobscured - the live TextField only
        // covers the text portion of valueRect, matching the same
        // swatch+text offset PropertyItem::paint() draws), not
        // reimplemented here.
        newui::Rect textRect = valueRect;
        if (node.property->type() == std::type_index(typeid(newui::Color))) {
            textRect = newui::Rect(valueRect.left() + PropertyItem::kSwatchSize + 6.0f, valueRect.top(),
                valueRect.size().width - PropertyItem::kSwatchSize - 6.0f, valueRect.size().height);
        }

        auto* textField = new newui::TextField();
        textField->setVisible(true);
        textField->setText(utf8ToWide(liveEditor_->valueAsString()));
        textField->setBounds(textRect);
        textField->onLostFocus.add(this, &PropertiesGrid::handleLiveTextCommit);
        treeView_->addChild(textField);
        liveEditorWidget_ = textField;
    }

    void PropertiesGrid::repositionLiveEditor()
    {
        if (liveEditorWidget_ == nullptr) {
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

        if (auto* toggle = dynamic_cast<newui::Toggle*>(liveEditorWidget_)) {
            float box = PropertyItem::kCheckboxSize;
            toggle->setBounds(newui::Rect(valueRect.left(), valueRect.top() + (valueRect.size().height - box) * 0.5f, box, box));
            return;
        }
        if (auto* dropdown = dynamic_cast<newui::DropDownList*>(liveEditorWidget_)) {
            dropdown->setBounds(valueRect);
            return;
        }
        if (auto* textField = dynamic_cast<newui::TextField*>(liveEditorWidget_)) {
            newui::Rect textRect = valueRect;
            PropertiesModel::Node node = model_.nodeAt(*path);
            if (node.property != nullptr && node.property->type() == std::type_index(typeid(newui::Color))) {
                textRect = newui::Rect(valueRect.left() + PropertyItem::kSwatchSize + 6.0f, valueRect.top(),
                    valueRect.size().width - PropertyItem::kSwatchSize - 6.0f, valueRect.size().height);
            }
            textField->setBounds(textRect);
        }
    }

    newui::SyncReturn PropertiesGrid::handleLiveTextCommit(newui::View& /*sender*/)
    {
        auto* textField = dynamic_cast<newui::TextField*>(liveEditorWidget_);
        if (textField != nullptr && liveEditor_ != nullptr) {
            liveEditor_->setValueFromString(wideToUtf8(textField->text()));
            treeView_->style().markDirty();
        }
        return newui::SyncReturn::Ignored;
    }

    newui::SyncReturn PropertiesGrid::handleLiveToggleChanged(newui::Toggle& sender)
    {
        if (liveEditor_ != nullptr) {
            liveEditor_->setValueFromString(sender.isChecked() ? "true" : "false");
        }
        return newui::SyncReturn::Handled;
    }

    newui::SyncReturn PropertiesGrid::handleLiveDropdownChanged(newui::DropDownList& sender)
    {
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
        if (!isPointNearDivider(pt)) {
            return newui::SyncReturn::Ignored;
        }
        draggingDivider_ = true;
        return newui::SyncReturn::Handled;
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
