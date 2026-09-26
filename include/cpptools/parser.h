#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "cpptools/diagnostic.h"
#include "cpptools/symbol.h"

// Opaque libclang handle - avoids pulling <clang-c/Index.h> into every consumer of this header.
// Must match clang-c/Index.h's own typedef exactly (a bare `void*` alias, not a pointer to a
// named struct) or the two definitions conflict once a .cpp includes both headers.
typedef void* CXIndex;
// ...and the translation unit's (clang-c/Index.h: `typedef struct CXTranslationUnitImpl *CXTranslationUnit;`).
struct CXTranslationUnitImpl;

namespace cpptools {

// Disposes a libclang translation unit (defined in parser.cpp, where clang-c is included).
struct TranslationUnitDeleter {
    void operator()(CXTranslationUnitImpl* tu) const;
};

struct ParseResult {
    std::vector<Symbol> symbols;
    std::vector<Diagnostic> diagnostics;
};

// Move-only RAII wrapper around a libclang CXIndex. CXIndex is itself a `void*` alias (see
// clang-c/Index.h), so it doesn't fit std::unique_ptr's pointer-to-object model cleanly - a
// small hand-rolled wrapper is simpler here, same reasoning newui's own ClipboardScope uses for
// a non-pointer-shaped resource.
class ClangIndex {
public:
    ClangIndex();
    ~ClangIndex();

    ClangIndex(const ClangIndex&) = delete;
    ClangIndex& operator=(const ClangIndex&) = delete;
    ClangIndex(ClangIndex&& other) noexcept;
    ClangIndex& operator=(ClangIndex&& other) noexcept;

    CXIndex get() const { return index_; }

private:
    CXIndex index_ = nullptr;
};

// Default compile args - a fixed placeholder good enough for a single standalone file. Loading
// real per-file flags from a compile_commands.json is out of scope for now; compileArgs is
// already a parameter on both entry points so that can be layered on later without an API change.
std::vector<std::string> defaultCompileArgs();

class Parser {
public:
    Parser();

    // Parses filePath from disk.
    ParseResult parseFile(const std::string& filePath,
                           const std::vector<std::string>& compileArgs = defaultCompileArgs());

    // Parses content as if it were filePath's contents - lets a caller (or a test) supply an
    // in-memory buffer without touching disk, e.g. a live editor buffer that hasn't been saved.
    ParseResult parseBuffer(const std::string& filePath, const std::string& content,
                             const std::vector<std::string>& compileArgs = defaultCompileArgs());

private:
    ClangIndex index_;
};

// Parses one file again and again as it is edited, cheaply: the libclang translation unit is kept,
// and every parse after the first is a clang_reparseTranslationUnit() - libclang reuses the file's
// precompiled preamble (its #includes and everything before the first declaration) while that is
// unchanged, which is where nearly all the time of a parse of a file with heavy includes goes. A
// different file or different flags, or a reparse that fails, starts over from scratch.
//
// update() may be called from any thread, one at a time (calls are serialized). Meant to be held by
// something long-lived (a document) and shared with the worker that calls it.
class Session {
public:
    Session();

    // As Parser::parseBuffer(): content is what filePath contains now.
    ParseResult update(const std::string& filePath, const std::string& content,
                       const std::vector<std::string>& compileArgs = defaultCompileArgs());

    // How many updates parsed from scratch, and how many reparsed the kept translation unit
    // (diagnostics and tests).
    std::size_t parseCount() const;
    std::size_t reparseCount() const;

private:
    mutable std::mutex mutex_;
    ClangIndex index_;
    std::unique_ptr<CXTranslationUnitImpl, TranslationUnitDeleter> unit_;
    std::string path_;
    std::vector<std::string> args_;
    std::size_t parses_ = 0;
    std::size_t reparses_ = 0;
};

} // namespace cpptools
