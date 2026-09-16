#pragma once

#include <newui/geometry.h>
#include <newui/shapes.h>
#include <newui/view.h>

namespace CodeToolsVsix
{
    // Where and how a newui::CalloutTool should actually appear - anchored next to
    // anchorScreenRect (the real screen rect of whatever UI element opened it: a button, a
    // Properties-grid row's own "..." affordance, ...), but always fully inside
    // containerScreenRect (the real screen rect of the design surface's own window) rather than
    // spilling off it. Shared by every newui::CalloutTool this codebase shows - not specific to
    // Layout/ViewStyle's own type-swap popup (LayoutPropertyEditor/ViewStylePropertyEditor,
    // PropertyEditor.h/.cpp), its first consumer - any future popup anchored to a clicked UI
    // element should reuse placeCallout() below rather than hand-rolling its own placement math.
    struct CalloutPlacement
    {
        newui::Rect bounds;
        newui::shapes::TailSide tailSide = newui::shapes::TailSide::Top;
        float tailPosition = 0.5f;
    };

    // Tries all 4 sides of anchorScreenRect - below, above, right, left, in that preference order
    // - and picks the first whose *primary* axis genuinely fits inside containerScreenRect, never
    // just clamping past an edge on that axis the way a naive "always below" placement did (a
    // real, reported bug: a popup near a docked panel's own edge used to spill straight off the
    // window). The cross axis is centered on the anchor and clamped into containerScreenRect the
    // same way regardless of which side is chosen. tailPosition is always recomputed against the
    // actual, possibly-clamped cross-axis position so the tail keeps pointing at the anchor's own
    // center even when clamping shifts the popup - a fixed 0.5 would otherwise point at the
    // middle of the popup itself. Falls back to "below" (with the same horizontal clamp, however
    // far it overflows containerScreenRect's own bottom edge) if literally none of the 4 sides
    // fit - still the least-bad option, and this never leaves bounds/tailPosition unset.
    CalloutPlacement placeCallout(const newui::Rect& anchorScreenRect, const newui::Size& popupSize,
        const newui::Rect& containerScreenRect);

    // The real screen rect of the design surface's own window that owner belongs to - owner's
    // RootView, converted via RootView::localToScreen() (newui has no more direct way to get a
    // window's own screen rect as of this pass - confirmed by grepping the whole include tree;
    // worth a real View::localToScreen()/screenToLocal() API in the user's own D:\code\newui repo
    // eventually). The natural containerScreenRect for placeCallout() above whenever "stay inside
    // the visual designer's own window" is literally what's wanted - returns an empty Rect if
    // owner has no attached RootView.
    newui::Rect designerWindowScreenRect(newui::View* owner);
}
