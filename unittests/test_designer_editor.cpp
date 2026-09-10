#include "../extension/NativeEditControls/DesignerEditor.h"

#include <newui/bundle.h>
#include <newui/controls.h>
#include <newui/frame.h>
#include <newui/keyboard_constants.h>
#include <newui/layout.h>
#include <newui/mouse_constants.h>
#include <newui/reflection.h>
#include <newui/rootview.h>
#include <newui/rootviewproxy.h>
#include <newui/subview.h>

#include <gtest/gtest.h>

#include <fstream>
#include <memory>
#include <string>

namespace
{
    // DesignerEditor::load()/save() resolve an arbitrary absolute path
    // directly (Bundle::loadRootViewFromFile()/writeRootViewToFile()) - a
    // plain temp directory, no "\Resources\" shape required.
    class DesignerEditorFileFixture : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            char tempPathBuf[MAX_PATH]{};
            ::GetTempPathA(MAX_PATH, tempPathBuf);
            dir_ = std::string(tempPathBuf) + "DesignerEditorTest";
            ::CreateDirectoryA(dir_.c_str(), nullptr);
            path_ = dir_ + "\\DesignerEditorProbe.newui";
        }

        void TearDown() override
        {
            ::DeleteFileA(path_.c_str());
            ::RemoveDirectoryA(dir_.c_str());
        }

        std::wstring filePath() const
        {
            return std::wstring(path_.begin(), path_.end());
        }

        void writeFile(const std::string& contents)
        {
            std::ofstream file(path_, std::ios::binary);
            file << contents;
        }

        std::string dir_;
        std::string path_;
    };
}

TEST_F(DesignerEditorFileFixture, ConstructionSetsDesignTimeOnlyOnTheDesignSurfaceNotTheHostingChrome)
{
    // isDesignTime() no longer propagates from an owning RootView (view.cpp) -
    // a View reports only its own explicitly-set flag. root/Workspace itself
    // are just this editor's hosting chrome, never marked design-time;
    // Workspace's own constructor marks exactly frameProxy_/rootViewProxy_
    // (the actual design surface) instead - see its own comment for why.
    newui::RootView view(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    CodeToolsVsix::DesignerEditor editor(&view);

    EXPECT_FALSE(view.isDesignTime());
    ASSERT_NE(editor.workspace(), nullptr);
    EXPECT_FALSE(editor.workspace()->isDesignTime());
    EXPECT_FALSE(editor.workspace()->toolboxPane()->isDesignTime());
    EXPECT_FALSE(editor.workspace()->propertiesPane()->isDesignTime());

    ASSERT_NE(editor.workspace()->frameProxy(), nullptr);
    EXPECT_TRUE(editor.workspace()->frameProxy()->isDesignTime());
    ASSERT_NE(editor.workspace()->rootViewProxy(), nullptr);
    EXPECT_TRUE(editor.workspace()->rootViewProxy()->isDesignTime());
}

// Reproduces testharness.cpp's real construction sequence, which is not
// what any existing Workspace/DesignerEditor test exercises: those all
// either call Workspace::setBounds() directly (bypassing a parent's own
// FlexLayout arrange entirely) or construct DesignerEditor onto an empty
// RootView no one has resized. testharness.cpp instead (1) gives root its
// own FlexLayout and a first child (MenuBar, weight 0) *before* the
// DesignerEditor exists, then (2) constructs DesignerEditor(&root), whose
// setupUI() replaces that layout and adds Workspace as weight-1 second
// child, then (3) resizes an already-shown window. A real bug report says
// the Workspace pane shows only a tiny sliver of content after that, and
// that a later resize changes nothing - this test isolates exactly that
// sequence without needing a live window at all.
TEST_F(DesignerEditorFileFixture, WorkspaceGetsRealBoundsWhenAddedAlongsideAPreexistingSiblingThenResized)
{
    newui::RootView root(nullptr, newui::Rect(0, 0, 10, 10), "harnessRoot");

    auto rootLayout = std::make_unique<newui::FlexLayout>(newui::Orientation::Vertical);
    rootLayout->setSpacing(0.0f);
    rootLayout->setPadding(0.0f);
    root.setLayout(std::move(rootLayout));

    auto* menuBarStandIn = new newui::SubView();
    menuBarStandIn->setName("menuBarStandIn");
    menuBarStandIn->setVisible(true);
    menuBarStandIn->setDesiredSize(newui::Size(0.0f, 24.0f));
    menuBarStandIn->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(0.0f));
    root.addChild(menuBarStandIn);

    CodeToolsVsix::DesignerEditor editor(&root);
    ASSERT_NE(editor.workspace(), nullptr);

    root.setBounds(newui::Rect(0, 0, 1000, 700));

    EXPECT_GT(editor.workspace()->bounds().size().width, 900.0f);
    EXPECT_GT(editor.workspace()->bounds().size().height, 600.0f);

    // >0.0f alone isn't a strong enough check here - a real bug (fixed in
    // newui's own Splitter::clampSplitPosition(), see its own comment)
    // silently collapsed every Workspace pane split to minPaneSize()
    // (40px) instead of its real configured value. frameProxy_'s height
    // here tracks "middle"'s own splitPosition, which Workspace.cpp never
    // overrides - Splitter's own real default (200.0f) - so 150.0f is a
    // threshold comfortably between the old broken 40px floor and that
    // real, correct value, not an exact-match assertion on an unconfigured
    // default that could legitimately change later. frameProxy_'s width
    // tracks centerAndRight's own explicit 560.0f split, several hundred
    // pixels clear of the same 40px floor.
    ASSERT_NE(editor.workspace()->frameProxy(), nullptr);
    EXPECT_GT(editor.workspace()->frameProxy()->bounds().size().width, 150.0f);
    EXPECT_GT(editor.workspace()->frameProxy()->bounds().size().height, 150.0f);
}

TEST_F(DesignerEditorFileFixture, ClickInsideTheDesignSurfaceSelectsTheHitChild)
{
    newui::RootView root(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    CodeToolsVsix::DesignerEditor editor(&root);
    ASSERT_NE(editor.workspace(), nullptr);
    // Forces a real Splitter/FlexLayout arrange pass, same as
    // WorkspaceGetsRealBoundsWhenAddedAlongsideAPreexistingSiblingThenResized
    // above - without this the whole tree stays at its tiny 10x10
    // construction-time bounds and nothing has a real on-screen position.
    // Wide enough that canvasWell() ends up wider than frameProxy_'s own
    // fixed kDefaultCanvasWidth (640) once Toolbox (220)/Properties (300)/
    // dividers are reserved - a narrower window leaves frameProxy_
    // centered-and-overflowing past canvasWell's own edges (a real,
    // reported bug: handleMouseDownForSelection() used to gate only on
    // surfaceBounds, frameProxy_/rootViewProxy_'s own - possibly
    // overflowing - bounds, letting a click on the Properties/Toolbox
    // chrome still resolve to a design-surface child underneath it), which
    // would make a click near the surface's own top-left corner (as below)
    // fall outside the real, visible canvas - not what this test means to
    // exercise.
    root.setBounds(newui::Rect(0, 0, 1400, 700));

    newui::RootViewProxy* surface = editor.workspace()->rootViewProxy();
    ASSERT_NE(surface, nullptr);

    auto* control = new newui::SubView();
    control->setName("probeControl");
    control->setVisible(true);
    control->setBounds(newui::Rect(10, 10, 40, 20));
    surface->addChild(control);

    ASSERT_NE(editor.selectionOverlay(), nullptr);
    EXPECT_EQ(editor.viewDesignerController().primary(), nullptr);

    newui::Rect surfaceBounds = CodeToolsVsix::SelectionOverlay::boundsInRootView(surface);
    newui::Point insideControl(surfaceBounds.left() + 20.0f, surfaceBounds.top() + 15.0f);
    root.onMouseDown(root, insideControl, newui::mbmLeftButton, newui::kmUndefined);

    EXPECT_EQ(editor.viewDesignerController().primary(), control);

    // A subsequent click on empty design-surface space (past the control's
    // own 40x20 bounds, still inside the surface itself) clears it again.
    newui::Point emptyCanvas(surfaceBounds.left() + 300.0f, surfaceBounds.top() + 300.0f);
    root.onMouseDown(root, emptyCanvas, newui::mbmLeftButton, newui::kmUndefined);

    EXPECT_EQ(editor.viewDesignerController().primary(), nullptr);
}

TEST_F(DesignerEditorFileFixture, CtrlClickAddsASecondControlToTheSelection)
{
    newui::RootView root(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    CodeToolsVsix::DesignerEditor editor(&root);
    ASSERT_NE(editor.workspace(), nullptr);
    // See ClickInsideTheDesignSurfaceSelectsTheHitChild's own comment for
    // why this needs to be wide enough that canvasWell() isn't narrower
    // than frameProxy_'s fixed size.
    root.setBounds(newui::Rect(0, 0, 1400, 700));

    newui::RootViewProxy* surface = editor.workspace()->rootViewProxy();
    ASSERT_NE(surface, nullptr);

    auto* first = new newui::SubView();
    first->setName("first");
    first->setVisible(true);
    first->setBounds(newui::Rect(10, 10, 40, 20));
    surface->addChild(first);

    auto* second = new newui::SubView();
    second->setName("second");
    second->setVisible(true);
    second->setBounds(newui::Rect(10, 40, 40, 20));
    surface->addChild(second);

    newui::Rect surfaceBounds = CodeToolsVsix::SelectionOverlay::boundsInRootView(surface);
    root.onMouseDown(root, newui::Point(surfaceBounds.left() + 20.0f, surfaceBounds.top() + 15.0f),
        newui::mbmLeftButton, newui::kmUndefined);
    root.onMouseDown(root, newui::Point(surfaceBounds.left() + 20.0f, surfaceBounds.top() + 45.0f),
        newui::mbmLeftButton, newui::kmCtrl);

    ASSERT_EQ(editor.viewDesignerController().selected().size(), 2u);
    EXPECT_TRUE(editor.viewDesignerController().isSelected(first));
    EXPECT_TRUE(editor.viewDesignerController().isSelected(second));
    EXPECT_EQ(editor.viewDesignerController().primary(), second);
}

TEST_F(DesignerEditorFileFixture, ClickOutsideTheDesignSurfaceLeavesSelectionUntouched)
{
    newui::RootView root(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    CodeToolsVsix::DesignerEditor editor(&root);
    ASSERT_NE(editor.workspace(), nullptr);
    // See ClickInsideTheDesignSurfaceSelectsTheHitChild's own comment for
    // why this needs to be wide enough that canvasWell() isn't narrower
    // than frameProxy_'s fixed size.
    root.setBounds(newui::Rect(0, 0, 1400, 700));

    newui::RootViewProxy* surface = editor.workspace()->rootViewProxy();
    ASSERT_NE(surface, nullptr);
    auto* control = new newui::SubView();
    control->setName("probeControl");
    control->setVisible(true);
    control->setBounds(newui::Rect(10, 10, 40, 20));
    surface->addChild(control);

    newui::Rect surfaceBounds = CodeToolsVsix::SelectionOverlay::boundsInRootView(surface);
    root.onMouseDown(root, newui::Point(surfaceBounds.left() + 20.0f, surfaceBounds.top() + 15.0f),
        newui::mbmLeftButton, newui::kmUndefined);
    ASSERT_EQ(editor.viewDesignerController().primary(), control);

    // A click on the Toolbox pane (well to the left of the design surface,
    // still inside the pane) doesn't touch the existing selection.
    root.onMouseDown(root, newui::Point(5.0f, 5.0f), newui::mbmLeftButton, newui::kmUndefined);
    EXPECT_EQ(editor.viewDesignerController().primary(), control);
}

TEST_F(DesignerEditorFileFixture, ClickInFrameProxysOverflowPastCanvasWellIsIgnored)
{
    // Real, reported bug: in a window narrow enough that canvasWell() ends
    // up narrower than frameProxy_'s own fixed kDefaultCanvasWidth (640),
    // frameProxy_ (centered inside canvasWell) overflows past canvasWell's
    // own edges on screen - handleMouseDownForSelection() used to gate
    // only on surfaceBounds (frameProxy_/rootViewProxy_'s own, possibly-
    // overflowing bounds), so a click that landed on the Properties pane
    // (or one of its own expand arrows) but happened to fall within that
    // overflow could still get misread as a click on the design surface,
    // silently changing the canvas selection out from under whatever was
    // actually clicked. Deliberately narrow (not the 1400-wide window the
    // other click tests above use) specifically to reproduce that overflow.
    newui::RootView root(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    CodeToolsVsix::DesignerEditor editor(&root);
    ASSERT_NE(editor.workspace(), nullptr);
    root.setBounds(newui::Rect(0, 0, 1000, 700));

    newui::RootViewProxy* surface = editor.workspace()->rootViewProxy();
    ASSERT_NE(surface, nullptr);
    auto* control = new newui::SubView();
    control->setName("probeControl");
    control->setVisible(true);
    control->setBounds(newui::Rect(10, 10, 40, 20));
    surface->addChild(control);

    newui::Rect surfaceBounds = CodeToolsVsix::SelectionOverlay::boundsInRootView(surface);
    newui::Rect canvasWellBounds = CodeToolsVsix::SelectionOverlay::boundsInRootView(editor.workspace()->canvasWell());
    // Confirms this test setup actually reproduces the overflow condition -
    // otherwise the assertion below would trivially pass for the wrong
    // reason (nothing to do with the bug at all).
    ASSERT_LT(canvasWellBounds.size().width, CodeToolsVsix::Workspace::kDefaultCanvasWidth);

    newui::Point nearControl(surfaceBounds.left() + 20.0f, surfaceBounds.top() + 15.0f);
    ASSERT_FALSE(canvasWellBounds.contains(nearControl))
        << "expected this point to fall in frameProxy_'s overflow past canvasWell's own edge";

    root.onMouseDown(root, nearControl, newui::mbmLeftButton, newui::kmUndefined);
    EXPECT_EQ(editor.viewDesignerController().primary(), nullptr);
}

// ---------------------------------------------------------------------------
// Move - drag-to-reposition/-reorder/-recell, routed through
// LayoutEditingPolicy::policyFor() (LayoutEditingPolicy.h). Real
// onMouseDown/onMouseMove/onMouseUp Delegate calls, same convention the
// Selection tests above already use - not a synthetic bypass of anything
// (see [[feedback_no_synthetic_input_unit_tests]]'s own carve-out).
// ---------------------------------------------------------------------------

TEST_F(DesignerEditorFileFixture, DraggingAFreeCanvasControlMovesItAndPushesOneUndoStep)
{
    newui::RootView root(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    CodeToolsVsix::DesignerEditor editor(&root);
    ASSERT_NE(editor.workspace(), nullptr);
    root.setBounds(newui::Rect(0, 0, 1400, 700));

    newui::RootViewProxy* surface = editor.workspace()->rootViewProxy();
    ASSERT_NE(surface, nullptr);

    auto* control = new newui::SubView();
    control->setName("probeControl");
    control->setVisible(true);
    control->setBounds(newui::Rect(10, 10, 40, 20));
    surface->addChild(control);

    newui::Rect surfaceBounds = CodeToolsVsix::SelectionOverlay::boundsInRootView(surface);
    newui::Point startPt(surfaceBounds.left() + 20.0f, surfaceBounds.top() + 15.0f);
    root.onMouseDown(root, startPt, newui::mbmLeftButton, newui::kmUndefined);
    ASSERT_EQ(editor.viewDesignerController().primary(), control);

    newui::Point draggedPt = startPt + newui::Point(50.0f, 30.0f);
    root.onMouseMove(root, draggedPt, newui::mbmLeftButton, 0);
    EXPECT_EQ(control->bounds(), newui::Rect(60.0f, 40.0f, 40.0f, 20.0f));

    root.onMouseUp(root, draggedPt, newui::mbmLeftButton, 0);

    EXPECT_TRUE(editor.undoStack().canUndo());
    editor.undoStack().undo();
    EXPECT_EQ(control->bounds(), newui::Rect(10.0f, 10.0f, 40.0f, 20.0f));
}

TEST_F(DesignerEditorFileFixture, DraggingAFlexLayoutChildReordersItAndPushesOneUndoStep)
{
    newui::RootView root(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    CodeToolsVsix::DesignerEditor editor(&root);
    ASSERT_NE(editor.workspace(), nullptr);
    root.setBounds(newui::Rect(0, 0, 1400, 700));

    newui::RootViewProxy* surface = editor.workspace()->rootViewProxy();
    ASSERT_NE(surface, nullptr);

    auto* row = new newui::SubView();
    row->setName("row");
    row->setVisible(true);
    row->setBounds(newui::Rect(10, 10, 300, 20));
    row->setLayout(std::make_unique<newui::FlexLayout>(newui::Orientation::Horizontal));
    surface->addChild(row);

    auto* a = new newui::SubView();
    a->setName("a");
    a->setVisible(true);
    a->setDesiredSize(newui::Size(100.0f, 20.0f));
    auto* b = new newui::SubView();
    b->setName("b");
    b->setVisible(true);
    b->setDesiredSize(newui::Size(100.0f, 20.0f));
    auto* c = new newui::SubView();
    c->setName("c");
    c->setVisible(true);
    c->setDesiredSize(newui::Size(100.0f, 20.0f));
    row->addChild(a);
    row->addChild(b);
    row->addChild(c);
    // a: [0,100), b: [100,200), c: [200,300) within row's own local space, after FlexLayout arranges them.

    newui::Rect rowBounds = CodeToolsVsix::SelectionOverlay::boundsInRootView(row);
    newui::Point startPt(rowBounds.left() + 50.0f, rowBounds.top() + 10.0f);  // inside a
    root.onMouseDown(root, startPt, newui::mbmLeftButton, newui::kmUndefined);
    ASSERT_EQ(editor.viewDesignerController().primary(), a);

    // a's hypothetical center (200) lands past b's own center (150), before c's (250).
    newui::Point draggedPt = startPt + newui::Point(150.0f, 0.0f);
    root.onMouseMove(root, draggedPt, newui::mbmLeftButton, 0);

    ASSERT_EQ(row->childViews()[0], b);
    ASSERT_EQ(row->childViews()[1], a);
    ASSERT_EQ(row->childViews()[2], c);

    root.onMouseUp(root, draggedPt, newui::mbmLeftButton, 0);

    EXPECT_TRUE(editor.undoStack().canUndo());
    editor.undoStack().undo();
    EXPECT_EQ(row->childViews()[0], a);
    EXPECT_EQ(row->childViews()[1], b);
    EXPECT_EQ(row->childViews()[2], c);
}

TEST_F(DesignerEditorFileFixture, ClickWithoutDraggingPushesNoUndoStep)
{
    newui::RootView root(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    CodeToolsVsix::DesignerEditor editor(&root);
    ASSERT_NE(editor.workspace(), nullptr);
    root.setBounds(newui::Rect(0, 0, 1400, 700));

    newui::RootViewProxy* surface = editor.workspace()->rootViewProxy();
    ASSERT_NE(surface, nullptr);
    auto* control = new newui::SubView();
    control->setName("probeControl");
    control->setVisible(true);
    control->setBounds(newui::Rect(10, 10, 40, 20));
    surface->addChild(control);

    newui::Rect surfaceBounds = CodeToolsVsix::SelectionOverlay::boundsInRootView(surface);
    newui::Point pt(surfaceBounds.left() + 20.0f, surfaceBounds.top() + 15.0f);
    root.onMouseDown(root, pt, newui::mbmLeftButton, newui::kmUndefined);
    root.onMouseMove(root, pt, newui::mbmLeftButton, 0);  // zero movement - never crosses the drag threshold
    root.onMouseUp(root, pt, newui::mbmLeftButton, 0);

    EXPECT_FALSE(editor.undoStack().canUndo());
    EXPECT_EQ(control->bounds(), newui::Rect(10.0f, 10.0f, 40.0f, 20.0f));
}

TEST_F(DesignerEditorFileFixture, ACardLayoutChildCannotBeDraggedAtAll)
{
    newui::RootView root(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    CodeToolsVsix::DesignerEditor editor(&root);
    ASSERT_NE(editor.workspace(), nullptr);
    root.setBounds(newui::Rect(0, 0, 1400, 700));

    newui::RootViewProxy* surface = editor.workspace()->rootViewProxy();
    ASSERT_NE(surface, nullptr);

    auto* stack = new newui::SubView();
    stack->setName("stack");
    stack->setVisible(true);
    stack->setBounds(newui::Rect(10, 10, 100, 50));
    stack->setLayout(std::make_unique<newui::CardLayout>());
    surface->addChild(stack);

    auto* page = new newui::SubView();
    page->setName("page");
    page->setVisible(true);
    stack->addChild(page);
    // CardLayout::arrange() fills the container completely - page's own bounds are stack's own
    // client size, at stack's *local* origin (0,0), not stack's own parent-local position.
    newui::Rect expectedPageBounds(0.0f, 0.0f, stack->bounds().size().width, stack->bounds().size().height);
    ASSERT_EQ(page->bounds(), expectedPageBounds);

    newui::Rect pageBounds = CodeToolsVsix::SelectionOverlay::boundsInRootView(page);
    newui::Point startPt(pageBounds.left() + 10.0f, pageBounds.top() + 10.0f);
    root.onMouseDown(root, startPt, newui::mbmLeftButton, newui::kmUndefined);
    ASSERT_EQ(editor.viewDesignerController().primary(), page);

    newui::Point draggedPt = startPt + newui::Point(50.0f, 0.0f);
    root.onMouseMove(root, draggedPt, newui::mbmLeftButton, 0);
    root.onMouseUp(root, draggedPt, newui::mbmLeftButton, 0);

    EXPECT_EQ(page->bounds(), expectedPageBounds);
    EXPECT_FALSE(editor.undoStack().canUndo());
}

TEST_F(DesignerEditorFileFixture, DraggingAFreeCanvasControlIntoAnotherContainerReparentsItAndPushesOneUndoStep)
{
    newui::RootView root(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    CodeToolsVsix::DesignerEditor editor(&root);
    ASSERT_NE(editor.workspace(), nullptr);
    root.setBounds(newui::Rect(0, 0, 1400, 700));

    newui::RootViewProxy* surface = editor.workspace()->rootViewProxy();
    ASSERT_NE(surface, nullptr);

    auto* containerA = new newui::SubView();
    containerA->setName("containerA");
    containerA->setVisible(true);
    containerA->setBounds(newui::Rect(10, 10, 100, 100));
    containerA->setLayout(std::make_unique<newui::AnchorLayout>());
    surface->addChild(containerA);

    auto* containerB = new newui::SubView();
    containerB->setName("containerB");
    containerB->setVisible(true);
    containerB->setBounds(newui::Rect(200, 10, 100, 100));
    containerB->setLayout(std::make_unique<newui::AnchorLayout>());
    surface->addChild(containerB);

    auto* control = new newui::SubView();
    control->setName("control");
    control->setVisible(true);
    control->setBounds(newui::Rect(10, 10, 30, 20));  // containerA-local
    containerA->addChild(control);

    newui::Rect controlRootBounds = CodeToolsVsix::SelectionOverlay::boundsInRootView(control);
    newui::Point startPt(controlRootBounds.left() + 5.0f, controlRootBounds.top() + 5.0f);
    root.onMouseDown(root, startPt, newui::mbmLeftButton, newui::kmUndefined);
    ASSERT_EQ(editor.viewDesignerController().primary(), control);

    newui::Rect containerBRootBounds = CodeToolsVsix::SelectionOverlay::boundsInRootView(containerB);
    newui::Point dropPt(containerBRootBounds.left() + 20.0f, containerBRootBounds.top() + 20.0f);
    root.onMouseMove(root, dropPt, newui::mbmLeftButton, 0);
    root.onMouseUp(root, dropPt, newui::mbmLeftButton, 0);

    EXPECT_TRUE(containerA->childViews().empty());
    ASSERT_EQ(containerB->childViews().size(), 1u);
    EXPECT_EQ(containerB->childViews()[0], control);
    EXPECT_EQ(control->parent(), containerB);

    // Regression coverage for a real, reported bug: reparenting changes the tree's real
    // structure (unlike an ordinary same-parent Move), but the commit originally never called
    // viewDesignerModel_.refresh() - Document Outline kept showing the pre-reparent shape,
    // including a stale empty row for containerA that emptied PropertiesGrid when clicked.
    // {0} addresses root() itself in this model's own path convention - containerA is
    // root()'s child 0, so its own children live at {0, 0}; containerB's at {0, 1}.
    EXPECT_EQ(editor.viewDesignerModel().childCount({0, 0}), 0u);
    EXPECT_EQ(editor.viewDesignerModel().childCount({0, 1}), 1u);

    EXPECT_TRUE(editor.undoStack().canUndo());
    editor.undoStack().undo();

    EXPECT_TRUE(containerB->childViews().empty());
    ASSERT_EQ(containerA->childViews().size(), 1u);
    EXPECT_EQ(containerA->childViews()[0], control);
    EXPECT_EQ(control->parent(), containerA);
    EXPECT_EQ(control->bounds(), newui::Rect(10.0f, 10.0f, 30.0f, 20.0f));

    EXPECT_EQ(editor.viewDesignerModel().childCount({0, 0}), 1u);
    EXPECT_EQ(editor.viewDesignerModel().childCount({0, 1}), 0u);
}

TEST_F(DesignerEditorFileFixture, DraggingAFlexLayoutChildOutOfItsRowReparentsItIntoAnotherContainer)
{
    // Regression test for a real, reported bug: cross-container reparenting first shipped
    // gated to FreePosition-sourced entries only, leaving a FlexLayout-row child (the only
    // real container in most actual test documents, e.g. examples/overlay1.cpp's own
    // buttonRow/sliderRow) able to reorder within its row but never reparent out of it at all.
    newui::RootView root(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    CodeToolsVsix::DesignerEditor editor(&root);
    ASSERT_NE(editor.workspace(), nullptr);
    root.setBounds(newui::Rect(0, 0, 1400, 700));

    newui::RootViewProxy* surface = editor.workspace()->rootViewProxy();
    ASSERT_NE(surface, nullptr);

    auto* row = new newui::SubView();
    row->setName("row");
    row->setVisible(true);
    row->setBounds(newui::Rect(10, 10, 200, 30));
    row->setLayout(std::make_unique<newui::FlexLayout>(newui::Orientation::Horizontal));
    surface->addChild(row);

    auto* a = new newui::SubView();
    a->setName("a");
    a->setVisible(true);
    a->setDesiredSize(newui::Size(80.0f, 30.0f));
    row->addChild(a);

    auto* target = new newui::SubView();
    target->setName("target");
    target->setVisible(true);
    target->setBounds(newui::Rect(300, 10, 100, 100));
    target->setLayout(std::make_unique<newui::AnchorLayout>());
    surface->addChild(target);

    newui::Rect aRootBounds = CodeToolsVsix::SelectionOverlay::boundsInRootView(a);
    newui::Point startPt(aRootBounds.left() + 5.0f, aRootBounds.top() + 5.0f);
    root.onMouseDown(root, startPt, newui::mbmLeftButton, newui::kmUndefined);
    ASSERT_EQ(editor.viewDesignerController().primary(), a);

    newui::Rect targetRootBounds = CodeToolsVsix::SelectionOverlay::boundsInRootView(target);
    newui::Point dropPt(targetRootBounds.left() + 20.0f, targetRootBounds.top() + 20.0f);
    root.onMouseMove(root, dropPt, newui::mbmLeftButton, 0);
    root.onMouseUp(root, dropPt, newui::mbmLeftButton, 0);

    EXPECT_TRUE(row->childViews().empty());
    ASSERT_EQ(target->childViews().size(), 1u);
    EXPECT_EQ(target->childViews()[0], a);
    EXPECT_EQ(a->parent(), target);

    EXPECT_TRUE(editor.undoStack().canUndo());
    editor.undoStack().undo();

    EXPECT_TRUE(target->childViews().empty());
    ASSERT_EQ(row->childViews().size(), 1u);
    EXPECT_EQ(row->childViews()[0], a);
    EXPECT_EQ(a->parent(), row);
}

TEST_F(DesignerEditorFileFixture, DraggingAFlexLayoutChildIntoAVerticalFlexLayoutTargetLandsAtTheDropPointNotAStaleSourceIndex)
{
    // Regression test for a real, reported bug: buildReparentAction() used to read the dragged
    // view's own bounds() to figure out "where is it right now" - correct for a FreePosition
    // source (whose bounds() really do track the cursor), but wrong for a LinearReorder source
    // (whose bounds() are managed by the *source* row's own FlexLayout the whole time, never the
    // cursor) - the control landed wherever the source row's own layout last put it (e.g. the
    // 3rd slot), not at the real drop point.
    newui::RootView root(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    CodeToolsVsix::DesignerEditor editor(&root);
    ASSERT_NE(editor.workspace(), nullptr);
    root.setBounds(newui::Rect(0, 0, 1400, 700));

    newui::RootViewProxy* surface = editor.workspace()->rootViewProxy();
    ASSERT_NE(surface, nullptr);

    auto* row = new newui::SubView();
    row->setName("row");
    row->setVisible(true);
    row->setBounds(newui::Rect(10, 10, 200, 30));
    row->setLayout(std::make_unique<newui::FlexLayout>(newui::Orientation::Horizontal));
    surface->addChild(row);

    auto* a = new newui::SubView();
    a->setName("a");
    a->setVisible(true);
    a->setDesiredSize(newui::Size(80.0f, 30.0f));
    row->addChild(a);

    auto* target = new newui::SubView();
    target->setName("target");
    target->setVisible(true);
    target->setBounds(newui::Rect(300, 10, 100, 200));
    target->setLayout(std::make_unique<newui::FlexLayout>(newui::Orientation::Vertical));
    surface->addChild(target);

    auto* t0 = new newui::SubView();
    t0->setName("t0");
    t0->setVisible(true);
    t0->setDesiredSize(newui::Size(100.0f, 50.0f));
    target->addChild(t0);

    auto* t1 = new newui::SubView();
    t1->setName("t1");
    t1->setVisible(true);
    t1->setDesiredSize(newui::Size(100.0f, 50.0f));
    target->addChild(t1);

    newui::Rect aRootBounds = CodeToolsVsix::SelectionOverlay::boundsInRootView(a);
    newui::Point startPt(aRootBounds.left() + 5.0f, aRootBounds.top() + 5.0f);
    root.onMouseDown(root, startPt, newui::mbmLeftButton, newui::kmUndefined);
    ASSERT_EQ(editor.viewDesignerController().primary(), a);

    // Drop near target's own top edge - above t0's own center - so a should land at index 0.
    newui::Rect targetRootBounds = CodeToolsVsix::SelectionOverlay::boundsInRootView(target);
    newui::Point dropPt(targetRootBounds.left() + 10.0f, targetRootBounds.top() + 5.0f);
    root.onMouseMove(root, dropPt, newui::mbmLeftButton, 0);
    root.onMouseUp(root, dropPt, newui::mbmLeftButton, 0);

    ASSERT_EQ(target->childViews().size(), 3u);
    EXPECT_EQ(target->childViews()[0], a);
    EXPECT_EQ(target->childViews()[1], t0);
    EXPECT_EQ(target->childViews()[2], t1);
}

TEST_F(DesignerEditorFileFixture, DraggingATopLevelControlIntoASiblingContainerReparentsIntoIt)
{
    // Regression test for a real, reported bug: once a control's own parent is rootViewProxy()
    // itself, it becomes rootViewProxy()'s own frontmost child - an ordinary hit-test always
    // re-hit the dragged control itself first (a FreePosition drag's cursor is always inside its
    // own live-tracked bounds), never reaching a sibling container at all, and the old "only
    // hit-test once outside the current parent's own bounds" gate could never fire either, since
    // every sibling sits *within* rootViewProxy()'s own bounds.
    newui::RootView root(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    CodeToolsVsix::DesignerEditor editor(&root);
    ASSERT_NE(editor.workspace(), nullptr);
    root.setBounds(newui::Rect(0, 0, 1400, 700));

    newui::RootViewProxy* surface = editor.workspace()->rootViewProxy();
    ASSERT_NE(surface, nullptr);

    auto* control = new newui::SubView();
    control->setName("control");
    control->setVisible(true);
    control->setBounds(newui::Rect(10, 10, 30, 20));  // surface-local
    surface->addChild(control);

    auto* target = new newui::SubView();
    target->setName("target");
    target->setVisible(true);
    target->setBounds(newui::Rect(200, 10, 100, 100));  // well clear of control
    target->setLayout(std::make_unique<newui::AnchorLayout>());
    surface->addChild(target);

    newui::Rect controlRootBounds = CodeToolsVsix::SelectionOverlay::boundsInRootView(control);
    newui::Point startPt(controlRootBounds.left() + 5.0f, controlRootBounds.top() + 5.0f);
    root.onMouseDown(root, startPt, newui::mbmLeftButton, newui::kmUndefined);
    ASSERT_EQ(editor.viewDesignerController().primary(), control);

    newui::Rect targetRootBounds = CodeToolsVsix::SelectionOverlay::boundsInRootView(target);
    newui::Point dropPt(targetRootBounds.left() + 20.0f, targetRootBounds.top() + 20.0f);
    root.onMouseMove(root, dropPt, newui::mbmLeftButton, 0);
    root.onMouseUp(root, dropPt, newui::mbmLeftButton, 0);

    ASSERT_EQ(target->childViews().size(), 1u);
    EXPECT_EQ(target->childViews()[0], control);
    EXPECT_EQ(control->parent(), target);
    ASSERT_EQ(surface->childViews().size(), 1u);
    EXPECT_EQ(surface->childViews()[0], target);

    editor.undoStack().undo();

    EXPECT_TRUE(target->childViews().empty());
    EXPECT_EQ(control->parent(), surface);
}

TEST_F(DesignerEditorFileFixture, DraggingOutsideAnyContainerFallsBackToRootViewProxyAsTheReparentTarget)
{
    newui::RootView root(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    CodeToolsVsix::DesignerEditor editor(&root);
    ASSERT_NE(editor.workspace(), nullptr);
    root.setBounds(newui::Rect(0, 0, 1400, 700));

    newui::RootViewProxy* surface = editor.workspace()->rootViewProxy();
    ASSERT_NE(surface, nullptr);

    auto* container = new newui::SubView();
    container->setName("container");
    container->setVisible(true);
    container->setBounds(newui::Rect(10, 10, 60, 60));
    container->setLayout(std::make_unique<newui::AnchorLayout>());
    surface->addChild(container);

    auto* control = new newui::SubView();
    control->setName("control");
    control->setVisible(true);
    control->setBounds(newui::Rect(5, 5, 20, 20));  // container-local
    container->addChild(control);

    newui::Rect controlRootBounds = CodeToolsVsix::SelectionOverlay::boundsInRootView(control);
    newui::Point startPt(controlRootBounds.left() + 5.0f, controlRootBounds.top() + 5.0f);
    root.onMouseDown(root, startPt, newui::mbmLeftButton, newui::kmUndefined);
    ASSERT_EQ(editor.viewDesignerController().primary(), control);

    // Well clear of container's own 60x60 bounds, still inside the surface itself.
    newui::Rect surfaceBounds = CodeToolsVsix::SelectionOverlay::boundsInRootView(surface);
    newui::Point dropPt(surfaceBounds.left() + 300.0f, surfaceBounds.top() + 300.0f);
    root.onMouseMove(root, dropPt, newui::mbmLeftButton, 0);
    root.onMouseUp(root, dropPt, newui::mbmLeftButton, 0);

    EXPECT_TRUE(container->childViews().empty());
    EXPECT_EQ(control->parent(), surface);

    editor.undoStack().undo();

    EXPECT_EQ(control->parent(), container);
    EXPECT_EQ(control->bounds(), newui::Rect(5.0f, 5.0f, 20.0f, 20.0f));
}

TEST(DesignerEditor, OutlineReparentRequestedMovesTheDraggedViewAndIsUndoAware)
{
    // Document Outline's own drag gesture only detects/reports (DocumentOutline::
    // onReparentRequested) - DesignerEditor::handleOutlineReparentRequested() is what actually
    // performs the real, undo-aware View::setParent() mutation. Fire the delegate directly, same
    // "real public Delegate calls are an approved exception" convention the canvas' own
    // root.onMouseDown()/onMouseMove()/onMouseUp() drag tests already use.
    newui::RootView root(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    CodeToolsVsix::DesignerEditor editor(&root);
    ASSERT_NE(editor.workspace(), nullptr);
    root.setBounds(newui::Rect(0, 0, 1400, 700));

    newui::RootViewProxy* surface = editor.workspace()->rootViewProxy();
    ASSERT_NE(surface, nullptr);

    auto* control = new newui::SubView();
    control->setName("control");
    control->setVisible(true);
    control->setBounds(newui::Rect(10, 10, 30, 20));
    surface->addChild(control);

    auto* target = new newui::SubView();
    target->setName("target");
    target->setVisible(true);
    target->setBounds(newui::Rect(200, 10, 100, 100));
    target->setLayout(std::make_unique<newui::AnchorLayout>());
    surface->addChild(target);

    CodeToolsVsix::DocumentOutline* outline = editor.workspace()->documentOutlinePane();
    ASSERT_NE(outline, nullptr);
    outline->onReparentRequested(*outline, control, target);

    ASSERT_EQ(target->childViews().size(), 1u);
    EXPECT_EQ(target->childViews()[0], control);
    EXPECT_EQ(control->parent(), target);
    EXPECT_TRUE(surface->childViews().size() == 1u && surface->childViews()[0] == target);

    ASSERT_TRUE(editor.undoStack().canUndo());
    editor.undoStack().undo();

    EXPECT_TRUE(target->childViews().empty());
    EXPECT_EQ(control->parent(), surface);
}

TEST(DesignerEditor, OutlineReparentRequestedIgnoresADropOntoTheDraggedViewsOwnCurrentParent)
{
    newui::RootView root(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    CodeToolsVsix::DesignerEditor editor(&root);
    ASSERT_NE(editor.workspace(), nullptr);
    root.setBounds(newui::Rect(0, 0, 1400, 700));

    newui::RootViewProxy* surface = editor.workspace()->rootViewProxy();
    ASSERT_NE(surface, nullptr);

    auto* control = new newui::SubView();
    control->setName("control");
    control->setVisible(true);
    control->setBounds(newui::Rect(10, 10, 30, 20));
    surface->addChild(control);

    CodeToolsVsix::DocumentOutline* outline = editor.workspace()->documentOutlinePane();
    ASSERT_NE(outline, nullptr);
    outline->onReparentRequested(*outline, control, surface);

    EXPECT_FALSE(editor.undoStack().canUndo());
    EXPECT_EQ(control->parent(), surface);
}

TEST_F(DesignerEditorFileFixture, LoadFailsForAMissingFile)
{
    newui::RootView view(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    CodeToolsVsix::DesignerEditor editor(&view);

    std::wstring path = filePath();
    EXPECT_FALSE(editor.load(path.c_str(), path.size()));
}

TEST_F(DesignerEditorFileFixture, LoadPopulatesTheRootViewFromARealFrameShapedFile)
{
    writeFile(R"({
        type: "Frame",
        title: "Probe Title",
        rootView: {
            type: "RootView",
            childViews: [
                { type: "SubView", name: "probeChild" },
            ],
        },
    })");

    newui::RootView view(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    CodeToolsVsix::DesignerEditor editor(&view);

    std::wstring path = filePath();
    ASSERT_TRUE(editor.load(path.c_str(), path.size()));

    // view itself now hosts only the Workspace chrome - the loaded
    // document's own tree lives under workspace()->rootViewProxy() (see
    // DesignerEditor's own header comment).
    ASSERT_EQ(view.childViews().size(), 1u);
    EXPECT_EQ(view.childViews()[0], editor.workspace());

    ASSERT_NE(editor.workspace()->rootViewProxy(), nullptr);
    ASSERT_EQ(editor.workspace()->rootViewProxy()->childViews().size(), 1u);
    EXPECT_EQ(editor.workspace()->rootViewProxy()->childViews()[0]->name(), "probeChild");
    EXPECT_TRUE(editor.workspace()->rootViewProxy()->childViews()[0]->isDesignTime());
}

TEST_F(DesignerEditorFileFixture, SaveFailsWhenTheContainingDirectoryDoesNotExist)
{
    newui::RootView view(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    CodeToolsVsix::DesignerEditor editor(&view);

    std::wstring badPath = L"C:\\SomewhereThatDoesNotExist12345\\DesignerEditorProbe.newui";
    EXPECT_FALSE(editor.save(badPath.c_str(), badPath.size()));
}

TEST_F(DesignerEditorFileFixture, SavePreservesTitleAndBoundsWhileReplacingRootView)
{
    // Seed a real Frame-shaped file, same as the load test above.
    writeFile(R"({
        title: "Original Title",
        bounds: { type: "Rect", pos: { type: "Point", x: 1, y: 2 }, size: { type: "Size", width: 300, height: 200 } },
        rootView: {
            type: "RootView",
            childViews: [
                { type: "SubView", name: "oldChild" },
            ],
        },
    })");

    // A DesignerEditor whose own design surface (workspace()->
    // rootViewProxy(), not its hosting RootView directly - see this test
    // file's own LoadPopulatesTheRootViewFromARealFrameShapedFile) has
    // different content entirely.
    newui::RootView view(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    CodeToolsVsix::DesignerEditor editor(&view);
    newui::SubView* newChild = new newui::SubView();
    newChild->setName("editedChild");
    editor.workspace()->rootViewProxy()->addChild(newChild);

    std::wstring path = filePath();
    ASSERT_TRUE(editor.save(path.c_str(), path.size()));

    newui::Frame reloaded;
    ASSERT_TRUE(newui::Bundle::instance().loadFrameFromFile(reloaded, path_));

    EXPECT_EQ(reloaded.getTitle(), "Original Title");
    EXPECT_EQ(reloaded.getBounds(), newui::Rect(1, 2, 300, 200));
    ASSERT_EQ(reloaded.rootView().childViews().size(), 1u);
    EXPECT_EQ(reloaded.rootView().childViews()[0]->name(), "editedChild");
}

// Toolbar (top of Workspace) - real newui::Toolbar/ToolbarButton/
// SegmentedControl, not the plain SubView it used to be. No file needed -
// same bare-RootView construction as ConstructionSetsDesignTimeOnly... above.
TEST(DesignerEditorToolbar, TopBarHasFiveButtonsAndAModeControl)
{
    newui::RootView view(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    CodeToolsVsix::DesignerEditor editor(&view);

    ASSERT_NE(editor.workspace(), nullptr);
    EXPECT_NE(editor.workspace()->newButton(), nullptr);
    EXPECT_NE(editor.workspace()->openButton(), nullptr);
    EXPECT_NE(editor.workspace()->saveButton(), nullptr);
    EXPECT_NE(editor.workspace()->undoButton(), nullptr);
    EXPECT_NE(editor.workspace()->redoButton(), nullptr);
    EXPECT_NE(editor.workspace()->modeControl(), nullptr);
}

TEST(DesignerEditorToolbar, ModeControlHasDesignSourceDataFlowWithOnlyDesignEnabled)
{
    newui::RootView view(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    CodeToolsVsix::DesignerEditor editor(&view);

    auto* mode = editor.workspace()->modeControl();
    ASSERT_EQ(mode->segments().size(), 3u);
    EXPECT_EQ(mode->segments()[0], "Design");
    EXPECT_EQ(mode->segments()[1], "Source");
    EXPECT_EQ(mode->segments()[2], "Data Flow");
    EXPECT_TRUE(mode->isSegmentEnabled(CodeToolsVsix::Workspace::kDesignModeSegment));
    EXPECT_FALSE(mode->isSegmentEnabled(CodeToolsVsix::Workspace::kSourceModeSegment));
    EXPECT_FALSE(mode->isSegmentEnabled(CodeToolsVsix::Workspace::kDataFlowModeSegment));
    EXPECT_EQ(mode->selectedIndex(), CodeToolsVsix::Workspace::kDesignModeSegment);
}

TEST(DesignerEditorToolbar, UndoRedoButtonsStartDisabled)
{
    newui::RootView view(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    CodeToolsVsix::DesignerEditor editor(&view);

    EXPECT_FALSE(editor.workspace()->undoButton()->isEnabled());
    EXPECT_FALSE(editor.workspace()->redoButton()->isEnabled());
}

// Fires the real Control::onClick delegate directly - the same "call the
// real public API, not a synthesized OS mouse event" convention Toolbox's
// own onEntryActivated tests already use (test_toolbox.cpp), not the
// keyboard/mouse-handler simulation [[feedback_no_synthetic_input_unit_tests]]
// warns against - this exercises the business logic a click reaches, not
// whether a click reaches it.
TEST(DesignerEditorToolbar, PushingARealUndoableActionEnablesUndoAndOnUndoEnablesRedo)
{
    newui::RootView view(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    CodeToolsVsix::DesignerEditor editor(&view);

    bool didUndo = false;
    editor.undoStack().push(newui::UndoableAction{"test action", [](){}, [&didUndo](){ didUndo = true; }});

    EXPECT_TRUE(editor.workspace()->undoButton()->isEnabled());
    EXPECT_FALSE(editor.workspace()->redoButton()->isEnabled());

    editor.workspace()->undoButton()->onClick(*editor.workspace()->undoButton());

    EXPECT_TRUE(didUndo);
    EXPECT_FALSE(editor.workspace()->undoButton()->isEnabled());
    EXPECT_TRUE(editor.workspace()->redoButton()->isEnabled());
}

TEST(DesignerEditorToolbar, ToolboxAddIsUndoAwareAndMarksTheDocumentDirty)
{
    newui::RootView view(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    CodeToolsVsix::DesignerEditor editor(&view);
    ASSERT_FALSE(editor.isDirty());
    ASSERT_FALSE(editor.workspace()->undoButton()->isEnabled());

    auto* created = new newui::SubView();
    created->setName("addedChild");
    editor.workspace()->toolboxPane()->onEntryActivated(*editor.workspace()->toolboxPane(), created);

    ASSERT_EQ(editor.workspace()->rootViewProxy()->childViews().size(), 1u);
    EXPECT_EQ(editor.workspace()->rootViewProxy()->childViews()[0], created);
    EXPECT_TRUE(created->isDesignTime());
    EXPECT_TRUE(editor.isDirty());
    EXPECT_TRUE(editor.workspace()->undoButton()->isEnabled());

    editor.workspace()->undoButton()->onClick(*editor.workspace()->undoButton());
    EXPECT_TRUE(editor.workspace()->rootViewProxy()->childViews().empty());

    editor.workspace()->redoButton()->onClick(*editor.workspace()->redoButton());
    ASSERT_EQ(editor.workspace()->rootViewProxy()->childViews().size(), 1u);
    EXPECT_EQ(editor.workspace()->rootViewProxy()->childViews()[0], created);
}

TEST(DesignerEditorToolbar, ToolboxAddGivesTheNewControlSensibleDefaultBoundsAndLayoutParams)
{
    // Regression test for a real, reported bug: a freshly-created Toolbox control used to
    // land at Rect() = (0,0,0,0) with no LayoutParams at all - since rootViewProxy_'s own
    // AnchorLayout leaves an unconfigured child exactly where it is forever, it stayed pinned
    // at the top-left corner through every future resize too.
    newui::RootView view(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    CodeToolsVsix::DesignerEditor editor(&view);

    auto* created = new newui::SubView();
    editor.workspace()->toolboxPane()->onEntryActivated(*editor.workspace()->toolboxPane(), created);

    EXPECT_TRUE(created->isVisible());
    EXPECT_NE(created->bounds(), newui::Rect());
    EXPECT_GT(created->bounds().size().width, 0.0f);
    EXPECT_GT(created->bounds().size().height, 0.0f);

    auto* params = dynamic_cast<newui::AnchorLayoutParams*>(created->layoutParams());
    ASSERT_NE(params, nullptr);
    EXPECT_TRUE(newui::hasAnchor(params->anchors, newui::Anchor::Left));
    EXPECT_TRUE(newui::hasAnchor(params->anchors, newui::Anchor::Top));
    EXPECT_GT(params->width, 0.0f);
    EXPECT_GT(params->height, 0.0f);
}

TEST(DesignerEditorToolbar, ToolboxAddNestsIntoTheSelectedContainerWhenOneExists)
{
    newui::RootView view(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    CodeToolsVsix::DesignerEditor editor(&view);

    // ToolboxRegistry::isContainer() requires a real Layout attached - the live signal that
    // dropping a child into it actually does something.
    auto* container = new newui::SubView();
    container->setName("container");
    container->setVisible(true);
    container->setLayout(std::make_unique<newui::AnchorLayout>());
    editor.workspace()->rootViewProxy()->addChild(container);
    editor.viewDesignerController().selectExclusive(container);

    auto* created = new newui::SubView();
    editor.workspace()->toolboxPane()->onEntryActivated(*editor.workspace()->toolboxPane(), created);

    ASSERT_EQ(container->childViews().size(), 1u);
    EXPECT_EQ(container->childViews()[0], created);
    // rootViewProxy_ itself only ever gained the one container, not created too.
    EXPECT_EQ(editor.workspace()->rootViewProxy()->childViews().size(), 1u);
}

TEST(DesignerEditorToolbar, ToolboxAddFallsBackToRootViewProxyWhenTheSelectionHasNoLayout)
{
    newui::RootView view(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    CodeToolsVsix::DesignerEditor editor(&view);

    // A bare SubView with no Layout attached isn't a container yet (per the current, deliberate
    // "reject until a Layout can actually be added/changed/removed on it from the Properties
    // panel" scoping) - not a legitimate drop target.
    auto* bareSubView = new newui::SubView();
    bareSubView->setVisible(true);
    editor.workspace()->rootViewProxy()->addChild(bareSubView);
    editor.viewDesignerController().selectExclusive(bareSubView);

    auto* created = new newui::SubView();
    editor.workspace()->toolboxPane()->onEntryActivated(*editor.workspace()->toolboxPane(), created);

    EXPECT_TRUE(bareSubView->childViews().empty());
    ASSERT_EQ(editor.workspace()->rootViewProxy()->childViews().size(), 2u);
    EXPECT_EQ(editor.workspace()->rootViewProxy()->childViews()[1], created);
}

TEST(DesignerEditorToolbar, ToolboxAddFallsBackToRootViewProxyWhenTheSelectionIsNotAContainer)
{
    newui::RootView view(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    CodeToolsVsix::DesignerEditor editor(&view);

    // A Button has no Layout of its own either - not a legitimate drop target.
    auto* button = new newui::Button();
    button->setVisible(true);
    editor.workspace()->rootViewProxy()->addChild(button);
    editor.viewDesignerController().selectExclusive(button);

    auto* created = new newui::SubView();
    editor.workspace()->toolboxPane()->onEntryActivated(*editor.workspace()->toolboxPane(), created);

    EXPECT_TRUE(button->childViews().empty());
    ASSERT_EQ(editor.workspace()->rootViewProxy()->childViews().size(), 2u);
    EXPECT_EQ(editor.workspace()->rootViewProxy()->childViews()[1], created);
}

TEST(DesignerEditorToolbar, DeleteKeyRemovesTheSelectedControlAndIsUndoAware)
{
    newui::RootView view(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    CodeToolsVsix::DesignerEditor editor(&view);

    auto* child = new newui::SubView();
    editor.workspace()->rootViewProxy()->addChild(child);
    editor.viewDesignerController().selectExclusive(child);
    ASSERT_FALSE(editor.isDirty());

    view.onKeyDown(view, 0, 0, 0, static_cast<std::uint32_t>(newui::vkDelete));

    EXPECT_TRUE(editor.workspace()->rootViewProxy()->childViews().empty());
    EXPECT_EQ(editor.viewDesignerController().primary(), nullptr);
    EXPECT_TRUE(editor.isDirty());
    EXPECT_TRUE(editor.workspace()->undoButton()->isEnabled());

    editor.workspace()->undoButton()->onClick(*editor.workspace()->undoButton());
    ASSERT_EQ(editor.workspace()->rootViewProxy()->childViews().size(), 1u);
    EXPECT_EQ(editor.workspace()->rootViewProxy()->childViews()[0], child);
}

// Real bug caught live: the first version of handleKeyDownForDelete()
// called removeChild()/addChild() on rootViewProxy() unconditionally,
// which silently no-oped for a view nested inside a container SubView
// (delete did nothing) and, on undo, re-attached the same instance a
// SECOND time directly under rootViewProxy() - leaving it attached to
// two parents at once (corrupting the tree, and crashing on teardown
// since both parents then try to delete the same object).
TEST(DesignerEditorToolbar, DeleteKeyRemovesAControlNestedInsideAContainerAndUndoRestoresItToTheSameParent)
{
    newui::RootView view(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    CodeToolsVsix::DesignerEditor editor(&view);

    auto* container = new newui::SubView();
    container->setName("buttonRow");
    auto* nested = new newui::SubView();
    nested->setName("toggleButton");
    editor.workspace()->rootViewProxy()->addChild(container);
    container->addChild(nested);

    editor.viewDesignerController().selectExclusive(nested);
    view.onKeyDown(view, 0, 0, 0, static_cast<std::uint32_t>(newui::vkDelete));

    ASSERT_TRUE(container->childViews().empty());
    ASSERT_EQ(editor.workspace()->rootViewProxy()->childViews().size(), 1u);
    EXPECT_EQ(editor.workspace()->rootViewProxy()->childViews()[0], container);

    editor.workspace()->undoButton()->onClick(*editor.workspace()->undoButton());

    ASSERT_EQ(container->childViews().size(), 1u);
    EXPECT_EQ(container->childViews()[0], nested);
    EXPECT_EQ(editor.workspace()->rootViewProxy()->childViews().size(), 1u);  // not re-added a second time here
}

TEST(DesignerEditorToolbar, DeleteKeyWithNoSelectionDoesNothing)
{
    newui::RootView view(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    CodeToolsVsix::DesignerEditor editor(&view);

    auto* child = new newui::SubView();
    editor.workspace()->rootViewProxy()->addChild(child);

    view.onKeyDown(view, 0, 0, 0, static_cast<std::uint32_t>(newui::vkDelete));

    EXPECT_EQ(editor.workspace()->rootViewProxy()->childViews().size(), 1u);
    EXPECT_FALSE(editor.workspace()->undoButton()->isEnabled());
}

TEST(DesignerEditorToolbar, UndoRedoStatusLabelReflectsTheStackTopAsActionsAreCommittedUndoneAndRedone)
{
    newui::RootView view(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    CodeToolsVsix::DesignerEditor editor(&view);
    // Real, live-reported bug: text() alone doesn't prove anything renders.
    // The actual cause was a zero desiredSize() - Label (unlike a plain
    // SubView) already defaults to visible, but this codebase's Labels/
    // ToolbarButtons never self-measure (see zoomLabel()'s own real
    // desiredSize() right next to this one) - with none set, AnchorLayout
    // gave it zero width, so it painted nothing regardless of its text.
    // A real resize (matching WorkspaceGetsRealBoundsWhenAddedAlongside...
    // above) is needed for bounds() to reflect anything beyond the tiny
    // 10x10 construction-time default.
    view.setBounds(newui::Rect(0, 0, 1000, 700));
    ASSERT_GT(editor.workspace()->undoRedoStatusLabel()->bounds().size().width, 0.0f);
    ASSERT_TRUE(editor.workspace()->undoRedoStatusLabel()->text().empty());

    editor.undoStack().push(newui::UndoableAction{"Add Button", [](){}, [](){}});
    EXPECT_EQ(editor.workspace()->undoRedoStatusLabel()->text(), "Undo: Add Button");

    editor.workspace()->undoButton()->onClick(*editor.workspace()->undoButton());
    EXPECT_EQ(editor.workspace()->undoRedoStatusLabel()->text(), "Redo: Add Button");

    editor.workspace()->redoButton()->onClick(*editor.workspace()->redoButton());
    EXPECT_EQ(editor.workspace()->undoRedoStatusLabel()->text(), "Undo: Add Button");
}

TEST(DesignerEditorToolbar, ClickingNewClearsTheDesignSurfaceAndSelection)
{
    newui::RootView view(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    CodeToolsVsix::DesignerEditor editor(&view);

    auto* child = new newui::SubView();
    editor.workspace()->rootViewProxy()->addChild(child);
    editor.viewDesignerController().selectExclusive(child);
    ASSERT_FALSE(editor.workspace()->rootViewProxy()->childViews().empty());
    ASSERT_NE(editor.viewDesignerController().primary(), nullptr);

    editor.workspace()->newButton()->onClick(*editor.workspace()->newButton());

    EXPECT_TRUE(editor.workspace()->rootViewProxy()->childViews().empty());
    EXPECT_EQ(editor.viewDesignerController().primary(), nullptr);
    EXPECT_FALSE(editor.isDirty());
}
