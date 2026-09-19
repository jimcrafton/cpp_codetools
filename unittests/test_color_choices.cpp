#include "../extension/NativeEditControls/ColorChoices.h"
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

TEST(ColorChoices, SystemColorsComeFirstThenEveryNamedColor)
{
    const auto& choices = CodeToolsVsix::colorChoices();
    constexpr std::size_t kSystemCount = 10;  // one per newui::UIColorRole
    constexpr std::size_t kNamedCount = sizeof(newui::kNamedColors) / sizeof(newui::kNamedColors[0]);
    ASSERT_EQ(choices.size(), kSystemCount + kNamedCount);

    for (std::size_t i = 0; i < kSystemCount; ++i) {
        EXPECT_TRUE(choices[i].isSystem) << choices[i].name;
    }
    for (std::size_t i = kSystemCount; i < choices.size(); ++i) {
        EXPECT_FALSE(choices[i].isSystem) << choices[i].name;
    }
    EXPECT_EQ(choices[0].name, "WindowBackground");
}

TEST(ColorChoices, EveryChoiceNameResolvesToAColor)
{
    for (const auto& choice : CodeToolsVsix::colorChoices()) {
        newui::Color color;
        EXPECT_TRUE(newui::Color::fromString(choice.name, color)) << choice.name;
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
    EXPECT_EQ(values.front(), "WindowBackground");
    EXPECT_NE(std::find(values.begin(), values.end(), "cornflowerblue"), values.end());
}
