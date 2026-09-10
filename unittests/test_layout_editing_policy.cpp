#include "../extension/NativeEditControls/LayoutEditingPolicy.h"

#include <newui/rootview.h>
#include <newui/subview.h>

#include <blend2d/blend2d.h>

#include <gtest/gtest.h>

using namespace CodeToolsVsix;

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

// ---------------------------------------------------------------------------
// policyFor() dispatch - the real newui::Layout subtype -> kind() mapping.
// ---------------------------------------------------------------------------

TEST(PolicyFor, NullLayoutResolvesToFreePosition) {
    EXPECT_EQ(policyFor(nullptr).kind(), GeometryEditKind::FreePosition);
}

TEST(PolicyFor, AnchorLayoutResolvesToFreePosition) {
    newui::AnchorLayout layout;
    EXPECT_EQ(policyFor(&layout).kind(), GeometryEditKind::FreePosition);
}

TEST(PolicyFor, FlexLayoutResolvesToLinearReorder) {
    newui::FlexLayout layout;
    EXPECT_EQ(policyFor(&layout).kind(), GeometryEditKind::LinearReorder);
}

TEST(PolicyFor, GridLayoutResolvesToGridCell) {
    newui::GridLayout layout;
    EXPECT_EQ(policyFor(&layout).kind(), GeometryEditKind::GridCell);
}

TEST(PolicyFor, CardLayoutResolvesToNone) {
    newui::CardLayout layout;
    EXPECT_EQ(policyFor(&layout).kind(), GeometryEditKind::None);
}

// ---------------------------------------------------------------------------
// FreePositionPolicy
// ---------------------------------------------------------------------------

TEST(FreePositionPolicyResolve, ProposesBoundsShiftedByTheDragDelta) {
    GeometryDragContext ctx;
    ctx.startBounds = newui::Rect(10, 20, 30, 40);
    ctx.startPt = newui::Point(0, 0);
    ctx.currentPt = newui::Point(5, -3);

    GeometryEditResult result = policyFor(nullptr).resolve(ctx);

    EXPECT_EQ(result.kind, GeometryEditKind::FreePosition);
    EXPECT_EQ(result.proposedBounds, newui::Rect(15, 17, 30, 40));
}

TEST(FreePositionPolicyApplyPreview, MovesTheViewToProposedBounds) {
    auto* view = new newui::SubView();
    view->setBounds(newui::Rect(0, 0, 10, 10));

    GeometryDragContext ctx;
    ctx.view = view;
    GeometryEditResult result;
    result.proposedBounds = newui::Rect(5, 5, 10, 10);

    policyFor(nullptr).applyPreview(ctx, result);

    EXPECT_EQ(view->bounds(), newui::Rect(5, 5, 10, 10));

    delete view;
}

TEST(FreePositionPolicyCommit, WithNoParentLayoutJustMovesBoundsAndWritesNoLayoutParams) {
    auto* parent = new newui::SubView();
    auto* view = new newui::SubView();
    view->setVisible(true);
    parent->addChild(view);
    view->setBounds(newui::Rect(0, 0, 10, 10));

    GeometryDragContext ctx;
    ctx.view = view;
    ctx.parent = parent;
    GeometryEditResult startResult;
    startResult.proposedBounds = newui::Rect(0, 0, 10, 10);
    GeometryEditResult endResult;
    endResult.proposedBounds = newui::Rect(20, 30, 10, 10);

    newui::UndoableAction action = policyFor(parent->layout()).commit(ctx, startResult, endResult);
    action.doIt();

    EXPECT_EQ(view->bounds(), newui::Rect(20, 30, 10, 10));
    EXPECT_EQ(view->layoutParams(), nullptr);

    action.undoIt();

    EXPECT_EQ(view->bounds(), newui::Rect(0, 0, 10, 10));

    delete view;
    delete parent;
}

TEST(FreePositionPolicyCommit, WithAnAnchorLayoutParentWritesFreshAnchorLayoutParams) {
    // Regression coverage for the generalization made when this policy was extracted from the
    // original Move code: any real AnchorLayout parent gets fresh AnchorLayoutParams on commit,
    // not only rootViewProxy() specifically - see LayoutEditingPolicy.cpp's own comment on
    // applyFreePositionAnchorParams() for why the narrower check wasn't actually load-bearing.
    auto* parent = new newui::SubView();
    parent->setLayout(std::make_unique<newui::AnchorLayout>());
    auto* view = new newui::SubView();
    view->setVisible(true);
    parent->addChild(view);
    view->setBounds(newui::Rect(0, 0, 10, 10));

    GeometryDragContext ctx;
    ctx.view = view;
    ctx.parent = parent;
    GeometryEditResult startResult;
    startResult.proposedBounds = newui::Rect(0, 0, 10, 10);
    GeometryEditResult endResult;
    endResult.proposedBounds = newui::Rect(20, 30, 10, 10);

    newui::UndoableAction action = policyFor(parent->layout()).commit(ctx, startResult, endResult);
    action.doIt();

    auto* params = dynamic_cast<newui::AnchorLayoutParams*>(view->layoutParams());
    ASSERT_NE(params, nullptr);
    EXPECT_FLOAT_EQ(params->leftMargin, 20.0f);
    EXPECT_FLOAT_EQ(params->topMargin, 30.0f);

    action.undoIt();

    params = dynamic_cast<newui::AnchorLayoutParams*>(view->layoutParams());
    ASSERT_NE(params, nullptr);
    EXPECT_FLOAT_EQ(params->leftMargin, 0.0f);
    EXPECT_FLOAT_EQ(params->topMargin, 0.0f);

    delete view;
    delete parent;
}

TEST(FreePositionPolicyDrawCue, PaintsNothing) {
    auto* view = new newui::SubView();
    view->setBounds(newui::Rect(10, 10, 20, 20));

    GeometryDragContext ctx;
    ctx.view = view;
    GeometryEditResult result;

    BLImage surface;
    ASSERT_EQ(surface.create(64, 64, BL_FORMAT_PRGB32), BL_SUCCESS);
    BLContext blContext(surface);
    blContext.clear_all();
    policyFor(nullptr).drawCue(blContext, ctx, result);
    blContext.end();

    EXPECT_FALSE(anyPixelPainted(surface, 64, 64));

    delete view;
}

// ---------------------------------------------------------------------------
// LinearReorderPolicy - FlexLayout
// ---------------------------------------------------------------------------

TEST(LinearReorderPolicyResolve, DraggingPastASiblingsMidpointTargetsTheNextIndex) {
    auto* container = new newui::SubView();
    container->setBounds(newui::Rect(0, 0, 300, 20));
    container->setLayout(std::make_unique<newui::FlexLayout>(newui::Orientation::Horizontal));

    auto* a = new newui::SubView();
    a->setDesiredSize(newui::Size(100.0f, 20.0f));
    a->setVisible(true);
    auto* b = new newui::SubView();
    b->setDesiredSize(newui::Size(100.0f, 20.0f));
    b->setVisible(true);
    auto* c = new newui::SubView();
    c->setDesiredSize(newui::Size(100.0f, 20.0f));
    c->setVisible(true);
    container->addChild(a);
    container->addChild(b);
    container->addChild(c);
    // a: [0,100), b: [100,200), c: [200,300) after FlexLayout arranges them.

    const LayoutEditingPolicy& policy = policyFor(container->layout());
    ASSERT_EQ(policy.kind(), GeometryEditKind::LinearReorder);

    GeometryDragContext ctx;
    ctx.view = a;
    ctx.parent = container;
    ctx.startBounds = a->bounds();
    ctx.startPt = newui::Point(0, 0);
    ctx.currentPt = newui::Point(150, 0);  // a's hypothetical bounds -> (150,0,100,20), center 200

    GeometryEditResult result = policy.resolve(ctx);

    EXPECT_EQ(result.kind, GeometryEditKind::LinearReorder);
    // b's center (150) is before 200, c's center (250) is not - lands at index 1.
    EXPECT_EQ(result.targetSiblingIndex, 1u);

    delete a;
    delete b;
    delete c;
    delete container;
}

TEST(LinearReorderPolicyApplyPreview, LiveReordersTheRealChildList) {
    auto* container = new newui::SubView();
    container->setBounds(newui::Rect(0, 0, 300, 20));
    container->setLayout(std::make_unique<newui::FlexLayout>(newui::Orientation::Horizontal));

    auto* a = new newui::SubView();
    a->setDesiredSize(newui::Size(100.0f, 20.0f));
    a->setVisible(true);
    auto* b = new newui::SubView();
    b->setDesiredSize(newui::Size(100.0f, 20.0f));
    b->setVisible(true);
    auto* c = new newui::SubView();
    c->setDesiredSize(newui::Size(100.0f, 20.0f));
    c->setVisible(true);
    container->addChild(a);
    container->addChild(b);
    container->addChild(c);

    const LayoutEditingPolicy& policy = policyFor(container->layout());
    GeometryDragContext ctx;
    ctx.view = a;
    ctx.parent = container;
    GeometryEditResult result;
    result.targetSiblingIndex = 1;

    policy.applyPreview(ctx, result);

    ASSERT_EQ(container->childViews().size(), 3u);
    EXPECT_EQ(container->childViews()[0], b);
    EXPECT_EQ(container->childViews()[1], a);
    EXPECT_EQ(container->childViews()[2], c);
    // reorderChild() triggers a real arrange() pass too - a's own bounds should have shifted to
    // wherever FlexLayout put its new index, not stayed at its original [0,100) slot.
    EXPECT_FLOAT_EQ(a->bounds().left(), 100.0f);

    delete a;
    delete b;
    delete c;
    delete container;
}

TEST(LinearReorderPolicyCommit, DoItAndUndoItReorderToTheRightIndices) {
    auto* container = new newui::SubView();
    container->setLayout(std::make_unique<newui::FlexLayout>(newui::Orientation::Horizontal));

    auto* a = new newui::SubView();
    a->setVisible(true);
    auto* b = new newui::SubView();
    b->setVisible(true);
    auto* c = new newui::SubView();
    c->setVisible(true);
    container->addChild(a);
    container->addChild(b);
    container->addChild(c);

    const LayoutEditingPolicy& policy = policyFor(container->layout());
    GeometryDragContext ctx;
    ctx.view = a;
    ctx.parent = container;
    GeometryEditResult startResult;
    startResult.targetSiblingIndex = 0;
    GeometryEditResult endResult;
    endResult.targetSiblingIndex = 2;

    newui::UndoableAction action = policy.commit(ctx, startResult, endResult);
    action.doIt();

    EXPECT_EQ(container->childViews()[2], a);

    action.undoIt();

    EXPECT_EQ(container->childViews()[0], a);

    delete a;
    delete b;
    delete c;
    delete container;
}

TEST(LinearReorderPolicyDrawCue, PaintsAHighlightAroundTheDraggedView) {
    newui::RootView root(nullptr, newui::Rect(0, 0, 64, 64), "root");
    auto* container = new newui::SubView();
    container->setBounds(newui::Rect(0, 0, 64, 64));
    container->setLayout(std::make_unique<newui::FlexLayout>(newui::Orientation::Horizontal));
    root.addChild(container);

    auto* view = new newui::SubView();
    view->setDesiredSize(newui::Size(20.0f, 20.0f));
    view->setVisible(true);
    container->addChild(view);

    GeometryDragContext ctx;
    ctx.view = view;
    ctx.parent = container;
    GeometryEditResult result;
    result.kind = GeometryEditKind::LinearReorder;

    BLImage surface;
    ASSERT_EQ(surface.create(64, 64, BL_FORMAT_PRGB32), BL_SUCCESS);
    BLContext blContext(surface);
    blContext.clear_all();
    policyFor(container->layout()).drawCue(blContext, ctx, result);
    blContext.end();

    EXPECT_TRUE(anyPixelPainted(surface, 64, 64));
}

// ---------------------------------------------------------------------------
// GridCellPolicy - GridLayout
// ---------------------------------------------------------------------------

TEST(GridCellPolicyResolve, MapsTheDraggedCenterToItsContainingCell) {
    auto* container = new newui::SubView();
    container->setBounds(newui::Rect(0, 0, 120, 50));

    auto grid = std::make_unique<newui::GridLayout>();
    grid->addFixedColumn(20.0f);
    grid->addStarColumn(1.0f);
    grid->addStarColumn(3.0f);
    grid->addFixedRow(50.0f);
    container->setLayout(std::move(grid));

    auto* child = new newui::SubView();
    child->setVisible(true);
    child->setLayoutParams(std::make_unique<newui::GridLayoutParams>(0, 0));
    container->addChild(child);
    // columns after arrange(): 0 [0,20), 1 [20,45), 2 [45,120) (leftover 100 split 1:3 -> 25/75).

    const LayoutEditingPolicy& policy = policyFor(container->layout());
    ASSERT_EQ(policy.kind(), GeometryEditKind::GridCell);

    GeometryDragContext ctx;
    ctx.view = child;
    ctx.parent = container;
    ctx.startBounds = child->bounds();
    ctx.startPt = newui::Point(0, 0);
    ctx.currentPt = newui::Point(60, 0);  // hypothetical bounds (60,0,20,50), center x = 70

    GeometryEditResult result = policy.resolve(ctx);

    EXPECT_EQ(result.kind, GeometryEditKind::GridCell);
    EXPECT_EQ(result.targetRow, 0u);
    EXPECT_EQ(result.targetColumn, 2u);

    delete child;
    delete container;
}

TEST(GridCellPolicyApplyPreview, SetsGridLayoutParamsAndRelayouts) {
    auto* container = new newui::SubView();
    container->setBounds(newui::Rect(0, 0, 120, 50));

    auto grid = std::make_unique<newui::GridLayout>();
    grid->addFixedColumn(20.0f);
    grid->addStarColumn(1.0f);
    grid->addStarColumn(3.0f);
    grid->addFixedRow(50.0f);
    container->setLayout(std::move(grid));

    auto* child = new newui::SubView();
    child->setVisible(true);
    child->setLayoutParams(std::make_unique<newui::GridLayoutParams>(0, 0));
    container->addChild(child);

    const LayoutEditingPolicy& policy = policyFor(container->layout());
    GeometryDragContext ctx;
    ctx.view = child;
    ctx.parent = container;
    GeometryEditResult result;
    result.targetRow = 0;
    result.targetColumn = 2;

    policy.applyPreview(ctx, result);

    auto* params = dynamic_cast<newui::GridLayoutParams*>(child->layoutParams());
    ASSERT_NE(params, nullptr);
    EXPECT_EQ(params->row, 0u);
    EXPECT_EQ(params->column, 2u);
    EXPECT_FLOAT_EQ(child->bounds().left(), 45.0f);

    delete child;
    delete container;
}

TEST(GridCellPolicyCommit, DoItAndUndoItMoveToTheRightCells) {
    auto* container = new newui::SubView();
    container->setBounds(newui::Rect(0, 0, 120, 50));

    auto grid = std::make_unique<newui::GridLayout>();
    grid->addFixedColumn(20.0f);
    grid->addStarColumn(1.0f);
    grid->addStarColumn(3.0f);
    grid->addFixedRow(50.0f);
    container->setLayout(std::move(grid));

    auto* child = new newui::SubView();
    child->setVisible(true);
    child->setLayoutParams(std::make_unique<newui::GridLayoutParams>(0, 0));
    container->addChild(child);

    const LayoutEditingPolicy& policy = policyFor(container->layout());
    GeometryDragContext ctx;
    ctx.view = child;
    ctx.parent = container;
    GeometryEditResult startResult;
    startResult.targetRow = 0;
    startResult.targetColumn = 0;
    GeometryEditResult endResult;
    endResult.targetRow = 0;
    endResult.targetColumn = 2;

    newui::UndoableAction action = policy.commit(ctx, startResult, endResult);
    action.doIt();

    EXPECT_FLOAT_EQ(child->bounds().left(), 45.0f);

    action.undoIt();

    EXPECT_FLOAT_EQ(child->bounds().left(), 0.0f);

    delete child;
    delete container;
}

TEST(GridCellPolicyDrawCue, PaintsGridLinesAndTheTargetCellHighlight) {
    newui::RootView root(nullptr, newui::Rect(0, 0, 64, 64), "root");
    auto* container = new newui::SubView();
    container->setBounds(newui::Rect(0, 0, 64, 64));

    auto grid = std::make_unique<newui::GridLayout>();
    grid->addFixedColumn(32.0f);
    grid->addFixedColumn(32.0f);
    grid->addFixedRow(64.0f);
    container->setLayout(std::move(grid));
    root.addChild(container);

    auto* view = new newui::SubView();
    view->setVisible(true);
    view->setLayoutParams(std::make_unique<newui::GridLayoutParams>(0, 0));
    container->addChild(view);

    GeometryDragContext ctx;
    ctx.view = view;
    ctx.parent = container;
    GeometryEditResult result;
    result.kind = GeometryEditKind::GridCell;
    result.targetRow = 0;
    result.targetColumn = 1;

    BLImage surface;
    ASSERT_EQ(surface.create(64, 64, BL_FORMAT_PRGB32), BL_SUCCESS);
    BLContext blContext(surface);
    blContext.clear_all();
    policyFor(container->layout()).drawCue(blContext, ctx, result);
    blContext.end();

    EXPECT_TRUE(anyPixelPainted(surface, 64, 64));
}

// ---------------------------------------------------------------------------
// NoGeometryPolicy - CardLayout, or any future/unrecognized Layout subtype.
// ---------------------------------------------------------------------------

TEST(NoGeometryPolicy, ResolveAndCommitAreBothInertNoOps) {
    newui::CardLayout layout;
    const LayoutEditingPolicy& policy = policyFor(&layout);

    GeometryDragContext ctx;
    GeometryEditResult result = policy.resolve(ctx);
    EXPECT_EQ(result.kind, GeometryEditKind::None);

    newui::UndoableAction action = policy.commit(ctx, result, result);
    EXPECT_FALSE(static_cast<bool>(action.doIt));
    EXPECT_FALSE(static_cast<bool>(action.undoIt));
}
