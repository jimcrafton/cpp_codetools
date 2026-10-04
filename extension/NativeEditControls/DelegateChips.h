#pragma once

#include <newui/color.h>
#include <newui/geometry.h>

#include <blend2d/blend2d.h>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace CodeToolsVsix
{
    // One listener on a Delegates row, drawn as a chip: its label and a small "x" that removes it.
    struct DelegateChip
    {
        newui::Rect bounds;      // the whole chip, in the same space as the area it was laid out in
        newui::Rect removeBox;   // the "x" inside it
        std::string label;
        std::size_t index = 0;   // which listener (the order describedListeners() gave)
    };

    struct DelegateChipHit
    {
        std::size_t index = 0;
        bool remove = false;     // the "x", not the label
    };

    // Layout, painting and hit-testing of a row's chips, one place so they cannot disagree. Chips sit
    // left to right in `area`; one that doesn't fit shrinks (its label is ellipsized when painted) and
    // the rest are left out.
    class DelegateChips
    {
    public:
        static constexpr float kHeight = 16.0f;
        static constexpr float kGap = 3.0f;
        static constexpr float kPadding = 4.0f;
        static constexpr float kRemoveSize = 8.0f;
        static constexpr float kMinWidth = 44.0f;

        static std::vector<DelegateChip> layout(const std::vector<std::string>& labels, const newui::Rect& area);

        static void paint(BLContext& ctx, const std::vector<DelegateChip>& chips, const newui::Color& textColor,
                          const newui::Color& borderColor, const newui::Color& removeColor);

        static std::optional<DelegateChipHit> hitTest(const std::vector<DelegateChip>& chips, const newui::Point& point);
    };
}
