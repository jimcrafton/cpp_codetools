#include "cpptools/includeanalysis.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <set>

namespace cpptools {

namespace {

std::string keyOf(const std::string& path) {
    std::string key = ProjectIndex::normalizePath(path);
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return key;
}

std::string stemOf(const std::string& path) {
    return std::filesystem::path(path).stem().string();
}

bool isHeader(const std::string& path) {
    const std::string ext = std::filesystem::path(path).extension().string();
    return ext == ".h" || ext == ".hpp" || ext == ".hxx";
}

}  // namespace

IncludeGraph::IncludeGraph(const ProjectIndex& index) {
    auto nodeFor = [this](const std::string& path) -> std::size_t {
        const std::string key = keyOf(path);
        auto found = byKey_.find(key);
        if (found != byKey_.end()) return found->second;
        Node node;
        node.path = ProjectIndex::normalizePath(path);
        nodes_.push_back(std::move(node));
        byKey_.emplace(key, nodes_.size() - 1);
        return nodes_.size() - 1;
    };

    for (const std::string& file : index.files()) {
        const std::size_t self = nodeFor(file);
        nodes_[self].indexed = true;
        nodes_[self].includes = index.includesOf(file);
        for (const auto& reference : index.referencesOf(file)) nodes_[self].refers.push_back(reference.first);
        for (const IncludeEdge& edge : nodes_[self].includes) {
            const std::size_t target = nodeFor(edge.included);
            nodes_[target].external = nodes_[target].external || edge.external;
        }
    }
    // A header the index parsed on its own is a project file, whatever an edge to it said.
    for (Node& node : nodes_) {
        if (node.indexed) node.external = false;
    }

    for (std::size_t from = 0; from < nodes_.size(); ++from) {
        for (const IncludeEdge& edge : nodes_[from].includes) {
            std::vector<std::size_t>& list = nodes_[find(edge.included)].includers;
            if (std::find(list.begin(), list.end(), from) == list.end()) list.push_back(from);
        }
    }
    for (Node& node : nodes_) {
        std::sort(node.includers.begin(), node.includers.end(),
                  [this](std::size_t a, std::size_t b) { return nodes_[a].path < nodes_[b].path; });
    }

    for (const auto& declared : index.declaringFiles()) {
        std::vector<std::size_t>& list = declaredIn_[declared.first];
        for (const std::string& file : declared.second) {
            const std::size_t node = find(file);
            if (node == std::size_t(-1)) continue;
            list.push_back(node);
            nodes_[node].declares = true;
        }
    }
}

std::size_t IncludeGraph::find(const std::string& path) const {
    auto found = byKey_.find(keyOf(path));
    return found == byKey_.end() ? std::size_t(-1) : found->second;
}

// How many nodes lie above (who includes me, through any depth) or below (what I include) `from`.
std::size_t IncludeGraph::count(std::size_t from, bool upward, bool sourcesOnly) const {
    std::vector<bool> seen(nodes_.size(), false);
    std::vector<std::size_t> pending{ from };
    seen[from] = true;
    std::size_t total = 0;
    while (!pending.empty()) {
        const std::size_t current = pending.back();
        pending.pop_back();
        auto visit = [&](std::size_t next) {
            if (next == std::size_t(-1) || seen[next]) return;
            seen[next] = true;
            if (!sourcesOnly || (nodes_[next].indexed && !isHeader(nodes_[next].path))) ++total;
            pending.push_back(next);
        };
        if (upward) {
            for (std::size_t next : nodes_[current].includers) visit(next);
        } else {
            for (const IncludeEdge& edge : nodes_[current].includes) visit(find(edge.included));
        }
    }
    return total;
}

std::vector<bool> IncludeGraph::reach(std::size_t from) const {
    std::vector<bool> seen(nodes_.size(), false);
    std::vector<std::size_t> pending{ from };
    seen[from] = true;
    while (!pending.empty()) {
        const std::size_t current = pending.back();
        pending.pop_back();
        for (const IncludeEdge& edge : nodes_[current].includes) {
            const std::size_t next = find(edge.included);
            if (next == std::size_t(-1) || seen[next]) continue;
            seen[next] = true;
            pending.push_back(next);
        }
    }
    return seen;
}

HeaderImpact IncludeGraph::impactOf(const std::string& path) const {
    HeaderImpact impact;
    const std::size_t node = find(path);
    if (node == std::size_t(-1)) {
        impact.path = ProjectIndex::normalizePath(path);
        return impact;
    }
    impact.path = nodes_[node].path;
    impact.external = nodes_[node].external;
    impact.directIncluders = nodes_[node].includers.size();
    impact.transitiveIncluders = count(node, true);
    impact.transitiveSources = count(node, true, true);
    impact.transitiveIncludes = count(node, false);
    return impact;
}

std::vector<HeaderImpact> IncludeGraph::ranked(bool external) const {
    std::vector<HeaderImpact> found;
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        if (nodes_[i].includers.empty()) continue;
        if (nodes_[i].external && !external) continue;
        found.push_back(impactOf(nodes_[i].path));
    }
    std::sort(found.begin(), found.end(), [](const HeaderImpact& a, const HeaderImpact& b) {
        if (a.transitiveSources != b.transitiveSources) return a.transitiveSources > b.transitiveSources;
        if (a.transitiveIncluders != b.transitiveIncluders) return a.transitiveIncluders > b.transitiveIncluders;
        return a.path < b.path;
    });
    return found;
}

std::size_t IncludeGraph::projectFileCount() const {
    return static_cast<std::size_t>(std::count_if(nodes_.begin(), nodes_.end(), [](const Node& n) { return n.indexed; }));
}

std::size_t IncludeGraph::sourceFileCount() const {
    return static_cast<std::size_t>(std::count_if(nodes_.begin(), nodes_.end(),
                                                  [](const Node& n) { return n.indexed && !isHeader(n.path); }));
}

std::vector<HeaderImpact> IncludeGraph::projectFiles() const {
    std::vector<HeaderImpact> found;
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        if (!nodes_[i].indexed) continue;
        HeaderImpact impact;
        impact.path = nodes_[i].path;
        impact.transitiveIncludes = count(i, false);
        found.push_back(std::move(impact));
    }
    return found;
}

std::vector<std::string> IncludeGraph::includersOf(const std::string& path) const {
    std::vector<std::string> found;
    const std::size_t node = find(path);
    if (node == std::size_t(-1)) return found;
    for (std::size_t includer : nodes_[node].includers) found.push_back(nodes_[includer].path);
    return found;
}

std::vector<IncludeEdge> IncludeGraph::includesOf(const std::string& path) const {
    const std::size_t node = find(path);
    return node == std::size_t(-1) ? std::vector<IncludeEdge>() : nodes_[node].includes;
}

std::vector<PruneHint> IncludeGraph::pruneHintsFor(const std::string& includer) const {
    std::vector<PruneHint> hints;
    const std::size_t from = find(includer);
    if (from == std::size_t(-1) || !nodes_[from].indexed) return hints;
    const Node& node = nodes_[from];
    if (isHeader(node.path) && !node.declares) return hints;   // an umbrella header exists to include

    // the nodes declaring what this file refers to
    std::set<std::size_t> used;
    for (const std::string& usr : node.refers) {
        auto found = declaredIn_.find(usr);
        if (found != declaredIn_.end()) used.insert(found->second.begin(), found->second.end());
    }

    std::set<std::size_t> judgedHeaders;
    for (const IncludeEdge& edge : node.includes) {
        const std::size_t header = find(edge.included);
        if (header == std::size_t(-1) || nodes_[header].external || !nodes_[header].indexed) continue;
        if (stemOf(nodes_[header].path) == stemOf(node.path)) continue;   // a file's own header
        if (!judgedHeaders.insert(header).second) continue;

        const std::vector<bool> below = reach(header);
        bool declaresAnything = false;
        bool usedFromIt = false;
        for (std::size_t i = 0; i < below.size() && !usedFromIt; ++i) {
            if (!below[i]) continue;
            if (used.count(i) != 0) usedFromIt = true;
            if (nodes_[i].declares) declaresAnything = true;
        }
        // Only a header that declares something can be judged unused: one of macros alone says nothing.
        if (!usedFromIt && declaresAnything) hints.push_back(PruneHint{ node.path, nodes_[header].path, edge.line });
    }
    return hints;
}

std::vector<PruneHint> IncludeGraph::pruneHints() const {
    std::vector<PruneHint> hints;
    for (const Node& node : nodes_) {
        if (!node.indexed) continue;
        std::vector<PruneHint> mine = pruneHintsFor(node.path);
        hints.insert(hints.end(), mine.begin(), mine.end());
    }
    return hints;
}

}  // namespace cpptools
