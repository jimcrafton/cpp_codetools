#include "../extension/NativeEditControls/PropertiesPanel.h"

#include <newui/controls.h>
#include <newui/layout.h>

#include <gtest/gtest.h>

// registerReflectionData() is run once for this whole binary by test_component_editor.cpp's own
// ::testing::Environment.

namespace
{
    class PropertiesPanelTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            CodeToolsVsix::PropertyEditorRegistry::instance().registerBuiltinEditors();
            button_.setLayoutParams(std::make_unique<newui::AnchorLayoutParams>());
            panel_.grid()->setSelection(&button_);
        }

        // Rows the TreeView actually shows right now (an expanded group's children count).
        std::size_t visibleRows() { return panel_.grid()->treeView()->controller().visibleCount(); }

        CodeToolsVsix::PropertiesPanel panel_;
        newui::Button button_;
    };
}

TEST_F(PropertiesPanelTest, TypingInTheFilterBoxNarrowsTheGridAndOpensTheGroupsThatMatched)
{
    const std::size_t allRows = visibleRows();
    ASSERT_GT(allRows, 10u);

    panel_.filterField()->setText(L"margin");

    EXPECT_EQ(panel_.grid()->model().filter(), "margin");
    // layoutParams (1) + its four margins, opened for you.
    EXPECT_EQ(visibleRows(), 5u);
    EXPECT_TRUE(panel_.grid()->treeView()->controller().isExpanded({0}));
}

TEST_F(PropertiesPanelTest, ClearingTheFilterRestoresEveryRowWithTheGroupsClosedAgain)
{
    const std::size_t allRows = visibleRows();
    panel_.filterField()->setText(L"margin");
    ASSERT_LT(visibleRows(), allRows);

    panel_.filterField()->setText(L"");

    EXPECT_EQ(panel_.grid()->model().filter(), "");
    EXPECT_EQ(visibleRows(), allRows);
    EXPECT_FALSE(panel_.grid()->treeView()->controller().isExpanded({0}));
}

TEST_F(PropertiesPanelTest, TheAZToggleSwitchesTheGridToAlphabeticalAndBack)
{
    EXPECT_FALSE(panel_.grid()->model().alphabetical());

    panel_.sortButton()->setChecked(true);
    EXPECT_TRUE(panel_.grid()->model().alphabetical());
    EXPECT_EQ(std::any_cast<std::string>(panel_.grid()->model().value(std::vector<std::size_t>{0})), "name");
    EXPECT_EQ(std::any_cast<std::string>(panel_.grid()->model().value(std::vector<std::size_t>{1})), "bounds");

    panel_.sortButton()->setChecked(false);
    EXPECT_FALSE(panel_.grid()->model().alphabetical());
}

TEST_F(PropertiesPanelTest, TheFilterAndOrderSurviveSelectingAnotherControl)
{
    panel_.filterField()->setText(L"margin");
    panel_.sortButton()->setChecked(true);

    newui::Button other;
    other.setLayoutParams(std::make_unique<newui::AnchorLayoutParams>());
    panel_.grid()->setSelection(&other);

    EXPECT_EQ(panel_.grid()->model().filter(), "margin");
    EXPECT_TRUE(panel_.grid()->model().alphabetical());
    EXPECT_EQ(panel_.grid()->model().childCount({}), 1u);   // still just layoutParams for the new control
}

TEST_F(PropertiesPanelTest, ThePanelStacksAHeaderAboveTheGrid)
{
    ASSERT_EQ(panel_.childViews().size(), 2u);
    EXPECT_EQ(panel_.childViews()[1], panel_.grid());
    newui::SubView* header = panel_.childViews()[0];
    EXPECT_EQ(header->childViews().size(), 2u);   // the filter box and the A-Z toggle
    EXPECT_EQ(header->childViews()[0], panel_.filterField());
    EXPECT_EQ(header->childViews()[1], panel_.sortButton());
    EXPECT_TRUE(panel_.sortButton()->isToggleButton());
}
