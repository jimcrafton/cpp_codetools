#include "DesignerArrange.h"

#include "LayoutEditingPolicy.h"
#include "SelectionOverlay.h"

#include <algorithm>

namespace CodeToolsVsix
{
    namespace
    {
        bool contains(const std::vector<newui::SubView*>& list, const newui::SubView* view)
        {
            return std::find(list.begin(), list.end(), view) != list.end();
        }

        newui::Rect shifted(const newui::Rect& r, float dx, float dy)
        {
            return newui::Rect(r.left() + dx, r.top() + dy, r.width(), r.height());
        }

        // Moves the selected entries of `order` (paint order) and returns the new order.
        std::vector<newui::SubView*> reordered(const std::vector<newui::SubView*>& order,
            const std::vector<newui::SubView*>& selected, ZOrderOp op)
        {
            std::vector<newui::SubView*> result = order;
            auto isSelected = [&](newui::SubView* v) { return contains(selected, v); };

            switch (op) {
            case ZOrderOp::BringToFront:
                std::stable_partition(result.begin(), result.end(), [&](newui::SubView* v) { return !isSelected(v); });
                break;
            case ZOrderOp::SendToBack:
                std::stable_partition(result.begin(), result.end(), isSelected);
                break;
            case ZOrderOp::BringForward:
                // From the top down, so two adjacent selected views both step up past the same sibling.
                for (std::size_t i = result.size(); i-- > 0;) {
                    if (isSelected(result[i]) && i + 1 < result.size() && !isSelected(result[i + 1])) {
                        std::swap(result[i], result[i + 1]);
                    }
                }
                break;
            case ZOrderOp::SendBackward:
                for (std::size_t i = 0; i < result.size(); ++i) {
                    if (isSelected(result[i]) && i > 0 && !isSelected(result[i - 1])) {
                        std::swap(result[i], result[i - 1]);
                    }
                }
                break;
            }
            return result;
        }

        std::vector<newui::SubView*> freeViews(const std::vector<newui::SubView*>& views)
        {
            std::vector<newui::SubView*> result;
            for (newui::SubView* view : views) {
                if (hasFreeGeometry(view) && !contains(result, view)) {
                    result.push_back(view);
                }
            }
            return result;
        }
    }

    std::vector<OrderChange> computeZOrder(const std::vector<newui::SubView*>& selected, ZOrderOp op)
    {
        // Group the selection by parent, in first-seen order.
        std::vector<newui::View*> parents;
        for (newui::SubView* view : selected) {
            newui::View* parent = view->parent();
            if (parent != nullptr && std::find(parents.begin(), parents.end(), parent) == parents.end()) {
                parents.push_back(parent);
            }
        }

        std::vector<OrderChange> changes;
        for (newui::View* parent : parents) {
            std::vector<newui::SubView*> siblings = parent->childViews();
            std::vector<newui::SubView*> mine;
            for (newui::SubView* view : selected) {
                if (view->parent() == parent) {
                    mine.push_back(view);
                }
            }
            std::vector<newui::SubView*> after = reordered(siblings, mine, op);
            if (after != siblings) {
                changes.push_back({ parent, std::move(siblings), std::move(after) });
            }
        }
        return changes;
    }

    bool hasFreeGeometry(const newui::SubView* view)
    {
        const newui::View* parent = view != nullptr ? view->parent() : nullptr;
        if (parent == nullptr) {
            return false;
        }
        return policyFor(const_cast<newui::Layout*>(parent->layout())).kind() == GeometryEditKind::FreePosition;
    }

    std::vector<BoundsChange> computeAlign(const std::vector<newui::SubView*>& views,
        const newui::SubView* anchor, AlignKind kind)
    {
        std::vector<BoundsChange> changes;
        if (anchor == nullptr) {
            return changes;
        }
        const newui::Rect target = SelectionOverlay::boundsInRootView(anchor);

        for (newui::SubView* view : freeViews(views)) {
            if (view == anchor) {
                continue;
            }
            const newui::Rect now = SelectionOverlay::boundsInRootView(view);
            float dx = 0.0f;
            float dy = 0.0f;
            switch (kind) {
            case AlignKind::Left: dx = target.left() - now.left(); break;
            case AlignKind::HorizontalCenter:
                dx = (target.left() + target.width() * 0.5f) - (now.left() + now.width() * 0.5f); break;
            case AlignKind::Right: dx = target.right() - now.right(); break;
            case AlignKind::Top: dy = target.top() - now.top(); break;
            case AlignKind::VerticalMiddle:
                dy = (target.top() + target.height() * 0.5f) - (now.top() + now.height() * 0.5f); break;
            case AlignKind::Bottom: dy = target.bottom() - now.bottom(); break;
            }
            if (dx != 0.0f || dy != 0.0f) {
                // Parents only translate, so a root-space delta is the same delta in parent-local space.
                changes.push_back({ view, view->bounds(), shifted(view->bounds(), dx, dy) });
            }
        }
        return changes;
    }

    std::vector<BoundsChange> computeDistribute(const std::vector<newui::SubView*>& views, DistributeKind kind)
    {
        std::vector<BoundsChange> changes;
        std::vector<newui::SubView*> free = freeViews(views);
        if (free.size() < 3) {
            return changes;
        }

        const bool horizontal = kind == DistributeKind::Horizontal;
        auto start = [&](newui::SubView* v) {
            newui::Rect r = SelectionOverlay::boundsInRootView(v);
            return horizontal ? r.left() : r.top();
        };
        auto extent = [&](newui::SubView* v) {
            newui::Rect r = SelectionOverlay::boundsInRootView(v);
            return horizontal ? r.width() : r.height();
        };
        std::stable_sort(free.begin(), free.end(), [&](newui::SubView* a, newui::SubView* b) {
            return start(a) < start(b);
        });

        const float first = start(free.front());
        const float last = start(free.back()) + extent(free.back());
        float used = 0.0f;
        for (newui::SubView* v : free) {
            used += extent(v);
        }
        const float gap = (last - first - used) / static_cast<float>(free.size() - 1);

        float cursor = first;
        for (std::size_t i = 0; i < free.size(); ++i) {
            newui::SubView* v = free[i];
            if (i != 0 && i + 1 != free.size()) {
                const float delta = cursor - start(v);
                if (delta != 0.0f) {
                    changes.push_back({ v, v->bounds(),
                        horizontal ? shifted(v->bounds(), delta, 0.0f) : shifted(v->bounds(), 0.0f, delta) });
                }
            }
            cursor += extent(v) + gap;
        }
        return changes;
    }

    std::vector<BoundsChange> computeMatchSize(const std::vector<newui::SubView*>& views,
        const newui::SubView* anchor, MatchSizeKind kind)
    {
        std::vector<BoundsChange> changes;
        if (anchor == nullptr) {
            return changes;
        }
        const newui::Rect model = anchor->bounds();
        for (newui::SubView* view : freeViews(views)) {
            if (view == anchor) {
                continue;
            }
            const newui::Rect now = view->bounds();
            const float width = kind == MatchSizeKind::Height ? now.width() : model.width();
            const float height = kind == MatchSizeKind::Width ? now.height() : model.height();
            if (width != now.width() || height != now.height()) {
                changes.push_back({ view, now, newui::Rect(now.left(), now.top(), width, height) });
            }
        }
        return changes;
    }
}
