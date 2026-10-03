// collectProblems / problemOnLine / nextProblem / previousProblem / problemSummary: the lines with a
// squiggle that the C++ editor's "1 of N problems" steps through - plain data, no UI.

#include <gtest/gtest.h>

#include "../extension/NativeEditControls/ProblemList.h"
#include "../extension/NativeEditControls/HighlightController.h"

using namespace CodeToolsVsix;

namespace {

newui::text::TextStyleRange range(std::size_t start, std::size_t length, const char* style, const std::string& message = "") {
    newui::text::TextStyleRange r;
    r.start = start;
    r.length = length;
    r.style = style;
    r.annotation = message;
    return r;
}

const std::wstring kText = L"int a;\nint b = ;\nint c;\nint d = e;\n";   // lines 0..3 start at 0, 7, 17, 24

std::vector<Problem> linesWithProblems(std::initializer_list<std::size_t> lines) {
    std::vector<Problem> problems;
    for (std::size_t line : lines) {
        Problem p;
        p.line = line;
        problems.push_back(p);
    }
    return problems;
}

}

TEST(ProblemList, OneProblemPerLineInLineOrderWithItsPositionAndMessage) {
    const auto problems = collectProblems({
        range(32, 1, kProblemStyleName, "undeclared 'e'"),     // line 3, the 'e'
        range(15, 1, kProblemStyleName, "expected expression") // line 1, the ';'
    }, kText);

    ASSERT_EQ(problems.size(), 2u);
    EXPECT_EQ(problems[0].line, 1u);
    EXPECT_EQ(problems[0].start, 15u);
    EXPECT_EQ(problems[0].column, 8u);   // 15 - 7
    EXPECT_EQ(problems[0].message, "expected expression");
    EXPECT_TRUE(problems[0].isError);
    EXPECT_EQ(problems[1].line, 3u);
    EXPECT_EQ(problems[1].column, 8u);   // 32 - 24
}

TEST(ProblemList, SeveralRangesOnALineAreOneProblem) {
    const auto problems = collectProblems({ range(7, 3, kProblemStyleName), range(15, 1, kProblemStyleName, "later") }, kText);

    ASSERT_EQ(problems.size(), 1u);
    EXPECT_EQ(problems[0].start, 7u) << "the first one on the line";
    EXPECT_EQ(problems[0].message, "later") << "but a message found on the line is kept";
}

TEST(ProblemList, OtherStylesAreNotProblems) {
    const auto problems = collectProblems({ range(0, 3, "keyword"), range(4, 1, "match"), range(15, 1, kWarningStyleName) }, kText);

    ASSERT_EQ(problems.size(), 1u);
    EXPECT_FALSE(problems[0].isError) << "a warning";
}

TEST(ProblemList, AnErrorOutranksAWarningOnTheSameLine) {
    const auto problems = collectProblems({
        range(7, 3, kWarningStyleName, "a warning"), range(15, 1, kProblemStyleName, "an error") }, kText);

    ASSERT_EQ(problems.size(), 1u);
    EXPECT_TRUE(problems[0].isError);
    EXPECT_EQ(problems[0].message, "an error");
    EXPECT_EQ(problems[0].start, 15u);
}

TEST(ProblemList, CrLfAndLoneCrEndLinesToo) {
    const std::wstring text = L"one\r\ntwo\rthree\nfour";   // lines start at 0, 5, 9, 15
    const auto problems = collectProblems({ range(6, 1, kProblemStyleName), range(10, 1, kProblemStyleName),
                                            range(16, 1, kProblemStyleName) }, text);

    ASSERT_EQ(problems.size(), 3u);
    EXPECT_EQ(problems[0].line, 1u);
    EXPECT_EQ(problems[1].line, 2u);
    EXPECT_EQ(problems[2].line, 3u);
    EXPECT_EQ(problems[2].column, 1u);
}

TEST(ProblemList, ARangePastTheEndOfTheTextIsClampedToItsLastLine) {
    const auto problems = collectProblems({ range(9999, 1, kProblemStyleName) }, L"a\nb");
    ASSERT_EQ(problems.size(), 1u);
    EXPECT_EQ(problems[0].line, 1u);
}

TEST(ProblemList, NoRangesNoProblems) {
    EXPECT_TRUE(collectProblems({}, kText).empty());
}

TEST(ProblemList, TheCaretIsOnAProblemOnlyOnItsLine) {
    const auto problems = linesWithProblems({ 1, 3, 8 });

    EXPECT_EQ(problemOnLine(problems, 3), std::optional<std::size_t>(1));
    EXPECT_EQ(problemOnLine(problems, 8), std::optional<std::size_t>(2));
    EXPECT_FALSE(problemOnLine(problems, 0).has_value());
    EXPECT_FALSE(problemOnLine(problems, 2).has_value());
    EXPECT_FALSE(problemOnLine(problems, 99).has_value());
}

TEST(ProblemList, NextIsTheFirstLaterLineAndWrapsToTheFirst) {
    const auto problems = linesWithProblems({ 1, 3, 8 });

    EXPECT_EQ(nextProblem(problems, 0), std::optional<std::size_t>(0));
    EXPECT_EQ(nextProblem(problems, 1), std::optional<std::size_t>(1)) << "from a problem's own line: the one after it";
    EXPECT_EQ(nextProblem(problems, 2), std::optional<std::size_t>(1));
    EXPECT_EQ(nextProblem(problems, 8), std::optional<std::size_t>(0)) << "past the last: wraps";
    EXPECT_EQ(nextProblem(problems, 50), std::optional<std::size_t>(0));
}

TEST(ProblemList, PreviousIsTheLastEarlierLineAndWrapsToTheLast) {
    const auto problems = linesWithProblems({ 1, 3, 8 });

    EXPECT_EQ(previousProblem(problems, 9), std::optional<std::size_t>(2));
    EXPECT_EQ(previousProblem(problems, 8), std::optional<std::size_t>(1)) << "from a problem's own line: the one before it";
    EXPECT_EQ(previousProblem(problems, 4), std::optional<std::size_t>(1));
    EXPECT_EQ(previousProblem(problems, 1), std::optional<std::size_t>(2)) << "before the first: wraps";
    EXPECT_EQ(previousProblem(problems, 0), std::optional<std::size_t>(2));
}

TEST(ProblemList, WithNoProblemsThereIsNowhereToGo) {
    EXPECT_FALSE(nextProblem({}, 3).has_value());
    EXPECT_FALSE(previousProblem({}, 3).has_value());
    EXPECT_FALSE(problemOnLine({}, 3).has_value());
}

TEST(ProblemList, WithOneProblemNextAndPreviousStayOnIt) {
    const auto problems = linesWithProblems({ 4 });
    EXPECT_EQ(nextProblem(problems, 4), std::optional<std::size_t>(0));
    EXPECT_EQ(previousProblem(problems, 4), std::optional<std::size_t>(0));
}

TEST(ProblemList, TheSummaryCountsAndSaysWhichOneTheCaretIsOn) {
    EXPECT_EQ(problemSummary(0, std::nullopt), "");
    EXPECT_EQ(problemSummary(1, std::nullopt), "1 problem");
    EXPECT_EQ(problemSummary(3, std::nullopt), "3 problems");
    EXPECT_EQ(problemSummary(3, std::optional<std::size_t>(1)), "2 of 3 problems");
    EXPECT_EQ(problemSummary(1, std::optional<std::size_t>(0)), "1 of 1 problems");
}
