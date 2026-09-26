#include "SourceView.h"
#include "TextEncoding.h"

#include <newui/font.h>
#include <newui/layout.h>
#include <newui/texthistory.h>
#include <newui/textstyle.h>
#include <newui/uicolormanager.h>

#include <lex/json5_language.h>
#include <lex/json5_parser.h>

#include <algorithm>
#include <memory>

namespace CodeToolsVsix
{
    namespace
    {
        constexpr float kErrorBarHeight = 26.0f;
        constexpr float kStatusBarHeight = 22.0f;
        constexpr float kBreadcrumbBarHeight = 22.0f;
        constexpr const char* kCrumbSeparator = "  \xE2\x80\xBA  ";   // " > " as a single angle quote

        // A string property's decoded value in object, or empty.
        std::string stringProperty(const lex::json5::ASTNode* object, const wchar_t* key)
        {
            if (object == nullptr || object->type != lex::json5::ASTNodeType::Object) {
                return {};
            }
            for (const lex::json5::ASTNodePtr& child : object->children()) {
                const lex::json5::ASTNode* value = child ? child->valueNode() : nullptr;
                if (value != nullptr && child->key == key && value->primitive == lex::json5::PrimitiveKind::String) {
                    return wideToUtf8(lex::json5::decodeString(value->text()));
                }
            }
            return {};
        }

        // " (Type)" for an object with a "type".
        std::string typeSuffix(const lex::json5::ASTNode* node)
        {
            const std::string type = stringProperty(node, L"type");
            return type.empty() ? std::string() : " (" + type + ")";
        }

    }

    std::vector<newui::text::TextFold> SourceView::foldsFor(const lex::json5::ParseResult& parsed, const std::wstring& text)
    {
        std::vector<newui::text::TextFold> folds;
        // Iterative - a deep document would overflow a recursive walk (see ASTNode's destructor).
        std::vector<const lex::json5::ASTNode*> work;
        auto push = [&](const lex::json5::ASTChildren& nodes) {
            for (const lex::json5::ASTNodePtr& node : nodes) {
                if (node) {
                    work.push_back(node.get());
                }
            }
        };
        if (parsed.root) {
            work.push_back(parsed.root.get());
        }
        push(parsed.leadingComments);
        push(parsed.trailingComments);
        while (!work.empty()) {
            const lex::json5::ASTNode* node = work.back();
            work.pop_back();
            if (const auto* children = std::get_if<lex::json5::ASTChildren>(&node->value)) {
                push(*children);
            }
            const bool container = node->isContainer();
            if ((!container && node->type != lex::json5::ASTNodeType::CommentBlock)
                || node->endLine <= node->startLine || node->endOffset > text.size()) {
                continue;
            }
            // Between the delimiters: "{" and "}" (or "[" "]"), "/*" and "*/" - a missing close
            // (an unfinished document) hides to the end.
            const std::size_t open = container ? 1 : 2;
            const wchar_t close = node->type == lex::json5::ASTNodeType::Object ? L'}'
                : node->type == lex::json5::ASTNodeType::Array ? L']' : L'/';
            const std::size_t closeLength = container ? 1 : 2;
            const bool closed = node->endOffset >= node->startOffset + open + closeLength && text[node->endOffset - 1] == close;
            newui::text::TextFold fold;
            fold.start = node->startOffset + open;
            const std::size_t end = closed ? node->endOffset - closeLength : node->endOffset;
            if (end <= fold.start) {
                continue;
            }
            fold.length = end - fold.start;
            fold.placeholder = L"...";
            folds.push_back(fold);
        }
        return folds;
    }

    std::string SourceView::breadcrumbsFor(const lex::json5::ParseResult& parsed, std::size_t caret)
    {
        const lex::json5::ASTNode* node = parsed.root.get();
        if (node == nullptr) {
            return {};
        }
        std::string crumbs = "root" + typeSuffix(node);
        while (node != nullptr && node->isContainer()) {
            // The child the caret is in (comments aren't part of the path).
            const lex::json5::ASTNode* next = nullptr;
            std::size_t index = 0;
            for (const lex::json5::ASTNodePtr& child : node->children()) {
                if (!child || child->isComment()) {
                    continue;
                }
                if (caret >= child->startOffset && caret < child->endOffset) {
                    next = child.get();
                    break;
                }
                ++index;
            }
            if (next == nullptr) {
                break;
            }
            if (node->type == lex::json5::ASTNodeType::Object) {
                const lex::json5::ASTNode* value = next->valueNode();
                crumbs += kCrumbSeparator + wideToUtf8(next->key) + typeSuffix(value);
                node = value != nullptr && caret >= value->startOffset && caret < value->endOffset ? value : nullptr;
            } else {
                const std::string name = stringProperty(next, L"name");
                crumbs += kCrumbSeparator + (name.empty() ? "[" + std::to_string(index) + "]" : name) + typeSuffix(next);
                node = next;
            }
        }
        return crumbs;
    }

    SourceView::SourceView()
    {
        setName("designSource");
        setVisible(true);
        setLayout(std::make_unique<newui::FlexLayout>(newui::Orientation::Vertical));
        style().setBackgroundColor(newui::UIColorManager::colorFor(newui::UIColorRole::WindowBackground));

        breadcrumbBar_ = new newui::Label();
        breadcrumbBar_->setName("designSourceBreadcrumbs");
        breadcrumbBar_->setDesiredSize(newui::Size(0.0f, kBreadcrumbBarHeight));
        breadcrumbBar_->setTextAlignment(newui::TextAlignment::Left);
        breadcrumbBar_->style().setBackgroundColor(newui::UIColorManager::colorFor(newui::UIColorRole::WindowBackground));
        addChild(breadcrumbBar_);

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
        textControl_ = new newui::TextFoldingControl();
        textControl_->setName("designSourceText");
        // Before anything subscribes to the model: this one remembers edits for undo.
        textControl_->setModel(std::make_unique<newui::text::HistoryTextModel>());
        scrollView_->addChild(textControl_);

        // The status row: the status text, then the show-whitespace toggle at the right.
        auto* statusRow = new newui::SubView();
        statusRow->setName("designSourceStatusRow");
        statusRow->setVisible(true);
        statusRow->setDesiredSize(newui::Size(0.0f, kStatusBarHeight));
        statusRow->setLayout(std::make_unique<newui::FlexLayout>(newui::Orientation::Horizontal));
        statusRow->style().setBackgroundColor(newui::UIColorManager::colorFor(newui::UIColorRole::WindowBackground));
        addChild(statusRow);

        statusBar_ = new newui::Label();
        statusBar_->setName("designSourceStatus");
        statusBar_->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
        statusBar_->setTextAlignment(newui::TextAlignment::Left);
        statusBar_->style().setBackgroundColor(newui::UIColorManager::colorFor(newui::UIColorRole::WindowBackground));
        statusRow->addChild(statusBar_);

        whitespaceToggle_ = new newui::Button();
        whitespaceToggle_->setName("designSourceShowWhitespace");
        whitespaceToggle_->setText("\xC2\xB6");   // a pilcrow, the usual show-whitespace glyph
        whitespaceToggle_->setToggleButton(true);
        whitespaceToggle_->setDesiredSize(newui::Size(28.0f, kStatusBarHeight));
        whitespaceToggle_->onCheckedChanged.add(this, &SourceView::handleWhitespaceToggled);
        statusRow->addChild(whitespaceToggle_);

        // Colors, squiggles and folds follow the text (off the UI thread); the status bar and the
        // breadcrumbs, which read the parse tree, follow each pass. Created before the subscription
        // below so that on the synchronous path (no run loop) the tree is fresh when the status updates.
        highlight_ = std::make_unique<HighlightController>(*textControl_, &SourceView::analyze);
        highlight_->setOnApplied([this](HighlightResult& result) {
            if (auto* parsed = std::any_cast<lex::json5::ParseResult>(&result.extra)) {
                parsed_ = std::move(*parsed);
            }
            updateStatus();
        });

        // Typing and setText() both change the model - the status bar follows.
        textControl_->model().onChanged.add(this, &SourceView::handleTextChanged);
        textControl_->controller().onEditStateChanged.add(this, &SourceView::handleEditStateChanged);
        updateStatus();
    }

    std::string SourceView::statusFor(const std::wstring& text, std::size_t caret,
        const std::vector<newui::text::TextRange>& selection, bool overwrite)
    {
        caret = caret < text.size() ? caret : text.size();
        const std::size_t line = static_cast<std::size_t>(std::count(text.begin(), text.begin() + caret, L'\n'));
        const std::size_t lineStart = caret == 0 ? 0 : text.rfind(L'\n', caret - 1);
        const std::size_t column = caret - (lineStart == std::wstring::npos || caret == 0 ? 0 : lineStart + 1);

        std::string status = "Ln " + std::to_string(line + 1) + ", Ch " + std::to_string(column + 1);
        std::size_t selected = 0;
        std::size_t selectedLines = 0;
        for (const newui::text::TextRange& range : selection) {
            if (!range.isValid() || range.start() >= text.size()) {
                continue;
            }
            const std::size_t end = range.end() < text.size() ? range.end() : text.size();
            selected += end - range.start();
            selectedLines += 1 + static_cast<std::size_t>(std::count(text.begin() + range.start(), text.begin() + end, L'\n'));
        }
        if (selected > 0) {
            status += "    Sel " + std::to_string(selected);
            if (selectedLines > 1) {
                status += " (" + std::to_string(selectedLines) + " lines)";
            }
        }
        status += overwrite ? "    OVR" : "    INS";
        return status;
    }

    void SourceView::updateStatus()
    {
        const newui::text::TextPosition caret = textControl_->caret().position();
        const std::size_t offset = caret.isValid() ? caret.offset() : 0;
        statusBar_->setText("  " + statusFor(textControl_->text(), offset,
            textControl_->selection().ranges(), textControl_->controller().isOverwriteMode()));
        breadcrumbBar_->setText("  " + breadcrumbsFor(parsed_, offset));
    }

    newui::SyncReturn SourceView::handleWhitespaceToggled(newui::Button& sender)
    {
        textControl_->setShowsWhitespace(sender.isChecked());
        return newui::SyncReturn::Handled;
    }

    newui::SyncReturn SourceView::handleEditStateChanged(newui::TextController& /*sender*/)
    {
        updateStatus();
        return newui::SyncReturn::Ignored;
    }

    SourceView::~SourceView() = default;

    newui::SyncReturn SourceView::handleTextChanged(newui::Model& /*sender*/)
    {
        updateStatus();
        return newui::SyncReturn::Ignored;
    }

    HighlightResult SourceView::analyze(const std::wstring& text)
    {
        HighlightResult result;
        // Squiggles for what doesn't parse, then lex's colors and its own problems.
        lex::json5::ParseResult parsed = lex::json5::parse(text);
        for (const lex::json5::ParseError& error : parsed.errors) {
            addProblemRange(result.ranges, text.size(), error.offset, error.length);
        }
        appendStyleRanges(lex::json5Language(), text, result.ranges);

        // Unparsable: no folds to offer (the control keeps the ones it has, moved along with the edit).
        result.foldsValid = parsed.root != nullptr;
        if (result.foldsValid) {
            result.folds = foldsFor(parsed, text);
        }
        result.extra = std::move(parsed);
        return result;
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
