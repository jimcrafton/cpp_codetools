#pragma once

#include <newui/font.h>
#include <newui/subview.h>

namespace CodeToolsVsix
{
    // The dark "pasteboard" area Workspace centers its FrameProxy inside
    // (Workspace.cpp's own canvasWell_ member) - draws structural alignment
    // guides (thin lines extending across this view's own full bounds, at
    // its one child's - the FrameProxy - top/left/bottom/right edges) plus
    // a width/height dimension ruler (a gapped, arrow-tipped line with a
    // "NNN px" label) in the margin around it. Purely decorative/
    // informational - no hit-testing of its own, and deliberately unrelated
    // to SelectionOverlay (selection state never affects this). Reads its
    // one child's bounds directly (childViews()[0]) rather than needing a
    // FrameProxy* handed in - Workspace already guarantees FrameProxy is
    // this view's only child (see Workspace.cpp).
    class CanvasWell : public newui::SubView
    {
    public:
        CanvasWell();

        void paint(BLContext& ctx) override;

    private:
        newui::Font rulerFont_;
    };
}
