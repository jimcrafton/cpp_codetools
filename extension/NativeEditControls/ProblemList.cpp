#include "ProblemList.h"
#include "HighlightController.h"

#include <algorithm>
#include <map>

namespace CodeToolsVsix
{
    namespace
    {
        // Offsets at which each line starts (LF, CRLF and a lone CR all end a line, as FindEngine counts them).
        std::vector<std::size_t> lineStarts(const std::wstring& text)
        {
            std::vector<std::size_t> starts{ 0 };
            for (std::size_t i = 0; i < text.size(); ++i) {
                if (text[i] == L'\n') {
                    starts.push_back(i + 1);
                } else if (text[i] == L'\r') {
                    if (i + 1 < text.size() && text[i + 1] == L'\n') {
                        ++i;
                    }
                    starts.push_back(i + 1);
                }
            }
            return starts;
        }
    }

    std::vector<Problem> collectProblems(const std::vector<newui::text::TextStyleRange>& ranges,
                                         const std::wstring& text)
    {
        const std::vector<std::size_t> starts = lineStarts(text);

        // One problem per line; the map keeps them in line order.
        std::map<std::size_t, Problem> byLine;
        for (const newui::text::TextStyleRange& range : ranges) {
            const bool error = range.style == kProblemStyleName;
            if (!error && range.style != kWarningStyleName) {
                continue;
            }
            const std::size_t start = range.start < text.size() ? range.start : text.size();
            const std::size_t line = static_cast<std::size_t>(
                std::upper_bound(starts.begin(), starts.end(), start) - starts.begin()) - 1;

            auto found = byLine.find(line);
            if (found == byLine.end()) {
                Problem problem;
                problem.start = start;
                problem.line = line;
                problem.column = start - starts[line];
                problem.isError = error;
                problem.message = range.annotation;
                byLine.emplace(line, std::move(problem));
            } else if (error && !found->second.isError) {
                // A warning was there first; the error is what this line is now.
                found->second.isError = true;
                found->second.start = start;
                found->second.column = start - starts[line];
                found->second.message = range.annotation;
            } else if (found->second.message.empty() && !range.annotation.empty() && error == found->second.isError) {
                found->second.message = range.annotation;
            }
        }

        std::vector<Problem> problems;
        problems.reserve(byLine.size());
        for (auto& entry : byLine) {
            problems.push_back(std::move(entry.second));
        }
        return problems;
    }

    std::optional<std::size_t> problemOnLine(const std::vector<Problem>& problems, std::size_t caretLine)
    {
        const auto found = std::lower_bound(problems.begin(), problems.end(), caretLine,
            [](const Problem& problem, std::size_t line) { return problem.line < line; });
        if (found != problems.end() && found->line == caretLine) {
            return static_cast<std::size_t>(found - problems.begin());
        }
        return std::nullopt;
    }

    std::optional<std::size_t> nextProblem(const std::vector<Problem>& problems, std::size_t caretLine)
    {
        if (problems.empty()) {
            return std::nullopt;
        }
        const auto after = std::upper_bound(problems.begin(), problems.end(), caretLine,
            [](std::size_t line, const Problem& problem) { return line < problem.line; });
        return after == problems.end() ? 0 : static_cast<std::size_t>(after - problems.begin());
    }

    std::optional<std::size_t> previousProblem(const std::vector<Problem>& problems, std::size_t caretLine)
    {
        if (problems.empty()) {
            return std::nullopt;
        }
        const auto atOrAfter = std::lower_bound(problems.begin(), problems.end(), caretLine,
            [](const Problem& problem, std::size_t line) { return problem.line < line; });
        return atOrAfter == problems.begin() ? problems.size() - 1
                                             : static_cast<std::size_t>(atOrAfter - problems.begin()) - 1;
    }

    std::string problemSummary(std::size_t count, std::optional<std::size_t> current)
    {
        if (count == 0) {
            return std::string();
        }
        if (current.has_value()) {
            return std::to_string(*current + 1) + " of " + std::to_string(count) + " problems";
        }
        return std::to_string(count) + (count == 1 ? " problem" : " problems");
    }
}
