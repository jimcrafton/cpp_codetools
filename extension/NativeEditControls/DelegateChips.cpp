#include "DelegateChips.h"

#include "PaintUtils.h"

#include <newui/font.h>

namespace CodeToolsVsix
{
    namespace
    {
        double labelWidth(const std::string& label)
        {
            newui::Font font = newui::FontManager::getSystemFont(newui::SystemUIFont::Message);
            BLFont* blFont = font.blFont();
            if (blFont == nullptr || !blFont->is_valid()) {
                return static_cast<double>(label.size()) * 7.0;
            }
            return measureTextWidth(*blFont, label);
        }
    }

    std::vector<DelegateChip> DelegateChips::layout(const std::vector<std::string>& labels, const newui::Rect& area)
    {
        std::vector<DelegateChip> chips;
        const float height = kHeight < area.size().height ? kHeight : area.size().height;
        const float top = area.top() + (area.size().height - height) * 0.5f;

        float x = area.left();
        for (std::size_t i = 0; i < labels.size(); ++i) {
            const float remaining = area.right() - x;
            float width = kPadding + static_cast<float>(labelWidth(labels[i])) + kGap + kRemoveSize + kPadding;
            if (width > remaining) {
                if (remaining < kMinWidth) {
                    break;
                }
                width = remaining;
            }

            DelegateChip chip;
            chip.index = i;
            chip.label = labels[i];
            chip.bounds = newui::Rect(x, top, width, height);
            chip.removeBox = newui::Rect(x + width - kPadding - kRemoveSize, top + (height - kRemoveSize) * 0.5f,
                                         kRemoveSize, kRemoveSize);
            chips.push_back(std::move(chip));
            x += width + kGap;
        }
        return chips;
    }

    void DelegateChips::paint(BLContext& ctx, const std::vector<DelegateChip>& chips, const newui::Color& textColor,
                              const newui::Color& borderColor, const newui::Color& removeColor)
    {
        for (const DelegateChip& chip : chips) {
            ctx.save();
            ctx.set_stroke_style(borderColor.toBLRgba32());
            ctx.set_stroke_width(1.0);
            ctx.stroke_round_rect(BLRect(chip.bounds.left(), chip.bounds.top(), chip.bounds.size().width,
                                         chip.bounds.size().height), 4.0);

            // The "x": two short strokes inside its box.
            const double inset = 2.5;
            const double left = chip.removeBox.left() + inset;
            const double top = chip.removeBox.top() + inset;
            const double right = chip.removeBox.right() - inset;
            const double bottom = chip.removeBox.bottom() - inset;
            ctx.set_stroke_style(removeColor.toBLRgba32());
            ctx.stroke_line(BLPoint(left, top), BLPoint(right, bottom));
            ctx.stroke_line(BLPoint(left, bottom), BLPoint(right, top));
            ctx.restore();

            const float textLeft = chip.bounds.left() + kPadding;
            const float textRight = chip.removeBox.left() - kGap;
            paintText(ctx, newui::Rect(textLeft, chip.bounds.top(), textRight - textLeft, chip.bounds.size().height),
                      chip.label, textColor);
        }
    }

    std::optional<DelegateChipHit> DelegateChips::hitTest(const std::vector<DelegateChip>& chips, const newui::Point& point)
    {
        for (const DelegateChip& chip : chips) {
            if (chip.bounds.contains(point)) {
                return DelegateChipHit{ chip.index, chip.removeBox.contains(point) };
            }
        }
        return std::nullopt;
    }
}
