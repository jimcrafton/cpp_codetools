#include "SelectionOverlay.h"

#include <newui/uicolormanager.h>

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

    newui::Rect SelectionOverlay::boundsInRootView(const newui::View* view)
    {
        if (view == nullptr) {
            return newui::Rect();
        }
        return newui::Rect(rootLocalOrigin(view), view->bounds().size());
    }

    void SelectionOverlay::paint(BLContext& ctx, const newui::Rect& /*rect*/)
    {
        const std::vector<newui::SubView*>& selected = controller_.selected();
        if (selected.empty()) {
            return;
        }

        BLRgba32 accent = newui::UIColorManager::colorFor(newui::UIColorRole::HighlightBackground).toBLRgba32();

        // See the constructor's own comment - without this, a selection
        // outline/handle paints straight over the Toolbox/Properties
        // chrome around the design surface, since Overlay paints on top
        // of the *entire* hosting RootView pane, not just clipView_'s own
        // area.
        bool clipping = clipView_ != nullptr;
        if (clipping) {
            newui::Rect clipRect = boundsInRootView(clipView_);
            ctx.save();
            ctx.clip_to_rect(BLRect(clipRect.left(), clipRect.top(), clipRect.size().width, clipRect.size().height));
        }

        for (newui::SubView* view : selected) {
            // Inset by 1px - matches Main.dc.html's own
            // ".node-wrap.selected { outline-offset: -1px }".
            newui::Rect outline = boundsInRootView(view).deflate(1.0f);
            ctx.set_stroke_style(accent);
            ctx.set_stroke_width(kOutlineWidth);
            ctx.stroke_box(outline.left(), outline.top(), outline.right(), outline.bottom());
        }

        if (newui::SubView* primaryView = controller_.primary()) {
            newui::Rect bounds = boundsInRootView(primaryView);
            paintHandle(ctx, bounds.left(), bounds.top(), accent);
            paintHandle(ctx, bounds.right(), bounds.top(), accent);
            paintHandle(ctx, bounds.left(), bounds.bottom(), accent);
            paintHandle(ctx, bounds.right(), bounds.bottom(), accent);
        }

        if (clipping) {
            ctx.restore();
        }
    }
}
