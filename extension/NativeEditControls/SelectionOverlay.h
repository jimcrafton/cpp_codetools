#pragma once

#include "ViewDesignerController.h"

#include <newui/geometry.h>
#include <newui/overlay.h>
#include <newui/subview.h>

namespace CodeToolsVsix
{
    // Selection + handles (bluesky/designer-plan.md 6.1 item 3) - a design-
    // mode-only overlay painted on top of the whole editor pane via newui's
    // existing RootView::setOverlay() mechanism (overlay.h's own class
    // comment names exactly this use: "whatever needs to sit above the
    // entire view hierarchy regardless of z-order within it"). Purely a
    // paint adapter - selection state itself lives on ViewDesignerController
    // (a const reference, given at construction), not here; this class does
    // no hit-testing/mouse handling of its own (Overlay isn't a SubView -
    // see overlay.h) and owns nothing beyond how to draw whatever the
    // controller currently reports selected.
    //
    // Multi-select: ViewDesignerController::selected() is kept in click
    // order, so primary() (selected().back()) is always well-defined -
    // every selected view gets the outline, only primary() also gets the
    // four corner handles. Visual only for v1 - dragging a handle doesn't
    // resize anything yet (a deliberately deferred follow-up).
    class SelectionOverlay : public newui::Overlay
    {
    public:
        explicit SelectionOverlay(const ViewDesignerController& controller) : controller_(controller) {}

        // view's bounds in the coordinate space Overlay::paint() itself
        // already draws in (overlay.h - (0,0) at the hosting RootView's own
        // top-left). Walks view->parent() up to the root, undoing each
        // ancestor's own origin() (scroll offset) one level at a time - the
        // same math View::paintChildren()/hitTestChildren() already apply
        // per level (view.cpp), just accumulated across the whole chain.
        // Deliberately kept local here rather than added to newui::View
        // itself - no second real consumer for a generic version of this
        // exists yet (an "ask before assuming placement" call).
        static newui::Rect boundsInRootView(const newui::View* view);

        void paint(BLContext& ctx, const newui::Rect& rect) override;

    private:
        const ViewDesignerController& controller_;
    };
}
