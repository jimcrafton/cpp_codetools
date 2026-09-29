#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace CodeToolsVsix
{
    // What a stretch of C++ source is: ordinary code, a comment, or a string/character literal.
    enum class MatchKind { Code, Comment, String };

    // Which kinds of match a search accepts.
    enum class KindFilter { All, Code, Comment, String };

    struct FindOptions
    {
        bool matchCase = false;
        bool wholeWord = false;
        KindFilter kind = KindFilter::All;
        // Only matches wholly inside [rangeStart, rangeEnd) count (the whole text by default).
        std::size_t rangeStart = 0;
        std::size_t rangeEnd = static_cast<std::size_t>(-1);
    };

    struct FindMatch
    {
        std::size_t start = 0;
        std::size_t length = 0;
        MatchKind kind = MatchKind::Code;   // the kind at start
    };

    // The kind of every character of text (comment / string literal / code), as the C++ lexer sees it.
    std::vector<MatchKind> classifyText(const std::wstring& text);

    // Every non-overlapping occurrence of query in text, in order. Nothing for an empty query. Always
    // lexes the text (even with options.kind == All) so each FindMatch::kind is accurate - a caller
    // that just wants to know WHERE something is (the minimap's ticks) needs it as much as a kind
    // filter does.
    std::vector<FindMatch> findAll(const std::wstring& text, const std::wstring& query, const FindOptions& options);

    // The identifier the offset is inside of, or just after; empty if it isn't on one.
    std::wstring tokenAt(const std::wstring& text, std::size_t offset);

    // Where a search should start: the first match that ends at or after offset, else the first match
    // (a search wraps). npos with no matches.
    constexpr std::size_t kNoMatch = static_cast<std::size_t>(-1);
    std::size_t firstMatchAtOrAfter(const std::vector<FindMatch>& matches, std::size_t offset);

    // ---- lines (Go to line) --------------------------------------------------------------------------
    // A line ends at \n, \r\n or a lone \r - the one rule used everywhere in the editor.

    // How many lines the text has (an empty text has one).
    std::size_t lineCount(const std::wstring& text);

    // The offset of a 1-based line and column. A line past the end gives the last line; a column past
    // the end of the line gives the end of the line (before its terminator); 0 counts as 1.
    std::size_t offsetOfLine(const std::wstring& text, std::size_t line, std::size_t column = 1);

    // The 1-based line offset is on.
    std::size_t lineOfOffset(const std::wstring& text, std::size_t offset);
}
