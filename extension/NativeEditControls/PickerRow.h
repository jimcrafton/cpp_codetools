#pragma once

#include <newui/controls.h>
#include <newui/subview.h>
#include <newui/uicolormanager.h>

#include <functional>
#include <string>

namespace CodeToolsVsix
{
    // One line in a CalloutTool list popup (the delegate picker, the controller issues list). A row
    // with no action is informational; one with onChosen draws a border and calls it on click.
    class PickerRow : public newui::SubView
    {
    public:
        static constexpr float kRowHeight = 26.0f;
        static constexpr float kPadding = 8.0f;
        static constexpr float kTopReserve = 20.0f;   // clears CalloutTool's own top tail + margin

        PickerRow(const std::string& text, std::function<void()> onChosen)
            : onChosen_(std::move(onChosen))
        {
            setVisible(true);
            onMouseDown.add(this, &PickerRow::handleMouseDown);
            auto* label = new newui::Label();
            label->setText(text);
            label_ = label;
            // A click lands on the deepest view under the cursor and doesn't bubble, so the label
            // (which covers most of the row) has to run the row's action itself.
            label->onMouseDown.add(this, &PickerRow::handleMouseDown);
            // The label is the deepest view under the cursor, so it reports hover for the row too.
            label->onMouseEntered.add(this, &PickerRow::handleEntered);
            label->onMouseLeft.add(this, &PickerRow::handleLeft);
            onMouseEntered.add(this, &PickerRow::handleEntered);
            onMouseLeft.add(this, &PickerRow::handleLeft);
            addChild(label);
        }

        void setBounds(const newui::Rect& bounds) override
        {
            newui::SubView::setBounds(bounds);
            const newui::Rect local = getClientBounds();
            label_->setBounds(newui::Rect(8.0f, 0.0f, local.width() - 16.0f, local.height()));
        }

        void paint(BLContext& ctx) override
        {
            const newui::Rect bounds = getClientBounds();
            if (bounds.width() <= 0.0f || bounds.height() <= 0.0f) {
                return;
            }
            ctx.save();
            ctx.set_fill_style(newui::UIColorManager::colorFor(newui::UIColorRole::ControlBackground).toBLRgba32());
            ctx.fill_round_rect(BLRect(bounds), 4.0);
            if (onChosen_) {
                ctx.set_stroke_style(newui::UIColorManager::colorFor(newui::UIColorRole::ControlBorder).toBLRgba32());
                ctx.set_stroke_width(1.0);
                ctx.stroke_round_rect(BLRect(bounds), 4.0);
            }
            if (onChosen_ && hovered_) {
                BLRgba32 tint = newui::UIColorManager::colorFor(newui::UIColorRole::HighlightBackground).toBLRgba32();
                tint.setA(70);
                ctx.set_fill_style(tint);
                ctx.fill_round_rect(BLRect(bounds), 4.0);
            }
            ctx.restore();
        }

    private:
        newui::SyncReturn handleEntered(newui::View&, const newui::Point&, std::uint32_t, std::uint32_t)
        {
            setHovered(true);
            return newui::SyncReturn::Handled;
        }

        newui::SyncReturn handleLeft(newui::View&, const newui::Point&, std::uint32_t, std::uint32_t)
        {
            setHovered(false);
            return newui::SyncReturn::Handled;
        }

        void setHovered(bool hovered)
        {
            if (hovered_ != hovered) {
                hovered_ = hovered;
                style().markDirty();
            }
        }

        newui::SyncReturn handleMouseDown(newui::View&, const newui::Point&, std::uint32_t, std::uint32_t)
        {
            if (onChosen_) {
                onChosen_();
            }
            return newui::SyncReturn::Handled;
        }

        std::function<void()> onChosen_;
        newui::Label* label_ = nullptr;
        bool hovered_ = false;
    };
}
