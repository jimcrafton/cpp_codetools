#include "../extension/NativeEditControls/DesignerClipboard.h"
#include "../extension/NativeEditControls/DesignerEditor.h"

#include <newui/bundle.h>
#include <newui/dragndrop.h>
#include <newui/controls.h>
#include <newui/frame.h>
#include <newui/keyboard_constants.h>
#include <newui/layout.h>
#include <newui/mouse_constants.h>
#include <newui/reflection.h>
#include <newui/rootview.h>
#include <newui/rootviewproxy.h>
#include <newui/subview.h>
#include <newui/controls.h>

#include <gtest/gtest.h>

#include <any>
#include <fstream>
#include <iterator>
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
    // onDropRequested) - DesignerEditor::handleOutlineDropRequested() is what actually
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
    outline->onDropRequested(*outline, control, target, CodeToolsVsix::DocumentOutlineDropDisposition::Into);

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
    outline->onDropRequested(*outline, control, surface, CodeToolsVsix::DocumentOutlineDropDisposition::Into);

    EXPECT_FALSE(editor.undoStack().canUndo());
    EXPECT_EQ(control->parent(), surface);
}

TEST(DesignerEditor, OutlineDropRequestedReordersWithinTheSameParent)
{
    // Same-parent Before/After is a pure reorder, not a reparent - a, b, c share container as
    // their real parent throughout; dropping a After b must land the list as [b, a, c] and be
    // undo-aware, with no parent change at all.
    newui::RootView root(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    CodeToolsVsix::DesignerEditor editor(&root);
    ASSERT_NE(editor.workspace(), nullptr);
    root.setBounds(newui::Rect(0, 0, 1400, 700));

    newui::RootViewProxy* surface = editor.workspace()->rootViewProxy();
    ASSERT_NE(surface, nullptr);

    auto* container = new newui::SubView();
    container->setName("container");
    container->setLayout(std::make_unique<newui::FlexLayout>(newui::Orientation::Vertical));
    surface->addChild(container);

    auto* a = new newui::SubView();
    a->setName("a");
    container->addChild(a);
    auto* b = new newui::SubView();
    b->setName("b");
    container->addChild(b);
    auto* c = new newui::SubView();
    c->setName("c");
    container->addChild(c);

    CodeToolsVsix::DocumentOutline* outline = editor.workspace()->documentOutlinePane();
    ASSERT_NE(outline, nullptr);
    outline->onDropRequested(*outline, a, b, CodeToolsVsix::DocumentOutlineDropDisposition::After);

    ASSERT_EQ(container->childViews().size(), 3u);
    EXPECT_EQ(container->childViews()[0], b);
    EXPECT_EQ(container->childViews()[1], a);
    EXPECT_EQ(container->childViews()[2], c);
    EXPECT_EQ(a->parent(), container);

    ASSERT_TRUE(editor.undoStack().canUndo());
    editor.undoStack().undo();

    ASSERT_EQ(container->childViews().size(), 3u);
    EXPECT_EQ(container->childViews()[0], a);
    EXPECT_EQ(container->childViews()[1], b);
    EXPECT_EQ(container->childViews()[2], c);
}

TEST(DesignerEditor, OutlineDropRequestedBeforeASiblingLandsAtThatSiblingsRealIndex)
{
    newui::RootView root(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    CodeToolsVsix::DesignerEditor editor(&root);
    ASSERT_NE(editor.workspace(), nullptr);
    root.setBounds(newui::Rect(0, 0, 1400, 700));

    newui::RootViewProxy* surface = editor.workspace()->rootViewProxy();
    ASSERT_NE(surface, nullptr);

    auto* container = new newui::SubView();
    container->setName("container");
    container->setLayout(std::make_unique<newui::FlexLayout>(newui::Orientation::Vertical));
    surface->addChild(container);

    auto* a = new newui::SubView();
    a->setName("a");
    container->addChild(a);
    auto* b = new newui::SubView();
    b->setName("b");
    container->addChild(b);
    auto* c = new newui::SubView();
    c->setName("c");
    container->addChild(c);

    CodeToolsVsix::DocumentOutline* outline = editor.workspace()->documentOutlinePane();
    ASSERT_NE(outline, nullptr);
    // c dropped Before a - moves to the very front.
    outline->onDropRequested(*outline, c, a, CodeToolsVsix::DocumentOutlineDropDisposition::Before);

    ASSERT_EQ(container->childViews().size(), 3u);
    EXPECT_EQ(container->childViews()[0], c);
    EXPECT_EQ(container->childViews()[1], a);
    EXPECT_EQ(container->childViews()[2], b);
}

TEST(DesignerEditor, OutlineDropRequestedIntoOwnParentMovesToTheEndAndIsUndoAware)
{
    // Dropping directly on the parent's own row is now a real gesture (Into, referenceRow ==
    // the dragged view's current parent) rather than a silent no-op - it means "move to the end
    // of this same list", exercised here with a already at the front.
    newui::RootView root(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    CodeToolsVsix::DesignerEditor editor(&root);
    ASSERT_NE(editor.workspace(), nullptr);
    root.setBounds(newui::Rect(0, 0, 1400, 700));

    newui::RootViewProxy* surface = editor.workspace()->rootViewProxy();
    ASSERT_NE(surface, nullptr);

    auto* container = new newui::SubView();
    container->setName("container");
    container->setLayout(std::make_unique<newui::FlexLayout>(newui::Orientation::Vertical));
    surface->addChild(container);

    auto* a = new newui::SubView();
    a->setName("a");
    container->addChild(a);
    auto* b = new newui::SubView();
    b->setName("b");
    container->addChild(b);

    CodeToolsVsix::DocumentOutline* outline = editor.workspace()->documentOutlinePane();
    ASSERT_NE(outline, nullptr);
    outline->onDropRequested(*outline, a, container, CodeToolsVsix::DocumentOutlineDropDisposition::Into);

    ASSERT_EQ(container->childViews().size(), 2u);
    EXPECT_EQ(container->childViews()[0], b);
    EXPECT_EQ(container->childViews()[1], a);

    ASSERT_TRUE(editor.undoStack().canUndo());
    editor.undoStack().undo();

    ASSERT_EQ(container->childViews().size(), 2u);
    EXPECT_EQ(container->childViews()[0], a);
    EXPECT_EQ(container->childViews()[1], b);
}

TEST(DesignerEditor, OutlineDropRequestedBeforeASiblingInADifferentContainerReparentsAtThatIndex)
{
    // Position-precise cross-container insert - dropping Before a specific row in a *different*
    // container reparents into that row's own real parent, landing exactly at its index, not
    // just appended at the end (the pre-existing Into behavior).
    newui::RootView root(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    CodeToolsVsix::DesignerEditor editor(&root);
    ASSERT_NE(editor.workspace(), nullptr);
    root.setBounds(newui::Rect(0, 0, 1400, 700));

    newui::RootViewProxy* surface = editor.workspace()->rootViewProxy();
    ASSERT_NE(surface, nullptr);

    auto* source = new newui::SubView();
    source->setName("source");
    source->setLayout(std::make_unique<newui::AnchorLayout>());
    surface->addChild(source);

    auto* control = new newui::SubView();
    control->setName("control");
    control->setVisible(true);
    control->setBounds(newui::Rect(5, 5, 20, 20));
    source->addChild(control);

    auto* target = new newui::SubView();
    target->setName("target");
    target->setBounds(newui::Rect(200, 10, 100, 100));
    target->setLayout(std::make_unique<newui::FlexLayout>(newui::Orientation::Vertical));
    surface->addChild(target);

    auto* x = new newui::SubView();
    x->setName("x");
    target->addChild(x);
    auto* y = new newui::SubView();
    y->setName("y");
    target->addChild(y);

    CodeToolsVsix::DocumentOutline* outline = editor.workspace()->documentOutlinePane();
    ASSERT_NE(outline, nullptr);
    // control dropped Before y (inside target, a different container than source).
    outline->onDropRequested(*outline, control, y, CodeToolsVsix::DocumentOutlineDropDisposition::Before);

    EXPECT_EQ(control->parent(), target);
    ASSERT_EQ(target->childViews().size(), 3u);
    EXPECT_EQ(target->childViews()[0], x);
    EXPECT_EQ(target->childViews()[1], control);
    EXPECT_EQ(target->childViews()[2], y);
    EXPECT_TRUE(source->childViews().empty());

    ASSERT_TRUE(editor.undoStack().canUndo());
    editor.undoStack().undo();

    EXPECT_EQ(control->parent(), source);
    ASSERT_EQ(source->childViews().size(), 1u);
    EXPECT_EQ(source->childViews()[0], control);
    ASSERT_EQ(target->childViews().size(), 2u);
    EXPECT_EQ(target->childViews()[0], x);
    EXPECT_EQ(target->childViews()[1], y);
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

TEST_F(DesignerEditorFileFixture, LoadingASecondFileCompletelyReplacesTheFirstDocument)
{
    writeFile(R"({
        type: "Frame",
        title: "First Title",
        rootView: {
            type: "RootView",
            childViews: [
                { type: "SubView", name: "firstA" },
                { type: "SubView", name: "firstB" },
                { type: "SubView", name: "firstC" },
            ],
        },
    })");

    newui::RootView view(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    CodeToolsVsix::DesignerEditor editor(&view);

    std::wstring path = filePath();
    ASSERT_TRUE(editor.load(path.c_str(), path.size()));
    ASSERT_EQ(editor.workspace()->rootViewProxy()->childViews().size(), 3u);
    editor.viewDesignerController().selectExclusive(editor.workspace()->rootViewProxy()->childViews()[2]);

    // Second file: fewer children, no title.
    writeFile(R"({
        type: "Frame",
        rootView: {
            type: "RootView",
            childViews: [
                { type: "SubView", name: "secondOnly" },
            ],
        },
    })");
    ASSERT_TRUE(editor.load(path.c_str(), path.size()));

    ASSERT_EQ(editor.workspace()->rootViewProxy()->childViews().size(), 1u);
    EXPECT_EQ(editor.workspace()->rootViewProxy()->childViews()[0]->name(), "secondOnly");
    EXPECT_EQ(editor.workspace()->frameProxy()->title(), "");
    EXPECT_EQ(editor.viewDesignerController().primary(), nullptr);
}

TEST_F(DesignerEditorFileFixture, LoadSizesTheFrameProxyAroundTheFilesRootViewClientSize)
{
    writeFile(R"({
        type: "Frame",
        title: "Sized",
        bounds: { type: "Rect", pos: { type: "Point", x: 0, y: 0 }, size: { type: "Size", width: 380, height: 612 } },
        rootView: {
            type: "RootView", visible: true,
            bounds: { type: "Rect", pos: { type: "Point", x: 0, y: 0 }, size: { type: "Size", width: 380, height: 574 } },
        },
    })");

    newui::RootView view(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    CodeToolsVsix::DesignerEditor editor(&view);

    std::wstring path = filePath();
    ASSERT_TRUE(editor.load(path.c_str(), path.size()));

    // Client area (below the mock title bar) matches the file's rootView; the frame adds the bar.
    EXPECT_FLOAT_EQ(editor.workspace()->frameProxy()->bounds().width(), 380.0f);
    EXPECT_FLOAT_EQ(editor.workspace()->frameProxy()->bounds().height(), 574.0f + newui::FrameProxy::kTitleBarHeight);
    EXPECT_FLOAT_EQ(editor.workspace()->rootViewProxy()->bounds().width(), 380.0f);
    EXPECT_FLOAT_EQ(editor.workspace()->rootViewProxy()->bounds().height(), 574.0f);

    // New goes back to the default blank-document size.
    editor.workspace()->newButton()->onClick(*editor.workspace()->newButton());
    EXPECT_FLOAT_EQ(editor.workspace()->frameProxy()->bounds().width(), CodeToolsVsix::Workspace::kDefaultCanvasWidth);
    EXPECT_FLOAT_EQ(editor.workspace()->frameProxy()->bounds().height(), CodeToolsVsix::Workspace::kDefaultCanvasHeight);
}

TEST_F(DesignerEditorFileFixture, LoadingAnUnreadableFileLeavesTheOpenDocumentUntouched)
{
    newui::RootView view(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    CodeToolsVsix::DesignerEditor editor(&view);
    editor.workspace()->rootViewProxy()->addChild(new newui::SubView());

    writeFile("{ this is not valid json5 ");
    std::wstring path = filePath();
    EXPECT_FALSE(editor.load(path.c_str(), path.size()));

    EXPECT_EQ(editor.workspace()->rootViewProxy()->childViews().size(), 1u);
}

TEST_F(DesignerEditorFileFixture, LoadAdoptsThePathAndLeavesTheDocumentCleanThenNewResetsBoth)
{
    writeFile(R"({ type: "Frame", rootView: { type: "RootView", childViews: [ { type: "SubView", name: "c" } ] } })");
    newui::RootView view(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    CodeToolsVsix::DesignerEditor editor(&view);

    std::wstring path = filePath();
    ASSERT_TRUE(editor.load(path.c_str(), path.size()));
    EXPECT_EQ(editor.document().filePath(), path_);
    EXPECT_FALSE(editor.isDirty());

    editor.markDirty();
    EXPECT_TRUE(editor.isDirty());
    EXPECT_TRUE(editor.document().isModified());

    // Dirty, so New prompts - answer Discard (the default prompt is a blocking message box).
    editor.documentController().setUnsavedChangesHandler([](newui::Document&) {
        return newui::UnsavedChangesChoice::Discard;
    });
    editor.workspace()->newButton()->onClick(*editor.workspace()->newButton());
    EXPECT_FALSE(editor.document().hasFilePath());
    EXPECT_FALSE(editor.isDirty());
}

TEST_F(DesignerEditorFileFixture, NewOverAModifiedDocumentKeepsItWhenTheUserCancels)
{
    newui::RootView view(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    CodeToolsVsix::DesignerEditor editor(&view);
    int prompts = 0;
    editor.documentController().setUnsavedChangesHandler([&](newui::Document&) {
        ++prompts;
        return newui::UnsavedChangesChoice::Cancel;
    });
    editor.workspace()->rootViewProxy()->addChild(new newui::SubView());
    editor.markDirty();

    editor.workspace()->newButton()->onClick(*editor.workspace()->newButton());

    EXPECT_EQ(prompts, 1);
    EXPECT_EQ(editor.workspace()->rootViewProxy()->childViews().size(), 1u);
    EXPECT_TRUE(editor.isDirty());
}

TEST_F(DesignerEditorFileFixture, NewOverAModifiedDocumentClearsItWhenTheUserDiscards)
{
    newui::RootView view(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    CodeToolsVsix::DesignerEditor editor(&view);
    editor.documentController().setUnsavedChangesHandler([](newui::Document&) {
        return newui::UnsavedChangesChoice::Discard;
    });
    editor.workspace()->rootViewProxy()->addChild(new newui::SubView());
    editor.markDirty();

    editor.workspace()->newButton()->onClick(*editor.workspace()->newButton());

    EXPECT_TRUE(editor.workspace()->rootViewProxy()->childViews().empty());
    EXPECT_FALSE(editor.isDirty());
    EXPECT_FALSE(editor.document().hasFilePath());
}

TEST_F(DesignerEditorFileFixture, NewOverAModifiedDocumentSavesFirstWhenTheUserChoosesSave)
{
    writeFile(R"({ type: "Frame", rootView: { type: "RootView" } })");
    newui::RootView view(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    CodeToolsVsix::DesignerEditor editor(&view);
    std::wstring path = filePath();
    ASSERT_TRUE(editor.load(path.c_str(), path.size()));

    editor.workspace()->rootViewProxy()->addChild(new newui::SubView());
    editor.markDirty();
    editor.documentController().setUnsavedChangesHandler([](newui::Document&) {
        return newui::UnsavedChangesChoice::Save;
    });

    editor.workspace()->newButton()->onClick(*editor.workspace()->newButton());

    EXPECT_TRUE(editor.workspace()->rootViewProxy()->childViews().empty());
    newui::Frame reloaded;
    ASSERT_TRUE(newui::Bundle::instance().loadFrameFromFile(reloaded, path_));
    EXPECT_EQ(reloaded.rootView().childViews().size(), 1u);  // the child added before New was saved
}

TEST_F(DesignerEditorFileFixture, FirstSaveOverAnExistingFileKeepsTheOriginalAsABakCopy)
{
    const std::string original = R"(// hand-written header comment
{ type: "Frame", rootView: { type: "RootView" } })";
    writeFile(original);
    newui::RootView view(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    CodeToolsVsix::DesignerEditor editor(&view);
    std::wstring path = filePath();
    ASSERT_TRUE(editor.load(path.c_str(), path.size()));

    ASSERT_TRUE(editor.save(path.c_str(), path.size()));

    std::ifstream bak(path_ + ".bak", std::ios::binary);
    ASSERT_TRUE(bak.is_open());
    std::string bakText((std::istreambuf_iterator<char>(bak)), std::istreambuf_iterator<char>());
    bak.close();
    EXPECT_EQ(bakText, original);
    ::DeleteFileA((path_ + ".bak").c_str());
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

TEST(DesignerEditorToolbar, ModeControlHasDesignSourceDataFlowWithDesignAndSourceEnabled)
{
    newui::RootView view(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    CodeToolsVsix::DesignerEditor editor(&view);

    auto* mode = editor.workspace()->modeControl();
    ASSERT_EQ(mode->segments().size(), 3u);
    EXPECT_EQ(mode->segments()[0], "Design");
    EXPECT_EQ(mode->segments()[1], "Source");
    EXPECT_EQ(mode->segments()[2], "Data Flow");
    EXPECT_TRUE(mode->isSegmentEnabled(CodeToolsVsix::Workspace::kDesignModeSegment));
    EXPECT_TRUE(mode->isSegmentEnabled(CodeToolsVsix::Workspace::kSourceModeSegment));
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
    EXPECT_TRUE(newui::hasAnchor(params->anchors(), newui::Anchor::Left));
    EXPECT_TRUE(newui::hasAnchor(params->anchors(), newui::Anchor::Top));
    EXPECT_GT(params->width(), 0.0f);
    EXPECT_GT(params->height(), 0.0f);
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

// ---------------------------------------------------------------------------
// The Properties panel's own Parent picker (PropertiesModel::Kind::ParentPicker) - wired in
// setupUI() via propertiesPane()->setParentCandidatesProvider()/setParentChangeRequestedHandler(),
// both private (parentCandidatesFor()/handlePropertiesParentChangeRequested(), DesignerEditor.h),
// so exercised here entirely through the real public path (propertiesPane()'s own TreeView),
// same "real public entry point, not a private reach-in" convention the canvas/Outline drag tests
// above already follow.
// ---------------------------------------------------------------------------

TEST(DesignerEditor, PropertiesParentPickerReparentsTheSelectedViewAndIsUndoAware)
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
    container->setLayout(std::make_unique<newui::AnchorLayout>());
    container->setBounds(newui::Rect(200, 10, 100, 100));
    surface->addChild(container);

    auto* control = new newui::SubView();
    control->setName("control");
    control->setVisible(true);
    control->setBounds(newui::Rect(10, 10, 30, 20));
    surface->addChild(control);

    CodeToolsVsix::PropertiesGrid* propertiesGrid = editor.workspace()->propertiesPane();
    ASSERT_NE(propertiesGrid, nullptr);
    propertiesGrid->setSelection(control);
    ASSERT_EQ(propertiesGrid->model().nodeAt({0}).kind, CodeToolsVsix::PropertiesModel::Kind::ParentPicker);

    // Same "select, then click the value column" gesture PropertiesGridTest's own
    // selectAndClickValueColumn() drives - reimplemented here since that helper is local to
    // test_properties_grid.cpp.
    propertiesGrid->treeView()->setSelectedPath(std::vector<std::size_t>{0});
    std::optional<newui::Rect> rowRect = propertiesGrid->treeView()->rectForPath(std::vector<std::size_t>{0});
    ASSERT_TRUE(rowRect.has_value());
    auto* propsController = dynamic_cast<CodeToolsVsix::PropertiesTreeController*>(&propertiesGrid->treeView()->controller());
    ASSERT_NE(propsController, nullptr);
    newui::Rect valueRect = CodeToolsVsix::PropertyItem::valueRectFor(*rowRect, std::vector<std::size_t>{0}, propsController->keyColumnFraction());
    newui::Point clickPt(valueRect.left() + 2.0f, valueRect.top() + 2.0f);
    propertiesGrid->treeView()->onMouseDown(*propertiesGrid->treeView(), clickPt, 0, 0);

    ASSERT_EQ(propertiesGrid->treeView()->childViews().size(), 1u);
    auto* dropdown = dynamic_cast<newui::DropDownList*>(propertiesGrid->treeView()->childViews()[0]);
    ASSERT_NE(dropdown, nullptr);

    // Real container candidates only (surface + container - control itself is excluded, and
    // there's nothing else in the tree) - the exact breadcrumb text isn't asserted here, just that
    // "container" is findable and picking it actually reparents control.
    std::size_t containerIndex = dropdown->model()->size();
    for (std::size_t i = 0; i < dropdown->model()->size(); ++i) {
        std::any value = dropdown->model()->value(i);
        if (const std::string* text = std::any_cast<std::string>(&value); text != nullptr && text->find("container") != std::string::npos) {
            containerIndex = i;
            break;
        }
    }
    ASSERT_LT(containerIndex, dropdown->model()->size()) << "expected \"container\" among the dropdown's own candidates";

    dropdown->setSelectedIndex(containerIndex);

    EXPECT_EQ(control->parent(), container);
    ASSERT_TRUE(editor.undoStack().canUndo());
    editor.undoStack().undo();
    EXPECT_EQ(control->parent(), surface);
}

TEST(DesignerEditor, PropertiesParentPickerNeverOffersTheSelectedViewsOwnDescendantsOrItself)
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
    container->setLayout(std::make_unique<newui::AnchorLayout>());
    surface->addChild(container);

    auto* grandchild = new newui::SubView();
    grandchild->setName("grandchild");
    grandchild->setVisible(true);
    container->addChild(grandchild);

    CodeToolsVsix::PropertiesGrid* propertiesGrid = editor.workspace()->propertiesPane();
    ASSERT_NE(propertiesGrid, nullptr);
    propertiesGrid->setSelection(container);
    ASSERT_EQ(propertiesGrid->model().nodeAt({0}).kind, CodeToolsVsix::PropertiesModel::Kind::ParentPicker);

    propertiesGrid->treeView()->setSelectedPath(std::vector<std::size_t>{0});
    std::optional<newui::Rect> rowRect = propertiesGrid->treeView()->rectForPath(std::vector<std::size_t>{0});
    ASSERT_TRUE(rowRect.has_value());
    auto* propsController = dynamic_cast<CodeToolsVsix::PropertiesTreeController*>(&propertiesGrid->treeView()->controller());
    ASSERT_NE(propsController, nullptr);
    newui::Rect valueRect = CodeToolsVsix::PropertyItem::valueRectFor(*rowRect, std::vector<std::size_t>{0}, propsController->keyColumnFraction());
    propertiesGrid->treeView()->onMouseDown(*propertiesGrid->treeView(),
        newui::Point(valueRect.left() + 2.0f, valueRect.top() + 2.0f), 0, 0);

    ASSERT_EQ(propertiesGrid->treeView()->childViews().size(), 1u);
    auto* dropdown = dynamic_cast<newui::DropDownList*>(propertiesGrid->treeView()->childViews()[0]);
    ASSERT_NE(dropdown, nullptr);

    // container is being reparented, so neither it nor its own descendant (grandchild) can
    // legally appear - only surface (the root) is left as a candidate.
    ASSERT_EQ(dropdown->model()->size(), 1u);
    std::any value = dropdown->model()->value(std::size_t(0));
    const std::string* text = std::any_cast<std::string>(&value);
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(*text, surface->name());
}

// ---------------------------------------------------------------------------
// Toolbox drag-and-drop onto the design surface: dropToolboxEntryAt() is what the surface's
// DropTarget calls (after reading the cursor) - driven directly here, no real OLE drag.
// ---------------------------------------------------------------------------

namespace
{
    std::wstring toolboxPayload(const std::string& displayName)
    {
        const auto& categories = CodeToolsVsix::ToolboxRegistry::categories();
        for (std::size_t c = 0; c < categories.size(); ++c) {
            for (std::size_t e = 0; e < categories[c].entries.size(); ++e) {
                if (categories[c].entries[e].displayName == displayName) {
                    return CodeToolsVsix::Toolbox::dragPayloadFor(c, e);
                }
            }
        }
        return std::wstring();
    }
}

TEST(DesignerEditorToolboxDrop, DroppingOntoAContainerNestsItThereAtTheDropPointAndIsUndoable)
{
    newui::RootView root(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    CodeToolsVsix::DesignerEditor editor(&root);
    root.setBounds(newui::Rect(0, 0, 1400, 700));
    newui::RootViewProxy* surface = editor.workspace()->rootViewProxy();

    auto* container = new newui::SubView();
    container->setVisible(true);
    container->setBounds(newui::Rect(10, 10, 300, 200));
    container->setLayout(std::make_unique<newui::AnchorLayout>());
    surface->addChild(container);
    ASSERT_FALSE(editor.isDirty());

    newui::Rect containerRoot = CodeToolsVsix::SelectionOverlay::boundsInRootView(container);
    newui::Point dropPt(containerRoot.left() + 40.0f, containerRoot.top() + 25.0f);
    ASSERT_TRUE(editor.dropToolboxEntryAt(toolboxPayload("Button"), dropPt));

    ASSERT_EQ(container->childViews().size(), 1u);
    newui::SubView* created = container->childViews()[0];
    EXPECT_TRUE(created->isDesignTime());
    EXPECT_TRUE(created->isVisible());
    EXPECT_FLOAT_EQ(created->bounds().left(), 40.0f);   // container-local, top-left at the drop point
    EXPECT_FLOAT_EQ(created->bounds().top(), 25.0f);
    EXPECT_FLOAT_EQ(created->bounds().width(), CodeToolsVsix::Workspace::kNewControlDefaultWidth);
    EXPECT_FLOAT_EQ(created->bounds().height(), CodeToolsVsix::Workspace::kNewControlDefaultHeight);
    EXPECT_TRUE(editor.isDirty());
    EXPECT_EQ(editor.viewDesignerModel().childCount({0, 0}), 1u);  // Outline refreshed

    ASSERT_TRUE(editor.undoStack().canUndo());
    EXPECT_EQ(editor.undoStack().undoDescription(), "Add Button");
    editor.undoStack().undo();
    EXPECT_TRUE(container->childViews().empty());
    EXPECT_EQ(editor.viewDesignerModel().childCount({0, 0}), 0u);

    editor.undoStack().redo();
    ASSERT_EQ(container->childViews().size(), 1u);
    EXPECT_EQ(container->childViews()[0], created);
}

TEST(DesignerEditorToolboxDrop, DroppingOnEmptySurfaceAddsToTheRootViewProxyAtTheDropPoint)
{
    newui::RootView root(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    CodeToolsVsix::DesignerEditor editor(&root);
    root.setBounds(newui::Rect(0, 0, 1400, 700));
    newui::RootViewProxy* surface = editor.workspace()->rootViewProxy();

    newui::Rect surfaceRoot = CodeToolsVsix::SelectionOverlay::boundsInRootView(surface);
    newui::Point dropPt(surfaceRoot.left() + 60.0f, surfaceRoot.top() + 70.0f);
    ASSERT_TRUE(editor.dropToolboxEntryAt(toolboxPayload("Button"), dropPt));

    ASSERT_EQ(surface->childViews().size(), 1u);
    EXPECT_FLOAT_EQ(surface->childViews()[0]->bounds().left(), 60.0f);
    EXPECT_FLOAT_EQ(surface->childViews()[0]->bounds().top(), 70.0f);
}

TEST(DesignerEditorToolboxDrop, DroppingIntoAFlexContainerInsertsAtTheIndexTheDropPointFallsAt)
{
    newui::RootView root(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    CodeToolsVsix::DesignerEditor editor(&root);
    root.setBounds(newui::Rect(0, 0, 1400, 700));
    newui::RootViewProxy* surface = editor.workspace()->rootViewProxy();

    auto* column = new newui::SubView();
    column->setVisible(true);
    column->setBounds(newui::Rect(10, 10, 100, 300));
    column->setLayout(std::make_unique<newui::FlexLayout>(newui::Orientation::Vertical));
    surface->addChild(column);
    auto* first = new newui::SubView();
    first->setVisible(true);
    first->setBounds(newui::Rect(0, 0, 100, 30));
    column->addChild(first);
    auto* second = new newui::SubView();
    second->setVisible(true);
    second->setBounds(newui::Rect(0, 30, 100, 30));
    column->addChild(second);

    newui::Rect columnRoot = CodeToolsVsix::SelectionOverlay::boundsInRootView(column);
    // Dropped control's own center (drop y + half its default height) lands between the two
    // existing children's centers -> insertion index 1.
    newui::Point dropPt(columnRoot.left() + 10.0f, columnRoot.top() + 14.0f);
    ASSERT_TRUE(editor.dropToolboxEntryAt(toolboxPayload("Button"), dropPt));

    ASSERT_EQ(column->childViews().size(), 3u);
    EXPECT_EQ(column->childViews()[0], first);
    EXPECT_EQ(column->childViews()[2], second);
    EXPECT_NE(column->childViews()[1], first);
    EXPECT_NE(column->childViews()[1], second);
}

TEST(DesignerEditorToolboxDrop, ADropOffTheSurfaceOrOfForeignTextChangesNothing)
{
    newui::RootView root(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    CodeToolsVsix::DesignerEditor editor(&root);
    root.setBounds(newui::Rect(0, 0, 1400, 700));
    newui::RootViewProxy* surface = editor.workspace()->rootViewProxy();
    newui::Rect surfaceRoot = CodeToolsVsix::SelectionOverlay::boundsInRootView(surface);

    // Off the design surface entirely (well left of it, in the toolbox's own area).
    EXPECT_FALSE(editor.dropToolboxEntryAt(toolboxPayload("Button"), newui::Point(2.0f, 2.0f)));
    // On the surface, but not a Toolbox payload.
    newui::Point onSurface(surfaceRoot.left() + 30.0f, surfaceRoot.top() + 30.0f);
    EXPECT_FALSE(editor.dropToolboxEntryAt(L"some other text", onSurface));

    EXPECT_TRUE(surface->childViews().empty());
    EXPECT_FALSE(editor.undoStack().canUndo());
    EXPECT_FALSE(editor.isDirty());
}

TEST(DesignerEditorToolboxDrop, TheDesignSurfaceHasATextDropTargetWired)
{
    newui::RootView root(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    CodeToolsVsix::DesignerEditor editor(&root);

    newui::DropTarget* target = editor.workspace()->rootViewProxy()->dropTarget();
    ASSERT_NE(target, nullptr);
    EXPECT_FALSE(target->onTextDroppedAt.empty());
    EXPECT_FALSE(target->onTextDragOver.empty());
    EXPECT_FALSE(target->onDragLeave.empty());
}

// ---- Hover feedback while a Toolbox drag is over the surface ----

namespace
{
    // A designer with an AnchorLayout container at (10,10) 300x200 and a vertical FlexLayout column
    // at (400,10) 100x300 holding two 30px-tall children; root-space points computed from them.
    struct HoverFixture
    {
        newui::RootView root{nullptr, newui::Rect(0, 0, 10, 10), "designerRoot"};
        CodeToolsVsix::DesignerEditor editor{&root};
        newui::RootViewProxy* surface = nullptr;
        newui::SubView* anchorBox = nullptr;
        newui::SubView* column = nullptr;

        HoverFixture()
        {
            root.setBounds(newui::Rect(0, 0, 1400, 700));
            surface = editor.workspace()->rootViewProxy();

            anchorBox = new newui::SubView();
            anchorBox->setVisible(true);
            anchorBox->setBounds(newui::Rect(10, 10, 300, 200));
            anchorBox->setLayout(std::make_unique<newui::AnchorLayout>());
            surface->addChild(anchorBox);

            column = new newui::SubView();
            column->setVisible(true);
            column->setBounds(newui::Rect(400, 10, 100, 300));
            column->setLayout(std::make_unique<newui::FlexLayout>(newui::Orientation::Vertical));
            surface->addChild(column);
            for (float y : { 0.0f, 30.0f }) {
                auto* child = new newui::SubView();
                child->setVisible(true);
                child->setBounds(newui::Rect(0, y, 100, 30));
                column->addChild(child);
            }
        }

        newui::Point inAnchorBox(float x, float y) const
        {
            newui::Rect r = CodeToolsVsix::SelectionOverlay::boundsInRootView(anchorBox);
            return newui::Point(r.left() + x, r.top() + y);
        }
        newui::Point inColumn(float x, float y) const
        {
            newui::Rect r = CodeToolsVsix::SelectionOverlay::boundsInRootView(column);
            return newui::Point(r.left() + x, r.top() + y);
        }
    };
}

TEST(DesignerEditorToolboxHover, OverAFreePositionContainerHighlightsItAndShowsAGhostAtTheDropPoint)
{
    HoverFixture f;
    ASSERT_TRUE(f.editor.hoverToolboxEntryAt(toolboxPayload("Button"), f.inAnchorBox(40, 25)));

    EXPECT_EQ(f.editor.toolboxHoverTarget(), f.anchorBox);
    std::optional<newui::Rect> ghost = f.editor.toolboxHoverGhostRect();
    ASSERT_TRUE(ghost.has_value());
    newui::Rect boxRoot = CodeToolsVsix::SelectionOverlay::boundsInRootView(f.anchorBox);
    EXPECT_FLOAT_EQ(ghost->left(), boxRoot.left() + 40.0f);
    EXPECT_FLOAT_EQ(ghost->top(), boxRoot.top() + 25.0f);
    EXPECT_FLOAT_EQ(ghost->size().width, CodeToolsVsix::Workspace::kNewControlDefaultWidth);

    // Hovering creates nothing and pushes no undo step.
    EXPECT_EQ(f.anchorBox->childViews().size(), 0u);
    EXPECT_FALSE(f.editor.undoStack().canUndo());

    f.editor.endToolboxHover();
    EXPECT_EQ(f.editor.toolboxHoverTarget(), nullptr);
    EXPECT_FALSE(f.editor.toolboxHoverGhostRect().has_value());
}

TEST(DesignerEditorToolboxHover, OverAFlexContainerResolvesTheInsertionIndexAndNeedsNoGhost)
{
    HoverFixture f;
    ASSERT_TRUE(f.editor.hoverToolboxEntryAt(toolboxPayload("Button"), f.inColumn(10, 14)));

    EXPECT_EQ(f.editor.toolboxHoverTarget(), f.column);
    EXPECT_FALSE(f.editor.toolboxHoverGhostRect().has_value());  // the insertion line is the cue
    const CodeToolsVsix::GeometryEditResult* result = f.editor.toolboxHoverResult();
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->kind, CodeToolsVsix::GeometryEditKind::LinearReorder);
    EXPECT_EQ(result->targetSiblingIndex, 1u);  // same point the drop test above inserts at
}

TEST(DesignerEditorToolboxHover, ForeignTextOrOffTheSurfaceIsRefusedAndClearsEarlierFeedback)
{
    HoverFixture f;
    ASSERT_TRUE(f.editor.hoverToolboxEntryAt(toolboxPayload("Button"), f.inAnchorBox(40, 25)));
    ASSERT_NE(f.editor.toolboxHoverTarget(), nullptr);

    EXPECT_FALSE(f.editor.hoverToolboxEntryAt(L"some other text", f.inAnchorBox(40, 25)));
    EXPECT_EQ(f.editor.toolboxHoverTarget(), nullptr);

    ASSERT_TRUE(f.editor.hoverToolboxEntryAt(toolboxPayload("Button"), f.inAnchorBox(40, 25)));
    EXPECT_FALSE(f.editor.hoverToolboxEntryAt(toolboxPayload("Button"), newui::Point(2.0f, 2.0f)));
    EXPECT_EQ(f.editor.toolboxHoverTarget(), nullptr);
}

// The whole path a real drag takes, minus only the OS's DoDragDrop loop: newui's COMDropTarget
// negotiating the cursor effect, feeding hover positions, and delivering leave/drop.
TEST(DesignerEditorToolboxHover, ThroughARealCOMDropTargetTheHoverNegotiatesEffectsAndAdropCreatesTheControl)
{
    HoverFixture f;
    auto comDropTarget = Microsoft::WRL::Make<newui::COMDropTarget>(f.root);
    auto payload = Microsoft::WRL::Make<newui::TextDataObject>(toolboxPayload("Button"));
    auto foreign = Microsoft::WRL::Make<newui::TextDataObject>(L"just some text");

    DWORD effect = DROPEFFECT_COPY;
    comDropTarget->dragEnterAt(payload.Get(), f.inAnchorBox(40, 25), &effect);
    EXPECT_EQ(effect, static_cast<DWORD>(DROPEFFECT_COPY));
    EXPECT_EQ(f.editor.toolboxHoverTarget(), f.anchorBox);

    effect = DROPEFFECT_COPY;
    comDropTarget->dragOverAt(f.inColumn(10, 14), &effect);  // moved into the flex column
    EXPECT_EQ(effect, static_cast<DWORD>(DROPEFFECT_COPY));
    EXPECT_EQ(f.editor.toolboxHoverTarget(), f.column);

    effect = DROPEFFECT_COPY;
    comDropTarget->dragOverAt(newui::Point(2.0f, 2.0f), &effect);  // off the surface entirely
    EXPECT_EQ(effect, static_cast<DWORD>(DROPEFFECT_NONE));
    EXPECT_EQ(f.editor.toolboxHoverTarget(), nullptr);

    effect = DROPEFFECT_COPY;
    comDropTarget->dragOverAt(f.inAnchorBox(40, 25), &effect);
    ASSERT_EQ(f.editor.toolboxHoverTarget(), f.anchorBox);
    comDropTarget->DragLeave();  // e.g. Escape
    EXPECT_EQ(f.editor.toolboxHoverTarget(), nullptr);

    // Foreign text over the surface: the surface accepts text, but its hover handler refuses.
    effect = DROPEFFECT_COPY;
    comDropTarget->dragEnterAt(foreign.Get(), f.inAnchorBox(40, 25), &effect);
    EXPECT_EQ(effect, static_cast<DWORD>(DROPEFFECT_NONE));
    EXPECT_EQ(f.editor.toolboxHoverTarget(), nullptr);
    comDropTarget->DragLeave();

    // A real drop: ends the hover and creates the control at the drop point.
    effect = DROPEFFECT_COPY;
    comDropTarget->dragEnterAt(payload.Get(), f.inAnchorBox(40, 25), &effect);
    ASSERT_EQ(f.editor.toolboxHoverTarget(), f.anchorBox);
    effect = DROPEFFECT_COPY;
    comDropTarget->dropAt(payload.Get(), f.inAnchorBox(40, 25), &effect);
    EXPECT_EQ(f.editor.toolboxHoverTarget(), nullptr);
    ASSERT_EQ(f.anchorBox->childViews().size(), 1u);
    EXPECT_FLOAT_EQ(f.anchorBox->childViews()[0]->bounds().left(), 40.0f);
    EXPECT_FLOAT_EQ(f.anchorBox->childViews()[0]->bounds().top(), 25.0f);
}

// Real bug: during a real drag Windows runs its own modal loop (DoDragDrop), and RootView::
// markDirty() only *schedules* the repaint on the app's RunLoop idle queue - which doesn't run until
// that loop ends. So every hover change has to repaint synchronously, or the cues only show up
// after the drag is already over. Unchanged hovers (DragOver also fires on a timer) must not
// repaint the whole window each time.
TEST(DesignerEditorToolboxHover, HoverChangesRepaintSynchronouslyAndAnUnchangedHoverDoesNot)
{
    HoverFixture f;
    int redraws = 0;
    f.root.onRedrawNeeded.add([&redraws](newui::RootView&) {
        ++redraws;
        return newui::SyncReturn::Ignored;
    });

    ASSERT_TRUE(f.editor.hoverToolboxEntryAt(toolboxPayload("Button"), f.inAnchorBox(40, 25)));
    EXPECT_EQ(redraws, 1);  // entering repaints right now, not "later on idle"

    ASSERT_TRUE(f.editor.hoverToolboxEntryAt(toolboxPayload("Button"), f.inAnchorBox(40, 25)));
    EXPECT_EQ(redraws, 1);  // same spot again (the periodic DragOver): nothing changed

    ASSERT_TRUE(f.editor.hoverToolboxEntryAt(toolboxPayload("Button"), f.inAnchorBox(80, 60)));
    EXPECT_EQ(redraws, 2);  // the ghost moved

    ASSERT_TRUE(f.editor.hoverToolboxEntryAt(toolboxPayload("Button"), f.inColumn(10, 14)));
    EXPECT_EQ(redraws, 3);  // different container

    f.editor.endToolboxHover();
    EXPECT_EQ(redraws, 4);  // leaving repaints right now too, so the cues actually disappear
    f.editor.endToolboxHover();
    EXPECT_EQ(redraws, 4);  // nothing was showing
}

// Real pixels: the hover's ghost has to actually be painted by the overlay - including with
// nothing selected, which SelectionOverlay::paint() used to bail out on.
TEST(DesignerEditorToolboxHover, TheGhostOutlineIsActuallyPaintedByTheOverlayWithNothingSelected)
{
    HoverFixture f;
    ASSERT_EQ(f.editor.viewDesignerController().primary(), nullptr);
    ASSERT_NE(f.root.overlay(), nullptr);
    ASSERT_TRUE(f.editor.hoverToolboxEntryAt(toolboxPayload("Button"), f.inAnchorBox(40, 25)));
    newui::Rect ghost = *f.editor.toolboxHoverGhostRect();

    BLImage image(1400, 700, BL_FORMAT_PRGB32);
    {
        BLContext ctx(image);
        ctx.clear_all();
        f.root.overlay()->paint(ctx, newui::Rect(0, 0, 1400, 700));
        ctx.end();
    }
    BLImageData data{};
    ASSERT_EQ(image.get_data(&data), BL_SUCCESS);

    // Some pixel in a small band around the ghost's left edge mid-height must be the green
    // "valid drop" stroke (G clearly above R and B).
    bool foundGreen = false;
    int y = static_cast<int>(ghost.top() + ghost.size().height * 0.5f);
    for (int x = static_cast<int>(ghost.left()) - 2; x <= static_cast<int>(ghost.left()) + 2; ++x) {
        const auto* row = static_cast<const std::uint8_t*>(data.pixel_data) + y * data.stride;
        std::uint32_t px = reinterpret_cast<const std::uint32_t*>(row)[x];
        std::uint32_t g = (px >> 8) & 0xFF, r = (px >> 16) & 0xFF, b = px & 0xFF;
        if (g > r + 40 && g > b + 20) {
            foundGreen = true;
        }
    }
    EXPECT_TRUE(foundGreen);

    // ...and once the hover ends, the same paint leaves no ghost behind.
    f.editor.endToolboxHover();
    BLImage after(1400, 700, BL_FORMAT_PRGB32);
    {
        BLContext ctx(after);
        ctx.clear_all();
        f.root.overlay()->paint(ctx, newui::Rect(0, 0, 1400, 700));
        ctx.end();
    }
    BLImageData afterData{};
    ASSERT_EQ(after.get_data(&afterData), BL_SUCCESS);
    const auto* afterRow = static_cast<const std::uint8_t*>(afterData.pixel_data) + y * afterData.stride;
    for (int x = static_cast<int>(ghost.left()) - 2; x <= static_cast<int>(ghost.left()) + 2; ++x) {
        EXPECT_EQ(reinterpret_cast<const std::uint32_t*>(afterRow)[x] >> 24, 0u) << "x=" << x;
    }
}

// ---------------------------------------------------------------------------
// ComponentEditor verbs (right-click context menu) - the native popup itself blocks on real
// input, so the tests drive createComponentEditorFor() directly, which is everything the menu's
// items call.
// ---------------------------------------------------------------------------

TEST(DesignerEditorComponentEditor, ATabControlsAddTabVerbIsUndoableRefreshesTheOutlineAndMarksDirty)
{
    CodeToolsVsix::ComponentEditorRegistry::instance().registerBuiltinEditors();
    CodeToolsVsix::PropertyEditorRegistry::instance().registerBuiltinEditors();

    newui::RootView root(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    CodeToolsVsix::DesignerEditor editor(&root);
    root.setBounds(newui::Rect(0, 0, 1400, 700));
    newui::RootViewProxy* surface = editor.workspace()->rootViewProxy();

    auto* tabs = new newui::TabControl();
    tabs->setBounds(newui::Rect(10, 10, 300, 200));
    surface->addChild(tabs);
    editor.viewDesignerModel().refresh();
    ASSERT_FALSE(editor.isDirty());

    auto verbs = editor.createComponentEditorFor(tabs);
    ASSERT_NE(verbs, nullptr);
    verbs->executeVerb(0);

    EXPECT_EQ(tabs->tabCount(), 1u);
    EXPECT_TRUE(editor.isDirty());
    ASSERT_TRUE(editor.undoStack().canUndo());
    EXPECT_EQ(editor.undoStack().undoDescription(), "Add Tab");
    editor.undoStack().undo();
    EXPECT_EQ(tabs->tabCount(), 0u);
}

TEST(DesignerEditorComponentEditor, AClassWithNoRegisteredEditorYieldsNone)
{
    CodeToolsVsix::ComponentEditorRegistry::instance().registerBuiltinEditors();
    CodeToolsVsix::PropertyEditorRegistry::instance().registerBuiltinEditors();

    newui::RootView root(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    CodeToolsVsix::DesignerEditor editor(&root);
    newui::SubView plain;

    EXPECT_EQ(editor.createComponentEditorFor(&plain), nullptr);
    EXPECT_EQ(editor.createComponentEditorFor(nullptr), nullptr);
}

TEST(DesignerEditorComponentEditor, InternalAndReadOnlyViewsGetNoComponentEditor)
{
    CodeToolsVsix::ComponentEditorRegistry::instance().registerBuiltinEditors();
    CodeToolsVsix::PropertyEditorRegistry::instance().registerBuiltinEditors();

    newui::RootView root(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    CodeToolsVsix::DesignerEditor editor(&root);
    auto* tabs = new newui::TabControl();

    ASSERT_NE(editor.createComponentEditorFor(tabs), nullptr);
    tabs->setDesignTimeFlag(newui::DesignTimeFlags::ReadOnly);
    EXPECT_EQ(editor.createComponentEditorFor(tabs), nullptr);
    tabs->setDesignTimeFlags(newui::DesignTimeFlags::Internal);
    EXPECT_EQ(editor.createComponentEditorFor(tabs), nullptr);

    tabs->destroy();
    delete tabs;
}

// Tabs added through the designer's "Add Tab" verb come back, with their labels, after Save + reopen.
TEST_F(DesignerEditorFileFixture, TabsAddedInTheDesignerSurviveSaveAndReopen)
{
    CodeToolsVsix::ComponentEditorRegistry::instance().registerBuiltinEditors();
    CodeToolsVsix::PropertyEditorRegistry::instance().registerBuiltinEditors();
    std::wstring path = filePath();
    {
        newui::RootView root(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
        CodeToolsVsix::DesignerEditor editor(&root);
        root.setBounds(newui::Rect(0, 0, 1400, 700));
        auto* tabs = new newui::TabControl();
        tabs->setBounds(newui::Rect(10, 10, 300, 200));
        editor.workspace()->rootViewProxy()->addChild(tabs);
        auto verbs = editor.createComponentEditorFor(tabs);
        verbs->executeVerb(0);
        verbs->executeVerb(0);
        static_cast<newui::TabPage*>(tabs->page(1))->setTitle("Second");
        ASSERT_EQ(tabs->tabCount(), 2u);
        ASSERT_TRUE(editor.save(path.c_str(), path.size()));
    }

    newui::RootView root(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    CodeToolsVsix::DesignerEditor editor(&root);
    root.setBounds(newui::Rect(0, 0, 1400, 700));
    ASSERT_TRUE(editor.load(path.c_str(), path.size()));

    newui::RootViewProxy* surface = editor.workspace()->rootViewProxy();
    ASSERT_EQ(surface->childViews().size(), 1u);
    auto* tabs = dynamic_cast<newui::TabControl*>(surface->childViews()[0]);
    ASSERT_NE(tabs, nullptr);
    EXPECT_EQ(tabs->childViews().size(), 2u);  // strip + pages area, nothing duplicated
    ASSERT_EQ(tabs->tabCount(), 2u);
    EXPECT_EQ(tabs->tabButton(0)->name(), "Tab 1");
    EXPECT_EQ(tabs->tabButton(1)->name(), "Second");
    EXPECT_NE(dynamic_cast<newui::TabPage*>(tabs->page(0)), nullptr);

    // The outline shows just the two pages under the control.
    EXPECT_EQ(editor.viewDesignerModel().childCount(std::vector<std::size_t>{0, 0}), 2u);
}

// The drag feedback overrides a dragged view's Cursor - which is a real, saved property the user can
// set. It must come back exactly as it was (a system kind or a custom file cursor), and a view that
// was never dragged must not be touched at all.
TEST(DesignerEditorDragCursor, TheViewsOwnCursorIsRestoredAfterTheDragFeedback)
{
    newui::RootView root(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    CodeToolsVsix::DesignerEditor editor(&root);

    newui::SubView view;
    view.cursor().setCursorKind(newui::CursorKind::Hand);   // what the user chose in the grid
    newui::SubView untouched;
    untouched.cursor().setCursorKind(newui::CursorKind::IBeam);

    editor.beginDragCursor(&view, newui::CursorKind::SizeAll);
    EXPECT_EQ(view.cursorKind(), newui::CursorKind::SizeAll);
    editor.beginDragCursor(&view, newui::CursorKind::Hand);   // the reparent-target feedback, same drag
    EXPECT_EQ(view.cursorKind(), newui::CursorKind::Hand);
    editor.beginDragCursor(&view, newui::CursorKind::SizeAll);

    editor.endDragCursors();
    EXPECT_EQ(view.cursorKind(), newui::CursorKind::Hand);    // the user's, not Arrow
    EXPECT_EQ(untouched.cursorKind(), newui::CursorKind::IBeam);

    editor.endDragCursors();   // nothing parked: a plain click must change nothing
    EXPECT_EQ(view.cursorKind(), newui::CursorKind::Hand);
}

TEST(DesignerEditorDragCursor, ACustomFileCursorSurvivesTheDragToo)
{
    char tempPath[MAX_PATH]{};
    ::GetTempPathA(MAX_PATH, tempPath);
    const std::string svg = std::string(tempPath) + "DragCursor.svg";
    {
        std::ofstream file(svg, std::ios::binary);
        file << R"(<svg xmlns="http://www.w3.org/2000/svg" width="32" height="32"><rect width="32" height="32" fill="green"/></svg>)";
    }

    newui::RootView root(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    CodeToolsVsix::DesignerEditor editor(&root);
    newui::SubView view;
    ASSERT_TRUE(view.cursor().loadPath(svg));
    ASSERT_EQ(view.cursorKind(), newui::CursorKind::Custom);

    editor.beginDragCursor(&view, newui::CursorKind::SizeAll);
    EXPECT_EQ(view.cursorKind(), newui::CursorKind::SizeAll);
    editor.endDragCursors();

    EXPECT_EQ(view.cursorKind(), newui::CursorKind::Custom);
    EXPECT_EQ(view.cursor().path(), svg);
    ::DeleteFileA(svg.c_str());
}

// ---------------------------------------------------------------------------
// Copy / paste / duplicate. The system clipboard itself is never touched here (a test run must not
// clobber the developer's clipboard) - pasteSerializedViews() takes the text directly.
// ---------------------------------------------------------------------------

TEST(DesignerEditorCopyPaste, DuplicateClonesTheSelectionOffsetIntoTheSameParentAsOneUndoStep)
{
    HoverFixture f;
    auto* button = new newui::Button();
    button->setText("Go");
    button->setName("myButton");
    button->setBounds(newui::Rect(20, 30, 80, 24));
    f.anchorBox->addChild(button);
    f.editor.viewDesignerModel().refresh();
    f.editor.viewDesignerController().selectExclusive(button);
    ASSERT_FALSE(f.editor.isDirty());

    ASSERT_TRUE(f.editor.duplicateSelection());

    ASSERT_EQ(f.anchorBox->childViews().size(), 2u);
    auto* clone = dynamic_cast<newui::Button*>(f.anchorBox->childViews()[1]);
    ASSERT_NE(clone, nullptr);
    EXPECT_EQ(clone->text(), "Go");
    EXPECT_FALSE(clone->name().empty());
    EXPECT_NE(clone->name(), "myButton");          // a fresh unique name, not a duplicate
    EXPECT_TRUE(clone->isDesignTime());
    auto* params = dynamic_cast<newui::AnchorLayoutParams*>(clone->layoutParams());
    ASSERT_NE(params, nullptr);
    EXPECT_FLOAT_EQ(params->leftMargin(), 20.0f + CodeToolsVsix::DesignerEditor::kPasteOffsetPixels);
    EXPECT_FLOAT_EQ(params->topMargin(), 30.0f + CodeToolsVsix::DesignerEditor::kPasteOffsetPixels);
    EXPECT_FLOAT_EQ(params->width(), 80.0f);
    EXPECT_TRUE(f.editor.isDirty());
    ASSERT_EQ(f.editor.viewDesignerController().selected().size(), 1u);
    EXPECT_EQ(f.editor.viewDesignerController().primary(), clone);   // the clone is what's selected now

    ASSERT_TRUE(f.editor.undoStack().canUndo());
    EXPECT_EQ(f.editor.undoStack().undoDescription(), "Duplicate Control");
    f.editor.undoStack().undo();
    EXPECT_EQ(f.anchorBox->childViews().size(), 1u);
    EXPECT_TRUE(f.editor.viewDesignerController().selected().empty());
    f.editor.undoStack().redo();
    ASSERT_EQ(f.anchorBox->childViews().size(), 2u);
    EXPECT_EQ(f.anchorBox->childViews()[1], clone);                  // the very same instance
}

TEST(DesignerEditorCopyPaste, DuplicatingAContainerAndItsChildTogetherClonesOnlyTheContainer)
{
    HoverFixture f;
    auto* button = new newui::Button();
    f.anchorBox->addChild(button);
    f.editor.viewDesignerModel().refresh();
    const std::size_t before = f.surface->childViews().size();
    f.editor.viewDesignerController().setSelection({ f.anchorBox, button });

    ASSERT_TRUE(f.editor.duplicateSelection());

    ASSERT_EQ(f.surface->childViews().size(), before + 1);
    auto* clonedBox = f.surface->childViews().back();
    EXPECT_EQ(clonedBox->childViews().size(), 1u);   // the child came along inside the container's clone
}

TEST(DesignerEditorCopyPaste, PasteGoesIntoTheNearestContainerAboveTheSelectionAndRekindsItsParams)
{
    HoverFixture f;
    auto* source = new newui::Button();
    source->setText("Pasted");
    source->setBounds(newui::Rect(5, 5, 60, 20));
    f.anchorBox->addChild(source);
    std::string text = CodeToolsVsix::DesignerClipboard::serialize(*source);

    // A child of the vertical Flex column is selected - not a container itself, so its parent gets the paste.
    newui::SubView* columnChild = f.column->childViews()[0];
    f.editor.viewDesignerModel().refresh();
    f.editor.viewDesignerController().selectExclusive(columnChild);

    ASSERT_TRUE(f.editor.pasteSerializedViews({ text }, 0.0f));

    ASSERT_EQ(f.column->childViews().size(), 3u);
    auto* pasted = dynamic_cast<newui::Button*>(f.column->childViews()[2]);
    ASSERT_NE(pasted, nullptr);
    EXPECT_EQ(pasted->text(), "Pasted");
    // The source had AnchorLayoutParams; under a FlexLayout it must carry Flex ones instead.
    EXPECT_NE(dynamic_cast<newui::FlexLayoutParams*>(pasted->layoutParams()), nullptr);
    EXPECT_EQ(f.editor.undoStack().undoDescription(), "Paste Control");
}

TEST(DesignerEditorCopyPaste, WithNothingSelectedAPasteLandsOnTheDesignSurface)
{
    HoverFixture f;
    auto* source = new newui::Button();
    source->setBounds(newui::Rect(5, 5, 60, 20));
    f.anchorBox->addChild(source);
    std::string text = CodeToolsVsix::DesignerClipboard::serialize(*source);
    f.editor.viewDesignerController().clearSelection();
    const std::size_t before = f.surface->childViews().size();

    ASSERT_TRUE(f.editor.pasteSerializedViews({ text, text }, 0.0f));

    EXPECT_EQ(f.surface->childViews().size(), before + 2);
    EXPECT_EQ(f.editor.undoStack().undoDescription(), "Paste Controls");
    EXPECT_FALSE(f.editor.pasteSerializedViews({ "garbage" }, 0.0f));   // unreadable text changes nothing
}

TEST(DesignerEditorCopyPaste, DuplicatingATabControlKeepsItsTabsAndUndoRemovesTheWholeClone)
{
    HoverFixture f;
    auto* tabs = new newui::TabControl();
    tabs->setBounds(newui::Rect(10, 10, 200, 100));
    tabs->addTab("Alpha", new newui::TabPage());
    tabs->addTab("Beta", new newui::TabPage());
    f.anchorBox->addChild(tabs);
    f.editor.viewDesignerModel().refresh();
    f.editor.viewDesignerController().selectExclusive(tabs);

    ASSERT_TRUE(f.editor.duplicateSelection());

    ASSERT_EQ(f.anchorBox->childViews().size(), 2u);
    auto* clone = dynamic_cast<newui::TabControl*>(f.anchorBox->childViews()[1]);
    ASSERT_NE(clone, nullptr);
    ASSERT_EQ(clone->tabCount(), 2u);
    EXPECT_EQ(clone->tabButton(1)->name(), "Beta");

    f.editor.undoStack().undo();
    EXPECT_EQ(f.anchorBox->childViews().size(), 1u);
}

TEST(DesignerEditorCopyPaste, NothingSelectedMeansNothingToDuplicate)
{
    HoverFixture f;
    f.editor.viewDesignerController().clearSelection();
    EXPECT_FALSE(f.editor.duplicateSelection());
    EXPECT_FALSE(f.editor.undoStack().canUndo());
}

// The designer's shortcuts (Ctrl+C/X/V/D, PageUp/PageDown, Delete) share the window with real text
// fields: they must only act while the canvas has attention, and UIInputManager's click-to-focus
// policy is what says so (a click on the canvas clears focus, a click in a field focuses it).
TEST(DesignerEditorKeyboardOwnership, TheCanvasOwnsTheKeyboardOnlyWhileNothingIsFocused)
{
    newui::RootView root(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    CodeToolsVsix::DesignerEditor editor(&root);
    root.setBounds(newui::Rect(0, 0, 1400, 700));
    EXPECT_TRUE(editor.canvasOwnsKeyboard());            // nothing focused: the canvas has attention

    auto* field = new newui::TextField();                // stands in for a Properties field
    field->setVisible(true);
    editor.workspace()->addChild(field);
    root.setFocusedSubView(field);
    ASSERT_EQ(root.focusedSubView(), field);
    EXPECT_FALSE(editor.canvasOwnsKeyboard());           // the field owns Ctrl+C now, not the designer

    root.setFocusedSubView(nullptr);                     // what a canvas click resolves to
    EXPECT_TRUE(editor.canvasOwnsKeyboard());
}

// Ctrl+Z / Shift+Ctrl+Z, the toolbar buttons and the host's Edit commands all go through undo()/redo().
TEST(DesignerEditorUndoRedo, UndoAndRedoStepThroughTheStackAndKeepTheToolbarButtonsInStep)
{
    HoverFixture f;
    auto* button = new newui::Button();
    button->setBounds(newui::Rect(20, 30, 80, 24));
    f.anchorBox->addChild(button);
    f.editor.viewDesignerModel().refresh();
    f.editor.viewDesignerController().selectExclusive(button);

    EXPECT_FALSE(f.editor.undo());                                  // nothing yet
    EXPECT_FALSE(f.editor.redo());

    ASSERT_TRUE(f.editor.duplicateSelection());
    ASSERT_EQ(f.anchorBox->childViews().size(), 2u);
    EXPECT_TRUE(f.editor.workspace()->undoButton()->isEnabled());
    EXPECT_FALSE(f.editor.workspace()->redoButton()->isEnabled());

    EXPECT_TRUE(f.editor.undo());
    EXPECT_EQ(f.anchorBox->childViews().size(), 1u);
    EXPECT_FALSE(f.editor.workspace()->undoButton()->isEnabled());
    EXPECT_TRUE(f.editor.workspace()->redoButton()->isEnabled());

    EXPECT_TRUE(f.editor.redo());
    EXPECT_EQ(f.anchorBox->childViews().size(), 2u);
    EXPECT_FALSE(f.editor.redo());                                  // nothing further to redo
}

// ---------------------------------------------------------------------------
// Grid row / column verbs.
// ---------------------------------------------------------------------------

TEST(DesignerEditorGridVerbs, AddAndRemoveRowsAndColumnsAreUndoableAndOnlyOfferWhatApplies)
{
    HoverFixture f;
    auto* panel = new newui::SubView();
    panel->setVisible(true);
    panel->setBounds(newui::Rect(10, 250, 200, 100));
    panel->setLayout(std::make_unique<newui::GridLayout>());
    f.surface->addChild(panel);
    f.editor.viewDesignerModel().refresh();
    auto* grid = dynamic_cast<newui::GridLayout*>(panel->layout());

    auto editors = f.editor.createComponentEditorsFor(panel);
    ASSERT_EQ(editors.size(), 1u);
    ASSERT_EQ(editors[0]->verbCount(), 2u);            // nothing to remove yet
    EXPECT_EQ(editors[0]->verb(0), "Add Row");
    EXPECT_EQ(editors[0]->verb(1), "Add Column");

    editors[0]->executeVerb(0);
    editors[0]->executeVerb(0);
    editors[0]->executeVerb(1);
    EXPECT_EQ(grid->rows().size(), 2u);
    EXPECT_EQ(grid->columns().size(), 1u);
    EXPECT_EQ(grid->rows()[0].kind, newui::GridTrackKind::Star);
    EXPECT_EQ(f.editor.undoStack().undoDescription(), "Add Column");
    EXPECT_TRUE(f.editor.isDirty());

    ASSERT_EQ(editors[0]->verbCount(), 4u);            // remove appears once there is something to remove
    EXPECT_EQ(editors[0]->verb(2), "Remove Last Row");
    EXPECT_EQ(editors[0]->verb(3), "Remove Last Column");
    editors[0]->executeVerb(2);
    EXPECT_EQ(grid->rows().size(), 1u);
    EXPECT_EQ(f.editor.undoStack().undoDescription(), "Remove Row");

    f.editor.undoStack().undo();                        // the removal
    EXPECT_EQ(grid->rows().size(), 2u);
    f.editor.undoStack().undo();                        // the column
    EXPECT_EQ(grid->columns().size(), 0u);
    f.editor.undoStack().redo();
    EXPECT_EQ(grid->columns().size(), 1u);
}

TEST(DesignerEditorGridVerbs, ViewsWithoutAGridLayoutGetNoGridVerbsAndAGridWithAClassEditorGetsBoth)
{
    CodeToolsVsix::ComponentEditorRegistry::instance().registerBuiltinEditors();
    CodeToolsVsix::PropertyEditorRegistry::instance().registerBuiltinEditors();
    HoverFixture f;
    EXPECT_TRUE(f.editor.createComponentEditorsFor(f.anchorBox).empty());   // Anchor layout, no class editor

    auto* tabs = new newui::TabControl();
    tabs->setBounds(newui::Rect(10, 10, 100, 60));
    f.surface->addChild(tabs);
    auto editors = f.editor.createComponentEditorsFor(tabs);
    ASSERT_EQ(editors.size(), 1u);                       // TabControl's own; its layout is Flex, not Grid
    EXPECT_NE(dynamic_cast<CodeToolsVsix::TabControlEditor*>(editors[0].get()), nullptr);
}

TEST(DesignerEditor, EditComponentOpensTheDefaultPropertyInThePropertiesGrid)
{
    CodeToolsVsix::ComponentEditorRegistry::instance().registerBuiltinEditors();
    CodeToolsVsix::PropertyEditorRegistry::instance().registerBuiltinEditors();
    newui::RootView root(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    CodeToolsVsix::DesignerEditor editor(&root);
    ASSERT_NE(editor.workspace(), nullptr);
    root.setBounds(newui::Rect(0, 0, 1400, 700));

    auto* button = new newui::Button();
    button->setText("OK");
    button->setVisible(true);
    button->setBounds(newui::Rect(10, 10, 80, 24));
    editor.workspace()->rootViewProxy()->addChild(button);
    editor.viewDesignerController().selectExclusive(button);

    // No RunLoop in tests, so the grid edit runs inline.
    editor.editComponent(button);

    CodeToolsVsix::PropertiesGrid* grid = editor.workspace()->propertiesPane();
    std::optional<std::vector<std::size_t>> path = grid->treeView()->selectedPath();
    ASSERT_TRUE(path.has_value());
    ASSERT_EQ(path->size(), 1u);
    const newui::reflection::Property* property = grid->model().nodeAt(*path).property;
    ASSERT_NE(property, nullptr);
    EXPECT_EQ(property->name(), "text");
    ASSERT_EQ(grid->treeView()->childViews().size(), 1u);
    auto* field = dynamic_cast<newui::TextField*>(grid->treeView()->childViews()[0]);
    ASSERT_NE(field, nullptr);
    EXPECT_EQ(root.focusedSubView(), field);
    ASSERT_EQ(field->selection().ranges().size(), 1u);
    EXPECT_EQ(field->selection().ranges()[0].start(), 0u);
    EXPECT_EQ(field->selection().ranges()[0].length(), 2u);
}

TEST(DesignerEditor, EditComponentOnATabSwitchesToItAndOpensItsPageTitle)
{
    CodeToolsVsix::ComponentEditorRegistry::instance().registerBuiltinEditors();
    CodeToolsVsix::PropertyEditorRegistry::instance().registerBuiltinEditors();
    newui::RootView root(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    CodeToolsVsix::DesignerEditor editor(&root);
    ASSERT_NE(editor.workspace(), nullptr);
    root.setBounds(newui::Rect(0, 0, 1400, 700));

    auto* tabs = new newui::TabControl();
    tabs->setVisible(true);
    tabs->setBounds(newui::Rect(10, 10, 300, 200));
    editor.workspace()->rootViewProxy()->addChild(tabs);
    for (int i = 0; i < 2; ++i) {
        auto* page = new newui::TabPage();
        page->setVisible(true);
        tabs->addTab("Tab " + std::to_string(i + 1), page);
    }
    tabs->selectTab(0);
    editor.viewDesignerController().selectExclusive(tabs);

    newui::Rect secondTab = CodeToolsVsix::SelectionOverlay::boundsInRootView(tabs->tabButton(1));
    ASSERT_GT(secondTab.size().width, 0.0f);
    editor.editComponent(tabs, newui::Point(secondTab.left() + 2.0f, secondTab.top() + 2.0f));

    EXPECT_EQ(tabs->selectedIndex(), 1u);
    EXPECT_EQ(editor.viewDesignerController().primary(), tabs->page(1));
    CodeToolsVsix::PropertiesGrid* grid = editor.workspace()->propertiesPane();
    std::optional<std::vector<std::size_t>> path = grid->treeView()->selectedPath();
    ASSERT_TRUE(path.has_value());
    const newui::reflection::Property* property = grid->model().nodeAt(*path).property;
    ASSERT_NE(property, nullptr);
    EXPECT_EQ(property->name(), "title");
}

TEST(DesignerEditor, EditComponentOnATabControlWithNoTabsDoesNothing)
{
    CodeToolsVsix::ComponentEditorRegistry::instance().registerBuiltinEditors();
    CodeToolsVsix::PropertyEditorRegistry::instance().registerBuiltinEditors();
    newui::RootView root(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    CodeToolsVsix::DesignerEditor editor(&root);
    root.setBounds(newui::Rect(0, 0, 1400, 700));

    auto* tabs = new newui::TabControl();
    tabs->setVisible(true);
    tabs->setBounds(newui::Rect(10, 10, 300, 200));
    editor.workspace()->rootViewProxy()->addChild(tabs);
    editor.viewDesignerController().selectExclusive(tabs);

    editor.editComponent(tabs, newui::Point(20, 20));

    EXPECT_EQ(editor.viewDesignerController().primary(), tabs);
    EXPECT_FALSE(editor.workspace()->propertiesPane()->treeView()->selectedPath().has_value());
}

namespace
{
    // A designer with a MenuBar on its surface: File (Open, Recent > a.txt, -, Exit) and Edit.
    struct MenuDesignerFixture
    {
        MenuDesignerFixture() : root(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot"), editor(&root)
        {
            CodeToolsVsix::PropertyEditorRegistry::instance().registerBuiltinEditors();
            root.setBounds(newui::Rect(0, 0, 1400, 700));
            bar = new newui::MenuBar();
            bar->setBounds(newui::Rect(10, 10, 400, 28));
            file = new newui::MenuItem("&File");
            open = file->addChild(std::make_unique<newui::MenuItem>("Open"));
            recent = file->addChild(std::make_unique<newui::MenuItem>("Recent"));
            recent->addChild(std::make_unique<newui::MenuItem>("a.txt"));
            file->addChild(newui::MenuItem::Separator());
            file->addChild(std::make_unique<newui::MenuItem>("Exit"));
            bar->addMenu(file);
            edit = new newui::MenuItem("Edit");
            bar->addMenu(edit);
            editor.workspace()->rootViewProxy()->addChild(bar);
            bar->updateLayout();
        }

        CodeToolsVsix::MenuDesigner& designer() { return *editor.menuDesigner(); }

        newui::RootView root;
        CodeToolsVsix::DesignerEditor editor;
        newui::MenuBar* bar = nullptr;
        newui::MenuItem* file = nullptr;
        newui::MenuItem* open = nullptr;
        newui::MenuItem* recent = nullptr;
        newui::MenuItem* edit = nullptr;
    };
}

TEST(MenuDesigner, SelectingAMenuBarOpensItWithAPlaceholderAfterTheLastMenu)
{
    MenuDesignerFixture f;
    f.editor.viewDesignerController().selectExclusive(f.bar);

    ASSERT_TRUE(f.designer().isOpen());
    EXPECT_EQ(f.designer().menuBar(), f.bar);
    EXPECT_TRUE(f.designer().visibleColumns().empty());
    ASSERT_NE(f.designer().barPlaceholder(), nullptr);
    ASSERT_TRUE(f.designer().barPlaceholder()->isVisible());

    newui::Rect lastButton = CodeToolsVsix::SelectionOverlay::boundsInRootView(f.bar->childViews().back());
    newui::Rect placeholder = CodeToolsVsix::SelectionOverlay::boundsInRootView(f.designer().barPlaceholder());
    EXPECT_GE(placeholder.left(), lastButton.right());
    EXPECT_TRUE(f.designer().contains(newui::Point(placeholder.left() + 4, placeholder.top() + 4)));
}

TEST(MenuDesigner, SelectingATopLevelMenuShowsItsItemsBelowItsButtonAndInTheGrid)
{
    MenuDesignerFixture f;
    f.editor.viewDesignerController().selectExclusive(f.bar);

    f.designer().select(f.file);

    std::vector<CodeToolsVsix::MenuColumnView*> columns = f.designer().visibleColumns();
    ASSERT_EQ(columns.size(), 1u);
    EXPECT_EQ(columns[0]->menu(), f.file);
    newui::Rect fileButton = CodeToolsVsix::SelectionOverlay::boundsInRootView(f.bar->childViews()[0]);
    newui::Rect column = CodeToolsVsix::SelectionOverlay::boundsInRootView(columns[0]);
    EXPECT_FLOAT_EQ(column.left(), fileButton.left());
    EXPECT_FLOAT_EQ(column.top(), CodeToolsVsix::SelectionOverlay::boundsInRootView(f.bar).bottom());

    EXPECT_EQ(f.editor.workspace()->propertiesPane()->selected(), f.file);
    EXPECT_EQ(f.editor.viewDesignerController().primary(), f.bar) << "the canvas selection stays on the bar";
}

TEST(MenuDesigner, SelectingAnItemWithASubmenuOpensItBesideItsRow)
{
    MenuDesignerFixture f;
    f.editor.viewDesignerController().selectExclusive(f.bar);

    f.designer().select(f.recent);

    std::vector<CodeToolsVsix::MenuColumnView*> columns = f.designer().visibleColumns();
    ASSERT_EQ(columns.size(), 2u);
    EXPECT_EQ(columns[0]->menu(), f.file);
    EXPECT_EQ(columns[1]->menu(), f.recent);
    newui::Rect first = CodeToolsVsix::SelectionOverlay::boundsInRootView(columns[0]);
    newui::Rect second = CodeToolsVsix::SelectionOverlay::boundsInRootView(columns[1]);
    EXPECT_LT(second.left(), first.right());   // overlaps by a hair, like a native cascade
    EXPECT_FLOAT_EQ(second.top(), first.top() + columns[0]->rowTop(1) - 2.0f);
}

TEST(MenuDesigner, SelectingALeafOpensNoSubmenu)
{
    MenuDesignerFixture f;
    f.editor.viewDesignerController().selectExclusive(f.bar);

    f.designer().select(f.open);

    ASSERT_EQ(f.designer().visibleColumns().size(), 1u);
    EXPECT_EQ(f.designer().visibleColumns()[0]->menu(), f.file);
    EXPECT_EQ(f.editor.workspace()->propertiesPane()->selected(), f.open);
}

TEST(MenuDesigner, SelectingAnotherControlClosesIt)
{
    MenuDesignerFixture f;
    auto* other = new newui::SubView();
    other->setVisible(true);
    other->setBounds(newui::Rect(10, 200, 50, 50));
    f.editor.workspace()->rootViewProxy()->addChild(other);
    f.editor.viewDesignerController().selectExclusive(f.bar);
    f.designer().select(f.recent);

    f.editor.viewDesignerController().selectExclusive(other);

    EXPECT_FALSE(f.designer().isOpen());
    EXPECT_TRUE(f.designer().visibleColumns().empty());
    EXPECT_FALSE(f.designer().barPlaceholder()->isVisible());
    EXPECT_EQ(f.editor.workspace()->propertiesPane()->selected(), other);
}

TEST(MenuDesigner, TopLevelMenuAtMapsBarButtonsToTheirMenus)
{
    MenuDesignerFixture f;
    f.editor.viewDesignerController().selectExclusive(f.bar);

    newui::Rect editButton = CodeToolsVsix::SelectionOverlay::boundsInRootView(f.bar->childViews()[1]);
    EXPECT_EQ(f.designer().topLevelMenuAt(newui::Point(editButton.left() + 3, editButton.top() + 3)), f.edit);
    EXPECT_EQ(f.designer().topLevelMenuAt(newui::Point(5, 600)), nullptr);
}

TEST(MenuDesigner, AColumnRowMapsPointsToItemsAndThePlaceholder)
{
    MenuDesignerFixture f;
    f.editor.viewDesignerController().selectExclusive(f.bar);
    f.designer().select(f.file);
    CodeToolsVsix::MenuColumnView* column = f.designer().visibleColumns()[0];

    EXPECT_EQ(column->rowAt(newui::Point(10, column->rowTop(1) + 2)), 1u);
    EXPECT_EQ(column->rowAt(newui::Point(10, column->rowTop(4) + 2)), 4u) << "row 4 is the Type Here placeholder";
    EXPECT_LT(column->rowTop(3) - column->rowTop(2), CodeToolsVsix::MenuColumnView::kRowHeight) << "row 2 is a separator";
}

TEST(MenuDesignerEditing, TypingIntoTheBarPlaceholderCreatesAMenuAndMovesIntoIt)
{
    MenuDesignerFixture f;
    f.editor.viewDesignerController().selectExclusive(f.bar);
    const std::size_t before = f.bar->menus().size();

    f.designer().beginEdit(&f.bar->root(), before);
    ASSERT_TRUE(f.designer().isEditing());
    newui::TextField* field = f.designer().editField();
    EXPECT_EQ(f.root.focusedSubView(), field);
    EXPECT_EQ(field->bounds(), f.designer().barPlaceholder()->bounds()) << "the field sits over the placeholder";

    field->setText(L"View");
    f.designer().commitEdit(true);

    ASSERT_EQ(f.bar->menus().size(), before + 1);
    newui::MenuItem* view = f.bar->menus().back();
    EXPECT_EQ(view->text(), "View");
    EXPECT_EQ(f.bar->childViews().size(), before + 1) << "a button for the new menu";
    EXPECT_EQ(f.designer().selectedItem(), view);
    EXPECT_TRUE(f.designer().isEditing()) << "Enter moves on to the new menu's first placeholder";
    EXPECT_TRUE(f.editor.isDirty());

    field->setText(L"Zoom");
    f.designer().commitEdit(true);
    field->setText(L"-");
    f.designer().commitEdit(true);
    field->setText(L"");
    f.designer().commitEdit(true);   // empty: nothing more, editing ends

    ASSERT_EQ(view->children().size(), 2u);
    EXPECT_EQ(view->children()[0]->text(), "Zoom");
    EXPECT_TRUE(view->children()[1]->isSeparator());
    EXPECT_FALSE(f.designer().isEditing());
    EXPECT_NE(f.root.focusedSubView(), field);
}

TEST(MenuDesignerEditing, AddingAnItemIsUndoableAndRedoable)
{
    MenuDesignerFixture f;
    f.editor.viewDesignerController().selectExclusive(f.bar);

    newui::MenuItem* help = f.designer().insertItem(&f.bar->root(), f.bar->menus().size(), "Help");
    ASSERT_EQ(f.bar->menus().size(), 3u);

    ASSERT_TRUE(f.editor.undoStack().canUndo());
    f.editor.undoStack().undo();
    EXPECT_EQ(f.bar->menus().size(), 2u);
    EXPECT_EQ(f.bar->childViews().size(), 2u);

    f.editor.undoStack().redo();
    ASSERT_EQ(f.bar->menus().size(), 3u);
    EXPECT_EQ(f.bar->menus()[2], help);
}

TEST(MenuDesignerEditing, RenamingAnItemIsUndoable)
{
    MenuDesignerFixture f;
    f.editor.viewDesignerController().selectExclusive(f.bar);
    f.designer().select(f.file);

    f.designer().beginEdit(f.file, 0);   // "Open"
    ASSERT_TRUE(f.designer().isEditing());
    EXPECT_EQ(f.designer().editField()->text(), L"Open");
    EXPECT_EQ(f.designer().selectedItem(), f.open);
    f.designer().editField()->setText(L"Open...");
    f.designer().commitEdit(false);

    EXPECT_EQ(f.open->text(), "Open...");
    EXPECT_FALSE(f.designer().isEditing());
    f.editor.undoStack().undo();
    EXPECT_EQ(f.open->text(), "Open");
}

TEST(MenuDesignerEditing, CancelLeavesTheMenuUnchanged)
{
    MenuDesignerFixture f;
    f.editor.viewDesignerController().selectExclusive(f.bar);
    f.designer().select(f.file);
    const std::size_t count = f.file->children().size();

    f.designer().beginEdit(f.file, count);
    f.designer().editField()->setText(L"Print");
    f.designer().cancelEdit();

    EXPECT_EQ(f.file->children().size(), count);
    EXPECT_FALSE(f.designer().isEditing());
    EXPECT_FALSE(f.editor.undoStack().canUndo());
}

TEST(MenuDesignerEditing, RenamingATopLevelMenuRelabelsItsButton)
{
    MenuDesignerFixture f;
    f.editor.viewDesignerController().selectExclusive(f.bar);

    f.designer().renameItem(f.edit, "Edit2");

    EXPECT_EQ(f.bar->childViews()[1]->name(), "Edit2");
}

TEST(MenuDesignerStructure, DeleteRemovesTheItemSelectsTheNextAndUndoRestoresItInPlace)
{
    MenuDesignerFixture f;
    f.editor.viewDesignerController().selectExclusive(f.bar);
    f.designer().select(f.open);

    f.designer().deleteItem(f.open);

    ASSERT_EQ(f.file->children().size(), 3u);
    EXPECT_EQ(f.file->children()[0], f.recent);
    EXPECT_EQ(f.designer().selectedItem(), f.recent);
    EXPECT_TRUE(f.editor.isDirty());

    f.editor.undoStack().undo();
    ASSERT_EQ(f.file->children().size(), 4u);
    EXPECT_EQ(f.file->children()[0], f.open);
}

TEST(MenuDesignerStructure, DeletingATopLevelMenuRemovesItsButton)
{
    MenuDesignerFixture f;
    f.editor.viewDesignerController().selectExclusive(f.bar);
    f.designer().select(f.edit);

    f.designer().deleteItem(f.edit);

    EXPECT_EQ(f.bar->menus().size(), 1u);
    EXPECT_EQ(f.bar->childViews().size(), 1u);
    EXPECT_EQ(f.designer().selectedItem(), f.file);
}

TEST(MenuDesignerStructure, InsertBeforeAddsANewItemAboveAndStartsRenamingIt)
{
    MenuDesignerFixture f;
    f.editor.viewDesignerController().selectExclusive(f.bar);
    f.designer().select(f.recent);

    newui::MenuItem* created = f.designer().insertBefore(f.recent);

    ASSERT_NE(created, nullptr);
    EXPECT_EQ(f.file->children()[1], created);
    EXPECT_EQ(f.file->children()[2], f.recent);
    EXPECT_EQ(created->text(), "New Item");
    EXPECT_EQ(f.designer().selectedItem(), created);
    ASSERT_TRUE(f.designer().isEditing());
    EXPECT_EQ(f.designer().editField()->text(), L"New Item");

    f.designer().editField()->setText(L"Save");
    f.designer().commitEdit(false);
    EXPECT_EQ(created->text(), "Save");
}

TEST(MenuDesignerStructure, InsertSeparatorAddsOneAboveAndKeepsTheSelection)
{
    MenuDesignerFixture f;
    f.editor.viewDesignerController().selectExclusive(f.bar);
    f.designer().select(f.recent);

    newui::MenuItem* created = f.designer().insertBefore(f.recent, true);

    ASSERT_NE(created, nullptr);
    EXPECT_TRUE(created->isSeparator());
    EXPECT_EQ(f.file->children()[1], created);
    EXPECT_EQ(f.designer().selectedItem(), f.recent);
    EXPECT_FALSE(f.designer().isEditing());
}

TEST(MenuDesignerStructure, CreateSubmenuOnALeafAddsAFirstItemAndRenamesIt)
{
    MenuDesignerFixture f;
    f.editor.viewDesignerController().selectExclusive(f.bar);
    f.designer().select(f.open);

    f.designer().createSubmenu(f.open);

    ASSERT_EQ(f.open->children().size(), 1u);
    EXPECT_EQ(f.designer().selectedItem(), f.open->children()[0]);
    EXPECT_TRUE(f.designer().isEditing());
    ASSERT_EQ(f.designer().visibleColumns().size(), 2u);
    EXPECT_EQ(f.designer().visibleColumns()[1]->menu(), f.open);
}

TEST(MenuDesignerStructure, CreateSubmenuOnAnItemWithOneSelectsItsFirstItem)
{
    MenuDesignerFixture f;
    f.editor.viewDesignerController().selectExclusive(f.bar);
    f.designer().select(f.recent);

    f.designer().createSubmenu(f.recent);

    EXPECT_EQ(f.recent->children().size(), 1u);
    EXPECT_EQ(f.designer().selectedItem(), f.recent->children()[0]);
    EXPECT_FALSE(f.designer().isEditing());
    EXPECT_FALSE(f.editor.undoStack().canUndo());
}

TEST(MenuDesignerStructure, ArrowNavigationSkipsSeparatorsWrapsAndCrossesMenus)
{
    MenuDesignerFixture f;
    using D = CodeToolsVsix::MenuDesigner::Direction;
    newui::MenuItem* exitItem = f.file->children()[3];
    newui::MenuItem* aTxt = f.recent->children()[0];
    f.editor.viewDesignerController().selectExclusive(f.bar);

    f.designer().select(f.file);
    f.designer().moveSelection(D::Down);
    EXPECT_EQ(f.designer().selectedItem(), f.open) << "Down from a bar menu opens it";
    f.designer().moveSelection(D::Down);
    EXPECT_EQ(f.designer().selectedItem(), f.recent);
    f.designer().moveSelection(D::Down);
    EXPECT_EQ(f.designer().selectedItem(), exitItem) << "skips the separator";
    f.designer().moveSelection(D::Down);
    EXPECT_EQ(f.designer().selectedItem(), f.open) << "wraps";
    f.designer().moveSelection(D::Up);
    EXPECT_EQ(f.designer().selectedItem(), exitItem);

    f.designer().select(f.recent);
    f.designer().moveSelection(D::Right);
    EXPECT_EQ(f.designer().selectedItem(), aTxt) << "Right enters a submenu";
    f.designer().moveSelection(D::Left);
    EXPECT_EQ(f.designer().selectedItem(), f.recent) << "Left leaves it";

    f.designer().select(f.open);
    f.designer().moveSelection(D::Right);
    EXPECT_EQ(f.designer().selectedItem(), f.edit) << "Right on a leaf moves to the next bar menu";
    f.designer().moveSelection(D::Left);
    EXPECT_EQ(f.designer().selectedItem(), f.file);
}

TEST(MenuDesignerDrag, MoveItemReordersWithinAMenuAndUndoRestores)
{
    MenuDesignerFixture f;
    newui::MenuItem* exitItem = f.file->children()[3];
    f.editor.viewDesignerController().selectExclusive(f.bar);

    f.designer().moveItem(f.open, f.file, 4);   // after Exit (index counted before the move)
    EXPECT_EQ(f.file->children().back(), f.open);
    EXPECT_EQ(f.file->children()[2], exitItem);

    f.designer().moveItem(f.open, f.file, 0);   // back to the top
    EXPECT_EQ(f.file->children()[0], f.open);

    f.editor.undoStack().undo();
    EXPECT_EQ(f.file->children().back(), f.open);
    f.editor.undoStack().undo();
    EXPECT_EQ(f.file->children()[0], f.open);
}

TEST(MenuDesignerDrag, MoveItemCarriesAnItemIntoAnotherMenuOrOntoTheBar)
{
    MenuDesignerFixture f;
    f.editor.viewDesignerController().selectExclusive(f.bar);

    f.designer().moveItem(f.open, f.edit, 0);
    EXPECT_EQ(f.open->parent(), f.edit);
    EXPECT_EQ(f.file->children().size(), 3u);

    f.designer().moveItem(f.recent, &f.bar->root(), 1);   // between File and Edit
    ASSERT_EQ(f.bar->menus().size(), 3u);
    EXPECT_EQ(f.bar->menus()[1], f.recent);
    EXPECT_EQ(f.bar->childViews().size(), 3u) << "a button for the new top-level menu";

    f.editor.undoStack().undo();
    EXPECT_EQ(f.recent->parent(), f.file);
    EXPECT_EQ(f.bar->childViews().size(), 2u);
}

TEST(MenuDesignerDrag, MoveItemRefusesADropIntoItsOwnSubmenu)
{
    MenuDesignerFixture f;
    f.editor.viewDesignerController().selectExclusive(f.bar);

    f.designer().moveItem(f.file, f.recent, 0);

    EXPECT_EQ(f.file->parent(), &f.bar->root());
    EXPECT_FALSE(f.editor.undoStack().canUndo());
}

TEST(MenuDesignerDrag, DropTargetsFollowRowHalvesAndBarButtonHalves)
{
    MenuDesignerFixture f;
    f.editor.viewDesignerController().selectExclusive(f.bar);
    f.designer().select(f.open);
    CodeToolsVsix::MenuColumnView* column = f.designer().visibleColumns()[0];
    const newui::Rect c = CodeToolsVsix::SelectionOverlay::boundsInRootView(column);
    f.designer().armDrag(f.open, newui::Point(c.left() + 10, c.top() + 10));

    auto upperOfRecent = f.designer().dropTargetAt(newui::Point(c.left() + 20, c.top() + column->rowTop(1) + 3));
    ASSERT_TRUE(upperOfRecent.has_value());
    EXPECT_EQ(upperOfRecent->parent, f.file);
    EXPECT_EQ(upperOfRecent->index, 1u);
    auto lowerOfRecent = f.designer().dropTargetAt(newui::Point(c.left() + 20, c.top() + column->rowTop(2) - 3));
    ASSERT_TRUE(lowerOfRecent.has_value());
    EXPECT_EQ(lowerOfRecent->index, 2u);

    const newui::Rect editButton = CodeToolsVsix::SelectionOverlay::boundsInRootView(f.bar->childViews()[1]);
    auto leftOfEdit = f.designer().dropTargetAt(newui::Point(editButton.left() + 2, editButton.top() + 5));
    ASSERT_TRUE(leftOfEdit.has_value());
    EXPECT_EQ(leftOfEdit->parent, &f.bar->root());
    EXPECT_EQ(leftOfEdit->index, 1u);

    EXPECT_FALSE(f.designer().dropTargetAt(newui::Point(5, 650)).has_value()) << "nowhere near the menus";
    f.designer().cancelDrag();
}

TEST(MenuDesignerDrag, ADragMovesTheItemShowsAMarkAndAShortPressIsJustAClick)
{
    MenuDesignerFixture f;
    f.editor.viewDesignerController().selectExclusive(f.bar);
    f.designer().select(f.open);
    CodeToolsVsix::MenuColumnView* column = f.designer().visibleColumns()[0];
    const newui::Rect c = CodeToolsVsix::SelectionOverlay::boundsInRootView(column);
    const newui::Point press(c.left() + 20, c.top() + column->rowTop(0) + 5);

    f.designer().armDrag(f.open, press);
    f.designer().dragTo(newui::Point(press.x + 1, press.y + 1));
    EXPECT_FALSE(f.designer().isDragActive()) << "under the threshold";
    f.designer().endDrag(newui::Point(press.x + 1, press.y + 1));
    EXPECT_EQ(f.file->children()[0], f.open);
    EXPECT_FALSE(f.editor.undoStack().canUndo());

    const newui::Point belowExit(c.left() + 20, c.top() + column->rowTop(4) - 2);
    f.designer().armDrag(f.open, press);
    f.designer().dragTo(belowExit);
    EXPECT_TRUE(f.designer().isDragActive());
    ASSERT_NE(f.designer().dropMark(), nullptr);
    EXPECT_TRUE(f.designer().dropMark()->isVisible());
    f.designer().endDrag(belowExit);

    EXPECT_EQ(f.file->children().back(), f.open);
    EXPECT_FALSE(f.designer().dropMark()->isVisible());
    EXPECT_EQ(f.designer().selectedItem(), f.open);
}

TEST(MenuDesignerPolish, TheOpenMenusBarButtonIsHighlighted)
{
    MenuDesignerFixture f;
    f.editor.viewDesignerController().selectExclusive(f.bar);
    f.designer().select(f.recent);

    ASSERT_NE(f.designer().barHighlight(), nullptr);
    EXPECT_TRUE(f.designer().barHighlight()->isVisible());
    EXPECT_EQ(CodeToolsVsix::SelectionOverlay::boundsInRootView(f.designer().barHighlight()),
        CodeToolsVsix::SelectionOverlay::boundsInRootView(f.bar->childViews()[0]));

    f.designer().select(nullptr);
    EXPECT_FALSE(f.designer().barHighlight()->isVisible());
}

// testharness destroys the editor on close, then the frame destroys its RootView (canvas well and
// all). That teardown used to lay the tree out again and reach a canvas-well listener pointing at
// the dead editor.
TEST(MenuDesigner, TheEditorCanBeDestroyedWhileItsRootViewLivesOn)
{
    CodeToolsVsix::PropertyEditorRegistry::instance().registerBuiltinEditors();
    newui::RootView root(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    auto* editor = new CodeToolsVsix::DesignerEditor(&root);
    root.setBounds(newui::Rect(0, 0, 1400, 700));

    auto* bar = new newui::MenuBar();
    bar->setBounds(newui::Rect(10, 10, 400, 28));
    auto* file = new newui::MenuItem("File");
    file->addChild(std::make_unique<newui::MenuItem>("Open"));
    bar->addMenu(file);
    editor->workspace()->rootViewProxy()->addChild(bar);
    editor->viewDesignerController().selectExclusive(bar);
    editor->menuDesigner()->select(file);
    editor->menuDesigner()->beginEdit(file, 1);   // an edit left open
    editor->menuDesigner()->editField()->setText(L"Unsaved");

    delete editor;
    EXPECT_EQ(file->children().size(), 1u) << "an open edit is dropped, not committed, on teardown";
    root.destroy();   // what the frame does next - its teardown used to lay out the canvas well
}

TEST(MenuDesignerEditing, EnterAfterRenamingANewSubmenuItemMovesToTheNextRow)
{
    MenuDesignerFixture f;
    f.editor.viewDesignerController().selectExclusive(f.bar);
    f.designer().select(f.open);

    f.designer().createSubmenu(f.open);   // "New Item", being renamed
    f.designer().editField()->setText(L"Recent A");
    f.designer().commitEdit(true);

    ASSERT_TRUE(f.designer().isEditing()) << "Enter moves on to the submenu's Type Here row";
    EXPECT_EQ(f.designer().editField()->text(), L"");
    f.designer().editField()->setText(L"Recent B");
    f.designer().commitEdit(true);

    ASSERT_EQ(f.open->children().size(), 2u);
    EXPECT_EQ(f.open->children()[0]->text(), "Recent A");
    EXPECT_EQ(f.open->children()[1]->text(), "Recent B");
}

TEST(MenuDesignerEditing, EnterAfterRenamingAMiddleItemMovesToRenameTheNextOne)
{
    MenuDesignerFixture f;
    f.editor.viewDesignerController().selectExclusive(f.bar);
    f.designer().select(f.open);

    f.designer().beginEdit(f.file, 0);   // rename Open
    f.designer().commitEdit(true);

    ASSERT_TRUE(f.designer().isEditing());
    EXPECT_EQ(f.designer().selectedItem(), f.recent);
    EXPECT_EQ(f.designer().editField()->text(), L"Recent");
}

TEST(MenuDesignerEditing, CreateSubmenuOpensTheNewItemWithAllItsTextSelected)
{
    MenuDesignerFixture f;
    f.editor.viewDesignerController().selectExclusive(f.bar);
    f.designer().select(f.open);

    f.designer().createSubmenu(f.open);

    newui::TextField* field = f.designer().editField();
    ASSERT_TRUE(f.designer().isEditing());
    ASSERT_EQ(field->selection().ranges().size(), 1u);
    EXPECT_EQ(field->selection().ranges()[0].start(), 0u);
    EXPECT_EQ(field->selection().ranges()[0].length(), field->text().size());
}

TEST(MenuDesignerNames, NewItemsGetUniqueNamesFromTheirCaptions)
{
    MenuDesignerFixture f;
    f.editor.viewDesignerController().selectExclusive(f.bar);

    newui::MenuItem* saveAs = f.designer().insertItem(f.file, 0, "Save &As...");
    newui::MenuItem* open2 = f.designer().insertItem(f.file, 0, "Open");
    newui::MenuItem* separator = f.designer().insertItem(f.file, 0, "-");
    newui::MenuItem* digits = f.designer().insertItem(f.file, 0, "123");

    EXPECT_EQ(saveAs->name(), "saveAs1");
    EXPECT_EQ(open2->name(), "open2") << "the fixture's own Open was named open1 when the bar opened";
    EXPECT_EQ(separator->name(), "separator2") << "the fixture's File menu already has separator1";
    EXPECT_EQ(digits->name(), "menuItem1") << "not a valid identifier start";
}

TEST(MenuDesignerNames, OpeningABarNamesItsUnnamedItemsAndKeepsLoadedNames)
{
    MenuDesignerFixture f;
    f.edit->setName("editMenu");   // as if loaded from a file

    f.editor.viewDesignerController().selectExclusive(f.bar);

    EXPECT_EQ(f.file->name(), "file1");
    EXPECT_EQ(f.open->name(), "open1");
    EXPECT_EQ(f.edit->name(), "editMenu");
    EXPECT_TRUE(f.root.nameManager().isTaken("editMenu"));
}

TEST(MenuDesignerNames, APlaceholderItemIsRenamedFromItsFirstCaptionAndUndoRestoresIt)
{
    MenuDesignerFixture f;
    f.editor.viewDesignerController().selectExclusive(f.bar);
    f.designer().select(f.open);

    f.designer().createSubmenu(f.open);   // "New Item"
    newui::MenuItem* created = f.open->children()[0];
    EXPECT_EQ(created->name(), "newItem1");
    f.designer().editField()->setText(L"Recent Files");
    f.designer().commitEdit(false);

    EXPECT_EQ(created->name(), "recentFiles1");
    f.editor.undoStack().undo();
    EXPECT_EQ(created->name(), "newItem1");
    EXPECT_EQ(created->text(), "New Item");
}

TEST(MenuDesignerNames, RenamingARealItemKeepsItsName)
{
    MenuDesignerFixture f;
    f.editor.viewDesignerController().selectExclusive(f.bar);

    f.designer().renameItem(f.open, "Open File");

    EXPECT_EQ(f.open->name(), "open1");
}

namespace
{
    void collectMenuItemNames(const newui::MenuItem& parent, std::vector<std::string>& out)
    {
        for (const newui::MenuItem* item : parent.children()) {
            out.push_back(item->name());
            collectMenuItemNames(*item, out);
        }
    }
}

TEST(MenuDesignerNames, DuplicatingAMenuBarGivesTheCopysItemsUniqueNames)
{
    MenuDesignerFixture f;
    f.editor.viewDesignerController().selectExclusive(f.bar);   // names the items: file1, open1, ...
    std::vector<std::string> originalNames;
    collectMenuItemNames(f.bar->root(), originalNames);

    ASSERT_TRUE(f.editor.duplicateSelection());

    const auto& surfaceChildren = f.editor.workspace()->rootViewProxy()->childViews();
    auto* copy = dynamic_cast<newui::MenuBar*>(surfaceChildren.back());
    ASSERT_NE(copy, nullptr);
    ASSERT_NE(copy, f.bar);
    std::vector<std::string> copyNames;
    collectMenuItemNames(copy->root(), copyNames);
    ASSERT_EQ(copyNames.size(), originalNames.size());
    for (const std::string& name : copyNames) {
        EXPECT_FALSE(name.empty());
        EXPECT_EQ(std::find(originalNames.begin(), originalNames.end(), name), originalNames.end()) << name;
    }
    EXPECT_EQ(copy->menus()[0]->name(), "file2");
}

TEST(MenuDesignerNames, PastingABarWhoseOriginalWasNeverOpenedStillAvoidsItsNames)
{
    MenuDesignerFixture f;
    f.file->setName("fileMenu");   // as loaded from a file, never opened in the menu designer

    newui::MenuBar copy;
    copy.addMenu(new newui::MenuItem("File"));
    copy.menus()[0]->setName("fileMenu");
    CodeToolsVsix::DesignerClipboard::uniquifyNames(copy, f.root);

    EXPECT_NE(copy.menus()[0]->name(), "fileMenu");
    EXPECT_EQ(copy.menus()[0]->name(), "file1");
}

TEST(DesignerPasteText, EditedBoundsInPastedTextAreRespectedUnderAnAnchorLayout)
{
    newui::RootView root(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    CodeToolsVsix::DesignerEditor editor(&root);
    root.setBounds(newui::Rect(0, 0, 1400, 700));
    newui::RootViewProxy* surface = editor.workspace()->rootViewProxy();

    auto* original = new newui::Button();
    original->setText("OK");
    original->setBounds(newui::Rect(10, 20, 80, 24));
    surface->addChild(original);
    std::string text = CodeToolsVsix::DesignerClipboard::serialize(*original);

    // What a user does in Notepad: move bounds.pos, leave layoutParams' margins alone.
    auto replaceFirst = [&text](const std::string& from, const std::string& to) {
        const std::size_t at = text.find(from, text.find("bounds"));
        ASSERT_NE(at, std::string::npos) << from;
        text.replace(at, from.size(), to);
    };
    replaceFirst("x: 10", "x: 200");
    replaceFirst("y: 20", "y: 150");

    ASSERT_TRUE(editor.pasteSerializedViews({ text }, 0.0f));
    newui::SubView* pasted = surface->childViews().back();
    ASSERT_NE(pasted, original);
    EXPECT_FLOAT_EQ(pasted->bounds().left(), 200.0f);
    EXPECT_FLOAT_EQ(pasted->bounds().top(), 150.0f);
}
