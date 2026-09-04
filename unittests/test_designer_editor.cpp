#include "../extension/NativeEditControls/DesignerEditor.h"

#include <newui/bundle.h>
#include <newui/frame.h>
#include <newui/rootview.h>
#include <newui/subview.h>

#include <gtest/gtest.h>

#include <fstream>
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

    ASSERT_EQ(view.childViews().size(), 1u);
    EXPECT_EQ(view.childViews()[0]->name(), "probeChild");
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

    // A DesignerEditor whose own RootView has different content entirely.
    newui::RootView view(nullptr, newui::Rect(0, 0, 10, 10), "designerRoot");
    CodeToolsVsix::DesignerEditor editor(&view);
    newui::SubView* newChild = new newui::SubView();
    newChild->setName("editedChild");
    view.addChild(newChild);

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
