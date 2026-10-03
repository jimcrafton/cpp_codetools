#include "IssuesPopup.h"
#include "CalloutPlacement.h"
#include "PaintUtils.h"
#include "PickerRow.h"

#include <newui/application.h>
#include <newui/controls.h>
#include <newui/fontmanager.h>
#include <newui/keyboard_constants.h>
#include <newui/layout.h>
#include <newui/models.h>
#include <newui/rootview.h>
#include <newui/runloop.h>
#include <newui/uicolormanager.h>

#include <memory>

namespace CodeToolsVsix
{
    // A few lines of source with line numbers, the problem's own line marked and the span it points at
    // underlined. Painted by hand in a monospace font; read-only, with no selection or scrolling.
    class CodeExcerptView : public newui::SubView
    {
    public:
        static constexpr float kRowHeight = 22.0f;
        static constexpr float kGutterWidth = 52.0f;
        static constexpr float kFontSize = 13.0f;
        static constexpr float kVerticalPadding = 6.0f;
        static constexpr float kCornerRadius = 6.0f;

        // Rows for the most lines an excerpt ever has, plus the padding - what height to give this view.
        static constexpr float fullHeight()
        {
            return kRowHeight * static_cast<float>(2 * IssuePeek::kContext + 1) + kVerticalPadding * 2.0f;
        }

        CodeExcerptView()
        {
            setVisible(true);
        }

        void setPeek(const IssuePeek& peek)
        {
            peek_ = peek;
            style().markDirty();
        }

        void paint(BLContext& ctx) override
        {
            const newui::Rect bounds = getClientBounds();
            if (bounds.width() <= 0.0f || bounds.height() <= 0.0f) {
                return;
            }

            ctx.save();
            ctx.set_fill_style(newui::UIColorManager::colorFor(newui::UIColorRole::ControlBackground).toBLRgba32());
            ctx.fill_round_rect(BLRect(bounds), kCornerRadius);
            ctx.set_stroke_style(newui::UIColorManager::colorFor(newui::UIColorRole::ControlBorder).toBLRgba32());
            ctx.set_stroke_width(1.0);
            ctx.stroke_round_rect(BLRect(bounds), kCornerRadius);
            ctx.clip_to_rect(BLRect(bounds));

            newui::Font font = newui::FontManager::monospaceFont(kFontSize);
            BLFont* blFont = font.blFont();
            if (blFont == nullptr || !blFont->is_valid() || peek_.excerpt.empty()) {
                ctx.restore();
                return;
            }
            const BLFontMetrics& metrics = blFont->metrics();
            const double textHeight = metrics.ascent + metrics.descent;

            // No UIColorRole is an "error" color; the squiggle and line tint use the same red/amber the
            // editor's own squiggles do.
            const BLRgba32 accent = peek_.isError ? BLRgba32(0xC4, 0x2B, 0x1C) : BLRgba32(0x9D, 0x5D, 0x00);
            const BLRgba32 tint = peek_.isError ? BLRgba32(0xC4, 0x2B, 0x1C, 0x2C) : BLRgba32(0x9D, 0x5D, 0x00, 0x2C);
            const BLRgba32 textColor = newui::UIColorManager::colorFor(newui::UIColorRole::ControlText).toBLRgba32();
            const BLRgba32 numberColor = newui::UIColorManager::colorFor(newui::UIColorRole::DisabledText).toBLRgba32();

            for (std::size_t i = 0; i < peek_.excerpt.size(); ++i) {
                const double top = bounds.top() + kVerticalPadding + kRowHeight * static_cast<double>(i);
                const double baseline = top + (kRowHeight - textHeight) * 0.5 + metrics.ascent;
                const bool own = i == peek_.excerptIndex();

                if (own) {
                    ctx.set_fill_style(tint);
                    ctx.fill_rect(BLRect(bounds.left(), top, bounds.width(), kRowHeight));
                }

                const std::string number = std::to_string(peek_.firstLine + i);
                const double numberWidth = measureTextWidth(*blFont, number);
                ctx.set_fill_style(own ? accent : numberColor);
                ctx.fill_utf8_text(BLPoint(bounds.left() + kGutterWidth - 12.0 - numberWidth, baseline), *blFont,
                                   number.c_str(), number.size());

                const std::string& line = peek_.excerpt[i];
                const double textLeft = bounds.left() + kGutterWidth;
                ctx.set_fill_style(textColor);
                ctx.fill_utf8_text(BLPoint(textLeft, baseline), *blFont, line.c_str(), line.size());

                if (own && peek_.underlineLength > 0 && peek_.underlineStart < line.size()) {
                    const std::string before = line.substr(0, peek_.underlineStart);
                    const std::string span = line.substr(peek_.underlineStart, peek_.underlineLength);
                    const double x0 = textLeft + measureTextWidth(*blFont, before);
                    const double x1 = x0 + measureTextWidth(*blFont, span);
                    paintSquiggle(ctx, x0, x1, baseline + 3.0, accent);
                }
            }
            ctx.restore();
        }

    private:
        // A small zigzag from x0 to x1 along y.
        static void paintSquiggle(BLContext& ctx, double x0, double x1, double y, BLRgba32 color)
        {
            constexpr double kStep = 2.0;
            constexpr double kAmplitude = 1.5;
            BLPath path;
            path.move_to(x0, y);
            bool up = true;
            for (double x = x0 + kStep; x < x1 + kStep; x += kStep) {
                path.line_to(x < x1 ? x : x1, up ? y - kAmplitude : y + kAmplitude);
                up = !up;
            }
            ctx.set_stroke_style(color);
            ctx.set_stroke_width(1.0);
            ctx.stroke_path(path);
        }

        IssuePeek peek_;
    };

    namespace
    {
        // What the popup's views and handlers share. The views are owned by the popup; the handlers
        // only run while it is alive.
        struct PopupState
        {
            std::vector<IssuePeek> issues;
            std::function<void(std::size_t)> onOpen;
            newui::PopupTool* popup = nullptr;
            newui::ListView* list = nullptr;
            CodeExcerptView* excerpt = nullptr;
            newui::Label* counter = nullptr;
            std::size_t index = 0;

            void refresh()
            {
                if (index >= issues.size()) {
                    return;
                }
                excerpt->setPeek(issues[index]);
                counter->setText(std::to_string(index + 1) + " of " + std::to_string(issues.size()));
                popup->markDirty();
                popup->repaintNow();
            }

            void step(int delta)
            {
                if (issues.empty()) {
                    return;
                }
                const std::size_t count = issues.size();
                const std::size_t next = delta > 0 ? (index + 1) % count : (index + count - 1) % count;
                list->setSelectedIndex(next);   // its selection handler does the rest
            }

            void open()
            {
                std::function<void(std::size_t)> callback = onOpen;
                const std::size_t chosen = index;
                newui::PopupTool* closing = popup;
                closing->dismiss();   // may free this state's owner closure: locals first, then nothing of ours
                auto run = [callback, chosen]() {
                    if (callback) {
                        callback(chosen);
                    }
                };
                // After the popup is gone: this runs inside the popup's own click dispatch.
                if (newui::RunLoop::current()) {
                    newui::RunLoop::current().post(std::move(run));
                } else {
                    run();
                }
            }
        };
    }

    newui::PopupTool* IssuesPopup::show(newui::View& owner, const newui::Rect& anchorScreenRect,
                                        const std::string& title, std::vector<IssuePeek> issues,
                                        std::function<void(std::size_t)> onOpen)
    {
        newui::RootView* rootView = owner.rootView();
        if (rootView == nullptr || rootView->windowHandle() == nullptr || issues.empty()) {
            return nullptr;
        }

        const std::size_t listRows = issues.size() < kMaxVisibleRows ? issues.size() : kMaxVisibleRows;
        const float listHeight = kRowHeight * static_cast<float>(listRows);
        const float innerHeight = kHeaderHeight + kGap + listHeight + kGap + CodeExcerptView::fullHeight() + kGap +
                                  kFooterHeight;
        const float height = PickerRow::kTopReserve + kPadding * 2.0f + innerHeight;
        const CalloutPlacement placement =
            placeCallout(anchorScreenRect, newui::Size(kWidth, height), designerWindowScreenRect(&owner));

        auto* popup = new newui::CalloutTool(rootView->windowHandle(), newui::Application::instance().instanceHandle(),
                                             placement.bounds, "issuesPopup");
        if (!popup->initialize()) {
            delete popup;
            return nullptr;
        }
        popup->setTailSide(placement.tailSide);
        popup->setTailPosition(placement.tailPosition);

        auto state = std::make_shared<PopupState>();
        state->popup = popup;
        state->onOpen = std::move(onOpen);
        state->issues = std::move(issues);

        // The reserved band is on the tail's side: above everything for a top tail, below it for a
        // bottom one (a popup opened above its anchor).
        const newui::Size actual = placement.bounds.size();
        const float contentLeft = kPadding;
        const float contentWidth = actual.width - kPadding * 2.0f;
        float y = placement.tailSide == newui::shapes::TailSide::Bottom ? kPadding : PickerRow::kTopReserve + kPadding;

        // Header: what the problems are in, and `< 1 of N >`.
        auto* heading = new newui::Label();
        heading->setVisible(true);
        heading->setText(std::to_string(state->issues.size()) + (state->issues.size() == 1 ? " problem in " : " problems in ") + title);
        heading->setTextAlignment(newui::TextAlignment::Left);
        const float navWidth = kNavButtonWidth * 2.0f + 70.0f;
        heading->setBounds(newui::Rect(contentLeft, y, contentWidth - navWidth, kHeaderHeight));
        popup->addChild(heading);

        auto* previous = new newui::Button();
        previous->setVisible(true);
        previous->setText("<");
        previous->setBounds(newui::Rect(contentLeft + contentWidth - navWidth, y, kNavButtonWidth, kHeaderHeight));
        popup->addChild(previous);

        state->counter = new newui::Label();
        state->counter->setVisible(true);
        state->counter->setBounds(newui::Rect(contentLeft + contentWidth - navWidth + kNavButtonWidth, y, 70.0f, kHeaderHeight));
        popup->addChild(state->counter);

        auto* next = new newui::Button();
        next->setVisible(true);
        next->setText(">");
        next->setBounds(newui::Rect(contentLeft + contentWidth - kNavButtonWidth, y, kNavButtonWidth, kHeaderHeight));
        popup->addChild(next);
        y += kHeaderHeight + kGap;

        // The problems, a few rows at a time.
        auto model = std::make_unique<newui::StringListModel>();
        for (const IssuePeek& issue : state->issues) {
            model->addItem(issue.summary);
        }
        state->list = new newui::ListView();
        state->list->setVisible(true);
        state->list->setModel(std::move(model));
        state->list->setRowHeight(kRowHeight);
        auto* scroll = new newui::ScrollView();
        scroll->setVisible(true);
        scroll->addChild(state->list);
        scroll->setBounds(newui::Rect(contentLeft, y, contentWidth, listHeight));
        popup->addChild(scroll);
        y += listHeight + kGap;

        // The selected problem's source.
        state->excerpt = new CodeExcerptView();
        state->excerpt->setBounds(newui::Rect(contentLeft, y, contentWidth, CodeExcerptView::fullHeight()));
        popup->addChild(state->excerpt);
        y += CodeExcerptView::fullHeight() + kGap;

        auto* openButton = new newui::Button();
        openButton->setVisible(true);
        openButton->setText("Open in editor");
        openButton->setBounds(newui::Rect(contentLeft + contentWidth - kOpenButtonWidth, y, kOpenButtonWidth, kFooterHeight));
        popup->addChild(openButton);

        // Wired last, so building the views above doesn't fire any of it.
        state->list->onSelectionChanged.add([state](newui::ListView& sender) {
            const std::optional<std::size_t> selected = sender.selectedIndex();
            if (selected.has_value() && *selected < state->issues.size()) {
                state->index = *selected;
                state->refresh();
            }
            return newui::SyncReturn::Handled;
        });
        previous->onClick.add([state](newui::Control&) {
            state->step(-1);
            return newui::SyncReturn::Handled;
        });
        next->onClick.add([state](newui::Control&) {
            state->step(1);
            return newui::SyncReturn::Handled;
        });
        openButton->onClick.add([state](newui::Control&) {
            state->open();
            return newui::SyncReturn::Handled;
        });
        popup->onKeyDown.add([state](newui::View&, std::uint32_t, int, int, std::uint32_t keyCode) {
            switch (keyCode) {
            case newui::vkReturn:
                state->open();
                return newui::SyncReturn::Handled;
            case newui::vkLeftArrow:
            case newui::vkUpArrow:
                state->step(-1);
                return newui::SyncReturn::Handled;
            case newui::vkRightArrow:
            case newui::vkDownArrow:
                state->step(1);
                return newui::SyncReturn::Handled;
            default:
                return newui::SyncReturn::Ignored;
            }
        });

        state->list->setSelectedIndex(0);   // shows the first problem's source
        popup->present();
        return popup;
    }
}
