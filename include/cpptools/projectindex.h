#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "cpptools/symbol.h"

namespace cpptools {

// A declaration found by the index: a class, a function, a field, ... Identified across files by its
// USR (libclang's Unified Symbol Resolution string), so a method declared in a header and defined in a
// .cpp is one symbol seen from two files.
struct IndexedSymbol {
    std::string usr;
    std::string name;            // "Session"
    std::string qualifiedName;   // "cpptools::Session"; anonymous namespaces are left out
    SymbolKind kind = SymbolKind::Other;
    SourceLocation location;     // where this declaration is written (file is normalized, see below)
    bool isDefinition = false;   // a class body, a function body; false for a forward declaration or prototype
    std::string parentUsr;       // the enclosing class or namespace; empty at the top level
    std::vector<std::string> bases;   // for a class: its base classes as written, qualified ("shapes::Shape")
};

// One #include written directly in a file.
struct IncludeEdge {
    std::string includer;        // normalized path
    std::string included;        // the file it resolved to, normalized
    std::size_t line = 0;        // 1-based line of the directive
    bool external = false;       // outside the project roots (a system or third-party header)
};

// Everything the index knows about one file: what it declares, what it includes, and what its own code
// refers to. Plain data, so it can be saved and compared.
// One diagnostic kept as an example of why a file has the errors, warnings or unresolved includes it has.
struct IndexedDiagnostic {
    std::string file;            // where libclang placed it (a header, for a "file not found" inside one)
    std::uint32_t line = 0;      // 1-based
    std::string message;
};

struct IndexedFile {
    std::string path;            // normalized: absolute, forward slashes
    std::int64_t modified = 0;   // last write time when it was parsed
    std::uint64_t size = 0;
    std::uint64_t flagsHash = 0; // of the compile flags it was parsed with
    std::uint64_t contentHash = 0;   // of the file's bytes when it was parsed; 0: unknown (a buffer, not the file)
    bool parsed = false;         // false: libclang produced nothing (see `error`)
    std::string error;
    // Diagnostics libclang reported in this file itself (not in a header it includes): errors include fatal ones,
    // but not "file not found", which says more about the compile flags than about the code: those are counted apart.
    std::uint32_t errors = 0;
    std::uint32_t warnings = 0;
    std::uint32_t unresolvedIncludes = 0;
    std::vector<IndexedDiagnostic> samples;   // the first few of those, in the order libclang reported them
    std::vector<IndexedSymbol> symbols;
    std::vector<IncludeEdge> includes;
    // USR -> how often this file's own code refers to it. Only declarations inside the project roots.
    std::map<std::string, std::uint32_t> references;
};

struct IndexProgress {
    std::size_t total = 0;
    std::size_t done = 0;        // parsed + skipped + failed
    std::size_t parsed = 0;
    std::size_t skipped = 0;     // already up to date
    std::size_t failed = 0;
    std::string current;         // the file that finished last
    std::vector<std::string> reread;   // the files parsed (or tried) this run: what was not up to date
};

class ProjectIndex {
public:
    // The flags to parse one file with; the default is compileFlagsFor(file).args.
    using FlagsProvider = std::function<std::vector<std::string>(const std::string& file)>;
    // Called after each file, from a worker thread (one at a time). Return false to stop.
    using ProgressCallback = std::function<bool(const IndexProgress&)>;

    ProjectIndex();
    ~ProjectIndex();
    ProjectIndex(const ProjectIndex&) = delete;
    ProjectIndex& operator=(const ProjectIndex&) = delete;

    // Directories whose declarations count as "the project": references to a declaration elsewhere are
    // not recorded, and an include of a file elsewhere is marked external. With none set, only system
    // headers are left out.
    void setRoots(std::vector<std::string> roots);
    void setFlagsProvider(FlagsProvider provider);

    // Parses the files that are new or changed since they were last indexed (modification time, size or
    // compile flags), on `threads` workers (0: all but one core). Blocks until done or stopped; call it
    // from a background thread. Files given twice are indexed once.
    IndexProgress indexFiles(const std::vector<std::string>& files, const ProgressCallback& progress = {},
                             unsigned threads = 0);

    // Parses one file now, whatever its freshness - on save, or with `content` for an editor buffer that
    // is not saved (then the file's modification time is not recorded, so the next indexFiles() parses it
    // again). False if libclang produced nothing.
    bool updateFile(const std::string& path, const std::string* content = nullptr);
    void removeFile(const std::string& path);

    // --- queries; thread-safe, and answered from what is indexed at that moment ---------------------

    std::size_t fileCount() const;
    std::vector<std::string> files() const;
    bool hasFile(const std::string& path) const;
    std::vector<IndexedSymbol> symbolsIn(const std::string& path) const;
    std::vector<IncludeEdge> includesOf(const std::string& path) const;
    // USR -> how often `path`'s own code refers to it (IndexedFile::references); empty for a file not indexed.
    std::map<std::string, std::uint32_t> referencesOf(const std::string& path) const;
    // USR -> the files that declare it, once each (normalized paths); the whole index in one pass.
    std::map<std::string, std::vector<std::string>> declaringFiles() const;

    // How many errors and warnings the last parse of `path` reported in that file; zeros for a file not indexed.
    struct Problems {
        std::uint32_t errors = 0;
        std::uint32_t warnings = 0;
        std::uint32_t unresolvedIncludes = 0;
        std::vector<IndexedDiagnostic> samples;
    };
    Problems problemsIn(const std::string& path) const;

    // Classes, structs and class templates that have a definition somewhere, once per USR, whose
    // qualified name contains `filter` (any case); names that start with it first, then alphabetical.
    // An empty filter lists them all.
    std::vector<IndexedSymbol> findClasses(const std::string& filter = std::string()) const;

    // Every indexed declaration of one symbol (its prototype and its definition, for example).
    std::vector<IndexedSymbol> declarationsOf(const std::string& usr) const;

    // The files that include `path`, directly, or through other files too when `transitive`.
    std::vector<std::string> includers(const std::string& path, bool transitive = false) const;

    // How often code in the project refers to a symbol, and from how many files. References from the
    // file a symbol is declared in count: a class used only by its own implementation is not zero.
    std::size_t referenceCount(const std::string& usr) const;
    std::size_t referencingFileCount(const std::string& usr) const;

    // --- persistence ---------------------------------------------------------------------------------
    // Safe to call while indexing runs: it writes what is in at that moment.
    bool save(const std::string& cachePath) const;
    // Replaces what is indexed. False (and nothing changes) if the file is missing or not in this format.
    bool load(const std::string& cachePath);

    // The path as the index keys it: absolute, forward slashes, no "." or "..".
    static std::string normalizePath(const std::string& path);

private:
    bool write(const std::string& path) const;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace cpptools
