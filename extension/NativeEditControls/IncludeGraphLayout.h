#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace cpptools { class IncludeGraph; }

namespace CodeToolsVsix
{
    // One box in the include graph.
    struct IncludeGraphNode
    {
        std::string path;           // empty for a "+N more" box
        std::string label;
        int column = 0;             // 0: the focused file; negative: files that include it; positive: what it includes
        std::size_t row = 0;        // within its column, top down
        std::size_t units = 0;      // translation units a change to it reaches
        bool external = false;      // outside the project roots
        std::size_t hidden = 0;     // for a "+N more" box: how many it stands for
    };

    // `from` includes `to`.
    struct IncludeGraphEdge
    {
        std::size_t from = 0;
        std::size_t to = 0;
    };

    // The files around one file: who includes it on the left, what it includes on the right, `depth` levels each
    // way. Boxes in a column are ordered by how many translation units they reach, the widest first.
    struct IncludeGraphLayout
    {
        std::vector<IncludeGraphNode> nodes;   // nodes[0] is the focused file
        std::vector<IncludeGraphEdge> edges;
        int firstColumn = 0;
        int lastColumn = 0;
        std::size_t tallestColumn = 0;         // most boxes in any column

        bool empty() const { return nodes.empty(); }
    };

    // The layout around `focus`. A column keeps at most `perColumn` boxes; the rest become one "+N more" box.
    // Empty for a file the graph does not know.
    IncludeGraphLayout layoutIncludeGraph(const cpptools::IncludeGraph& graph, const std::string& focus,
                                          std::size_t depth = 2, std::size_t perColumn = 8);
}
