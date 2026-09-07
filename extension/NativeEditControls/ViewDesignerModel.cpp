#include "ViewDesignerModel.h"

#include <algorithm>

namespace CodeToolsVsix
{
    void ViewDesignerModel::setRoot(newui::SubView* root)
    {
        root_ = root;
        onChanged(*this);
    }

    void ViewDesignerModel::refresh()
    {
        onChanged(*this);
    }

    std::size_t ViewDesignerModel::childCount(const std::vector<std::size_t>& path) const
    {
        if (path.empty()) {
            return root_ != nullptr ? 1 : 0;
        }
        newui::SubView* view = viewAt(path);
        return view != nullptr ? view->childViews().size() : 0;
    }

    newui::SubView* ViewDesignerModel::viewAt(const std::vector<std::size_t>& path) const
    {
        if (path.empty() || path[0] != 0 || root_ == nullptr) {
            return nullptr;
        }
        newui::SubView* current = root_;
        for (std::size_t i = 1; i < path.size(); ++i) {
            const auto& children = current->childViews();
            if (path[i] >= children.size()) {
                return nullptr;
            }
            current = children[path[i]];
        }
        return current;
    }

    std::optional<std::vector<std::size_t>> ViewDesignerModel::pathFor(const newui::SubView* view) const
    {
        if (view == nullptr || root_ == nullptr) {
            return std::nullopt;
        }
        if (static_cast<const newui::View*>(view) == static_cast<const newui::View*>(root_)) {
            return std::vector<std::size_t>{0};
        }

        std::vector<std::size_t> reversed;
        const newui::View* current = view;
        while (static_cast<const newui::View*>(current) != static_cast<const newui::View*>(root_)) {
            const newui::View* parent = current->parent();
            if (parent == nullptr) {
                return std::nullopt;
            }
            const auto& siblings = parent->childViews();
            auto it = std::find_if(siblings.begin(), siblings.end(), [current](const newui::SubView* s) {
                return static_cast<const newui::View*>(s) == current;
            });
            if (it == siblings.end()) {
                return std::nullopt;
            }
            reversed.push_back(static_cast<std::size_t>(it - siblings.begin()));
            current = parent;
        }
        reversed.push_back(0);
        std::reverse(reversed.begin(), reversed.end());
        return reversed;
    }
}
