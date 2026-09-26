#pragma once

#include <newui/controls.h>
#include <newui/subview.h>
#include <newui/textfolding.h>

#include <lex/highlight.h>
#include <lex/json5_parser.h>

#include "HighlightController.h"

#include <memory>

#include <string>

namespace CodeToolsVsix
{
    // The designer's Source page: the document as editable JSON5 text (a TextFoldingControl
    // scrolled by a ScrollView), syntax highlighted, with every multi-line object, array and block
    // comment foldable. Above it: breadcrumbs (where the caret is in the JSON5 tree), and an error
    // bar that shows only while there's an error to show. Below it: a status bar (caret line and
    // column, selection size, INS / OVR) with a show-whitespace toggle. Holds text only - DesignerEditor decides what the text is and when it's
    // applied.
    class SourceView : public newui::SubView
    {
    public:
        SourceView();
        ~SourceView() override;

        void setText(const std::string& utf8);
        std::string text() const;

        // Empty hides the bar.
        void setError(const std::string& message);
        std::string error() const;

        newui::TextFoldingControl* textControl() const { return textControl_; }
        newui::ScrollView* scrollView() const { return scrollView_; }
        newui::Label* errorBar() const { return errorBar_; }
        newui::Label* statusBar() const { return statusBar_; }
        newui::Label* breadcrumbBar() const { return breadcrumbBar_; }
        newui::Button* whitespaceToggle() const { return whitespaceToggle_; }

        // The path to caret in parsed: "root > panel (DemoPanel) > flags > [1]" - property names,
        // array indexes (an element's "name" in their place when it has one), and an object's
        // "type" in brackets.
        static std::string breadcrumbsFor(const lex::json5::ParseResult& parsed, std::size_t caret);

        // "Ln 3, Ch 12    Sel 14 (2 lines)    INS" - Ln / Ch 1-based, Sel only with a selection.
        static std::string statusFor(const std::wstring& text, std::size_t caret,
            const std::vector<newui::text::TextRange>& selection, bool overwrite);

        // The color runs and problem squiggles the highlighter produced for the current text - for
        // tests.
        const std::vector<newui::text::TextColorRun>& colorRuns() const { return textControl_->colorRuns(); }
        const std::vector<newui::text::TextDecoration>& problems() const { return textControl_->decorations(); }

        // A fold per multi-line object, array (hiding what's between the brackets) and block
        // comment in parsed, expanded; text is what was parsed.
        static std::vector<newui::text::TextFold> foldsFor(const lex::json5::ParseResult& parsed, const std::wstring& text);

        // The pure part of highlighting a JSON5 text: lex colors, squiggles for what doesn't lex or
        // parse, and the folds - with the parse tree as the result's extra (a lex::json5::ParseResult).
        // Thread-safe; what HighlightController runs on its worker.
        static HighlightResult analyze(const std::wstring& text);

    private:
        // The status bar follows the text as it changes (the colors and folds come from highlight_).
        newui::SyncReturn handleTextChanged(newui::Model& sender);
        newui::SyncReturn handleEditStateChanged(newui::TextController& sender);
        newui::SyncReturn handleWhitespaceToggled(newui::Button& sender);
        // The status bar and the breadcrumbs, for the caret and selection now.
        void updateStatus();

        newui::Label* errorBar_ = nullptr;
        newui::ScrollView* scrollView_ = nullptr;
        newui::TextFoldingControl* textControl_ = nullptr;
        newui::Label* statusBar_ = nullptr;
        newui::Label* breadcrumbBar_ = nullptr;
        newui::Button* whitespaceToggle_ = nullptr;
        lex::json5::ParseResult parsed_;   // the text as of the last highlight pass
        std::unique_ptr<HighlightController> highlight_;
    };
}
