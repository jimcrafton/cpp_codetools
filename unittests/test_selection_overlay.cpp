#include "../extension/NativeEditControls/SelectionOverlay.h"

#include <newui/rootview.h>
#include <newui/subview.h>

#include <gtest/gtest.h>

using CodeToolsVsix::SelectionOverlay;

TEST(SelectionOverlay, StartsWithNoSelection)
{
    SelectionOverlay overlay;
    EXPECT_TRUE(overlay.selected().empty());
    EXPECT_EQ(overlay.primary(), nullptr);
}

TEST(SelectionOverlay, SelectExclusiveReplacesWholeSelection)
{
    SelectionOverlay overlay;
    newui::SubView a;
    newui::SubView b;

    overlay.selectExclusive(&a);
    EXPECT_EQ(overlay.selected().size(), 1u);
    EXPECT_TRUE(overlay.isSelected(&a));
    EXPECT_EQ(overlay.primary(), &a);

    overlay.selectExclusive(&b);
    EXPECT_EQ(overlay.selected().size(), 1u);
    EXPECT_FALSE(overlay.isSelected(&a));
    EXPECT_EQ(overlay.primary(), &b);
}

TEST(SelectionOverlay, SelectExclusiveWithNullClearsSelection)
{
    SelectionOverlay overlay;
    newui::SubView a;
    overlay.selectExclusive(&a);

    overlay.selectExclusive(nullptr);
    EXPECT_TRUE(overlay.selected().empty());
    EXPECT_EQ(overlay.primary(), nullptr);
}

TEST(SelectionOverlay, ToggleSelectionAddsThenRemoves)
{
    SelectionOverlay overlay;
    newui::SubView a;
    newui::SubView b;
    overlay.selectExclusive(&a);

    overlay.toggleSelection(&b);
    EXPECT_EQ(overlay.selected().size(), 2u);
    EXPECT_TRUE(overlay.isSelected(&a));
    EXPECT_TRUE(overlay.isSelected(&b));
    EXPECT_EQ(overlay.primary(), &b);  // most recently added is primary

    overlay.toggleSelection(&a);
    EXPECT_EQ(overlay.selected().size(), 1u);
    EXPECT_FALSE(overlay.isSelected(&a));
    EXPECT_EQ(overlay.primary(), &b);
}

TEST(SelectionOverlay, ToggleSelectionWithNullIsANoOp)
{
    SelectionOverlay overlay;
    newui::SubView a;
    overlay.selectExclusive(&a);

    overlay.toggleSelection(nullptr);
    EXPECT_EQ(overlay.selected().size(), 1u);
    EXPECT_EQ(overlay.primary(), &a);
}

TEST(SelectionOverlay, ClearSelectionEmptiesTheSet)
{
    SelectionOverlay overlay;
    newui::SubView a;
    overlay.selectExclusive(&a);

    overlay.clearSelection();
    EXPECT_TRUE(overlay.selected().empty());
}

TEST(SelectionOverlay, BoundsInRootViewForADirectChildOfRootIsItsOwnBounds)
{
    newui::RootView root(nullptr, newui::Rect(0, 0, 500, 500), "root");
    auto* child = new newui::SubView();
    child->setBounds(newui::Rect(50, 60, 200, 150));
    root.addChild(child);

    newui::Rect result = SelectionOverlay::boundsInRootView(child);
    EXPECT_EQ(result, newui::Rect(50, 60, 200, 150));
}

TEST(SelectionOverlay, BoundsInRootViewAccumulatesThroughNestedContainersAndScrollOrigin)
{
    newui::RootView root(nullptr, newui::Rect(0, 0, 500, 500), "root");

    auto* container = new newui::SubView();
    container->setBounds(newui::Rect(50, 60, 300, 300));
    root.addChild(container);
    // A scroll offset on container shifts where its own children draw/hit-
    // test (View::paintChildren()/hitTestChildren(), view.cpp) - the same
    // shift boundsInRootView() has to undo to land on the right screen
    // position.
    container->setOrigin(newui::Point(10.0f, 20.0f));

    auto* child = new newui::SubView();
    child->setBounds(newui::Rect(5, 7, 30, 40));
    container->addChild(child);

    newui::Rect result = SelectionOverlay::boundsInRootView(child);
    EXPECT_EQ(result, newui::Rect(50.0f + 5.0f - 10.0f, 60.0f + 7.0f - 20.0f, 30, 40));
}

TEST(SelectionOverlay, BoundsInRootViewForNullViewIsAnEmptyRect)
{
    EXPECT_EQ(SelectionOverlay::boundsInRootView(nullptr), newui::Rect());
}
