#include "PropertiesPanel.h"

#include "TextEncoding.h"

#include <newui/layout.h>
#include <newui/uicolormanager.h>

namespace CodeToolsVsix
{
    PropertiesPanel::PropertiesPanel()
    {
        setVisible(true);
        setLayout(std::make_unique<newui::FlexLayout>(newui::Orientation::Vertical));

        auto* header = new newui::SubView();
        header->setName("propertiesFilterBar");
        header->setVisible(true);
        header->style().setBackgroundColor(newui::UIColorManager::colorFor(newui::UIColorRole::ControlBackground));
        auto headerLayout = std::make_unique<newui::FlexLayout>(newui::Orientation::Horizontal);
        headerLayout->setSpacing(4.0f);
        headerLayout->setPadding(4.0f);
        header->setLayout(std::move(headerLayout));
        header->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(0.0f));
        header->setDesiredSize(newui::Size(0.0f, kHeaderHeight));

        filterField_ = new newui::TextField();
        filterField_->setName("propertiesFilterField");
        filterField_->setVisible(true);
        filterField_->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
        filterField_->model().onChanged.add(this, &PropertiesPanel::handleFilterTextChanged);
        header->addChild(filterField_);

        sortButton_ = new newui::Button();
        sortButton_->setName("propertiesSortButton");
        sortButton_->setVisible(true);
        sortButton_->setText("A-Z");
        sortButton_->setToggleButton(true);
        sortButton_->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(0.0f));
        sortButton_->setDesiredSize(newui::Size(44.0f, 22.0f));
        sortButton_->onCheckedChanged.add(this, &PropertiesPanel::handleSortToggled);
        header->addChild(sortButton_);
        addChild(header);

        grid_ = new PropertiesGrid();
        grid_->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
        addChild(grid_);
    }

    newui::SyncReturn PropertiesPanel::handleFilterTextChanged(newui::Model& /*sender*/)
    {
        grid_->setFilterText(wideToUtf8(filterField_->text()));
        return newui::SyncReturn::Handled;
    }

    newui::SyncReturn PropertiesPanel::handleSortToggled(newui::Button& /*sender*/)
    {
        grid_->setAlphabetical(sortButton_->isChecked());
        return newui::SyncReturn::Handled;
    }
}
