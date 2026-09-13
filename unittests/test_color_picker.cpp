#include "../extension/NativeEditControls/ColorPicker.h"

#include <newui/geometry.h>

#include <gtest/gtest.h>

// Exercises ColorPicker's real public API - setColor()/setHue()/setSaturationValue()/setAlpha()
// and the real child controls (svSquare()/hueRail()/alphaRail()) it builds - via real
// onMouseDown/onMouseMove/onMouseUp calls on those children (setBounds() first to give a headless
// widget real dimensions, same convention test_splitter.cpp and GradientEditorDialog's own
// StopTrack tests already use), never simulating anything lower-level. Constructing a
// ColorPicker/its children needs no real native window at all - none of them create one.
TEST(ColorPickerTest, DefaultsToOpaqueWhite)
{
    CodeToolsVsix::ColorPicker picker;

    EXPECT_FLOAT_EQ(picker.saturation(), 0.0f);
    EXPECT_FLOAT_EQ(picker.value(), 1.0f);
    EXPECT_FLOAT_EQ(picker.alpha(), 1.0f);
}

TEST(ColorPickerTest, SetColorDecomposesIntoHueSaturationValueAlpha)
{
    CodeToolsVsix::ColorPicker picker;

    picker.setColor(newui::Color::fromHSV(120.0f, 0.5f, 0.75f, 0.25f));

    EXPECT_NEAR(picker.hue(), 120.0f, 0.01f);
    EXPECT_NEAR(picker.saturation(), 0.5f, 0.01f);
    EXPECT_NEAR(picker.value(), 0.75f, 0.01f);
    EXPECT_NEAR(picker.alpha(), 0.25f, 0.01f);
}

TEST(ColorPickerTest, ColorRoundTripsThroughHSVA)
{
    CodeToolsVsix::ColorPicker picker;
    newui::Color seed = newui::Color::fromHSV(45.0f, 0.6f, 0.9f, 0.8f);

    picker.setColor(seed);

    newui::Color result = picker.color();
    EXPECT_NEAR(result.r, seed.r, 0.01f);
    EXPECT_NEAR(result.g, seed.g, 0.01f);
    EXPECT_NEAR(result.b, seed.b, 0.01f);
    EXPECT_NEAR(result.a, seed.a, 0.01f);
}

TEST(ColorPickerTest, SetHueWrapsOutOfRangeValues)
{
    CodeToolsVsix::ColorPicker picker;

    picker.setHue(-30.0f);
    EXPECT_NEAR(picker.hue(), 330.0f, 0.01f);

    picker.setHue(390.0f);
    EXPECT_NEAR(picker.hue(), 30.0f, 0.01f);
}

TEST(ColorPickerTest, SetSaturationValueClamps)
{
    CodeToolsVsix::ColorPicker picker;

    picker.setSaturationValue(-1.0f, 2.0f);

    EXPECT_FLOAT_EQ(picker.saturation(), 0.0f);
    EXPECT_FLOAT_EQ(picker.value(), 1.0f);
}

TEST(ColorPickerTest, SetAlphaClamps)
{
    CodeToolsVsix::ColorPicker picker;

    picker.setAlpha(5.0f);
    EXPECT_FLOAT_EQ(picker.alpha(), 1.0f);

    picker.setAlpha(-5.0f);
    EXPECT_FLOAT_EQ(picker.alpha(), 0.0f);
}

// Same "no-ops if the result is unchanged" contract newui::Slider::setValue() documents - a
// direct setColor() call with the picker's own current value shouldn't fire onColorChanged again.
TEST(ColorPickerTest, SetColorIsANoOpWhenAlreadyThatColor)
{
    CodeToolsVsix::ColorPicker picker;
    picker.setColor(newui::Color::fromHSV(200.0f, 0.4f, 0.6f, 1.0f));

    int fireCount = 0;
    picker.onColorChanged.add([&fireCount](CodeToolsVsix::ColorPicker&) {
        ++fireCount;
        return newui::SyncReturn::Handled;
    });

    picker.setColor(newui::Color::fromHSV(200.0f, 0.4f, 0.6f, 1.0f));

    EXPECT_EQ(fireCount, 0);
}

TEST(ColorPickerTest, SetHueActuallyChangingFiresOnColorChanged)
{
    CodeToolsVsix::ColorPicker picker;

    int fireCount = 0;
    picker.onColorChanged.add([&fireCount](CodeToolsVsix::ColorPicker&) {
        ++fireCount;
        return newui::SyncReturn::Handled;
    });

    picker.setHue(90.0f);

    EXPECT_EQ(fireCount, 1);
}

// Drives the real SVSquare's own onMouseDown/onMouseMove path (not setSaturationValue()
// directly) - proves an actual drag gesture reaches it.
TEST(ColorPickerTest, DraggingTheRealSVSquareUpdatesSaturationAndValue)
{
    CodeToolsVsix::ColorPicker picker;
    newui::SubView* square = picker.svSquare();
    ASSERT_NE(square, nullptr);
    square->setBounds(newui::Rect(0.0f, 0.0f, 200.0f, 100.0f));

    newui::SyncReturn downResult = square->onMouseDown.syncCallFirst(*square, newui::Point(50.0f, 25.0f), 0, 0);
    EXPECT_EQ(downResult, newui::SyncReturn::Handled);
    EXPECT_NEAR(picker.saturation(), 0.25f, 0.01f);
    EXPECT_NEAR(picker.value(), 0.75f, 0.01f);

    newui::SyncReturn moveResult = square->onMouseMove.syncCallFirst(*square, newui::Point(150.0f, 90.0f), 0, 0);
    EXPECT_EQ(moveResult, newui::SyncReturn::Handled);
    EXPECT_NEAR(picker.saturation(), 0.75f, 0.01f);
    EXPECT_NEAR(picker.value(), 0.1f, 0.01f);

    newui::SyncReturn upResult = square->onMouseUp.syncCallFirst(*square, newui::Point(150.0f, 90.0f), 0, 0);
    EXPECT_EQ(upResult, newui::SyncReturn::Handled);

    newui::SyncReturn moveAfterUp = square->onMouseMove.syncCallFirst(*square, newui::Point(0.0f, 0.0f), 0, 0);
    EXPECT_EQ(moveAfterUp, newui::SyncReturn::Ignored);
    EXPECT_NEAR(picker.saturation(), 0.75f, 0.01f);
}

TEST(ColorPickerTest, DraggingTheRealHueRailUpdatesHue)
{
    CodeToolsVsix::ColorPicker picker;
    newui::SubView* rail = picker.hueRail();
    ASSERT_NE(rail, nullptr);
    rail->setBounds(newui::Rect(0.0f, 0.0f, 18.0f, 200.0f));

    newui::SyncReturn downResult = rail->onMouseDown.syncCallFirst(*rail, newui::Point(9.0f, 100.0f), 0, 0);
    EXPECT_EQ(downResult, newui::SyncReturn::Handled);
    EXPECT_NEAR(picker.hue(), 180.0f, 0.5f);
}

TEST(ColorPickerTest, DraggingTheRealAlphaRailUpdatesAlpha)
{
    CodeToolsVsix::ColorPicker picker;
    newui::SubView* rail = picker.alphaRail();
    ASSERT_NE(rail, nullptr);
    rail->setBounds(newui::Rect(0.0f, 0.0f, 18.0f, 200.0f));

    newui::SyncReturn downResult = rail->onMouseDown.syncCallFirst(*rail, newui::Point(9.0f, 50.0f), 0, 0);
    EXPECT_EQ(downResult, newui::SyncReturn::Handled);
    EXPECT_NEAR(picker.alpha(), 0.25f, 0.01f);
}
