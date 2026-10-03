#include "EditorStatusBar.h"
#include "FindEngine.h"
#include "HighlightController.h"

#include <newui/layout.h>
#include <newui/textstyle.h>
#include <newui/uicolormanager.h>

namespace CodeToolsVsix
{
    EditorStatusBar::EditorStatusBar(newui::View& host, newui::TextFoldingControl& text)
        : text_(text)
    {
        row_ = new newui::SubView();
        row_->setName("cppEditorStatusRow");
        row_->setVisible(true);
        row_->setDesiredSize(newui::Size(0.0f, kHeight));
        row_->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(0.0f));
        row_->setLayout(std::make_unique<newui::FlexLayout>(newui::Orientation::Horizontal));
        // The same accent bar the Designer's own status bar is (HighlightBackground / HighlightText).
        row_->style().setBackgroundColor(newui::UIColorManager::colorFor(newui::UIColorRole::HighlightBackground));
        host.addChild(row_);

        const BLRgba32 textColor = newui::UIColorManager::colorFor(newui::UIColorRole::HighlightText).toBLRgba32();

        position_ = new newui::Label();
        position_->setName("cppEditorStatusPosition");
        position_->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
        position_->setTextAlignment(newui::TextAlignment::Left);
        position_->setTextColor(textColor);
        row_->addChild(position_);

        previous_ = makeArrow("\xE2\x80\xB9", false);   // single left-pointing angle quotation mark
        row_->addChild(previous_);

        summary_ = new newui::Label();
        summary_->setName("cppEditorStatusProblems");
        summary_->setDesiredSize(newui::Size(kSummaryWidth, kHeight));
        summary_->setTextAlignment(newui::TextAlignment::Center);
        summary_->setTextColor(textColor);
        row_->addChild(summary_);

        next_ = makeArrow("\xE2\x80\xBA", true);        // single right-pointing angle quotation mark
        row_->addChild(next_);

        updateNavigator();
        caretMoved();
    }

    newui::Label* EditorStatusBar::makeArrow(const std::string& glyph, bool forward)
    {
        auto* arrow = new newui::Label();
        arrow->setName(forward ? "cppEditorNextProblem" : "cppEditorPreviousProblem");
        arrow->setText(glyph);
        arrow->setDesiredSize(newui::Size(kArrowWidth, kHeight));
        arrow->setTextAlignment(newui::TextAlignment::Center);
        arrow->setTextColor(newui::UIColorManager::colorFor(newui::UIColorRole::HighlightText).toBLRgba32());
        arrow->setCursor(newui::Cursor(newui::CursorKind::Hand));
        arrow->onMouseDown.add([this, forward](newui::View&, const newui::Point&, std::uint32_t, std::uint32_t) {
            if (forward) {
                goToNextProblem();
            } else {
                goToPreviousProblem();
            }
            return newui::SyncReturn::Handled;
        });
        return arrow;
    }

    std::string EditorStatusBar::positionFor(const std::wstring& text, std::size_t offset)
    {
        const std::size_t line = lineOfOffset(text, offset);
        const std::size_t column = offset - offsetOfLine(text, line, 1) + 1;
        return "  Ln " + std::to_string(line) + ", Col " + std::to_string(column);
    }

    std::size_t EditorStatusBar::caretOffset() const
    {
        const newui::text::TextPosition position = text_.caret().position();
        return position.isValid() ? position.offset() : 0;
    }

    void EditorStatusBar::refresh()
    {
        problems_ = collectProblems(text_.styledRanges(), text_.text());
        updateNavigator();
        publishMarks();
    }

    void EditorStatusBar::caretMoved()
    {
        position_->setText(positionFor(text_.text(), caretOffset()));
        updateNavigator();
        publishMarks();   // which tick is the current one
    }

    void EditorStatusBar::updateNavigator()
    {
        const std::optional<std::size_t> current = problemOnLine(problems_, lineOfOffset(text_.text(), caretOffset()) - 1);
        summary_->setText(problemSummary(problems_.size(), current));
        const bool any = !problems_.empty();
        previous_->setVisible(any);
        summary_->setVisible(any);
        next_->setVisible(any);
        row_->style().markDirty();
    }

    newui::Color EditorStatusBar::markColor(bool isError) const
    {
        // The squiggle's own color in the active theme; a plain red / green until a sheet is applied.
        const std::shared_ptr<const newui::TextStyleSheet>& sheet = text_.styleSheet();
        const newui::TextStyle* style = sheet != nullptr ? sheet->style(isError ? kProblemStyleName : kWarningStyleName) : nullptr;
        if (style != nullptr && !style->decorationColor().isNull()) {
            return style->decorationColor();
        }
        return isError ? newui::Color(0xC4 / 255.0f, 0x2B / 255.0f, 0x1C / 255.0f, 1.0f)
                       : newui::Color(0x2E / 255.0f, 0x8B / 255.0f, 0x2E / 255.0f, 1.0f);
    }

    void EditorStatusBar::publishMarks()
    {
        if (!marksSink_) {
            return;
        }
        const std::optional<std::size_t> current = problemOnLine(problems_, lineOfOffset(text_.text(), caretOffset()) - 1);
        std::vector<MinimapStrip::Mark> marks;
        marks.reserve(problems_.size());
        for (std::size_t i = 0; i < problems_.size(); ++i) {
            MinimapStrip::Mark mark;
            mark.line = problems_[i].line;
            mark.color = markColor(problems_[i].isError);
            mark.current = current.has_value() && *current == i;
            marks.push_back(mark);
        }
        marksSink_(std::move(marks));
    }

    void EditorStatusBar::goToNextProblem()
    {
        const std::optional<std::size_t> target = nextProblem(problems_, lineOfOffset(text_.text(), caretOffset()) - 1);
        if (target.has_value() && goTo_) {
            goTo_(problems_[*target].line + 1, problems_[*target].column + 1);
        }
    }

    void EditorStatusBar::goToPreviousProblem()
    {
        const std::optional<std::size_t> target = previousProblem(problems_, lineOfOffset(text_.text(), caretOffset()) - 1);
        if (target.has_value() && goTo_) {
            goTo_(problems_[*target].line + 1, problems_[*target].column + 1);
        }
    }
}
