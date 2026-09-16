#include "../extension/NativeEditControls/ViewStyleRegistry.h"

#include <newui/controls.h>
#include <newui/subview.h>
#include <newui/viewstyle.h>

#include <gtest/gtest.h>

#include <memory>

// registerReflectionData() is already run once globally for this whole binary by
// test_component_editor.cpp's own ::testing::Environment - not needed here, ViewStyleRegistry
// itself works purely off dynamic_cast, no reflection data involved.

namespace
{
    bool hasOption(const std::vector<CodeToolsVsix::ViewStyleOption>& options, const std::string& name)
    {
        for (const auto& option : options) {
            if (option.displayName == name) {
                return true;
            }
        }
        return false;
    }
}

TEST(ViewStyleRegistry, ButtonOffersItsThreeCuratedButtonStyles)
{
    newui::Button button;
    auto options = CodeToolsVsix::ViewStyleRegistry::optionsFor(&button);
    ASSERT_EQ(options.size(), 3u);
    EXPECT_TRUE(hasOption(options, "Plain"));
    EXPECT_TRUE(hasOption(options, "Themed"));
    EXPECT_TRUE(hasOption(options, "Fluent"));

    std::unique_ptr<newui::ViewStyle> plain(options[0].factory());
    EXPECT_NE(dynamic_cast<newui::ButtonStyle*>(plain.get()), nullptr);
}

TEST(ViewStyleRegistry, ToggleOffersItsThreeCuratedCheckBoxStyles)
{
    newui::Toggle toggle;
    auto options = CodeToolsVsix::ViewStyleRegistry::optionsFor(&toggle);
    ASSERT_EQ(options.size(), 3u);

    bool foundFluent = false;
    for (const auto& option : options) {
        std::unique_ptr<newui::ViewStyle> style(option.factory());
        if (dynamic_cast<newui::FluentCheckBoxStyle*>(style.get()) != nullptr) {
            foundFluent = true;
        }
    }
    EXPECT_TRUE(foundFluent);
}

TEST(ViewStyleRegistry, LabelOffersPlainAndLabelStyle)
{
    newui::Label label;
    auto options = CodeToolsVsix::ViewStyleRegistry::optionsFor(&label);
    ASSERT_EQ(options.size(), 2u);
    EXPECT_TRUE(hasOption(options, "Label"));
}

// A plain container SubView - not one of the specifically curated owner classes - falls back to
// the generic 3-entry table (ViewStyle/ThemedViewStyle/FluentCardStyle), never empty and never
// one of the ~40 internal sub-part styles (ThemedTrackbarThumbStyle etc.) this registry exists
// specifically to keep off an arbitrary View - see ViewStyleRegistry.h's own class comment.
TEST(ViewStyleRegistry, PlainSubViewFallsBackToTheGenericThreeEntryTable)
{
    newui::SubView view;
    auto options = CodeToolsVsix::ViewStyleRegistry::optionsFor(&view);
    ASSERT_EQ(options.size(), 3u);
    EXPECT_TRUE(hasOption(options, "Plain"));
    EXPECT_TRUE(hasOption(options, "Themed"));
    EXPECT_TRUE(hasOption(options, "Fluent Card"));

    bool foundFluentCard = false;
    for (const auto& option : options) {
        std::unique_ptr<newui::ViewStyle> style(option.factory());
        if (dynamic_cast<newui::FluentCardStyle*>(style.get()) != nullptr) {
            foundFluentCard = true;
        }
    }
    EXPECT_TRUE(foundFluentCard);
}

TEST(ViewStyleRegistry, NeverEmptyEvenForNullptr)
{
    auto options = CodeToolsVsix::ViewStyleRegistry::optionsFor(nullptr);
    EXPECT_FALSE(options.empty());
}
