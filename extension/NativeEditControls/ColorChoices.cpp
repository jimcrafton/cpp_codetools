#include "ColorChoices.h"
#include "PaintUtils.h"

#include <newui/color_constants.h>
#include <newui/uicolormanager.h>

#include <any>
#include <cctype>

namespace CodeToolsVsix
{
    namespace
    {
        // The UIColorRole names newui::Color::fromString() accepts (color.cpp's own kRoles table
        // is file-local there, so this mirrors it) - "system" colors in the picker's terms.
        constexpr const char* kSystemColorNames[] = {
            "WindowBackground", "WindowText", "ControlBackground", "ControlText", "ControlBorder",
            "DisabledText", "HighlightBackground", "HighlightText", "LinkText", "LinkHoverText",
        };

        constexpr float kSwatchGap = 8.0f;
        constexpr float kRowInset = 6.0f;

        std::uint32_t packed(const newui::Color& color)
        {
            return color.toBLRgba32().value;
        }
    }

    const std::vector<ColorChoice>& colorChoices()
    {
        static const std::vector<ColorChoice> choices = [] {
            std::vector<ColorChoice> result;
            result.push_back({ kNoColorName, false });
            for (const char* name : kSystemColorNames) {
                result.push_back({ name, true });
            }
            for (const newui::NamedColorEntry& entry : newui::kNamedColors) {
                newui::Color color;
                if (newui::Color::fromString(entry.name, color) && color.isNull()) {
                    continue;   // "transparent" - the same color as "none", which already heads the list
                }
                result.push_back({ entry.name, false });
            }
            return result;
        }();
        return choices;
    }

    bool isSystemColorName(const std::string& name)
    {
        for (const char* systemName : kSystemColorNames) {
            if (name == systemName) {
                return true;
            }
        }
        return false;
    }

    std::string colorDisplayText(const newui::Color& color)
    {
        return color.isNull() ? std::string(kNoColorName) : color.toString();
    }

    bool parseColorText(const std::string& text, newui::Color& outColor)
    {
        std::string lower;
        for (char c : text) {
            if (c != ' ' && c != '\t') {
                lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            }
        }
        if (lower == kNoColorName || lower == "null") {
            outColor = newui::Color::null();
            return true;
        }
        return newui::Color::fromString(text, outColor);
    }

    std::optional<std::string> colorChoiceNameFor(const newui::Color& color)
    {
        if (color.isNull()) {
            return std::string(kNoColorName);
        }
        std::uint32_t target = packed(color);
        for (const ColorChoice& choice : colorChoices()) {
            newui::Color candidate;
            if (parseColorText(choice.name, candidate) && packed(candidate) == target) {
                return choice.name;
            }
        }
        return std::nullopt;
    }

    void ColorSwatchListItem::paint(BLContext& ctx, const newui::Rect& rect, std::size_t index,
        newui::ListController& controller)
    {
        newui::Item::paint(ctx, rect);  // selection/hover fill

        auto textAt = [&controller](std::size_t i) -> std::string {
            newui::ListModel* model = controller.model();
            if (model == nullptr || i >= model->size()) {
                return std::string();
            }
            std::any value = model->value(i);
            const std::string* text = std::any_cast<std::string>(&value);
            return text != nullptr ? *text : std::string();
        };

        std::string name = textAt(index);
        newui::Color textColor = newui::UIColorManager::colorFor(
            isSelected() ? newui::UIColorRole::HighlightText : newui::UIColorRole::ControlText);

        // A thin divider above the first non-system row separates the two groups without needing
        // real header rows (which would be selectable rows in a plain ListView).
        if (index > 0 && !isSystemColorName(name) && isSystemColorName(textAt(index - 1))) {
            ctx.save();
            ctx.set_fill_style(newui::UIColorManager::colorFor(newui::UIColorRole::ControlBorder).toBLRgba32());
            ctx.fill_rect(BLRect(rect.left(), rect.top(), rect.size().width, 1.0));
            ctx.restore();
        }

        const newui::Rect& bounds = clientBounds();
        newui::Rect swatch(bounds.left() + kRowInset, bounds.top() + (bounds.size().height - kSwatchSize) * 0.5f,
            kSwatchSize, kSwatchSize);
        newui::Color color;
        if (parseColorText(name, color)) {
            paintSwatch(ctx, swatch, color, textColor);
        }

        float textLeft = swatch.right() + kSwatchGap;
        newui::Rect textRect(textLeft, bounds.top(), bounds.right() - textLeft, bounds.size().height);
        paintText(ctx, textRect, name, textColor);
    }

    newui::ListItem* ColorListController::createItem(std::size_t /*index*/)
    {
        return new ColorSwatchListItem();
    }

    void ColorListController::releaseItem(newui::Item* item)
    {
        delete item;
    }
}
