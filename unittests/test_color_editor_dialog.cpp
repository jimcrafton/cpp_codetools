#include "../extension/NativeEditControls/ColorEditorDialog.h"
#include "../extension/NativeEditControls/TextEncoding.h"

#include <gtest/gtest.h>

// Exercises ColorEditorDialog's real public API - never calling the inherited showModal()
// (blocks on a real modal message loop) - same convention test_gradient_editor_dialog.cpp
// already establishes. Constructing a ColorEditorDialog loads its static chrome from the real
// Resources/coloreditordialog.newui via newui::Bundle::loadDialog() - needs Bundle's
// resourcesDir() to resolve correctly, which it does by default (exe-relative) when this test
// binary runs from its own build output directory, same as every other resource-dependent test
// in this file already relies on (e.g. ToolboxRegistry's icon resources).
TEST(ColorEditorDialogTest, SetColorSeedsTheRealColorPicker)
{
    CodeToolsVsix::ColorEditorDialog dialog;
    ASSERT_NE(dialog.colorPicker(), nullptr);

    newui::Color seed(0.2f, 0.4f, 0.6f, 0.8f);
    dialog.setColor(seed);

    EXPECT_EQ(dialog.color().toString(), seed.toString());
    EXPECT_EQ(dialog.colorPicker()->color().toString(), seed.toString());
}

// color() is a real, live read of colorPicker_'s own current color - not a second, separately
// tracked copy - so a real drag/setter call on colorPicker() itself must be immediately visible
// through color() with no separate sync step.
TEST(ColorEditorDialogTest, ColorPickerIsTheOneRealSourceOfTruth)
{
    CodeToolsVsix::ColorEditorDialog dialog;
    // A real, fully saturated/valued color, not black/white/gray - toHSV() reports an arbitrary
    // hue for any achromatic color (color.h's own hueFromRGB() gating), so this needs a real hue
    // to actually survive the round trip through setHue().
    dialog.setColor(newui::Color(1.0f, 0.0f, 0.0f, 1.0f));

    dialog.colorPicker()->setHue(90.0f);

    EXPECT_FLOAT_EQ(dialog.color().toHSV().h, 90.0f);
}

TEST(ColorEditorDialogTest, DrivingTheRealColorPickerUpdatesTheHexFieldAndNewSwatch)
{
    CodeToolsVsix::ColorEditorDialog dialog;
    ASSERT_NE(dialog.hexField(), nullptr);
    ASSERT_NE(dialog.newSwatch(), nullptr);

    dialog.setColor(newui::Color(1.0f, 0.0f, 0.0f, 1.0f));

    EXPECT_EQ(dialog.hexField()->text(), CodeToolsVsix::utf8ToWide("ff0000"));
    EXPECT_EQ(dialog.newSwatch()->style().backgroundFill().color().toString(),
        newui::Color(1.0f, 0.0f, 0.0f, 1.0f).toString());
}

TEST(ColorEditorDialogTest, CommitHexFieldParsesValidHexAndPreservesAlpha)
{
    CodeToolsVsix::ColorEditorDialog dialog;
    dialog.setColor(newui::Color(0.0f, 0.0f, 0.0f, 0.5f));
    ASSERT_NE(dialog.hexField(), nullptr);

    dialog.hexField()->setText(CodeToolsVsix::utf8ToWide("00ff00"));
    dialog.commitHexField();

    newui::Color result = dialog.color();
    EXPECT_FLOAT_EQ(result.r, 0.0f);
    EXPECT_FLOAT_EQ(result.g, 1.0f);
    EXPECT_FLOAT_EQ(result.b, 0.0f);
    EXPECT_FLOAT_EQ(result.a, 0.5f);  // alpha untouched by a 6-digit hex commit
}

TEST(ColorEditorDialogTest, CommitHexFieldIgnoresInvalidText)
{
    CodeToolsVsix::ColorEditorDialog dialog;
    dialog.setColor(newui::Color(0.1f, 0.2f, 0.3f, 1.0f));
    ASSERT_NE(dialog.hexField(), nullptr);

    dialog.hexField()->setText(CodeToolsVsix::utf8ToWide("not-a-color"));
    dialog.commitHexField();

    newui::Color result = dialog.color();
    EXPECT_FLOAT_EQ(result.r, 0.1f);
    EXPECT_FLOAT_EQ(result.g, 0.2f);
    EXPECT_FLOAT_EQ(result.b, 0.3f);
}

TEST(ColorEditorDialogTest, SetFormatTabRelabelsAndRepopulatesTheValueGrid)
{
    CodeToolsVsix::ColorEditorDialog dialog;
    dialog.setColor(newui::Color::fromHSL(newui::HSLColor{120.0f, 0.5f, 0.5f, 1.0f}));
    ASSERT_EQ(dialog.formatTab(), CodeToolsVsix::ColorEditorDialog::Format::Rgb);

    dialog.setFormatTab(CodeToolsVsix::ColorEditorDialog::Format::Hsl);

    EXPECT_EQ(dialog.formatTab(), CodeToolsVsix::ColorEditorDialog::Format::Hsl);
    ASSERT_NE(dialog.valueGridField(0), nullptr);
    EXPECT_EQ(dialog.valueGridField(0)->text(), CodeToolsVsix::utf8ToWide("120"));
}

TEST(ColorEditorDialogTest, CommitValueGridFieldAppliesTheRightChannel)
{
    CodeToolsVsix::ColorEditorDialog dialog;
    dialog.setColor(newui::Color(0.0f, 0.0f, 0.0f, 1.0f));

    dialog.commitValueGridField(0, "255");  // R, in the default RGB tab

    EXPECT_FLOAT_EQ(dialog.color().r, 1.0f);
    EXPECT_FLOAT_EQ(dialog.color().g, 0.0f);
}

TEST(ColorEditorDialogTest, CommitValueGridFieldIgnoresUnparseableText)
{
    CodeToolsVsix::ColorEditorDialog dialog;
    dialog.setColor(newui::Color(0.5f, 0.5f, 0.5f, 1.0f));

    dialog.commitValueGridField(0, "not-a-number");

    EXPECT_FLOAT_EQ(dialog.color().r, 0.5f);
}

// Drives the real click path (a real onMouseDown on the actual swatch SubView, not
// selectSwatch() bypassed) - matches this file's own "the tab buttons don't work at all" class
// of bug ClickingTheSegmentedControlActuallyChangesKind (test_gradient_editor_dialog.cpp) exists
// to catch, applied to a swatch instead.
TEST(ColorEditorDialogTest, ClickingARealSwatchAppliesItsColor)
{
    CodeToolsVsix::ColorEditorDialog dialog;
    ASSERT_FALSE(dialog.swatchViews().empty());
    newui::SubView* swatch = dialog.swatchViews()[0];
    newui::Color expected = swatch->style().backgroundFill().color();

    swatch->onMouseDown(*swatch, newui::Point(1.0f, 1.0f), 0, 0);

    EXPECT_EQ(dialog.color().toString(), expected.toString());
}

TEST(ColorEditorDialogTest, AddSwatchFromCurrentAppendsAndIsSelectable)
{
    CodeToolsVsix::ColorEditorDialog dialog;
    dialog.setColor(newui::Color(0.9f, 0.1f, 0.1f, 1.0f));
    std::size_t before = dialog.swatchViews().size();

    dialog.addSwatchFromCurrent();

    std::vector<newui::SubView*> swatches = dialog.swatchViews();
    ASSERT_EQ(swatches.size(), before + 1);
    EXPECT_EQ(swatches.back()->style().backgroundFill().color().toString(),
        newui::Color(0.9f, 0.1f, 0.1f, 1.0f).toString());

    // The "+" button stays last even after an append.
    ASSERT_NE(dialog.addSwatchButton(), nullptr);
    ASSERT_FALSE(dialog.swatchesRow()->childViews().empty());
    EXPECT_EQ(dialog.swatchesRow()->childViews().back(), dialog.addSwatchButton());

    dialog.setColor(newui::Color(0.0f, 0.0f, 0.0f, 1.0f));
    dialog.selectSwatch(before);
    EXPECT_EQ(dialog.color().toString(), newui::Color(0.9f, 0.1f, 0.1f, 1.0f).toString());
}

TEST(ColorEditorDialogTest, RevertToOldRestoresTheSeededColor)
{
    CodeToolsVsix::ColorEditorDialog dialog;
    newui::Color original(0.2f, 0.7f, 0.3f, 1.0f);
    dialog.setColor(original);

    dialog.colorPicker()->setHue(200.0f);
    ASSERT_NE(dialog.color().toString(), original.toString());

    dialog.revertToOld();

    EXPECT_EQ(dialog.color().toString(), original.toString());
}
