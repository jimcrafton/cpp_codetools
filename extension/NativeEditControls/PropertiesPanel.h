#pragma once

#include "PropertiesGrid.h"

#include <newui/controls.h>
#include <newui/subview.h>

namespace CodeToolsVsix
{
    // The Properties pane: a slim header - a filter box and an "A-Z" toggle - above the
    // PropertiesGrid. The header can't live inside the grid (a ScrollView: it would scroll away with
    // the rows), so this wraps both. Typing in the box narrows the grid to properties whose name
    // contains the text; the toggle switches it between reflection order and A-Z (see
    // PropertiesModel::setFilter()/setAlphabetical() for the exact rules). The choices are the
    // model's, so they stay as different controls are selected.
    class PropertiesPanel : public newui::SubView
    {
    public:
        PropertiesPanel();

        PropertiesGrid* grid() const { return grid_; }
        newui::TextField* filterField() const { return filterField_; }
        newui::Button* sortButton() const { return sortButton_; }

        static constexpr float kHeaderHeight = 30.0f;

    private:
        newui::SyncReturn handleFilterTextChanged(newui::Model& sender);
        newui::SyncReturn handleSortToggled(newui::Button& sender);

        PropertiesGrid* grid_ = nullptr;
        newui::TextField* filterField_ = nullptr;
        newui::Button* sortButton_ = nullptr;
    };
}
