#pragma once

#include <cpptools/diagnostic.h>

#include <cstddef>
#include <string>
#include <vector>

namespace CodeToolsVsix
{
    // One problem, ready to show in a peek: a few lines of the source around it, where to underline,
    // and the line of text its list row shows. Plain data, so it can be built (and tested) without
    // any UI.
    struct IssuePeek
    {
        // How many lines of context around the problem's own line (so up to 2 * kContext + 1).
        static constexpr std::size_t kContext = 2;
        static constexpr std::size_t kTabWidth = 4;   // a tab in an excerpt is shown as this many spaces

        std::string summary;        // "error 6:10  'bojangle' file not found"
        std::string message;        // "'bojangle' file not found"
        bool isError = true;        // false: a warning
        std::size_t line = 0;       // the problem's own position, 1-based, as clang reported it
        std::size_t column = 0;     // (a byte column in the source line)

        std::size_t firstLine = 0;  // the source line number of excerpt[0]
        std::vector<std::string> excerpt;   // tabs already expanded
        // Where the underline goes within the problem's own excerpt line, in the excerpt's (tab-expanded)
        // columns, 0-based. underlineLength 0 = nothing to underline.
        std::size_t underlineStart = 0;
        std::size_t underlineLength = 0;

        // Index of the problem's own line within excerpt.
        std::size_t excerptIndex() const { return line - firstLine; }
    };

    // The peeks for issues (in the same order), whose positions are in headerText (UTF-8, LF or CRLF
    // lines). A problem whose line isn't in the text (a stale diagnostic) gets an empty excerpt.
    std::vector<IssuePeek> buildIssuePeeks(const std::vector<cpptools::Diagnostic>& issues,
                                           const std::string& headerText);
}
