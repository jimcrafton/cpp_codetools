#include "../extension/NativeEditControls/SelectionOverlay.h"

#include <newui/layout.h>
#include <newui/rootview.h>
#include <newui/subview.h>

#include <blend2d/blend2d.h>

#include <gtest/gtest.h>

#include <memory>

using CodeToolsVsix::SelectionOverlay;
using CodeToolsVsix::ViewDesignerController;

// Selection state/mutation itself now lives on ViewDesignerController (see
// test_view_designer_controller.cpp) - SelectionOverlay is purely a paint
// adapter over a ViewDesignerController it's given, plus the
// boundsInRootView() geometry helper below, which stays here since it's
// unrelated to selection state.

namespace {
    bool anyPixelPainted(const BLImage& surface, int width, int height) {
        BLImageData data;
        surface.get_data(&data);
        const uint8_t* bytes = static_cast<const uint8_t*>(data.pixel_data);
        for (int row = 0; row < height; ++row) {
            const uint8_t* rowBytes = bytes + row * data.stride;
            for (int i = 0; i < width * 4; ++i) {
                if (rowBytes[i] != 0) {
                    return true;
                }
            }
        }
        return false;
    }
}

TEST(SelectionOverlay, PaintDoesNothingWithNoSelection)
{
    ViewDesignerController controller;
    SelectionOverlay overlay(controller);

    BLImage surface;
    ASSERT_EQ(surface.create(64, 64, BL_FORMAT_PRGB32), BL_SUCCESS);
    BLContext ctx(surface);
    ctx.clear_all();
    overlay.paint(ctx, newui::Rect(0, 0, 64, 64));
    ctx.end();

    EXPECT_FALSE(anyPixelPainted(surface, 64, 64));
}

TEST(SelectionOverlay, PaintDrawsSomethingForARealSelection)
{
    ViewDesignerController controller;
    SelectionOverlay overlay(controller);

    newui::RootView root(nullptr, newui::Rect(0, 0, 64, 64), "root");
    auto* child = new newui::SubView();
    child->setBounds(newui::Rect(10, 10, 30, 30));
    root.addChild(child);
    controller.selectExclusive(child);

    BLImage surface;
    ASSERT_EQ(surface.create(64, 64, BL_FORMAT_PRGB32), BL_SUCCESS);
    BLContext ctx(surface);
    ctx.clear_all();
    overlay.paint(ctx, newui::Rect(0, 0, 64, 64));
    ctx.end();

    EXPECT_TRUE(anyPixelPainted(surface, 64, 64));
}

TEST(SelectionOverlay, PaintClipsToTheGivenClipViewsBounds)
{
    // A real, reported bug: with no clip, a selection outline paints
    // straight over the Toolbox/Properties chrome around the design
    // surface, since Overlay paints on top of the *entire* hosting
    // RootView pane (overlay.h), not just the design surface's own area.
    // clipView here (left half of the canvas) stands in for
    // DesignerEditor's real workspace_->rootViewProxy() - child's bounds
    // (right half) fall entirely outside it, so nothing should paint at
    // all once clipped.
    ViewDesignerController controller;

    newui::RootView root(nullptr, newui::Rect(0, 0, 64, 64), "root");
    auto* clipView = new newui::SubView();
    clipView->setBounds(newui::Rect(0, 0, 32, 64));
    root.addChild(clipView);

    auto* child = new newui::SubView();
    child->setBounds(newui::Rect(40, 10, 20, 20));
    root.addChild(child);
    controller.selectExclusive(child);

    SelectionOverlay overlay(controller, clipView);

    BLImage surface;
    ASSERT_EQ(surface.create(64, 64, BL_FORMAT_PRGB32), BL_SUCCESS);
    BLContext ctx(surface);
    ctx.clear_all();
    overlay.paint(ctx, newui::Rect(0, 0, 64, 64));
    ctx.end();

    EXPECT_FALSE(anyPixelPainted(surface, 64, 64));
}

TEST(SelectionOverlay, PaintWithNoClipViewGivenStaysUnclipped)
{
    // Same selection (entirely in the canvas' right half) as
    // PaintClipsToTheGivenClipViewsBounds above, but with no clipView
    // given at all (the default) - confirms that overload keeps its
    // original, unclipped behavior, so every existing caller/test
    // constructing SelectionOverlay with just a controller is unaffected.
    ViewDesignerController controller;

    newui::RootView root(nullptr, newui::Rect(0, 0, 64, 64), "root");
    auto* child = new newui::SubView();
    child->setBounds(newui::Rect(40, 10, 20, 20));
    root.addChild(child);
    controller.selectExclusive(child);

    SelectionOverlay overlay(controller);

    BLImage surface;
    ASSERT_EQ(surface.create(64, 64, BL_FORMAT_PRGB32), BL_SUCCESS);
    BLContext ctx(surface);
    ctx.clear_all();
    overlay.paint(ctx, newui::Rect(0, 0, 64, 64));
    ctx.end();

    EXPECT_TRUE(anyPixelPainted(surface, 64, 64));
}

TEST(SelectionOverlay, PaintDrawsALayoutAdornmentAboveTheParentForANestedSelection)
{
    // The layout adornment (parent boundary + a badge naming its real Layout) sits strictly
    // above the parent's own top edge - a region the plain outline/handles (drawn around
    // child, y in [40,70]) never reach - so any painted pixel up there can only be this.
    ViewDesignerController controller;

    newui::RootView root(nullptr, newui::Rect(0, 0, 200, 200), "root");
    auto* container = new newui::SubView();
    container->setBounds(newui::Rect(20, 40, 100, 100));
    container->setLayout(std::make_unique<newui::FlexLayout>(newui::Orientation::Horizontal));
    root.addChild(container);

    auto* child = new newui::SubView();
    child->setBounds(newui::Rect(0, 0, 30, 30));
    child->setVisible(true);
    container->addChild(child);

    controller.selectExclusive(child);
    SelectionOverlay overlay(controller);

    BLImage surface;
    ASSERT_EQ(surface.create(200, 200, BL_FORMAT_PRGB32), BL_SUCCESS);
    BLContext ctx(surface);
    ctx.clear_all();
    overlay.paint(ctx, newui::Rect(0, 0, 200, 200));
    ctx.end();

    BLImageData data;
    surface.get_data(&data);
    const uint8_t* bytes = static_cast<const uint8_t*>(data.pixel_data);
    bool paintedAboveTheParent = false;
    for (int row = 0; row < 39 && !paintedAboveTheParent; ++row) {
        const uint8_t* rowBytes = bytes + row * data.stride;
        for (int i = 0; i < 200 * 4; ++i) {
            if (rowBytes[i] != 0) {
                paintedAboveTheParent = true;
                break;
            }
        }
    }
    EXPECT_TRUE(paintedAboveTheParent);
}

TEST(SelectionOverlay, PaintSkipsTheLayoutAdornmentWhenParentIsTheTopLevelRootView)
{
    // Regression test for a real bug this feature's own addition caused: a child added
    // directly to the top-level RootView (parent->parent() == nullptr) previously got a
    // parent-boundary/badge drawn for the *entire* RootView, which broke clipping (the
    // RootView is bigger than any real clip target) and conveyed nothing useful anyway.
    ViewDesignerController controller;

    newui::RootView root(nullptr, newui::Rect(0, 0, 64, 64), "root");
    auto* child = new newui::SubView();
    child->setBounds(newui::Rect(10, 10, 30, 30));
    root.addChild(child);
    controller.selectExclusive(child);
    SelectionOverlay overlay(controller);

    BLImage surface;
    ASSERT_EQ(surface.create(64, 64, BL_FORMAT_PRGB32), BL_SUCCESS);
    BLContext ctx(surface);
    ctx.clear_all();
    overlay.paint(ctx, newui::Rect(0, 0, 64, 64));
    ctx.end();

    // Row 0 sits well above child's own outline/handles (child's top-left handle is centered
    // at y=10, 7px tall) - a parent-boundary/badge for root would paint there; nothing else does.
    BLImageData data;
    surface.get_data(&data);
    const uint8_t* row0 = static_cast<const uint8_t*>(data.pixel_data);
    bool paintedOnRootsOwnTopEdge = false;
    for (int i = 0; i < 64 * 4; ++i) {
        if (row0[i] != 0) {
            paintedOnRootsOwnTopEdge = true;
            break;
        }
    }
    EXPECT_FALSE(paintedOnRootsOwnTopEdge);
}

TEST(SelectionOverlay, BoundsInRootViewForADirectChildOfRootIsItsOwnBounds)
{
    newui::RootView root(nullptr, newui::Rect(0, 0, 500, 500), "root");
    auto* child = new newui::SubView();
    child->setBounds(newui::Rect(50, 60, 200, 150));
    root.addChild(child);

    newui::Rect result = SelectionOverlay::boundsInRootView(child);
    EXPECT_EQ(result, newui::Rect(50, 60, 200, 150));
}

TEST(SelectionOverlay, BoundsInRootViewAccumulatesThroughNestedContainersAndScrollOrigin)
{
    newui::RootView root(nullptr, newui::Rect(0, 0, 500, 500), "root");

    auto* container = new newui::SubView();
    container->setBounds(newui::Rect(50, 60, 300, 300));
    root.addChild(container);
    // A scroll offset on container shifts where its own children draw/hit-
    // test (View::paintChildren()/hitTestChildren(), view.cpp) - the same
    // shift boundsInRootView() has to undo to land on the right screen
    // position.
    container->setOrigin(newui::Point(10.0f, 20.0f));

    auto* child = new newui::SubView();
    child->setBounds(newui::Rect(5, 7, 30, 40));
    container->addChild(child);

    newui::Rect result = SelectionOverlay::boundsInRootView(child);
    EXPECT_EQ(result, newui::Rect(50.0f + 5.0f - 10.0f, 60.0f + 7.0f - 20.0f, 30, 40));
}

TEST(SelectionOverlay, BoundsInRootViewForNullViewIsAnEmptyRect)
{
    EXPECT_EQ(SelectionOverlay::boundsInRootView(nullptr), newui::Rect());
}
