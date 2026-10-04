// DelegateChips: layout and hit-testing of the chips on a Delegates row. Paint is not tested here
// (it needs a real context); the geometry is what clicks depend on.

#include <gtest/gtest.h>

#include "../extension/NativeEditControls/DelegateChips.h"

using namespace CodeToolsVsix;

TEST(DelegateChips, LaysChipsLeftToRightWithAGapAndAnXInsideEach) {
    const newui::Rect area(100, 10, 400, 24);
    const auto chips = DelegateChips::layout({"onSaveClick", "onOtherClick"}, area);

    ASSERT_EQ(chips.size(), 2u);
    EXPECT_FLOAT_EQ(chips[0].bounds.left(), 100.0f);
    EXPECT_FLOAT_EQ(chips[1].bounds.left(), chips[0].bounds.right() + DelegateChips::kGap);
    for (const DelegateChip& chip : chips) {
        EXPECT_GT(chip.bounds.size().width, DelegateChips::kMinWidth - 1.0f);
        EXPECT_TRUE(chip.bounds.contains(newui::Point(chip.removeBox.left() + 1.0f, chip.removeBox.top() + 1.0f)));
        EXPECT_GE(chip.bounds.top(), area.top());
        EXPECT_LE(chip.bounds.bottom(), area.bottom());
    }
    EXPECT_EQ(chips[0].index, 0u);
    EXPECT_EQ(chips[1].label, "onOtherClick");
}

TEST(DelegateChips, AChipThatDoesNotFitShrinksAndTheRestAreLeftOut) {
    const newui::Rect area(0, 0, 150, 24);
    const auto chips = DelegateChips::layout({"onSaveButtonClickHandler", "onSecond", "onThird"}, area);

    ASSERT_GE(chips.size(), 1u);
    EXPECT_LT(chips.size(), 3u);
    EXPECT_LE(chips.back().bounds.right(), area.right() + 0.01f);
}

TEST(DelegateChips, NothingFitsInAnAreaTooNarrowForEvenOneChip) {
    EXPECT_TRUE(DelegateChips::layout({"onSave"}, newui::Rect(0, 0, 20, 24)).empty());
    EXPECT_TRUE(DelegateChips::layout({}, newui::Rect(0, 0, 400, 24)).empty());
}

TEST(DelegateChips, HitTestTellsTheLabelFromTheXAndMissesTheGaps) {
    const auto chips = DelegateChips::layout({"onSaveClick", "onOtherClick"}, newui::Rect(0, 0, 400, 24));
    ASSERT_EQ(chips.size(), 2u);

    const newui::Point onLabel(chips[1].bounds.left() + 8.0f, chips[1].bounds.top() + 8.0f);
    const auto label = DelegateChips::hitTest(chips, onLabel);
    ASSERT_TRUE(label.has_value());
    EXPECT_EQ(label->index, 1u);
    EXPECT_FALSE(label->remove);

    const newui::Point onX(chips[0].removeBox.left() + 2.0f, chips[0].removeBox.top() + 2.0f);
    const auto remove = DelegateChips::hitTest(chips, onX);
    ASSERT_TRUE(remove.has_value());
    EXPECT_EQ(remove->index, 0u);
    EXPECT_TRUE(remove->remove);

    const newui::Point inGap(chips[0].bounds.right() + DelegateChips::kGap * 0.5f, chips[0].bounds.top() + 8.0f);
    EXPECT_FALSE(DelegateChips::hitTest(chips, inGap).has_value());
}
