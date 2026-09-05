#include "../extension/NativeEditControls/DesignerEditor.h"

#include <newui/bundle.h>
#include <newui/frame.h>
#include <newui/layout.h>
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
    // DesignerEditor::load()/save() only work against a real
    // "<root>\Resources\<bundleName>.newui" path (see
    // resolveBundleNameAndRoot(), DesignerEditor.cpp) - builds and tears
    // down exactly that shape under a temp directory, same on-disk-fixture
    // discipline as newui's own NewuiFileFixture (unittests/test_bundle.cpp).
    class DesignerEditorFileFixture : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            char tempPathBuf[MAX_PATH]{};
            ::GetTempPathA(MAX_PATH, tempPathBuf);
            root_ = std::string(tempPathBuf) + "DesignerEditorTest";
            resources_ = root_ + "\\Resources";
            ::CreateDirectoryA(root_.c_str(), nullptr);
            ::CreateDirectoryA(resources_.c_str(), nullptr);
        }

        void TearDown() override
        {
            newui::Bundle::instance().setExecutableDirOverride("");  // restore, same shared-singleton discipline as Bundle's own tests
            ::DeleteFileA((resources_ + "\\DesignerEditorProbe.newui").c_str());
            ::RemoveDirectoryA(resources_.c_str());
            ::RemoveDirectoryA(root_.c_str());
        }

        std::wstring filePath() const
        {
            std::string narrow = resources_ + "\\DesignerEditorProbe.newui";
            return std::wstring(narrow.begin(), narrow.end());
        }

        std::string root_;
        std::string resources_;
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

TEST_F(DesignerEditorFileFixture, LoadFailsForAPathNotUnderAResourcesFolder)
{
    newui::RootView view(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    CodeToolsVsix::DesignerEditor editor(&view);

    std::wstring badPath = L"C:\\SomewhereElse\\DesignerEditorProbe.newui";
    EXPECT_FALSE(editor.load(badPath.c_str(), badPath.size()));
}

TEST_F(DesignerEditorFileFixture, LoadPopulatesTheRootViewFromARealFrameShapedFile)
{
    newui::Frame sourceFrame;
    sourceFrame.setName("DesignerEditorProbe");
    sourceFrame.setTitle("Probe Title");
    newui::SubView* child = new newui::SubView();
    child->setName("probeChild");
    sourceFrame.rootView().addChild(child);

    newui::Bundle::instance().setExecutableDirOverride(root_);
    ASSERT_TRUE(newui::Bundle::instance().writeFrame(sourceFrame));
    newui::Bundle::instance().setExecutableDirOverride("");

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

TEST_F(DesignerEditorFileFixture, SaveFailsForAPathNotUnderAResourcesFolder)
{
    newui::RootView view(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    CodeToolsVsix::DesignerEditor editor(&view);

    std::wstring badPath = L"C:\\SomewhereElse\\DesignerEditorProbe.newui";
    EXPECT_FALSE(editor.save(badPath.c_str(), badPath.size()));
}

TEST_F(DesignerEditorFileFixture, SavePreservesTitleAndBoundsWhileReplacingRootView)
{
    // Seed a real Frame-shaped file, same as the load test above.
    newui::Frame sourceFrame;
    sourceFrame.setName("DesignerEditorProbe");
    sourceFrame.setTitle("Original Title");
    sourceFrame.setBounds(newui::Rect(1, 2, 300, 200));
    newui::SubView* oldChild = new newui::SubView();
    oldChild->setName("oldChild");
    sourceFrame.rootView().addChild(oldChild);

    newui::Bundle::instance().setExecutableDirOverride(root_);
    ASSERT_TRUE(newui::Bundle::instance().writeFrame(sourceFrame));
    newui::Bundle::instance().setExecutableDirOverride("");

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
    reloaded.setName("DesignerEditorProbe");
    newui::Bundle::instance().setExecutableDirOverride(root_);
    ASSERT_TRUE(newui::Bundle::instance().loadFrame(reloaded));
    newui::Bundle::instance().setExecutableDirOverride("");

    EXPECT_EQ(reloaded.getTitle(), "Original Title");
    EXPECT_EQ(reloaded.getBounds(), newui::Rect(1, 2, 300, 200));
    ASSERT_EQ(reloaded.rootView().childViews().size(), 1u);
    EXPECT_EQ(reloaded.rootView().childViews()[0]->name(), "editedChild");
}
