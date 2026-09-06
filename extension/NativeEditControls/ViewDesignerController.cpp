#include "ViewDesignerController.h"

#include <algorithm>

namespace CodeToolsVsix
{
    bool ViewDesignerController::isSelected(const newui::SubView* view) const
    {
        return std::find(selected_.begin(), selected_.end(), view) != selected_.end();
    }

    void ViewDesignerController::selectExclusive(newui::SubView* view)
    {
        selected_.clear();
        if (view != nullptr) {
            selected_.push_back(view);
        }
        onSelectionChanged(*this);
    }

    void ViewDesignerController::toggleSelection(newui::SubView* view)
    {
        if (view == nullptr) {
            return;
        }
        auto it = std::find(selected_.begin(), selected_.end(), view);
        if (it != selected_.end()) {
            selected_.erase(it);
        } else {
            selected_.push_back(view);
        }
        onSelectionChanged(*this);
    }

    void ViewDesignerController::clearSelection()
    {
        selected_.clear();
        onSelectionChanged(*this);
    }
}
