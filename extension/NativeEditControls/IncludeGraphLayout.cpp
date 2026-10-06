#include "IncludeGraphLayout.h"

#include <cpptools/includeanalysis.h>

#include <algorithm>
#include <filesystem>
#include <map>

namespace CodeToolsVsix
{
    namespace
    {
        std::string keyOf(const std::string& path)
        {
            std::string key = cpptools::ProjectIndex::normalizePath(path);
            std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return key;
        }

        // The files one step out from `from`, in `direction` (+1 what they include, -1 who includes them).
        std::vector<std::string> neighbours(const cpptools::IncludeGraph& graph, const std::string& from, int direction)
        {
            std::vector<std::string> found;
            if (direction > 0) {
                for (const cpptools::IncludeEdge& edge : graph.includesOf(from)) found.push_back(edge.included);
            } else {
                found = graph.includersOf(from);
            }
            return found;
        }
    }

    IncludeGraphLayout layoutIncludeGraph(const cpptools::IncludeGraph& graph, const std::string& focus, std::size_t depth,
                                          std::size_t perColumn)
    {
        IncludeGraphLayout layout;
        const cpptools::HeaderImpact start = graph.impactOf(focus);
        if (!graph.contains(focus) || perColumn == 0) return layout;

        std::map<std::string, std::size_t> placed;   // key -> node index
        auto add = [&](const std::string& path, int column) {
            const cpptools::HeaderImpact impact = graph.impactOf(path);
            IncludeGraphNode node;
            node.path = impact.path.empty() ? path : impact.path;
            node.label = std::filesystem::path(node.path).filename().string();
            node.column = column;
            node.units = impact.transitiveSources;
            node.external = impact.external;
            layout.nodes.push_back(std::move(node));
            placed[keyOf(path)] = layout.nodes.size() - 1;
        };
        add(start.path, 0);
        placed[keyOf(focus)] = 0;

        for (const int direction : { 1, -1 }) {
            std::vector<std::size_t> frontier = { 0 };
            for (std::size_t level = 1; level <= depth; ++level) {
                const int column = direction * static_cast<int>(level);
                std::vector<std::string> found;
                for (const std::size_t from : frontier) {
                    for (const std::string& path : neighbours(graph, layout.nodes[from].path, direction)) {
                        if (placed.count(keyOf(path)) != 0) continue;
                        if (std::find_if(found.begin(), found.end(), [&](const std::string& p) { return keyOf(p) == keyOf(path); }) != found.end()) continue;
                        found.push_back(path);
                    }
                }
                if (found.empty()) break;
                std::vector<std::pair<std::size_t, std::string>> ranked;
                for (const std::string& path : found) ranked.emplace_back(graph.impactOf(path).transitiveSources, path);
                std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
                    return a.first != b.first ? a.first > b.first : a.second < b.second;
                });
                frontier.clear();
                const std::size_t kept = std::min(ranked.size(), perColumn);
                for (std::size_t i = 0; i < kept; ++i) {
                    add(ranked[i].second, column);
                    frontier.push_back(layout.nodes.size() - 1);
                }
                if (ranked.size() > kept) {
                    IncludeGraphNode more;
                    more.column = column;
                    more.hidden = ranked.size() - kept;
                    more.label = "+" + std::to_string(more.hidden) + " more";
                    layout.nodes.push_back(std::move(more));
                }
            }
        }

        // rows: top down within a column, in the order the nodes were added (widest first); the focus is alone
        std::map<int, std::size_t> next;
        for (IncludeGraphNode& node : layout.nodes) {
            node.row = next[node.column]++;
            layout.firstColumn = std::min(layout.firstColumn, node.column);
            layout.lastColumn = std::max(layout.lastColumn, node.column);
            layout.tallestColumn = std::max(layout.tallestColumn, node.row + 1);
        }

        // an edge for each include between boxes one column apart
        for (std::size_t i = 0; i < layout.nodes.size(); ++i) {
            const IncludeGraphNode& node = layout.nodes[i];
            if (node.path.empty()) continue;
            for (const cpptools::IncludeEdge& edge : graph.includesOf(node.path)) {
                auto target = placed.find(keyOf(edge.included));
                if (target == placed.end()) continue;
                if (layout.nodes[target->second].column != node.column + 1) continue;
                layout.edges.push_back({ i, target->second });
            }
        }
        return layout;
    }
}
