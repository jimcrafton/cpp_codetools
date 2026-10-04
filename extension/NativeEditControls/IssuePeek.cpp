#include "IssuePeek.h"

namespace CodeToolsVsix
{
    namespace
    {
        struct SourceLine
        {
            std::size_t start = 0;   // byte offset of its first character
            std::string text;        // without the line break
        };

        std::vector<SourceLine> splitLines(const std::string& text)
        {
            std::vector<SourceLine> lines;
            std::size_t start = 0;
            while (start <= text.size()) {
                std::size_t end = text.find('\n', start);
                const bool last = end == std::string::npos;
                if (last) {
                    end = text.size();
                }
                std::size_t textEnd = end;
                if (textEnd > start && text[textEnd - 1] == '\r') {
                    --textEnd;
                }
                lines.push_back(SourceLine{ start, text.substr(start, textEnd - start) });
                if (last) {
                    break;
                }
                start = end + 1;
            }
            // A final line break ends the last line; it doesn't start another, empty one.
            if (lines.size() > 1 && lines.back().text.empty() && !text.empty() && text.back() == '\n') {
                lines.pop_back();
            }
            return lines;
        }

        // `text` with each tab turned into spaces up to the next multiple of kTabWidth.
        std::string expandTabs(const std::string& text)
        {
            std::string out;
            for (char c : text) {
                if (c == '\t') {
                    const std::size_t pad = IssuePeek::kTabWidth - out.size() % IssuePeek::kTabWidth;
                    out.append(pad, ' ');
                } else {
                    out += c;
                }
            }
            return out;
        }

        // The 0-based display column of byte offset `byteColumn` within `text` once tabs are expanded.
        std::size_t displayColumn(const std::string& text, std::size_t byteColumn)
        {
            return expandTabs(text.substr(0, byteColumn < text.size() ? byteColumn : text.size())).size();
        }
    }

    std::size_t utf16Column(const std::string& headerText, std::size_t line, std::size_t byteColumn)
    {
        const std::vector<SourceLine> lines = splitLines(headerText);
        if (line < 1 || line > lines.size() || byteColumn < 1) {
            return byteColumn;
        }
        const std::string& text = lines[line - 1].text;
        const std::size_t bytes = byteColumn - 1 < text.size() ? byteColumn - 1 : text.size();

        // Each character starts at a lead byte (not a 10xxxxxx continuation); a 4-byte one is a
        // surrogate pair in UTF-16.
        std::size_t units = 0;
        for (std::size_t i = 0; i < bytes; ++i) {
            const unsigned char byte = static_cast<unsigned char>(text[i]);
            if ((byte & 0xC0) != 0x80) {
                units += byte >= 0xF0 ? 2 : 1;
            }
        }
        return units + 1;
    }

    std::vector<IssuePeek> buildIssuePeeks(const std::vector<cpptools::Diagnostic>& issues,
                                           const std::string& headerText)
    {
        const std::vector<SourceLine> lines = splitLines(headerText);

        std::vector<IssuePeek> peeks;
        peeks.reserve(issues.size());
        for (const cpptools::Diagnostic& issue : issues) {
            IssuePeek peek;
            peek.isError = issue.severity == cpptools::Severity::Error || issue.severity == cpptools::Severity::Fatal;
            peek.message = issue.message;
            peek.line = issue.location.line;
            peek.column = issue.location.column;
            peek.summary = std::string(peek.isError ? "error " : "warning ") + std::to_string(peek.line) + ":" +
                           std::to_string(peek.column) + "  " + peek.message;

            if (peek.line >= 1 && peek.line <= lines.size()) {
                const std::size_t first = peek.line > IssuePeek::kContext + 1 ? peek.line - IssuePeek::kContext : 1;
                const std::size_t lastWanted = peek.line + IssuePeek::kContext;
                const std::size_t last = lastWanted < lines.size() ? lastWanted : lines.size();
                peek.firstLine = first;
                for (std::size_t n = first; n <= last; ++n) {
                    peek.excerpt.push_back(expandTabs(lines[n - 1].text));
                }

                const SourceLine& own = lines[peek.line - 1];
                const std::size_t byteColumn = peek.column > 0 ? peek.column - 1 : 0;
                peek.underlineStart = displayColumn(own.text, byteColumn);

                // The diagnostic's own range, when it has one that stays on this line.
                if (issue.rangeEndOffset > issue.location.offset) {
                    const std::size_t lineEnd = own.start + own.text.size();
                    const std::size_t end = issue.rangeEndOffset < lineEnd ? issue.rangeEndOffset : lineEnd;
                    if (end > issue.location.offset) {
                        const std::size_t endColumn = displayColumn(own.text, end - own.start);
                        peek.underlineLength = endColumn > peek.underlineStart ? endColumn - peek.underlineStart : 0;
                    }
                }
            }
            peeks.push_back(std::move(peek));
        }
        return peeks;
    }
}
