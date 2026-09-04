#include "../extension/NativeEditControls/Workspace.h"

#include <newui/layout.h>

#include <gtest/gtest.h>

// registerReflectionData() is already run once globally for this whole
// binary by test_component_editor.cpp's own ::testing::Environment - no
// separate registration needed here (same convention that file's own
// comment documents).

TEST(Workspace, AllFivePanesAndTheProxyPairAreBuilt) {
    auto* workspace = new CodeToolsVsix::Workspace();

    EXPECT_NE(workspace->topBar(), nullptr);
    EXPECT_NE(workspace->toolboxPane(), nullptr);
    EXPECT_NE(workspace->propertiesPane(), nullptr);
    EXPECT_NE(workspace->animationPane(), nullptr);
    EXPECT_NE(workspace->statusBar(), nullptr);
    EXPECT_NE(workspace->frameProxy(), nullptr);
    EXPECT_NE(workspace->rootViewProxy(), nullptr);

    delete workspace;
}

TEST(Workspace, RootViewProxyIsFrameProxysOnlyChild) {
    auto* workspace = new CodeToolsVsix::Workspace();

    ASSERT_EQ(workspace->frameProxy()->childViews().size(), 1u);
    EXPECT_EQ(workspace->frameProxy()->childViews()[0], workspace->rootViewProxy());

    delete workspace;
}

TEST(Workspace, RootViewProxyIsAnchoredBelowTheFrameProxysTitleBar) {
    auto* workspace = new CodeToolsVsix::Workspace();

    auto* params = dynamic_cast<newui::AnchorLayoutParams*>(workspace->rootViewProxy()->layoutParams());
    ASSERT_NE(params, nullptr);
    EXPECT_TRUE(newui::hasAnchor(params->anchors, newui::Anchor::Left));
    EXPECT_TRUE(newui::hasAnchor(params->anchors, newui::Anchor::Top));
    EXPECT_TRUE(newui::hasAnchor(params->anchors, newui::Anchor::Right));
    EXPECT_TRUE(newui::hasAnchor(params->anchors, newui::Anchor::Bottom));
    EXPECT_FLOAT_EQ(params->topMargin, newui::FrameProxy::kTitleBarHeight);

    delete workspace;
}

TEST(Workspace, ResizingTheWorkspaceKeepsTopAndStatusBarsFixedHeight) {
    auto* workspace = new CodeToolsVsix::Workspace();

    workspace->setBounds(newui::Rect(0, 0, 1000, 700));

    EXPECT_FLOAT_EQ(workspace->topBar()->bounds().size().height, CodeToolsVsix::Workspace::kTopBarHeight);
    EXPECT_FLOAT_EQ(workspace->statusBar()->bounds().size().height, CodeToolsVsix::Workspace::kStatusBarHeight);
    EXPECT_FLOAT_EQ(workspace->topBar()->bounds().size().width, 1000.0f);
    EXPECT_FLOAT_EQ(workspace->statusBar()->bounds().size().width, 1000.0f);

    delete workspace;
}

TEST(Workspace, MiddleAreaCascadesDownToTheFrameProxyAndRootViewProxy) {
    auto* workspace = new CodeToolsVsix::Workspace();

    workspace->setBounds(newui::Rect(0, 0, 1000, 700));

    // frameProxy's own bounds come from centerAndRight's Splitter arrangement,
    // several levels down - proves the whole chain of setBounds()-driven
    // cascades (Splitter -> Splitter -> Splitter -> FrameProxy) actually runs.
    EXPECT_GT(workspace->frameProxy()->bounds().size().width, 0.0f);
    EXPECT_GT(workspace->frameProxy()->bounds().size().height, 0.0f);

    // rootViewProxy is anchored below the title bar within frameProxy's own
    // bounds (AnchorLayoutParams::topMargin verified above), so its height
    // should be exactly frameProxy's height minus the title bar.
    float expectedRootViewHeight = workspace->frameProxy()->bounds().size().height - newui::FrameProxy::kTitleBarHeight;
    EXPECT_FLOAT_EQ(workspace->rootViewProxy()->bounds().size().height, expectedRootViewHeight);

    delete workspace;
}
