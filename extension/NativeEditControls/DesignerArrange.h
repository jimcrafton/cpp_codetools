#pragma once

#include <vector>

#include <newui/geometry.h>
#include <newui/subview.h>

namespace CodeToolsVsix
{
    // The View Designer's arrange operations - z-order, alignment, distribution, same-size - as pure
    // computations over a selection: each returns the changes to make (old and new state), and the
    // caller applies them as one undoable step. Nothing here mutates a view.

    enum class ZOrderOp { BringToFront, SendToBack, BringForward, SendBackward };

    // One parent's children before and after a z-order operation (the whole child list, in paint
    // order - later is on top). Only parents whose order actually changes are returned.
    struct OrderChange
    {
        newui::View* parent = nullptr;
        std::vector<newui::SubView*> before;
        std::vector<newui::SubView*> after;
    };

    // Moves the selected children within their parents, keeping the selected ones' relative order:
    // to the very front/back, or one step past the nearest unselected sibling.
    std::vector<OrderChange> computeZOrder(const std::vector<newui::SubView*>& selected, ZOrderOp op);

    enum class AlignKind { Left, HorizontalCenter, Right, Top, VerticalMiddle, Bottom };
    enum class DistributeKind { Horizontal, Vertical };
    enum class MatchSizeKind { Width, Height, Both };

    // A view's parent-local bounds before and after.
    struct BoundsChange
    {
        newui::SubView* view = nullptr;
        newui::Rect before;
        newui::Rect after;
    };

    // Whether view's geometry is its own to set: its parent has no Layout, or one that leaves child
    // positions to the child (AnchorLayout). A Flex/Grid/Card child's bounds come from its layout.
    bool hasFreeGeometry(const newui::SubView* view);

    // Aligns every free-geometry view in `views` to `anchor` (the "dominant" control - the last one
    // selected, by convention), compared in root coordinates so views in different containers
    // line up on screen. `anchor` itself never moves. Returns only the views that actually move.
    std::vector<BoundsChange> computeAlign(const std::vector<newui::SubView*>& views,
        const newui::SubView* anchor, AlignKind kind);

    // Spaces the free-geometry views evenly between the two outermost ones (which don't move).
    // Needs at least three; returns nothing otherwise.
    std::vector<BoundsChange> computeDistribute(const std::vector<newui::SubView*>& views, DistributeKind kind);

    // Gives every other free-geometry view anchor's width and/or height, keeping its top-left.
    std::vector<BoundsChange> computeMatchSize(const std::vector<newui::SubView*>& views,
        const newui::SubView* anchor, MatchSizeKind kind);
}
