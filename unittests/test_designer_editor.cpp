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
