#pragma once

#include "ExplorerModel.h"

#include <cpptools_analysis/macroanalysis.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace cpptools { class IncludeGraph; }

namespace CodeToolsVsix
{
    // The Analysis view's three sub-tabs, and what the Includes one can show.
    enum class AnalysisTab { Includes = 0, Macros = 1, Templates = 2 };
    enum class IncludeView { Impact = 0, PerFile = 1 };
    enum class MacroScope { ActiveFile = 0, Folder = 1 };

    // Includes, impact mode: the project's headers by how many files a change to each one rebuilds
    // (each opens onto the files that include it directly), then the includes that look removable.
    // `root` shortens the paths; `filter` (any case) keeps the rows whose path contains it.
    ExplorerNode buildIncludeImpactTree(const std::shared_ptr<const cpptools::IncludeGraph>& graph,
                                        const std::string& root, const std::string& filter = std::string());

    // Includes, per-file mode: each project file, opening onto what it includes, and so on down, one level at a time.
    ExplorerNode buildIncludeFileTree(const std::shared_ptr<const cpptools::IncludeGraph>& graph,
                                      const std::string& root, const std::string& filter = std::string());

    // One analyzed file, for the Macros view.
    struct MacroFile
    {
        std::string path;
        cpptools_analysis::MacroAnalysis analysis;
    };

    // Macros, listed first: each macro the file(s) use, with its uses under it, then the inactive regions and the
    // arguments evaluated more than once. `folderScope` says the files are a folder's, not one file: uses and
    // regions then name their file. Selecting a use shows its expansion steps in the detail pane (the row's `card`).
    ExplorerNode buildMacroTree(const std::vector<MacroFile>& files, const std::string& root, bool folderScope,
                                const std::string& filter = std::string());
}
