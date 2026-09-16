#include "../extension/NativeEditControls/CalloutPlacement.h"

#include <gtest/gtest.h>

using CodeToolsVsix::CalloutPlacement;
using CodeToolsVsix::placeCallout;

namespace
{
    // A generous window with plenty of room on every side of the anchor - the "everything fits"
    // baseline every other test deviates from one edge at a time.
    const newui::Rect kRoomyContainer(0.0f, 0.0f, 1000.0f, 1000.0f);
}

TEST(PlaceCallout, PrefersBelowWhenEverythingFits)
{
    newui::Rect anchor(500.0f, 500.0f, 20.0f, 20.0f);
    CalloutPlacement placement = placeCallout(anchor, newui::Size(200.0f, 100.0f), kRoomyContainer);

    EXPECT_EQ(placement.tailSide, newui::shapes::TailSide::Top);
    EXPECT_FLOAT_EQ(placement.bounds.top(), anchor.bottom() + 6.0f);
    // Horizontally centered on the anchor.
    float anchorCenterX = anchor.left() + anchor.width() * 0.5f;
    float boundsCenterX = placement.bounds.left() + placement.bounds.width() * 0.5f;
    EXPECT_NEAR(anchorCenterX, boundsCenterX, 0.01f);
    EXPECT_NEAR(placement.tailPosition, 0.5f, 0.01f);
}

TEST(PlaceCallout, FlipsAboveWhenBelowDoesNotFitButAboveDoes)
{
    // Anchor near the container's own bottom edge - no room below for a 100px-tall popup.
    newui::Rect anchor(500.0f, 950.0f, 20.0f, 20.0f);
    CalloutPlacement placement = placeCallout(anchor, newui::Size(200.0f, 100.0f), kRoomyContainer);

    EXPECT_EQ(placement.tailSide, newui::shapes::TailSide::Bottom);
    EXPECT_FLOAT_EQ(placement.bounds.bottom(), anchor.top() - 6.0f);
    EXPECT_GE(placement.bounds.top(), kRoomyContainer.top());
}

TEST(PlaceCallout, FliesOutToTheRightWhenNeitherVerticalDirectionFits)
{
    // Container tall enough to hold the popup somewhere, but the anchor sits flush against the
    // top edge - too little room above it (goes negative) *and*, since the container's own height
    // equals exactly anchor.bottom()+gap+popupHeight, not a pixel more below it either. Plenty of
    // room to its right, though - the popup should flip to a sideways flyout instead.
    newui::Rect container(0.0f, 0.0f, 1000.0f, 100.0f);
    newui::Rect anchor(10.0f, 0.0f, 20.0f, 20.0f);
    CalloutPlacement placement = placeCallout(anchor, newui::Size(200.0f, 100.0f), container);

    EXPECT_EQ(placement.tailSide, newui::shapes::TailSide::Left);
    EXPECT_FLOAT_EQ(placement.bounds.left(), anchor.right() + 6.0f);
    // Vertically centered on the anchor, clamped into the container.
    EXPECT_GE(placement.bounds.top(), container.top());
    EXPECT_LE(placement.bounds.bottom(), container.bottom());
}

TEST(PlaceCallout, FliesOutToTheLeftWhenNothingElseFits)
{
    // Anchor pinned to the container's own top-right corner - no room above, no room below (same
    // exact-height trick as the "flies right" test above), and no room to its right either
    // (flush against the container's own right edge) - only "left" is left.
    newui::Rect container(0.0f, 0.0f, 300.0f, 100.0f);
    newui::Rect anchor(280.0f, 0.0f, 20.0f, 20.0f);
    CalloutPlacement placement = placeCallout(anchor, newui::Size(200.0f, 100.0f), container);

    EXPECT_EQ(placement.tailSide, newui::shapes::TailSide::Right);
    EXPECT_FLOAT_EQ(placement.bounds.right(), anchor.left() - 6.0f);
}

TEST(PlaceCallout, ClampsTheCrossAxisIntoTheContainerAndKeepsTheTailPointingAtTheAnchor)
{
    // Anchor near the container's own right edge - centering a 200px-wide popup under it would
    // push the popup's own right edge past the container, so it should clamp left instead, and
    // the tail should still point back at the anchor's real horizontal center (not the popup's).
    newui::Rect container(0.0f, 0.0f, 400.0f, 1000.0f);
    newui::Rect anchor(390.0f, 100.0f, 20.0f, 20.0f);
    CalloutPlacement placement = placeCallout(anchor, newui::Size(200.0f, 100.0f), container);

    EXPECT_EQ(placement.tailSide, newui::shapes::TailSide::Top);
    EXPECT_LE(placement.bounds.right(), container.right());
    EXPECT_GT(placement.tailPosition, 0.5f);  // anchor sits right of the (now left-shifted) popup's own center
    EXPECT_LE(placement.tailPosition, 0.88f);  // still clamped off the rounded corner
}

TEST(PlaceCallout, TailPositionNeverReachesTheRoundedCorners)
{
    // Anchor pinned to the container's own left edge - without clamping, the tail fraction would
    // compute past 0 entirely (the anchor's center sits left of the clamped popup's own left
    // edge) - CalloutRoundRect::tailPosition()'s own doc comment says an out-of-[0,1] value places
    // the tip past the rounded corner, so this must stay clamped inside a safe margin instead.
    newui::Rect container(0.0f, 0.0f, 1000.0f, 1000.0f);
    newui::Rect anchor(0.0f, 100.0f, 10.0f, 10.0f);
    CalloutPlacement placement = placeCallout(anchor, newui::Size(200.0f, 100.0f), container);

    EXPECT_GE(placement.tailPosition, 0.12f);
    EXPECT_LE(placement.tailPosition, 0.88f);
}

TEST(PlaceCallout, FallsBackToBelowWhenLiterallyNothingFits)
{
    // A container far too small in every direction - still must return *something* well-formed
    // rather than crashing or leaving bounds/tailPosition unset.
    newui::Rect container(0.0f, 0.0f, 30.0f, 30.0f);
    newui::Rect anchor(10.0f, 10.0f, 10.0f, 10.0f);
    CalloutPlacement placement = placeCallout(anchor, newui::Size(200.0f, 100.0f), container);

    EXPECT_EQ(placement.tailSide, newui::shapes::TailSide::Top);
    EXPECT_GE(placement.tailPosition, 0.0f);
    EXPECT_LE(placement.tailPosition, 1.0f);
}

TEST(DesignerWindowScreenRect, EmptyForNullOwner)
{
    EXPECT_TRUE(CodeToolsVsix::designerWindowScreenRect(nullptr).size().width == 0.0f);
}
