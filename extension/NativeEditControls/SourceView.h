#pragma once

#include <newui/controls.h>
#include <newui/subview.h>

#include <lex/highlight.h>

#include <string>

namespace CodeToolsVsix
{
    // The designer's Source page: the document as editable JSON5 text (a TextControl scrolled by
    // a ScrollView), syntax highlighted, with an error bar above it that shows only while there's
    // an error to show. Holds text only - DesignerEditor decides what the text is and when it's
    // applied.
    class SourceView : public newui::SubView
    {
    public:
        SourceView();

        void setText(const std::string& utf8);
        std::string text() const;

        // Empty hides the bar.
        void setError(const std::string& message);
        std::string error() const;

        newui::TextControl* textControl() const { return textControl_; }
        newui::ScrollView* scrollView() const { return scrollView_; }
        newui::Label* errorBar() const { return errorBar_; }

        // The color runs and problem squiggles the highlighter produced for the current text - for
        // tests.
        const std::vector<newui::text::TextColorRun>& colorRuns() const { return textControl_->colorRuns(); }
        const std::vector<newui::text::TextDecoration>& problems() const { return textControl_->decorations(); }

    private:
        // Re-lexes the text, recolors it (lex's light or dark theme, following the system) and
        // squiggles what doesn't parse.
        void highlight();
        newui::SyncReturn handleTextChanged(newui::Model& sender);

        lex::SyntaxHighlighter highlighter_;
        newui::Label* errorBar_ = nullptr;
        newui::ScrollView* scrollView_ = nullptr;
        newui::TextControl* textControl_ = nullptr;
    };
}
