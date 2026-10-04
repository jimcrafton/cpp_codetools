#pragma once

#include "MinimapStrip.h"
#include "ProblemList.h"

#include <newui/controls.h>
#include <newui/subview.h>
#include <newui/textfolding.h>
#include <newui/view.h>

#include <functional>
#include <string>
#include <vector>

namespace CodeToolsVsix
{
    // The C++ editor's status row: the caret's position on the left and, while the text has problems,
    // `<  2 of 5 problems  >` on the right - the arrows step the caret through them (next / previous
    // line with a squiggle, wrapping), and "2 of 5" says which one the caret is on. It also keeps the
    // minimap's problem ticks (MarksSink) in step. Added to `host` as one more child, so create it before
    // anything that must stay last (the Find overlays). UI thread only.
    class EditorStatusBar
    {
    public:
        static constexpr float kHeight = 22.0f;
        static constexpr float kArrowWidth = 28.0f;
        static constexpr float kSummaryWidth = 150.0f;

        // Moves the caret to a 1-based line and column and brings it into view.
        using GoTo = std::function<void(std::size_t line, std::size_t column)>;
        // Where the problem ticks go (the minimap).
        using MarksSink = std::function<void(std::vector<MinimapStrip::Mark> marks)>;

        EditorStatusBar(newui::View& host, newui::TextFoldingControl& text);
        ~EditorStatusBar();

        void setGoTo(GoTo goTo) { goTo_ = std::move(goTo); }
        void setMarksSink(MarksSink sink) { marksSink_ = std::move(sink); }

        // The problems may have changed (a highlight pass landed): collect them again from the text
        // control's styled ranges, and refresh everything shown.
        void refresh();
        // The caret moved (or the text changed under it): the position and which problem it is on.
        void caretMoved();

        // The caret to the next / previous line with a problem, wrapping. No-op without any.
        void goToNextProblem();
        void goToPreviousProblem();

        newui::SubView* row() const { return row_; }
        const std::vector<Problem>& problems() const { return problems_; }
        // What the labels say - for tests and for hosts that want it.
        std::string positionText() const { return position_->text(); }
        std::string summaryText() const { return summary_->text(); }
        bool navigatorVisible() const { return summary_->isVisible(); }

        // "Ln 6, Col 10" for a caret at offset in text (1-based, like the editors it sits beside).
        static std::string positionFor(const std::wstring& text, std::size_t offset);

    private:
        // The inline message under the pointer is shown in full in place of the position; a click on
        // it puts the caret where the problem is.
        newui::SyncReturn annotationHovered(newui::TextController& sender, std::size_t index);
        newui::SyncReturn annotationClicked(newui::TextController& sender, std::size_t index);
        std::size_t caretOffset() const;
        newui::Label* makeArrow(const std::string& glyph, bool forward);
        void updateNavigator();
        void publishMarks();
        newui::Color markColor(bool isError) const;

        newui::TextFoldingControl& text_;
        newui::SubView* row_ = nullptr;
        newui::Label* position_ = nullptr;
        newui::Label* previous_ = nullptr;
        newui::Label* summary_ = nullptr;
        newui::Label* next_ = nullptr;

        newui::Connection clickConnection_;
        newui::Connection hoverConnection_;
        std::vector<Problem> problems_;
        GoTo goTo_;
        MarksSink marksSink_;
    };
}
