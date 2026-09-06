#include "../extension/NativeEditControls/Workspace.h"

#include <newui/layout.h>

#include <gtest/gtest.h>

// registerReflectionData() is already run once globally for this whole
// binary by test_component_editor.cpp's own ::testing::Environment - no
// separate registration needed here (same convention that file's own
// comment documents).

namespace {

// A real, fully laid-out Workspace (same "setBounds() cascades all the way
// down through Splitter -> Splitter -> Splitter -> FrameProxy" pattern
// test_workspace.cpp's own MiddleAreaCascadesDownToTheFrameProxyAndRootViewProxy
// already established), so canvasWell() and frameProxy() have real, non-
// degenerate bounds a resize drag can actually act on.
class CanvasWellTest : public ::testing::Test {
protected:
    void SetUp() override {
        workspace_ = new CodeToolsVsix::Workspace();
        workspace_->setBounds(newui::Rect(0.0f, 0.0f, 1000.0f, 700.0f));
    }

    void TearDown() override {
        delete workspace_;
    }

    // frameProxy()'s bounds are already in canvasWell()'s own local space -
    // it's canvasWell()'s direct (only) child.
    const newui::Rect& frameBounds() const {
        return workspace_->frameProxy()->bounds();
    }

    CodeToolsVsix::Workspace* workspace_ = nullptr;
};

}  // namespace

TEST_F(CanvasWellTest, ResizeEdgeAtDetectsEachGuideWithinTolerance) {
    CodeToolsVsix::CanvasWell* canvasWell = workspace_->canvasWell();
    const newui::Rect& frame = frameBounds();
    float midY = frame.top() + frame.size().height * 0.5f;
    float midX = frame.left() + frame.size().width * 0.5f;

    EXPECT_EQ(canvasWell->resizeEdgeAt(newui::Point(frame.left(), midY)),
        CodeToolsVsix::CanvasWell::ResizeEdge::Vertical);
    EXPECT_EQ(canvasWell->resizeEdgeAt(newui::Point(frame.right(), midY)),
        CodeToolsVsix::CanvasWell::ResizeEdge::Vertical);
    EXPECT_EQ(canvasWell->resizeEdgeAt(newui::Point(midX, frame.top())),
        CodeToolsVsix::CanvasWell::ResizeEdge::Horizontal);
    EXPECT_EQ(canvasWell->resizeEdgeAt(newui::Point(midX, frame.bottom())),
        CodeToolsVsix::CanvasWell::ResizeEdge::Horizontal);

    // Well inside the frame, away from every edge - not grabbable.
    EXPECT_EQ(canvasWell->resizeEdgeAt(newui::Point(midX, midY)),
        CodeToolsVsix::CanvasWell::ResizeEdge::None);

    // Just past the grab tolerance from the right edge.
    EXPECT_EQ(canvasWell->resizeEdgeAt(newui::Point(frame.right() + CodeToolsVsix::CanvasWell::kEdgeGrabTolerance + 1.0f, midY)),
        CodeToolsVsix::CanvasWell::ResizeEdge::None);
}

TEST_F(CanvasWellTest, BeginResizeDragFailsAwayFromAnyGuide) {
    CodeToolsVsix::CanvasWell* canvasWell = workspace_->canvasWell();
    const newui::Rect& frame = frameBounds();
    newui::Point center(frame.left() + frame.size().width * 0.5f, frame.top() + frame.size().height * 0.5f);

    EXPECT_FALSE(canvasWell->beginResizeDrag(center));
    EXPECT_FALSE(canvasWell->isResizingDrag());
}

TEST_F(CanvasWellTest, DraggingTheRightGuideResizesWidthSymmetricallyAboutTheCenter) {
    CodeToolsVsix::CanvasWell* canvasWell = workspace_->canvasWell();
    // Plain float copies, not references - frameBounds() aliases
    // frameProxy_'s own live bounds_, which continueResizeDrag() below
    // mutates in place; a reference taken here would silently track that
    // mutation instead of preserving the "before" values to compare against.
    const newui::Rect& frame = frameBounds();
    float midY = frame.top() + frame.size().height * 0.5f;
    float originalLeft = frame.left();
    float originalCenterX = frame.left() + frame.size().width * 0.5f;
    float originalHeight = frame.size().height;

    ASSERT_TRUE(canvasWell->beginResizeDrag(newui::Point(frame.right(), midY)));
    ASSERT_TRUE(canvasWell->isResizingDrag());

    // Drag the right edge further out than FrameProxy's own default 640px
    // width (so this is unambiguously a *grow*, not a shrink) - the whole
    // frame should grow symmetrically about its own (unchanged) center,
    // not just on the right side.
    canvasWell->continueResizeDrag(newui::Point(originalCenterX + 500.0f, midY));

    const newui::Rect& resized = frameBounds();
    EXPECT_FLOAT_EQ(resized.size().width, 1000.0f);
    EXPECT_FLOAT_EQ(resized.left() + resized.size().width * 0.5f, originalCenterX);
    // Height untouched by a width-only drag.
    EXPECT_FLOAT_EQ(resized.size().height, originalHeight);
    // The frame moved (its left edge shifted left as it grew symmetrically) -
    // sanity check this isn't a no-op.
    EXPECT_LT(resized.left(), originalLeft);
}

TEST_F(CanvasWellTest, DraggingTheLeftGuideAlsoResizesWidth) {
    // Confirmed by the user directly: either the left or the right guide
    // adjusts width, since the frame always stays centered regardless of
    // which one is grabbed.
    CodeToolsVsix::CanvasWell* canvasWell = workspace_->canvasWell();
    const newui::Rect& frame = frameBounds();
    float midY = frame.top() + frame.size().height * 0.5f;
    float originalCenterX = frame.left() + frame.size().width * 0.5f;

    ASSERT_TRUE(canvasWell->beginResizeDrag(newui::Point(frame.left(), midY)));
    canvasWell->continueResizeDrag(newui::Point(originalCenterX - 150.0f, midY));

    EXPECT_FLOAT_EQ(frameBounds().size().width, 300.0f);
}

TEST_F(CanvasWellTest, DraggingTheBottomGuideResizesHeightSymmetricallyAboutTheCenter) {
    CodeToolsVsix::CanvasWell* canvasWell = workspace_->canvasWell();
    // Plain float copy, not a reference - see the width-drag test's own
    // comment for why.
    const newui::Rect& frame = frameBounds();
    float midX = frame.left() + frame.size().width * 0.5f;
    float originalCenterY = frame.top() + frame.size().height * 0.5f;
    float originalWidth = frame.size().width;

    ASSERT_TRUE(canvasWell->beginResizeDrag(newui::Point(midX, frame.bottom())));
    canvasWell->continueResizeDrag(newui::Point(midX, originalCenterY + 100.0f));

    const newui::Rect& resized = frameBounds();
    EXPECT_FLOAT_EQ(resized.size().height, 200.0f);
    EXPECT_FLOAT_EQ(resized.top() + resized.size().height * 0.5f, originalCenterY);
    // Width untouched by a height-only drag.
    EXPECT_FLOAT_EQ(resized.size().width, originalWidth);
}

TEST_F(CanvasWellTest, ResizeNeverShrinksBelowTheMinimumFrameSize) {
    CodeToolsVsix::CanvasWell* canvasWell = workspace_->canvasWell();
    const newui::Rect& frame = frameBounds();
    float midY = frame.top() + frame.size().height * 0.5f;
    float originalCenterX = frame.left() + frame.size().width * 0.5f;

    ASSERT_TRUE(canvasWell->beginResizeDrag(newui::Point(frame.right(), midY)));
    // Drag right to the exact center - would naively compute width 0.
    canvasWell->continueResizeDrag(newui::Point(originalCenterX, midY));

    EXPECT_FLOAT_EQ(frameBounds().size().width, CodeToolsVsix::CanvasWell::kMinFrameSize);
}

TEST_F(CanvasWellTest, EndResizeDragStopsFurtherContinuation) {
    CodeToolsVsix::CanvasWell* canvasWell = workspace_->canvasWell();
    const newui::Rect& frame = frameBounds();
    float midY = frame.top() + frame.size().height * 0.5f;
    float originalCenterX = frame.left() + frame.size().width * 0.5f;

    ASSERT_TRUE(canvasWell->beginResizeDrag(newui::Point(frame.right(), midY)));
    canvasWell->continueResizeDrag(newui::Point(originalCenterX + 200.0f, midY));
    canvasWell->endResizeDrag();

    ASSERT_FALSE(canvasWell->isResizingDrag());
    float widthAfterEnd = frameBounds().size().width;

    // A further continueResizeDrag() call (e.g. a stray mouseMove after
    // mouseUp) must be a no-op now that the drag has ended.
    canvasWell->continueResizeDrag(newui::Point(originalCenterX + 500.0f, midY));
    EXPECT_FLOAT_EQ(frameBounds().size().width, widthAfterEnd);
}
