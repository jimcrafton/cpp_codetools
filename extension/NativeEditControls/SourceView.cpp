#include "SourceView.h"
#include "TextEncoding.h"

#include <newui/font.h>
#include <newui/layout.h>
#include <newui/uicolormanager.h>

#include <lex/json5_language.h>
#include <lex/json5_parser.h>

#include <memory>

namespace CodeToolsVsix
{
    namespace
    {
        constexpr float kErrorBarHeight = 26.0f;

        // lex's colors are 0xAARRGGBB.
        newui::Color colorFromArgb(std::uint32_t argb)
        {
            return newui::Color(((argb >> 16) & 0xFF) / 255.0f, ((argb >> 8) & 0xFF) / 255.0f,
                (argb & 0xFF) / 255.0f, ((argb >> 24) & 0xFF) / 255.0f);
        }
    }

    SourceView::SourceView() : highlighter_(lex::json5Language())
    {
        setName("designSource");
        setVisible(true);
        setLayout(std::make_unique<newui::FlexLayout>(newui::Orientation::Vertical));
        style().setBackgroundColor(newui::UIColorManager::colorFor(newui::UIColorRole::WindowBackground));

        errorBar_ = new newui::Label();
        errorBar_->setName("designSourceError");
        errorBar_->setVisible(false);
        errorBar_->setDesiredSize(newui::Size(0.0f, kErrorBarHeight));
        errorBar_->style().setBackgroundColor(newui::Color(0xF8D7DAu, false));   // a pale red, readable in both themes
        errorBar_->setTextColor(BLRgba32(0xFF842029u));
        addChild(errorBar_);

        scrollView_ = new newui::ScrollView();
        scrollView_->setName("designSourceScroll");
        scrollView_->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
        addChild(scrollView_);

        // Hosted by the ScrollView, which scrolls it (a TextControl has no scrollbar of its own).
        textControl_ = new newui::TextControl();
        textControl_->setName("designSourceText");
        textControl_->setFont(newui::Font("Consolas", 12.0f));
        scrollView_->addChild(textControl_);

        // Typing and setText() both change the model - recolor after either.
        textControl_->model().onChanged.add(this, &SourceView::handleTextChanged);
    }

    newui::SyncReturn SourceView::handleTextChanged(newui::Model& /*sender*/)
    {
        highlight();
        return newui::SyncReturn::Ignored;
    }

    void SourceView::highlight()
    {
        // Whole-document for now; SyntaxHighlighter::replace() can make it incremental later.
        const std::wstring text = textControl_->text();
        highlighter_.setText(text);
        const lex::Theme theme = newui::UIColorManager::isDarkMode() ? lex::Theme::dark() : lex::Theme::light();
        const newui::Color problemColor = colorFromArgb(theme.problemUnderline);

        // A squiggle for [start, start + length) - at least one character, so a zero-length error
        // (e.g. "unexpected end of input") still shows; at the very end, on the last character.
        std::vector<newui::text::TextDecoration> problems;
        auto squiggle = [&](std::size_t start, std::size_t length) {
            if (text.empty()) {
                return;
            }
            if (length == 0) {
                length = 1;
                start = start < text.size() ? start : text.size() - 1;
            }
            newui::text::TextDecoration decoration;
            decoration.start = start;
            decoration.length = length;
            decoration.kind = newui::text::TextDecorationKind::Squiggle;
            decoration.color = problemColor;
            problems.push_back(decoration);
        };
        for (const lex::json5::ParseError& error : lex::json5::parse(text).errors) {
            squiggle(error.offset, error.length);
        }

        std::vector<newui::text::TextColorRun> runs;
        for (std::size_t line = 0; line < highlighter_.lineCount(); ++line) {
            const std::size_t lineStart = highlighter_.lineStart(line);
            for (const lex::HighlightSpan& span : highlighter_.spans(line)) {
                if (span.isProblem()) {
                    squiggle(lineStart + span.column, span.length);
                }
                if (span.style == lex::StyleId::Default || span.length == 0) {
                    continue;   // drawn in the control's own text color
                }
                newui::text::TextColorRun run;
                run.start = lineStart + span.column;
                run.length = span.length;
                run.color = colorFromArgb(theme.style(span.style).foreground);
                runs.push_back(run);
            }
        }
        textControl_->setColorRuns(std::move(runs));
        textControl_->setDecorations(std::move(problems));
    }

    void SourceView::setText(const std::string& utf8)
    {
        textControl_->setText(utf8ToWide(utf8));
    }

    std::string SourceView::text() const
    {
        return wideToUtf8(textControl_->text());
    }

    void SourceView::setError(const std::string& message)
    {
        errorBar_->setText(message.empty() ? std::string() : "  " + message);
        errorBar_->setVisible(!message.empty());
        updateLayout();
        style().markDirty();
    }

    std::string SourceView::error() const
    {
        return errorBar_->isVisible() ? errorBar_->text().substr(2) : std::string();
    }
}
