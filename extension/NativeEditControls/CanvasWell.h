#pragma once

#include <newui/font.h>
#include <newui/geometry.h>
#include <newui/subview.h>

namespace CodeToolsVsix
{
    // The dark "pasteboard" area Workspace centers its FrameProxy inside
    // (Workspace.cpp's own canvasWell_ member) - draws structural alignment
    // guides (thin lines extending across this view's own full bounds, at
    // its one child's - the FrameProxy - top/left/bottom/right edges) plus
    // a width/height dimension ruler (a gapped, arrow-tipped line with a
    // "NNN px" label) in the margin around it, and lets the user drag those
    // same guide lines to resize FrameProxy - deliberately unrelated to
    // SelectionOverlay (selection state never affects any of this). Reads
    // its one child's bounds directly (childViews()[0]) rather than
    // needing a FrameProxy* handed in - Workspace already guarantees
    // FrameProxy is this view's only child (see Workspace.cpp).
    class CanvasWell : public newui::SubView
    {
    public:
        // How close (in pixels, this view's own local space) a point needs
        // to be to a guide line to grab it for a resize drag.
        static constexpr float kEdgeGrabTolerance = 4.0f;
        // FrameProxy never shrinks below this on either axis - keeps the
        // title bar/rounded corners/dimension rulers all still readable
        // rather than letting a drag collapse it to nothing.
        static constexpr float kMinFrameSize = 40.0f;

        CanvasWell();

        void paint(BLContext& ctx) override;

        // Which guide a point in this view's own local space is close
        // enough to grab for a resize drag - Vertical means the left/right
        // guide (drives FrameProxy's width), Horizontal means the top/
        // bottom guide (drives its height). None if pt isn't near either.
        enum class ResizeEdge { None, Vertical, Horizontal };
        ResizeEdge resizeEdgeAt(const newui::Point& localPt) const;

        // Starts/continues/ends a resize drag, and updates the hover
        // cursor - DesignerEditor drives all of these from root-level
        // mouse hooks (root->onMouseDown/onMouseMove/onMouseUp, which fire
        // unconditionally, before hit-testing), not this view's own
        // onMouseDown/onMouseMove/onMouseUp: a click landing on
        // FrameProxy's own hit region (which the guide lines sit right on
        // top of) would otherwise get silently swallowed by its
        // isDesignTime() gating (RootView::resolveInteractiveHit(),
        // rootview.cpp) before ever reaching this view's own delegates -
        // the same reasoning DesignerEditor::handleMouseDownForSelection()
        // already established for selection. FrameProxy stays centered in
        // this view throughout (its own AnchorLayoutParams keep
        // CenterX|CenterY) - dragging either the left or right guide
        // changes width, growing/shrinking symmetrically about the
        // center; top/bottom work the same way for height.
        bool beginResizeDrag(const newui::Point& localPt);
        void continueResizeDrag(const newui::Point& localPt);
        void endResizeDrag();
        bool isResizingDrag() const { return resizing_ != ResizeEdge::None; }
        void updateHoverCursor(const newui::Point& localPt);

    private:
        newui::Font rulerFont_;
        ResizeEdge resizing_ = ResizeEdge::None;
    };
}
