#pragma once

#include <string>
#include <vector>

#include "DocumentEditService.h"

namespace CodeToolsVsix
{
    struct IncludePlan
    {
        // The wanted headers the file doesn't include yet, in the order given.
        std::vector<std::string> missing;
        // The one insertion that adds them (UTF-16, against the content planned on), or none when
        // nothing is missing. It lands in the same edit group as whatever else is being changed.
        std::vector<TextEdit> edits;
    };

    // Which of `wanted` - include targets with their delimiters, "<newui/controls.h>" - a file lacks, and
    // the edit that adds them: after the file's last #include; else after #pragma once (blank line between);
    // else at the very top. A header counts as present when some #include line names the same path, with
    // either delimiter, any spacing, either slash and any case - it does not matter that it is also
    // reachable through another include: a file includes what it uses directly. Uses the file's own line
    // ending. Textual and instant - no parse - so it can run on the UI thread.
    IncludePlan planIncludes(const std::wstring& content, const std::vector<std::string>& wanted);
}
