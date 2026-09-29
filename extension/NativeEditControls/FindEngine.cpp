#include "FindEngine.h"

#include <lex/cpp_language.h>
#include <lex/highlight.h>

#include <cwctype>

namespace CodeToolsVsix
{
    namespace
    {
        bool isWordChar(wchar_t c)
        {
            return c == L'_' || std::iswalnum(static_cast<wint_t>(c)) != 0;
        }

        wchar_t foldCase(wchar_t c)
        {
            return static_cast<wchar_t>(std::towlower(static_cast<wint_t>(c)));
        }

        bool sameAt(const std::wstring& text, std::size_t at, const std::wstring& query, bool matchCase)
        {
            for (std::size_t i = 0; i < query.size(); ++i) {
                const wchar_t a = text[at + i];
                const wchar_t b = query[i];
                if (matchCase ? a != b : foldCase(a) != foldCase(b)) {
                    return false;
                }
            }
            return true;
        }

        bool accepts(KindFilter filter, MatchKind kind)
        {
            switch (filter) {
            case KindFilter::Code: return kind == MatchKind::Code;
            case KindFilter::Comment: return kind == MatchKind::Comment;
            case KindFilter::String: return kind == MatchKind::String;
            default: return true;
            }
        }
    }

    std::vector<MatchKind> classifyText(const std::wstring& text)
    {
        std::vector<MatchKind> kinds(text.size(), MatchKind::Code);
        lex::SyntaxHighlighter highlighter(lex::cppLanguage());
        highlighter.setText(text);
        for (std::size_t line = 0; line < highlighter.lineCount(); ++line) {
            const std::size_t lineStart = highlighter.lineStart(line);
            for (const lex::HighlightSpan& span : highlighter.spans(line)) {
                MatchKind kind = MatchKind::Code;
                if (span.style == lex::StyleId::Comment) {
                    kind = MatchKind::Comment;
                } else if (span.style == lex::StyleId::String) {
                    kind = MatchKind::String;
                } else {
                    continue;
                }
                for (std::size_t i = 0; i < span.length && lineStart + span.column + i < kinds.size(); ++i) {
                    kinds[lineStart + span.column + i] = kind;
                }
            }
        }
        return kinds;
    }

    std::vector<FindMatch> findAll(const std::wstring& text, const std::wstring& query, const FindOptions& options)
    {
        std::vector<FindMatch> matches;
        if (query.empty() || query.size() > text.size()) {
            return matches;
        }
        const std::size_t rangeEnd = options.rangeEnd < text.size() ? options.rangeEnd : text.size();
        const std::vector<MatchKind> kinds = classifyText(text);

        std::size_t at = options.rangeStart;
        while (at + query.size() <= rangeEnd) {
            if (!sameAt(text, at, query, options.matchCase)) {
                ++at;
                continue;
            }
            const std::size_t end = at + query.size();
            const bool wordOk = !options.wholeWord
                || ((at == 0 || !isWordChar(text[at - 1])) && (end >= text.size() || !isWordChar(text[end])));
            const MatchKind kind = kinds[at];
            if (wordOk && accepts(options.kind, kind)) {
                matches.push_back({ at, query.size(), kind });
                at = end;
            } else {
                ++at;
            }
        }
        return matches;
    }

    std::wstring tokenAt(const std::wstring& text, std::size_t offset)
    {
        if (offset > text.size()) {
            return std::wstring();
        }
        std::size_t start = offset;
        while (start > 0 && isWordChar(text[start - 1])) {
            --start;
        }
        std::size_t end = offset;
        while (end < text.size() && isWordChar(text[end])) {
            ++end;
        }
        if (start == end) {
            return std::wstring();
        }
        // An identifier can't begin with a digit: "123abc" is a number, not something to rename.
        if (std::iswdigit(static_cast<wint_t>(text[start])) != 0) {
            return std::wstring();
        }
        return text.substr(start, end - start);
    }

    std::size_t firstMatchAtOrAfter(const std::vector<FindMatch>& matches, std::size_t offset)
    {
        if (matches.empty()) {
            return kNoMatch;
        }
        for (std::size_t i = 0; i < matches.size(); ++i) {
            if (matches[i].start + matches[i].length >= offset) {
                return i;
            }
        }
        return 0;
    }

    namespace
    {
        // The end of the line break starting at i (\r\n is one break), or i if there is none there.
        std::size_t breakEndAt(const std::wstring& text, std::size_t i)
        {
            if (text[i] == L'\r') {
                return i + 1 < text.size() && text[i + 1] == L'\n' ? i + 2 : i + 1;
            }
            return text[i] == L'\n' ? i + 1 : i;
        }
    }

    std::size_t lineCount(const std::wstring& text)
    {
        std::size_t lines = 1;
        for (std::size_t i = 0; i < text.size();) {
            const std::size_t next = breakEndAt(text, i);
            if (next != i) {
                ++lines;
                i = next;
            } else {
                ++i;
            }
        }
        return lines;
    }

    std::size_t offsetOfLine(const std::wstring& text, std::size_t line, std::size_t column)
    {
        const std::size_t target = line == 0 ? 1 : line;
        std::size_t start = 0;
        std::size_t current = 1;
        for (std::size_t i = 0; i < text.size() && current < target;) {
            const std::size_t next = breakEndAt(text, i);
            if (next != i) {
                ++current;
                start = next;
                i = next;
            } else {
                ++i;
            }
        }
        std::size_t lineEnd = start;
        while (lineEnd < text.size() && text[lineEnd] != L'\n' && text[lineEnd] != L'\r') {
            ++lineEnd;
        }
        const std::size_t wanted = start + (column == 0 ? 0 : column - 1);
        return wanted < lineEnd ? wanted : lineEnd;
    }

    std::size_t lineOfOffset(const std::wstring& text, std::size_t offset)
    {
        const std::size_t limit = offset < text.size() ? offset : text.size();
        std::size_t line = 1;
        for (std::size_t i = 0; i < limit;) {
            const std::size_t next = breakEndAt(text, i);
            if (next != i) {
                if (next <= limit) {
                    ++line;
                }
                i = next;
            } else {
                ++i;
            }
        }
        return line;
    }
}
