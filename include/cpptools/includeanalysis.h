#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "cpptools/projectindex.h"

namespace cpptools {

// What one header costs the project: how many files a change to it makes the build redo.
struct HeaderImpact {
    std::string path;                 // normalized
    bool external = false;            // outside the project roots (system or third-party)
    std::size_t directIncluders = 0;  // files that #include it themselves
    std::size_t transitiveIncluders = 0;   // ... and the files that reach it through those
    std::size_t transitiveSources = 0;     // of those, the translation units: indexed files that are not headers
    std::size_t transitiveIncludes = 0;    // what it pulls in itself, through any depth
};

// An include that may be removable: nothing the including file refers to is declared in the header, or in
// anything the header includes. A hint, not a proof: macros, templates specialised elsewhere and
// what libclang does not record as a reference all go unseen.
struct PruneHint {
    std::string includer;
    std::string header;
    std::size_t line = 0;
};

// A snapshot of the include graph of a ProjectIndex, taken at construction, so it can be read while the
// index keeps changing and from any thread.
class IncludeGraph {
public:
    explicit IncludeGraph(const ProjectIndex& index);

    // Headers (any file something includes) by transitiveIncluders, largest first, then by path.
    // `external` false leaves out headers outside the project roots.
    std::vector<HeaderImpact> ranked(bool external = false) const;

    // The impact of one file; zeros for a file nothing includes and that includes nothing.
    HeaderImpact impactOf(const std::string& path) const;

    // Files that include `path` directly, sorted; and what `path` includes directly, in directive order.
    std::vector<std::string> includersOf(const std::string& path) const;
    std::vector<IncludeEdge> includesOf(const std::string& path) const;

    // Every include that may be removable, see PruneHint. Only project headers are judged.
    std::vector<PruneHint> pruneHints() const;
    // The hints for one file's includes.
    std::vector<PruneHint> pruneHintsFor(const std::string& includer) const;

    // Files the index parsed itself (the project's own, not what they include from outside); the second form
    // fills only path and transitiveIncludes, which is cheap enough to list them all.
    std::size_t projectFileCount() const;
    std::size_t sourceFileCount() const;   // of those, the ones that are not headers: the translation units
    std::vector<HeaderImpact> projectFiles() const;

    std::size_t fileCount() const { return nodes_.size(); }

    // Whether the graph has this file at all.
    bool contains(const std::string& path) const { return find(path) != static_cast<std::size_t>(-1); }

private:
    struct Node {
        std::string path;
        bool external = false;
        bool declares = false;  // declares at least one symbol
        bool indexed = false;   // parsed by the index itself, so it has includes and references
        std::vector<IncludeEdge> includes;
        std::vector<std::size_t> includers;   // node indexes, sorted by path
        std::vector<std::string> refers;      // USRs this file's own code refers to
    };

    std::size_t find(const std::string& path) const;   // npos when unknown
    std::size_t count(std::size_t from, bool upward, bool sourcesOnly = false) const;
    std::vector<bool> reach(std::size_t from) const;   // the node and everything it includes, by node index

    std::vector<Node> nodes_;
    std::map<std::string, std::size_t> byKey_;   // lowered normalized path -> node index
    std::map<std::string, std::vector<std::size_t>> declaredIn_;   // USR -> nodes that declare it
};

}  // namespace cpptools
