#include "HeaderFlags.h"

#include <cmakemodel/model.h>
#include <cpptools/projectindex.h>

#include <newui/utils.h>

#include <algorithm>
#include <filesystem>
#include <map>
#include <set>

namespace CodeToolsVsix
{
    namespace
    {
        // How many leading folders two normalized paths share.
        std::size_t sharedFolders(const std::string& a, const std::string& b)
        {
            std::size_t shared = 0;
            std::size_t start = 0;
            for (;;) {
                const std::size_t slashA = a.find('/', start);
                const std::size_t slashB = b.find('/', start);
                if (slashA == std::string::npos || slashA != slashB || a.compare(start, slashA - start, b, start, slashB - start) != 0) break;
                ++shared;
                start = slashA + 1;
            }
            return shared;
        }

        bool isHeader(const std::string& path)
        {
            const std::string ext = newui::toLowerCase(std::filesystem::path(path).extension().string());
            return ext == ".h" || ext == ".hpp" || ext == ".hxx" || ext == ".hh" || ext == ".inl";
        }
    }

    std::size_t borrowHeaderSettings(const cpptools::ProjectIndex& index, cmakemodel::CompileSettingsIndex& settings,
                                     std::vector<std::string>* borrowed)
    {
        const std::vector<std::string> files = index.files();

        // included file -> the files that include it, as the index recorded them
        std::map<std::string, std::vector<std::string>> includers;
        for (const std::string& file : files) {
            for (const cpptools::IncludeEdge& edge : index.includesOf(file)) {
                if (edge.external || edge.included.empty()) continue;
                includers[newui::toLowerCase(cpptools::ProjectIndex::normalizePath(edge.included))].push_back(file);
            }
        }

        constexpr std::size_t kMaxDepth = 8;
        std::size_t count = 0;
        for (const std::string& header : files) {
            if (!isHeader(header) || settings.find(header) != nullptr) continue;   // a target already says how it builds

            // Breadth first over who includes it: the first level with a file a target compiles gives the settings.
            std::set<std::string> seen{ newui::toLowerCase(header) };
            std::vector<std::string> level{ header };
            std::string chosen;
            for (std::size_t depth = 0; depth < kMaxDepth && chosen.empty() && !level.empty(); ++depth) {
                std::vector<std::string> next;
                for (const std::string& current : level) {
                    auto found = includers.find(newui::toLowerCase(cpptools::ProjectIndex::normalizePath(current)));
                    if (found == includers.end()) continue;
                    for (const std::string& includer : found->second) {
                        if (!seen.insert(newui::toLowerCase(includer)).second) continue;
                        next.push_back(includer);
                        if (settings.find(includer) == nullptr) continue;
                        const bool closer = chosen.empty() || sharedFolders(includer, header) > sharedFolders(chosen, header) ||
                                            (sharedFolders(includer, header) == sharedFolders(chosen, header) && includer < chosen);
                        if (closer) chosen = includer;
                    }
                }
                level = std::move(next);
            }
            if (chosen.empty()) continue;
            settings.borrow(header, chosen);
            ++count;
            if (borrowed != nullptr) borrowed->push_back(header);
        }
        return count;
    }
}
