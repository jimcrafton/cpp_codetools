#pragma once

#include <newui/runloop.h>
#include <newui/textfolding.h>
#include <newui/textstyle.h>

#include <lex/highlight.h>

#include <any>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace CodeToolsVsix
{
    // ---- the lex -> newui bridge, shared by every highlighted editor -------------------------------

    // The TextStyleSheet style name for a lex style (nullptr: drawn in the control's own look).
    const char* highlightStyleName(lex::StyleId style);

    // The squiggle styles' names (see highlightStyleSheet()): red for a problem, green for a warning.
    constexpr const char* kProblemStyleName = "problem";
    constexpr const char* kWarningStyleName = "warning";

    // lex's theme as a sheet: one style per highlightStyleName(), plus "problem" and "warning"
    // (squiggles).
    std::shared_ptr<const newui::TextStyleSheet> highlightStyleSheet(const lex::Theme& theme);

    // A squiggle (kProblemStyleName, or kWarningStyleName) over [start, start + length) of a text
    // textSize long - at least one character, so a zero-length problem (say "unexpected end of
    // input") still shows, on the last character when it is at the very end. Nothing for an empty
    // text.
    void addProblemRange(std::vector<newui::text::TextStyleRange>& ranges, std::size_t textSize,
        std::size_t start, std::size_t length, const char* style = kProblemStyleName);

    // Appends the colored runs `language` finds in text, and a squiggle for each problem token
    // (an error, or a malformed / unterminated one). Whole-document; thread-safe.
    void appendStyleRanges(const lex::Language& language, const std::wstring& text,
        std::vector<newui::text::TextStyleRange>& ranges);

    // Ranges computed for oldText, moved to where they are in newText. The difference is taken as
    // one edit (the text between their shared start and shared end): ranges wholly before it stay,
    // ranges wholly after it move by the change in length, and ranges touching it are dropped.
    std::vector<newui::text::TextStyleRange> shiftRangesThroughEdit(
        const std::vector<newui::text::TextStyleRange>& ranges,
        const newui::text::PieceTree& oldText, const newui::text::PieceTree& newText);

    // ---- the background passes ---------------------------------------------------------------------

    // What one pass over a text snapshot produces - plain data, built on a worker thread.
    struct HighlightResult
    {
        std::vector<newui::text::TextStyleRange> ranges;
        std::vector<newui::text::TextFold> folds;   // none collapsed
        // False when the text couldn't be analyzed well enough to say (say, it doesn't parse):
        // the control then keeps the folds it has, which it moves along with each edit.
        bool foldsValid = true;
        // Whatever else the language's owner wants back on the UI thread (a parse tree, ...); see
        // HighlightController::setOnApplied().
        std::any extra;
    };

    // Turns a text into a result. Runs on a worker thread: no UI state, thread-safe.
    using HighlightAnalyzer = std::function<HighlightResult(const std::wstring& text)>;

    // What the slower second pass produces: styled ranges (squiggles from a real parse) that are
    // shown on top of the first pass's, and whatever else the owner wants (an outline, ...).
    struct HighlightOverlay
    {
        std::vector<newui::text::TextStyleRange> ranges;
        std::any extra;
    };
    using OverlayAnalyzer = std::function<HighlightOverlay(const std::wstring& text)>;

    // Keeps a TextFoldingControl highlighted: after each edit, once typing pauses, it hands an O(1)
    // snapshot of the text to a worker running the analyzer, and applies the result on the run loop
    // - the colors and squiggles, and the folds (keeping collapsed the ones still there). A result
    // for text that has since changed is dropped and one worker runs at a time. With no run loop
    // running (tests) it analyzes and applies right away instead.
    //
    // An optional second pass (setOverlayAnalyzer) does the same on its own, longer pause, for what
    // costs more than lexing. Its ranges are kept apart from the first pass's, so neither pass
    // overwrites the other: the overlay is moved along through edits made since it was computed
    // (see shiftRangesThroughEdit()), and the first pass re-publishes it with each new set of colors.
    //
    // Watches control.model() as it is when this is constructed - install the model you want
    // (HistoryTextModel) first. UI thread only. Safe to destroy at any time: a worker still running
    // finds the controller gone and does nothing.
    class HighlightController
    {
    public:
        HighlightController(newui::TextFoldingControl& control, HighlightAnalyzer analyzer);
        ~HighlightController();
        HighlightController(const HighlightController&) = delete;
        HighlightController& operator=(const HighlightController&) = delete;

        // Called on the UI thread after each result is applied; the handler may move the result's
        // extra out.
        void setOnApplied(std::function<void(HighlightResult&)> handler) { onApplied_ = std::move(handler); }

        // How long typing must pause before the first pass starts (default 50 ms).
        void setDelay(std::chrono::milliseconds delay) { delays_[0] = delay; }

        // The second pass, and how long typing must pause before it starts (default 600 ms).
        void setOverlayAnalyzer(OverlayAnalyzer analyzer, std::chrono::milliseconds delay = std::chrono::milliseconds(600));
        // Called on the UI thread after each overlay is applied; the handler may move its extra out.
        void setOnOverlayApplied(std::function<void(HighlightOverlay&)> handler) { onOverlayApplied_ = std::move(handler); }

        // Run every pass on the current text and apply the results now, on this thread.
        void refresh();

    private:
        static constexpr int kColorPass = 0;
        static constexpr int kOverlayPass = 1;
        static constexpr int kPasses = 2;

        struct PassState
        {
            bool running = false;   // a worker is in flight
            bool dirty = false;     // the text changed again while it ran
        };
        // Shared with the workers' completion tasks, so they can tell the controller is gone.
        struct State
        {
            bool alive = true;
            std::size_t generation = 0;   // bumped on every text change
            PassState passes[kPasses];
        };

        void handleTextChanged();
        void schedule(int pass, newui::RunLoop& loop);
        void startPass(int pass, newui::RunLoop& loop);
        void applyColors(HighlightResult&& result, const newui::text::PieceTree& text, std::size_t generation);
        void applyOverlay(HighlightOverlay&& overlay, const newui::text::PieceTree& text, std::size_t generation);
        // The first pass's ranges and the overlay's together.
        void publishRanges();

        newui::TextFoldingControl& control_;
        HighlightAnalyzer analyzer_;
        OverlayAnalyzer overlayAnalyzer_;
        std::function<void(HighlightResult&)> onApplied_;
        std::function<void(HighlightOverlay&)> onOverlayApplied_;
        std::chrono::milliseconds delays_[kPasses] = { std::chrono::milliseconds(50), std::chrono::milliseconds(600) };
        std::shared_ptr<State> state_;
        newui::RunLoop* loop_ = nullptr;
        newui::RunLoop::TimerHandle timers_[kPasses] = { newui::RunLoop::kInvalidTimerHandle, newui::RunLoop::kInvalidTimerHandle };

        std::vector<newui::text::TextStyleRange> colors_;    // the first pass's, for the text as of colorsGeneration_
        std::size_t colorsGeneration_ = static_cast<std::size_t>(-1);
        std::vector<newui::text::TextStyleRange> overlay_;   // the second pass's, for overlayText_
        newui::text::PieceTree overlayText_;
    };
}
