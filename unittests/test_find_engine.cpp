// Tests for FindEngine: the pure search logic behind Find / Replace (no UI).

#include "../extension/NativeEditControls/FindEngine.h"

#include <gtest/gtest.h>

using namespace CodeToolsVsix;

namespace
{
    std::vector<std::size_t> starts(const std::vector<FindMatch>& matches)
    {
        std::vector<std::size_t> out;
        for (const FindMatch& m : matches) {
            out.push_back(m.start);
        }
        return out;
    }
}

TEST(FindEngine, FindsEveryNonOverlappingOccurrenceInOrder)
{
    EXPECT_EQ(starts(findAll(L"abcabcabc", L"abc", {})), (std::vector<std::size_t>{ 0, 3, 6 }));
    EXPECT_EQ(starts(findAll(L"aaaa", L"aa", {})), (std::vector<std::size_t>{ 0, 2 }));   // not 0,1,2
}

TEST(FindEngine, AnEmptyQueryOrAnEmptyTextFindsNothing)
{
    EXPECT_TRUE(findAll(L"abc", L"", {}).empty());
    EXPECT_TRUE(findAll(L"", L"abc", {}).empty());
    EXPECT_TRUE(findAll(L"ab", L"abc", {}).empty());
}

TEST(FindEngine, CaseIsIgnoredUnlessMatchCaseIsOn)
{
    EXPECT_EQ(findAll(L"Path path PATH", L"path", {}).size(), 3u);
    FindOptions exact;
    exact.matchCase = true;
    EXPECT_EQ(starts(findAll(L"Path path PATH", L"path", exact)), (std::vector<std::size_t>{ 5 }));
}

TEST(FindEngine, WholeWordSkipsMatchesInsideLongerIdentifiers)
{
    const std::wstring text = L"path filePath path_x (path) subpath";
    FindOptions word;
    word.wholeWord = true;
    // "path" alone (0) and inside the parentheses (22); not filePath, path_x (underscore is part of a word) or subpath.
    EXPECT_EQ(starts(findAll(text, L"path", word)), (std::vector<std::size_t>{ 0, 22 }));
    EXPECT_EQ(findAll(text, L"path", {}).size(), 5u);
}

TEST(FindEngine, TheRangeLimitsWhereMatchesAreFound)
{
    FindOptions inRange;
    inRange.rangeStart = 4;
    inRange.rangeEnd = 10;
    // "ab ab ab ab" (matches at 0, 3, 6, 9): only those lying wholly inside [4, 10) - 6 does, 3 starts too early
    // and 9 runs past the end (9 + 2 > 10).
    EXPECT_EQ(starts(findAll(L"ab ab ab ab", L"ab", inRange)), (std::vector<std::size_t>{ 6 }));
}

TEST(FindEngine, ClassifiesCommentsStringsAndCode)
{
    const std::wstring text = L"int path; // path\nchar* s = \"path\"; /* path */ path";
    const std::vector<MatchKind> kinds = classifyText(text);
    ASSERT_EQ(kinds.size(), text.size());
    EXPECT_EQ(kinds[text.find(L"path")], MatchKind::Code);
    EXPECT_EQ(kinds[text.find(L"// path") + 3], MatchKind::Comment);
    EXPECT_EQ(kinds[text.find(L"\"path\"") + 2], MatchKind::String);
    EXPECT_EQ(kinds[text.find(L"/* path */") + 4], MatchKind::Comment);
    EXPECT_EQ(kinds[text.rfind(L"path")], MatchKind::Code);
}

TEST(FindEngine, TheKindFilterKeepsOnlyThatKindOfMatch)
{
    const std::wstring text = L"int path; // path\nchar* s = \"path\"; /* path */ path";
    FindOptions options;
    options.kind = KindFilter::All;
    const std::vector<FindMatch> all = findAll(text, L"path", options);
    ASSERT_EQ(all.size(), 5u);
    // Every match's own kind is accurate even though nothing was filtered out - a caller that just
    // wants to know WHERE something is (the minimap's ticks) needs this as much as a kind filter does.
    EXPECT_EQ(all[0].kind, MatchKind::Code);
    EXPECT_EQ(all[1].kind, MatchKind::Comment);
    EXPECT_EQ(all[2].kind, MatchKind::String);
    EXPECT_EQ(all[3].kind, MatchKind::Comment);
    EXPECT_EQ(all[4].kind, MatchKind::Code);
    options.kind = KindFilter::Code;
    EXPECT_EQ(findAll(text, L"path", options).size(), 2u);
    options.kind = KindFilter::Comment;
    EXPECT_EQ(findAll(text, L"path", options).size(), 2u);
    options.kind = KindFilter::String;
    const std::vector<FindMatch> strings = findAll(text, L"path", options);
    ASSERT_EQ(strings.size(), 1u);
    EXPECT_EQ(strings[0].kind, MatchKind::String);
}

TEST(FindEngine, TokenAtFindsTheIdentifierUnderOrJustBeforeTheCaret)
{
    const std::wstring text = L"  int fooBar = 12;";
    EXPECT_EQ(tokenAt(text, 6), L"fooBar");    // inside
    EXPECT_EQ(tokenAt(text, 6 + 6), L"fooBar");   // just after its last character
    EXPECT_EQ(tokenAt(text, 5), L"int");    // just after "int"
    EXPECT_EQ(tokenAt(text, 0), L"");       // whitespace
    EXPECT_EQ(tokenAt(text, 14), L"");      // between "=" and "12"
    EXPECT_EQ(tokenAt(text, 16), L"");      // inside "12": a number, not an identifier
    EXPECT_EQ(tokenAt(text, 999), L"");     // past the end
}

TEST(FindEngine, FirstMatchAtOrAfterPicksTheMatchTheCaretIsOnOrTheNextOneAndWraps)
{
    const std::vector<FindMatch> matches = { { 10, 3, MatchKind::Code }, { 30, 3, MatchKind::Code } };
    EXPECT_EQ(firstMatchAtOrAfter(matches, 0), 0u);
    EXPECT_EQ(firstMatchAtOrAfter(matches, 11), 0u);    // inside the first
    EXPECT_EQ(firstMatchAtOrAfter(matches, 13), 0u);    // right at its end still counts as on it
    EXPECT_EQ(firstMatchAtOrAfter(matches, 14), 1u);
    EXPECT_EQ(firstMatchAtOrAfter(matches, 100), 0u);   // past every match: wraps
    EXPECT_EQ(firstMatchAtOrAfter({}, 5), kNoMatch);
}

TEST(FindEngine, LinesEndAtLfCrLfOrALoneCr)
{
    EXPECT_EQ(lineCount(L""), 1u);
    EXPECT_EQ(lineCount(L"a"), 1u);
    EXPECT_EQ(lineCount(L"a\nb\nc"), 3u);
    EXPECT_EQ(lineCount(L"a\r\nb\r\nc"), 3u);   // CRLF is one break
    EXPECT_EQ(lineCount(L"a\rb\rc"), 3u);
    EXPECT_EQ(lineCount(L"a\n"), 2u);           // a trailing break starts an empty last line
}

TEST(FindEngine, OffsetOfLineFindsTheLineAndColumnAndClamps)
{
    const std::wstring text = L"one\r\ntwo\nthree\rfour";   // lines start at 0, 5, 9, 15
    EXPECT_EQ(offsetOfLine(text, 1), 0u);
    EXPECT_EQ(offsetOfLine(text, 2), 5u);
    EXPECT_EQ(offsetOfLine(text, 3), 9u);
    EXPECT_EQ(offsetOfLine(text, 4), 15u);
    EXPECT_EQ(offsetOfLine(text, 2, 3), 7u);
    EXPECT_EQ(offsetOfLine(text, 2, 99), 8u);   // past the end of "two": its end, before the terminator
    EXPECT_EQ(offsetOfLine(text, 99), 15u);     // past the last line: the last line's start
    EXPECT_EQ(offsetOfLine(text, 0), 0u);       // 0 counts as 1
}

TEST(FindEngine, LineOfOffsetIsTheInverse)
{
    const std::wstring text = L"one\r\ntwo\nthree\rfour";
    EXPECT_EQ(lineOfOffset(text, 0), 1u);
    EXPECT_EQ(lineOfOffset(text, 3), 1u);   // at the end of line 1, before its terminator
    EXPECT_EQ(lineOfOffset(text, 4), 1u);   // between the \r and \n of one break: still line 1
    EXPECT_EQ(lineOfOffset(text, 5), 2u);
    EXPECT_EQ(lineOfOffset(text, 9), 3u);
    EXPECT_EQ(lineOfOffset(text, 15), 4u);
    EXPECT_EQ(lineOfOffset(text, 999), 4u);
}
