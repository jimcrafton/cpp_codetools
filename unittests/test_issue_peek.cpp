// buildIssuePeeks: the source excerpt, underline and list text for each problem the Designer's
// "N problems" popup shows - pure data, no UI.

#include <gtest/gtest.h>

#include "../extension/NativeEditControls/IssuePeek.h"
#include "../extension/NativeEditControls/Settings.h"

using namespace CodeToolsVsix;

namespace {

cpptools::Diagnostic problem(std::size_t line, std::size_t column, std::size_t offset, const std::string& message,
                             cpptools::Severity severity = cpptools::Severity::Error, std::size_t rangeEnd = 0) {
    cpptools::Diagnostic d;
    d.severity = severity;
    d.message = message;
    d.location.line = line;
    d.location.column = column;
    d.location.offset = offset;
    d.rangeEndOffset = rangeEnd;
    return d;
}

const std::string kHeader =
    "#pragma once\n"                 // 1
    "\n"                             // 2
    "#include <newui/controls.h>\n"  // 3
    "#include <bojangle>\n"          // 4
    "#include <newui/view.h>\n"      // 5
    "\n"                             // 6
    "class Foo {};\n";               // 7

}

TEST(IssuePeek, TheExcerptIsTheProblemsLineWithTwoLinesEitherSide) {
    const auto peeks = buildIssuePeeks({problem(4, 10, 0, "'bojangle' file not found")}, kHeader);

    ASSERT_EQ(peeks.size(), 1u);
    EXPECT_EQ(peeks[0].firstLine, 2u);
    ASSERT_EQ(peeks[0].excerpt.size(), 5u);
    EXPECT_EQ(peeks[0].excerpt[0], "");
    EXPECT_EQ(peeks[0].excerpt[2], "#include <bojangle>");
    EXPECT_EQ(peeks[0].excerptIndex(), 2u);
    EXPECT_EQ(peeks[0].excerpt[peeks[0].excerptIndex()], "#include <bojangle>");
}

TEST(IssuePeek, TheWindowIsClampedAtTheStartAndEndOfTheFile) {
    const auto first = buildIssuePeeks({problem(1, 1, 0, "x")}, kHeader);
    EXPECT_EQ(first[0].firstLine, 1u);
    EXPECT_EQ(first[0].excerpt.size(), 3u);   // lines 1..3
    EXPECT_EQ(first[0].excerptIndex(), 0u);

    const auto last = buildIssuePeeks({problem(7, 1, 0, "x")}, kHeader);
    EXPECT_EQ(last[0].firstLine, 5u);
    EXPECT_EQ(last[0].excerpt.size(), 3u);    // lines 5..7
    EXPECT_EQ(last[0].excerpt.back(), "class Foo {};");
}

TEST(IssuePeek, ASummaryNamesTheSeverityPositionAndMessage) {
    const auto peeks = buildIssuePeeks({problem(4, 10, 0, "'bojangle' file not found"),
                                        problem(7, 1, 0, "unused thing", cpptools::Severity::Warning)}, kHeader);

    EXPECT_EQ(peeks[0].summary, "error 4:10  'bojangle' file not found");
    EXPECT_TRUE(peeks[0].isError);
    EXPECT_EQ(peeks[1].summary, "warning 7:1  unused thing");
    EXPECT_FALSE(peeks[1].isError);
}

TEST(IssuePeek, TheUnderlineStartsAtTheColumnAndSpansTheDiagnosticsRange) {
    // Line 4 starts at byte 12+1+27+... compute it from the text rather than hard-coding.
    const std::size_t lineStart = kHeader.find("#include <bojangle>");
    const std::size_t at = lineStart + 9;                 // the '<'
    const auto peeks = buildIssuePeeks({problem(4, 10, at, "not found", cpptools::Severity::Error, at + 10)}, kHeader);

    EXPECT_EQ(peeks[0].underlineStart, 9u);
    EXPECT_EQ(peeks[0].underlineLength, 10u);   // "<bojangle>"
}

TEST(IssuePeek, ARangeThatRunsPastTheLineIsCutAtItsEnd) {
    const std::size_t lineStart = kHeader.find("#include <bojangle>");
    const std::size_t at = lineStart + 9;
    const auto peeks = buildIssuePeeks({problem(4, 10, at, "x", cpptools::Severity::Error, at + 500)}, kHeader);

    EXPECT_EQ(peeks[0].underlineLength, std::string("<bojangle>").size());
}

TEST(IssuePeek, WithoutARangeThereIsNothingToUnderline) {
    const auto peeks = buildIssuePeeks({problem(4, 10, 0, "x")}, kHeader);
    EXPECT_EQ(peeks[0].underlineLength, 0u);
}

TEST(IssuePeek, TabsAreExpandedAndTheUnderlineFollowsThem) {
    const std::string text = "int a;\n\tint b = c;\n";   // line 2: a tab, then "int b = c;"
    const std::size_t lineStart = text.find('\t');
    // The 'c' is at byte column 10 (1-based) of "\tint b = c;" -> display column 4 + 8 = 12.
    const std::size_t at = lineStart + 9;
    const auto peeks = buildIssuePeeks({problem(2, 10, at, "undeclared", cpptools::Severity::Error, at + 1)}, text);

    ASSERT_EQ(peeks[0].excerpt.size(), 2u);
    EXPECT_EQ(peeks[0].excerpt[1], "    int b = c;");
    EXPECT_EQ(peeks[0].underlineStart, 12u);
    EXPECT_EQ(peeks[0].underlineLength, 1u);
    EXPECT_EQ(peeks[0].excerpt[1][peeks[0].underlineStart], 'c');
}

TEST(IssuePeek, TheTabWidthComesFromTheOptions) {
    Settings::instance().set(Settings::kPeekTabWidth.key, L"2");
    const std::string text = "int a;\n\tint b = c;\n";
    const std::size_t at = text.find('\t') + 9;
    const auto peeks = buildIssuePeeks({problem(2, 10, at, "undeclared", cpptools::Severity::Error, at + 1)}, text);
    Settings::instance().set(Settings::kPeekTabWidth.key, L"4");   // the shared instance outlives the test

    ASSERT_EQ(peeks[0].excerpt.size(), 2u);
    EXPECT_EQ(peeks[0].excerpt[1], "  int b = c;");
    EXPECT_EQ(peeks[0].underlineStart, 10u);
}

TEST(IssuePeek, CrLfLinesAreSplitWithoutTheCarriageReturn) {
    const std::string text = "one\r\ntwo\r\nthree\r\n";
    const auto peeks = buildIssuePeeks({problem(2, 1, 5, "x")}, text);

    ASSERT_EQ(peeks[0].excerpt.size(), 3u);   // "one", "two", "three" - the final line break starts no fourth line
    EXPECT_EQ(peeks[0].excerpt[0], "one");
    EXPECT_EQ(peeks[0].excerpt[1], "two");
    EXPECT_EQ(peeks[0].excerpt[2], "three");
}

TEST(IssuePeek, ABytesColumnBecomesAUtf16UnitsColumn) {
    const std::string text = "int a;\n/*\xC3\xA9*/ int b;\n/*\xF0\x9F\x98\x80*/ int c;\n";

    EXPECT_EQ(utf16Column(text, 1, 5), 5u) << "ASCII: the same";
    // "/*é*/ int b;": 'b' is byte column 12 (é is two bytes) but UTF-16 column 11.
    EXPECT_EQ(utf16Column(text, 2, 12), 11u);
    // "/*😀*/ int c;": 'c' is byte column 14 (the emoji is four bytes) but UTF-16 column 12 (a pair).
    EXPECT_EQ(utf16Column(text, 3, 14), 12u);
    EXPECT_EQ(utf16Column(text, 2, 1), 1u);
}

TEST(IssuePeek, AColumnPastTheEndOfItsLineIsClampedToTheEndOfIt) {
    EXPECT_EQ(utf16Column("ab\ncd\n", 1, 99), 3u) << "one past the last character";
}

TEST(IssuePeek, ALineThatIsNotThereLeavesTheColumnAlone) {
    EXPECT_EQ(utf16Column("ab\n", 9, 7), 7u);
    EXPECT_EQ(utf16Column("ab\n", 0, 7), 7u);
    EXPECT_EQ(utf16Column("ab\n", 1, 0), 0u);
}

TEST(IssuePeek, AProblemOnALineThatIsNoLongerThereHasNoExcerpt) {
    const auto peeks = buildIssuePeeks({problem(99, 1, 0, "stale")}, kHeader);

    ASSERT_EQ(peeks.size(), 1u);
    EXPECT_TRUE(peeks[0].excerpt.empty());
    EXPECT_EQ(peeks[0].line, 99u);
    EXPECT_EQ(peeks[0].summary, "error 99:1  stale");
}
