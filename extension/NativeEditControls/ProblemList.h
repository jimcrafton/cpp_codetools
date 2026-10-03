#pragma once

#include <newui/textstyle.h>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace CodeToolsVsix
{
    // One line of the C++ editor that has a problem (a squiggled error or warning) - what the status
    // bar's "1 of N problems" steps through and the minimap shows a tick for. Plain data and plain
    // functions, so it needs no UI to test.
    struct Problem
    {
        std::size_t start = 0;    // offset of the first problem range on its line
        std::size_t line = 0;     // 0-based
        std::size_t column = 0;   // 0-based, in UTF-16 units, of `start` within its line
        bool isError = true;      // false: a warning only
        std::string message;      // clang's, if the range carries one (a lexer-level problem has none)
    };

    // The problems among ranges (the styled ranges of a text control), one per line, in line order. A
    // line with both an error and a warning is an error. Ranges of any other style are ignored.
    std::vector<Problem> collectProblems(const std::vector<newui::text::TextStyleRange>& ranges,
                                         const std::wstring& text);

    // The problem whose line is caretLine (0-based), if the caret is on one.
    std::optional<std::size_t> problemOnLine(const std::vector<Problem>& problems, std::size_t caretLine);
    // The first problem on a line after caretLine, wrapping around to the first; none if there are none.
    std::optional<std::size_t> nextProblem(const std::vector<Problem>& problems, std::size_t caretLine);
    // The last problem on a line before caretLine, wrapping around to the last; none if there are none.
    std::optional<std::size_t> previousProblem(const std::vector<Problem>& problems, std::size_t caretLine);

    // "" (no problems), "1 problem" / "3 problems", or "2 of 3 problems" when the caret is on one.
    std::string problemSummary(std::size_t count, std::optional<std::size_t> current);
}
