#include "../extension/NativeEditControls/ColorChoices.h"
#include "../extension/NativeEditControls/PaintUtils.h"
#include "../extension/NativeEditControls/PropertyEditor.h"

#include <newui/color_constants.h>
#include <newui/controls.h>
#include <newui/models.h>
#include <newui/uicolormanager.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <any>
#include <memory>
#include <string>
#include <vector>

namespace
{
    class NameRowsModel : public newui::ListModel
    {
    public:
        std::vector<std::string> rows;

        std::any value(const std::any& key) override
        {
            if (const std::size_t* index = std::any_cast<std::size_t>(&key)) {
                if (*index < rows.size()) {
                    return rows[*index];
                }
            }
            return std::any();
        }

        std::size_t size() const override { return rows.size(); }
    };
}

TEST(ColorChoices, NoneComesFirstThenSystemColorsThenEveryNamedColor)
{
    const auto& choices = CodeToolsVsix::colorChoices();
    constexpr std::size_t kSystemCount = 10;  // one per newui::UIColorRole
    constexpr std::size_t kNamedCount = sizeof(newui::kNamedColors) / sizeof(newui::kNamedColors[0]);
    // "none" replaces the CSS "transparent" (the same null color) - one entry each, not both.
    ASSERT_EQ(choices.size(), 1 + kSystemCount + kNamedCount - 1);

    EXPECT_EQ(choices[0].name, "none");
    EXPECT_FALSE(choices[0].isSystem);
    for (std::size_t i = 1; i <= kSystemCount; ++i) {
        EXPECT_TRUE(choices[i].isSystem) << choices[i].name;
    }
    for (std::size_t i = kSystemCount + 1; i < choices.size(); ++i) {
        EXPECT_FALSE(choices[i].isSystem) << choices[i].name;
    }
    EXPECT_EQ(choices[1].name, "WindowBackground");
    for (const auto& choice : choices) {
        EXPECT_NE(choice.name, "transparent");
    }
}

TEST(ColorChoices, EveryChoiceNameResolvesToAColor)
{
    for (const auto& choice : CodeToolsVsix::colorChoices()) {
        newui::Color color;
        EXPECT_TRUE(CodeToolsVsix::parseColorText(choice.name, color)) << choice.name;
    }
}

TEST(ColorChoices, ChoiceNameForAnExactPresetColor)
{
    EXPECT_EQ(CodeToolsVsix::colorChoiceNameFor(newui::Color::fromName("cornflowerblue")), "cornflowerblue");

    // A system role's current value maps back to that role (checked before the named colors).
    newui::Color windowBackground = newui::UIColorManager::colorFor(newui::UIColorRole::WindowBackground);
    EXPECT_EQ(CodeToolsVsix::colorChoiceNameFor(windowBackground), "WindowBackground");
}

TEST(ColorChoices, NoChoiceNameForACustomColor)
{
    EXPECT_FALSE(CodeToolsVsix::colorChoiceNameFor(newui::Color(0x123457u, false)).has_value());
}

// The real pixels, not just internal state: a row for "red" has to actually paint a red swatch.
TEST(ColorChoices, SwatchRowPaintsTheNamedColorAsRealPixels)
{
    NameRowsModel model;
    model.rows = { "red" };
    CodeToolsVsix::ColorListController controller;
    controller.setModel(&model);

    BLImage image(200, 22, BL_FORMAT_PRGB32);
    {
        BLContext ctx(image);
        ctx.clear_all();
        std::unique_ptr<newui::ListItem> item(controller.createItem(0));
        item->paint(ctx, newui::Rect(0, 0, 200, 22), 0, controller);
        ctx.end();
    }

    BLImageData data{};
    ASSERT_EQ(image.get_data(&data), BL_SUCCESS);
    // Swatch: inset 6px from the left, 14px square, vertically centered in a 22px row - so
    // (13, 11) is inside it, clear of its border.
    const auto* row = static_cast<const std::uint8_t*>(data.pixel_data) + 11 * data.stride;
    std::uint32_t pixel = reinterpret_cast<const std::uint32_t*>(row)[13];
    EXPECT_EQ(pixel & 0x00FFFFFFu, 0x00FF0000u);  // red, ignoring alpha
}

TEST(ColorPropertyEditor, HasBothADropdownAndADialog)
{
    // Constructed with no real property - only the style flags are read here.
    CodeToolsVsix::ColorPropertyEditor editor(nullptr, nullptr);
    EXPECT_EQ(editor.editStyle(), CodeToolsVsix::PropertyEditor::EditStyle::DropdownAndDialog);
    EXPECT_TRUE(editor.hasDropdown());
    EXPECT_TRUE(editor.hasDialog());

    std::vector<std::string> values = editor.dropdownValues();
    ASSERT_FALSE(values.empty());
    EXPECT_EQ(values.front(), "none");   // the null color heads the list, ahead of the system colors
    EXPECT_NE(std::find(values.begin(), values.end(), "WindowBackground"), values.end());
    EXPECT_NE(std::find(values.begin(), values.end(), "cornflowerblue"), values.end());
}

// ---------------------------------------------------------------------------
// The null color reads "none", not "#00000000".
// ---------------------------------------------------------------------------

TEST(ColorText, TheNullColorIsShownAsNoneAndEveryOtherColorKeepsItsHex)
{
    EXPECT_EQ(CodeToolsVsix::colorDisplayText(newui::Color::null()), "none");
    EXPECT_EQ(CodeToolsVsix::colorDisplayText(newui::Color(0.0f, 0.0f, 0.0f, 1.0f)), newui::Color(0.0f, 0.0f, 0.0f, 1.0f).toString());   // opaque black
    // Fully transparent but with real r/g/b is a real, deliberate color - not "unset".
    newui::Color clearRed(1.0f, 0.0f, 0.0f, 0.0f);
    EXPECT_EQ(CodeToolsVsix::colorDisplayText(clearRed), clearRed.toString());
}

TEST(ColorText, NoneAndNullParseToTheNullColorInAnyCaseAndOtherTextIsUnchanged)
{
    for (const char* text : { "none", "None", "NULL", " null ", "transparent" }) {
        newui::Color color(1.0f, 1.0f, 1.0f, 1.0f);
        ASSERT_TRUE(CodeToolsVsix::parseColorText(text, color)) << text;
        EXPECT_TRUE(color.isNull()) << text;
    }
    newui::Color red;
    ASSERT_TRUE(CodeToolsVsix::parseColorText("#ff0000ff", red));
    EXPECT_FLOAT_EQ(red.r, 1.0f);
    ASSERT_TRUE(CodeToolsVsix::parseColorText("cornflowerblue", red));
    EXPECT_FALSE(CodeToolsVsix::parseColorText("not a color", red));
    EXPECT_FALSE(CodeToolsVsix::parseColorText("", red));
}

TEST(ColorChoices, TheNullColorMapsBackToTheNoneChoice)
{
    EXPECT_EQ(CodeToolsVsix::colorChoiceNameFor(newui::Color::null()), "none");
}

// Real pixels: a white box with a red diagonal slash (Photoshop's "none") - not an invisible swatch.
TEST(ColorText, ANullSwatchPaintsAWhiteBoxWithARedDiagonalSlash)
{
    BLImage image(20, 20, BL_FORMAT_PRGB32);
    {
        BLContext ctx(image);
        ctx.clear_all();
        CodeToolsVsix::paintSwatch(ctx, newui::Rect(2, 2, 16, 16), newui::Color::null(), newui::Color(0.0f, 0.0f, 0.0f, 1.0f));
        ctx.end();
    }
    BLImageData data{};
    ASSERT_EQ(image.get_data(&data), BL_SUCCESS);
    auto pixelAt = [&](int x, int y) {
        return reinterpret_cast<const std::uint32_t*>(static_cast<const std::uint8_t*>(data.pixel_data) + y * data.stride)[x];
    };
    EXPECT_EQ(pixelAt(5, 8), 0xFFFFFFFFu);         // inside the box, well off the slash: opaque white
    const std::uint32_t onSlash = pixelAt(10, 10);  // the slash runs bottom-left to top-right through the middle
    EXPECT_GT((onSlash >> 16) & 0xFF, ((onSlash >> 8) & 0xFF) + 20u);   // reddish (antialiased), not white
    EXPECT_EQ(pixelAt(0, 0) >> 24, 0u);            // outside the box: untouched
}
