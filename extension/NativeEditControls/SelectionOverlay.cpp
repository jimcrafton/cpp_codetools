#include "SelectionOverlay.h"

#include <newui/uicolormanager.h>

#include <algorithm>

namespace CodeToolsVsix
{
    namespace
    {
        constexpr float kHandleSize = 7.0f;
        constexpr float kHandleHalf = kHandleSize * 0.5f;
        constexpr float kOutlineWidth = 2.0f;

        // Mirrors Main.dc.html's own ".handle" (7x7 white square, 1.5px
        // accent border) - centered on the given point, matching the
        // mockup's corner-of-the-box placement (h-tl/h-tr/h-bl/h-br).
        void paintHandle(BLContext& ctx, float cx, float cy, BLRgba32 borderColor)
        {
            BLRect r(cx - kHandleHalf, cy - kHandleHalf, kHandleSize, kHandleSize);
            ctx.set_fill_style(BLRgba32(0xFFFFFFFFu));
            ctx.fill_rect(r);
            ctx.set_stroke_style(borderColor);
            ctx.set_stroke_width(1.5);
            ctx.stroke_rect(r);
        }

        // Recursive step behind SelectionOverlay::boundsInRootView() below -
        // see that method's own comment for the math this composes.
        newui::Point rootLocalOrigin(const newui::View* view)
        {
            if (view == nullptr) {
                return newui::Point(0.0f, 0.0f);
            }
            const newui::View* parent = view->parent();
            if (parent == nullptr) {
                return newui::Point(0.0f, 0.0f);
            }
            newui::Point parentOrigin = rootLocalOrigin(parent);
            return newui::Point(
                parentOrigin.x + view->bounds().left() - parent->origin().x,
                parentOrigin.y + view->bounds().top() - parent->origin().y);
        }
    }

    bool SelectionOverlay::isSelected(const newui::SubView* view) const
    {
        return std::find(selected_.begin(), selected_.end(), view) != selected_.end();
    }

    void SelectionOverlay::selectExclusive(newui::SubView* view)
    {
        selected_.clear();
        if (view != nullptr) {
            selected_.push_back(view);
        }
    }

    void SelectionOverlay::toggleSelection(newui::SubView* view)
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
    }

    void SelectionOverlay::clearSelection()
    {
        selected_.clear();
    }

    newui::Rect SelectionOverlay::boundsInRootView(const newui::View* view)
    {
        if (view == nullptr) {
            return newui::Rect();
        }
        return newui::Rect(rootLocalOrigin(view), view->bounds().size());
    }

    void SelectionOverlay::paint(BLContext& ctx, const newui::Rect& /*rect*/)
    {
        if (selected_.empty()) {
            return;
        }

        BLRgba32 accent = newui::UIColorManager::colorFor(newui::UIColorRole::HighlightBackground).toBLRgba32();

        for (newui::SubView* view : selected_) {
            // Inset by 1px - matches Main.dc.html's own
            // ".node-wrap.selected { outline-offset: -1px }".
            newui::Rect outline = boundsInRootView(view).deflate(1.0f);
            ctx.set_stroke_style(accent);
            ctx.set_stroke_width(kOutlineWidth);
            ctx.stroke_box(outline.left(), outline.top(), outline.right(), outline.bottom());
        }

        if (newui::SubView* primaryView = primary()) {
            newui::Rect bounds = boundsInRootView(primaryView);
            paintHandle(ctx, bounds.left(), bounds.top(), accent);
            paintHandle(ctx, bounds.right(), bounds.top(), accent);
            paintHandle(ctx, bounds.left(), bounds.bottom(), accent);
            paintHandle(ctx, bounds.right(), bounds.bottom(), accent);
        }
    }
}
