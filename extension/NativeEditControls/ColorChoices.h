#pragma once

#include <newui/color.h>
#include <newui/controllers.h>
#include <newui/items.h>

#include <optional>
#include <string>
#include <vector>

namespace CodeToolsVsix
{
    // The colors ColorPropertyEditor's dropdown offers: newui's UIColorRole names first ("system"
    // colors - WindowBackground, ControlText, ...), then the CSS named colors. Names are what
    // newui::Color::fromString() already understands, so a chosen name commits through the
    // ordinary parseValue() path. A system color commits its *current* RGBA value (newui::Color
    // has no symbolic form), so it won't follow a later light/dark switch.
    struct ColorChoice
    {
        std::string name;
        bool isSystem = false;
    };

    // Built once; system colors first, then named colors in CSS (alphabetical) order.
    const std::vector<ColorChoice>& colorChoices();

    bool isSystemColorName(const std::string& name);

    // The null color (newui::Color::null(): all four channels zero - "unset", paints nothing) is shown
    // as "none" instead of "#00000000", and "none" heads the dropdown. Everywhere a Color is shown
    // as text or typed as text goes through these two so they agree.
    constexpr const char* kNoColorName = "none";
    std::string colorDisplayText(const newui::Color& color);
    // Parses "none"/"null" (any case) to the null color, else whatever newui::Color::fromString()
    // accepts (hex, CSS names - "transparent" is the null color too). False if it isn't a color.
    bool parseColorText(const std::string& text, newui::Color& outColor);

    // The choice whose *current* color equals color exactly (8-bit RGBA), or nullopt. System
    // colors are checked first, so a color that happens to equal both shows as the system one.
    std::optional<std::string> colorChoiceNameFor(const newui::Color& color);

    // A dropdown row: swatch + name (+ a divider above the first named color). Reads its text from
    // the controller's model, so it works with any string-row model of choice names.
    class ColorSwatchListItem : public newui::ListItem
    {
    public:
        ColorSwatchListItem() = default;
        void paint(BLContext& ctx, const newui::Rect& rect, std::size_t index, newui::ListController& controller) override;
    };

    // Creates ColorSwatchListItems directly (like ToolboxController does for its items - no
    // reflection registration needed), and frees released ones instead of pooling them, since the
    // base pool only ever hands back items of defaultItemClassName().
    class ColorListController : public newui::ListController
    {
    public:
        newui::ListItem* createItem(std::size_t index) override;
        void releaseItem(newui::Item* item) override;
    };
}
