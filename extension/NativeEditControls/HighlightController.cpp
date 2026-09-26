#include "HighlightController.h"

#include <newui/uicolormanager.h>

#include <algorithm>
#include <thread>

namespace CodeToolsVsix
{
    namespace
    {
        // lex's colors are 0xAARRGGBB.
        newui::Color colorFromArgb(std::uint32_t argb)
        {
            return newui::Color(((argb >> 16) & 0xFF) / 255.0f, ((argb >> 8) & 0xFF) / 255.0f,
                (argb & 0xFF) / 255.0f, ((argb >> 24) & 0xFF) / 255.0f);
        }
    }

    const char* highlightStyleName(lex::StyleId style)
    {
        switch (style) {
        case lex::StyleId::Comment: return "comment";
        case lex::StyleId::Keyword: return "keyword";
        case lex::StyleId::Constant: return "constant";
        case lex::StyleId::String: return "string";
        case lex::StyleId::Number: return "number";
        case lex::StyleId::Operator: return "operator";
        case lex::StyleId::Punctuation: return "punctuation";
        case lex::StyleId::Preprocessor: return "preprocessor";
        case lex::StyleId::Identifier: return "identifier";
        case lex::StyleId::PropertyName: return "propertyName";
        case lex::StyleId::Error: return "error";
        default: return nullptr;   // Default and Count: the control's own look
        }
    }

    std::shared_ptr<const newui::TextStyleSheet> highlightStyleSheet(const lex::Theme& theme)
    {
        auto sheet = std::make_shared<newui::TextStyleSheet>();
        for (std::size_t i = 0; i < lex::kStyleCount; ++i) {
            const lex::StyleId id = static_cast<lex::StyleId>(i);
            const char* name = highlightStyleName(id);
            if (name == nullptr) {
                continue;
            }
            const lex::TextStyle& look = theme.style(id);
            auto* style = new newui::TextStyle(name);
            style->setColor(colorFromArgb(look.foreground));
            if ((look.background >> 24) != 0) {
                style->setBackgroundColor(colorFromArgb(look.background));
            }
            style->setBold((look.font & lex::FontFlag_Bold) != 0);
            style->setItalic((look.font & lex::FontFlag_Italic) != 0);
            style->setUnderline((look.font & lex::FontFlag_Underline) != 0);
            sheet->addStyle(style);
        }
        auto* problem = new newui::TextStyle(kProblemStyleName);
        problem->setDecoration(newui::text::TextDecorationKind::Squiggle);
        problem->setDecorationColor(colorFromArgb(theme.problemUnderline));
        sheet->addStyle(problem);

        // Green, a shade that reads on the theme's background.
        const std::uint32_t background = theme.background;
        const unsigned brightness = (((background >> 16) & 0xFF) + ((background >> 8) & 0xFF) + (background & 0xFF)) / 3;
        auto* warning = new newui::TextStyle(kWarningStyleName);
        warning->setDecoration(newui::text::TextDecorationKind::Squiggle);
        warning->setDecorationColor(colorFromArgb(brightness < 128 ? 0xFF6CC070u : 0xFF2E8B2Eu));
        sheet->addStyle(warning);
        return sheet;
    }

    void addProblemRange(std::vector<newui::text::TextStyleRange>& ranges, std::size_t textSize,
        std::size_t start, std::size_t length, const char* style)
    {
        if (textSize == 0) {
            return;
        }
        if (length == 0) {
            length = 1;
            start = start < textSize ? start : textSize - 1;
        }
        ranges.push_back({ start, length, style });
    }

    void appendStyleRanges(const lex::Language& language, const std::wstring& text,
        std::vector<newui::text::TextStyleRange>& ranges)
    {
        lex::SyntaxHighlighter highlighter(language);
        highlighter.setText(text);
        for (std::size_t line = 0; line < highlighter.lineCount(); ++line) {
            const std::size_t lineStart = highlighter.lineStart(line);
            for (const lex::HighlightSpan& span : highlighter.spans(line)) {
                if (span.isProblem()) {
                    addProblemRange(ranges, text.size(), lineStart + span.column, span.length);
                }
                const char* name = highlightStyleName(span.style);
                if (name != nullptr && span.length != 0) {
                    ranges.push_back({ lineStart + span.column, span.length, name });
                }
            }
        }
    }

    std::vector<newui::text::TextStyleRange> shiftRangesThroughEdit(
        const std::vector<newui::text::TextStyleRange>& ranges,
        const newui::text::PieceTree& oldText, const newui::text::PieceTree& newText)
    {
        const std::size_t oldLength = oldText.length();
        const std::size_t newLength = newText.length();
        const std::size_t shorter = oldLength < newLength ? oldLength : newLength;
        const std::size_t prefix = oldText.commonPrefixLength(newText);
        const std::size_t suffix = oldText.commonSuffixLength(newText, shorter - prefix);
        const std::size_t editEnd = oldLength - suffix;   // in the old text
        const std::ptrdiff_t change = static_cast<std::ptrdiff_t>(newLength) - static_cast<std::ptrdiff_t>(oldLength);

        std::vector<newui::text::TextStyleRange> shifted;
        shifted.reserve(ranges.size());
        for (const newui::text::TextStyleRange& range : ranges) {
            const std::size_t end = range.start + range.length;
            if (end <= prefix) {
                shifted.push_back(range);
            } else if (range.start >= editEnd) {
                newui::text::TextStyleRange moved = range;
                moved.start = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(range.start) + change);
                shifted.push_back(std::move(moved));
            }   // else it touches the edit: dropped
        }
        return shifted;
    }

    HighlightController::HighlightController(newui::TextFoldingControl& control, HighlightAnalyzer analyzer)
        : control_(control), analyzer_(std::move(analyzer)), state_(std::make_shared<State>())
    {
        // Typing and setText() both change the model. The lambda holds the state, not just this, so
        // it stays harmless after the controller is gone.
        control_.model().onChanged.add([this, state = state_](newui::Model&) {
            if (state->alive) {
                handleTextChanged();
            }
            return newui::SyncReturn::Ignored;
        });
    }

    HighlightController::~HighlightController()
    {
        state_->alive = false;
        for (newui::RunLoop::TimerHandle& timer : timers_) {
            if (timer != newui::RunLoop::kInvalidTimerHandle) {
                loop_->cancelDelayed(timer);
            }
        }
    }

    void HighlightController::setOverlayAnalyzer(OverlayAnalyzer analyzer, std::chrono::milliseconds delay)
    {
        overlayAnalyzer_ = std::move(analyzer);
        delays_[kOverlayPass] = delay;
    }

    void HighlightController::handleTextChanged()
    {
        ++state_->generation;
        newui::RunLoop& loop = newui::RunLoop::current();
        if (!loop) {
            refresh();
            return;
        }
        // Until a pass lands the control keeps what it has - colors, squiggles and folds moved along
        // with the edit.
        schedule(kColorPass, loop);
        if (overlayAnalyzer_) {
            schedule(kOverlayPass, loop);
        }
    }

    void HighlightController::schedule(int pass, newui::RunLoop& loop)
    {
        if (timers_[pass] != newui::RunLoop::kInvalidTimerHandle) {
            loop_->cancelDelayed(timers_[pass]);
        }
        loop_ = &loop;
        timers_[pass] = loop.postDelayed(delays_[pass], [this, pass]() {
            timers_[pass] = newui::RunLoop::kInvalidTimerHandle;
            startPass(pass, *loop_);
            return true;   // once
        });
    }

    void HighlightController::refresh()
    {
        const newui::text::PieceTree text = control_.model().snapshot();
        const std::size_t generation = state_->generation;
        applyColors(analyzer_(text.str()), text, generation);
        if (overlayAnalyzer_) {
            applyOverlay(overlayAnalyzer_(text.str()), text, generation);
        }
    }

    void HighlightController::startPass(int pass, newui::RunLoop& loop)
    {
        PassState& passState = state_->passes[pass];
        if (passState.running) {
            passState.dirty = true;
            return;
        }
        passState.running = true;

        // An O(1) snapshot on this thread; the worker builds the string from it. Once the pass is
        // done, back on the loop: nothing to do if the controller is gone, and a result for text
        // that has changed since is dropped (the pass runs again if it was asked to meanwhile).
        const std::size_t generation = state_->generation;
        const newui::text::PieceTree snapshot = control_.model().snapshot();
        auto finish = [this, pass, state = state_, generation](const std::function<void()>& applyResult) {
            state->passes[pass].running = false;
            if (!state->alive) {
                return;
            }
            if (generation == state->generation) {
                applyResult();
            }
            if (state->passes[pass].dirty) {
                state->passes[pass].dirty = false;
                startPass(pass, newui::RunLoop::current());
            }
        };

        if (pass == kColorPass) {
            std::thread([this, finish, loop = &loop, analyzer = analyzer_, snapshot, generation]() {
                auto result = std::make_shared<HighlightResult>(analyzer(snapshot.str()));
                loop->post([this, finish, result, snapshot, generation]() {
                    finish([&]() { applyColors(std::move(*result), snapshot, generation); });
                });
            }).detach();
        } else {
            std::thread([this, finish, loop = &loop, analyzer = overlayAnalyzer_, snapshot, generation]() {
                auto result = std::make_shared<HighlightOverlay>(analyzer(snapshot.str()));
                loop->post([this, finish, result, snapshot, generation]() {
                    finish([&]() { applyOverlay(std::move(*result), snapshot, generation); });
                });
            }).detach();
        }
    }

    void HighlightController::applyColors(HighlightResult&& result, const newui::text::PieceTree& text, std::size_t generation)
    {
        // lex's light and dark themes as style sheets, built once; which one follows the system.
        static const std::shared_ptr<const newui::TextStyleSheet> lightSheet = highlightStyleSheet(lex::Theme::light());
        static const std::shared_ptr<const newui::TextStyleSheet> darkSheet = highlightStyleSheet(lex::Theme::dark());
        const std::shared_ptr<const newui::TextStyleSheet>& sheet = newui::UIColorManager::isDarkMode() ? darkSheet : lightSheet;

        if (control_.styleSheet() != sheet) {
            control_.setStyleSheet(sheet);
        }

        // The overlay was computed for an older text: bring it up to this one, and show both.
        overlay_ = shiftRangesThroughEdit(overlay_, overlayText_, text);
        overlayText_ = text;
        colors_ = std::move(result.ranges);
        colorsGeneration_ = generation;
        publishRanges();

        if (result.foldsValid) {
            std::vector<newui::text::TextFold> folds = std::move(result.folds);
            const std::vector<newui::text::TextFold>& current = control_.folds();
            for (newui::text::TextFold& fold : folds) {
                fold.collapsed = std::any_of(current.begin(), current.end(), [&](const newui::text::TextFold& old) {
                    return old.collapsed && old.start == fold.start;
                });
            }
            std::sort(folds.begin(), folds.end(), [](const newui::text::TextFold& a, const newui::text::TextFold& b) {
                return a.start != b.start ? a.start < b.start : a.length > b.length;
            });
            if (folds != current) {
                control_.setFolds(std::move(folds));
            }
        }

        if (onApplied_) {
            onApplied_(result);
        }
    }

    void HighlightController::applyOverlay(HighlightOverlay&& overlay, const newui::text::PieceTree& text, std::size_t generation)
    {
        overlay_ = std::move(overlay.ranges);
        overlayText_ = text;
        // Show it now if the colors on screen are for this very text; otherwise the next first pass
        // (already on its way) publishes it.
        if (colorsGeneration_ == generation) {
            publishRanges();
        }
        if (onOverlayApplied_) {
            onOverlayApplied_(overlay);
        }
    }

    void HighlightController::publishRanges()
    {
        std::vector<newui::text::TextStyleRange> ranges = colors_;
        ranges.insert(ranges.end(), overlay_.begin(), overlay_.end());
        control_.setStyledRanges(std::move(ranges));
    }
}
