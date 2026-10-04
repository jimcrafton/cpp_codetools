#include "MinimapStrip.h"

#include <newui/displayunit.h>
#include <newui/uicolormanager.h>

namespace CodeToolsVsix
{
    namespace
    {
    }

    MinimapStrip::MinimapStrip()
    {
        onMouseDown.add(this, &MinimapStrip::handleMouseDown);
    }

    void MinimapStrip::setLineCount(std::size_t lines)
    {
        lineCount_ = lines > 0 ? lines : 1;
        style().markDirty();
    }

    void MinimapStrip::setMarks(std::vector<Mark> marks)
    {
        marks_ = std::move(marks);
        style().markDirty();
    }

    void MinimapStrip::setCaretLine(std::size_t line)
    {
        caretLine_ = line;
        style().markDirty();
    }

    float MinimapStrip::yFor(std::size_t line) const
    {
        const float height = bounds().size().height;
        const float n = static_cast<float>(lineCount_);
        return (static_cast<float>(line) + 0.5f) / n * height;
    }

    std::size_t MinimapStrip::lineFor(float y) const
    {
        const float height = bounds().size().height;
        if (height <= 0.0f) {
            return 0;
        }
        float ratio = y / height;
        ratio = ratio < 0.0f ? 0.0f : (ratio > 1.0f ? 1.0f : ratio);
        const std::size_t line = static_cast<std::size_t>(ratio * static_cast<float>(lineCount_));
        return line < lineCount_ ? line : lineCount_ - 1;
    }

    void MinimapStrip::paint(BLContext& ctx)
    {
        const newui::Size size = bounds().size();
        if (size.width <= 0.0f || size.height <= 0.0f) {
            return;
        }

        const newui::DisplayMetrics metrics = displayMetrics();
        const float tickInset = kTickInset.toPixelsX(metrics);
        const float tickHeight = kTickHeight.toPixelsY(metrics);
        const float currentTickInset = kCurrentTickInset.toPixelsX(metrics);
        const float currentTickHeight = kCurrentTickHeight.toPixelsY(metrics);
        const float caretWidth = kCaretWidth.toPixelsX(metrics);
        const float caretHeight = kCaretHeight.toPixelsY(metrics);

        // A subtle separator from the code beside it.
        ctx.set_stroke_style(newui::UIColorManager::colorFor(newui::UIColorRole::ControlBorder).toBLRgba32());
        ctx.set_stroke_width(1.0);
        ctx.stroke_line(0.0, 0.0, 0.0, static_cast<double>(size.height));

        for (const Mark& mark : marks_) {
            const float y = yFor(mark.line);
            const float inset = mark.current ? currentTickInset : tickInset;
            const float height = mark.current ? currentTickHeight : tickHeight;
            const BLRect rect(inset, y - height * 0.5f, size.width - inset * 2.0f, height);
            ctx.set_fill_style(mark.color.toBLRgba32());
            ctx.fill_round_rect(rect, 1.0);
            if (mark.current) {
                ctx.set_stroke_style(newui::UIColorManager::colorFor(newui::UIColorRole::WindowText).toBLRgba32());
                ctx.set_stroke_width(1.0);
                ctx.stroke_round_rect(rect, 1.0);
            }
        }

        const float caretY = yFor(caretLine_);
        ctx.set_fill_style(newui::UIColorManager::colorFor(newui::UIColorRole::WindowText).toBLRgba32());
        ctx.fill_round_rect(BLRect(0.0, caretY - caretHeight * 0.5f, caretWidth, caretHeight), 1.0);
    }

    newui::SyncReturn MinimapStrip::handleMouseDown(newui::View& /*sender*/, const newui::Point& pt,
        std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/)
    {
        onLineClicked(*this, lineFor(pt.y));
        return newui::SyncReturn::Handled;
    }
}
