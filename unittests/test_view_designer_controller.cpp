#include "../extension/NativeEditControls/ViewDesignerController.h"

#include <newui/subview.h>

#include <gtest/gtest.h>

using CodeToolsVsix::ViewDesignerController;

namespace {
    int g_changedCount = 0;

    newui::SyncReturn CountSelectionChanged(ViewDesignerController& /*sender*/) {
        ++g_changedCount;
        return newui::SyncReturn::Handled;
    }
}

TEST(ViewDesignerController, StartsWithNoSelection)
{
    ViewDesignerController controller;
    EXPECT_TRUE(controller.selected().empty());
    EXPECT_EQ(controller.primary(), nullptr);
}

TEST(ViewDesignerController, SelectExclusiveReplacesWholeSelection)
{
    ViewDesignerController controller;
    newui::SubView a;
    newui::SubView b;

    controller.selectExclusive(&a);
    EXPECT_EQ(controller.selected().size(), 1u);
    EXPECT_TRUE(controller.isSelected(&a));
    EXPECT_EQ(controller.primary(), &a);

    controller.selectExclusive(&b);
    EXPECT_EQ(controller.selected().size(), 1u);
    EXPECT_FALSE(controller.isSelected(&a));
    EXPECT_EQ(controller.primary(), &b);
}

TEST(ViewDesignerController, SelectExclusiveWithNullClearsSelection)
{
    ViewDesignerController controller;
    newui::SubView a;
    controller.selectExclusive(&a);

    controller.selectExclusive(nullptr);
    EXPECT_TRUE(controller.selected().empty());
    EXPECT_EQ(controller.primary(), nullptr);
}

TEST(ViewDesignerController, ToggleSelectionAddsThenRemoves)
{
    ViewDesignerController controller;
    newui::SubView a;
    newui::SubView b;
    controller.selectExclusive(&a);

    controller.toggleSelection(&b);
    EXPECT_EQ(controller.selected().size(), 2u);
    EXPECT_TRUE(controller.isSelected(&a));
    EXPECT_TRUE(controller.isSelected(&b));
    EXPECT_EQ(controller.primary(), &b);  // most recently added is primary

    controller.toggleSelection(&a);
    EXPECT_EQ(controller.selected().size(), 1u);
    EXPECT_FALSE(controller.isSelected(&a));
    EXPECT_EQ(controller.primary(), &b);
}

TEST(ViewDesignerController, ToggleSelectionWithNullIsANoOp)
{
    ViewDesignerController controller;
    newui::SubView a;
    controller.selectExclusive(&a);

    controller.toggleSelection(nullptr);
    EXPECT_EQ(controller.selected().size(), 1u);
    EXPECT_EQ(controller.primary(), &a);
}

TEST(ViewDesignerController, ClearSelectionEmptiesTheSet)
{
    ViewDesignerController controller;
    newui::SubView a;
    controller.selectExclusive(&a);

    controller.clearSelection();
    EXPECT_TRUE(controller.selected().empty());
}

// Regression coverage for the whole reason this class exists: any UI piece
// that cares about selection (PropertiesGrid today, Document Outline
// later) subscribes to onSelectionChanged independently - the controller
// itself never needs to know who's listening.
TEST(ViewDesignerController, OnSelectionChangedFiresForEveryRealMutation)
{
    g_changedCount = 0;
    ViewDesignerController controller;
    controller.onSelectionChanged += CountSelectionChanged;
    newui::SubView a;

    controller.selectExclusive(&a);
    EXPECT_EQ(g_changedCount, 1);

    controller.toggleSelection(&a);  // removes it
    EXPECT_EQ(g_changedCount, 2);

    controller.clearSelection();
    EXPECT_EQ(g_changedCount, 3);
}

TEST(ViewDesignerController, OnSelectionChangedDoesNotFireForANoOpToggle)
{
    g_changedCount = 0;
    ViewDesignerController controller;
    controller.onSelectionChanged += CountSelectionChanged;

    controller.toggleSelection(nullptr);  // documented no-op
    EXPECT_EQ(g_changedCount, 0);
}
