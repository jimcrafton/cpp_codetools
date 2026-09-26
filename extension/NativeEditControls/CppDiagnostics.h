#pragma once

#include "HighlightController.h"

#include <cpptools/compileflags.h>
#include <cpptools/parser.h>

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace CodeToolsVsix
{
    // What to show. Until real compile flags are wired in, the parse can't find a file's includes,
    // and a missing header cascades into semantic errors (unknown types, undeclared names) that
    // aren't the user's mistakes - so by default only syntax-level errors are shown.
    struct DiagnosticFilter
    {
        // Only "Parse Issue" (a syntax error) and "Lexical or Preprocessor Issue" (a bad token, junk
        // after an #include, ...), and never "file not found".
        bool syntaxOnly = true;
        // Warnings get a green squiggle beside the errors' red one.
        bool warnings = true;
        // When the parse used a real project's flags (a compile_commands.json / compile_flags.txt was
        // found, so includes resolve), semantic errors and missing includes are real too: show
        // everything, whatever syntaxOnly says.
        bool followProjectFlags = true;
    };

    // UTF-8 byte offsets -> UTF-16 offsets into the same text: byteOffsets[i] maps to result[i]
    // (past-the-end and out-of-range map to wide.size()). One pass over the text.
    std::vector<std::size_t> utf8ToWideOffsets(const std::string& utf8, const std::wstring& wide,
        const std::vector<std::size_t>& byteOffsets);

    // Squiggles for the errors among diagnostics (parsed from wide's UTF-8 form utf8): each covers
    // the diagnostic's range when it has one, else the token it points at - or, when it points at
    // the gap after a token (clang reports a missing ';' there), that token.
    std::vector<newui::text::TextStyleRange> diagnosticRanges(const std::wstring& wide, const std::string& utf8,
        const std::vector<cpptools::Diagnostic>& diagnostics, const DiagnosticFilter& filter = {});

    // The symbol outline as text, "--- Outline (cpptools) ---", a line saying which compile flags
    // the parse used (flagsOrigin: cpptools::CompileFlags::origin), and an indented line per symbol.
    std::wstring formatOutline(const cpptools::ParseResult& result, const std::string& flagsOrigin = std::string());

    // The file the parse pretends the text belongs to (the real one, so its own directory is
    // searched for quoted includes), shared with the worker.
    class CppDocument
    {
    public:
        CppDocument() = default;
        ~CppDocument();
        CppDocument(const CppDocument&) = delete;
        CppDocument& operator=(const CppDocument&) = delete;

        void setPath(std::string path);
        // The file the parse treats the text as: the real one once set. Before that (an unsaved
        // buffer) an empty temporary .cpp - libclang reuses a precompiled preamble only for a main
        // file that exists on disk - removed when this is destroyed.
        std::string path() const;
        bool hasPath() const;

        // The flags to parse it with: from the project's compile_commands.json / compile_flags.txt
        // (cpptools::compileFlagsFor()) once it has a path, the defaults before. Looked up on first
        // use after each setPath() - on whichever thread asks, which is a worker in practice.
        cpptools::CompileFlags flags() const;

        // The libclang translation unit kept between parses of this file, so each pass after the
        // first is a reparse (the includes at the top aren't parsed again). Safe to use from the
        // worker: calls are serialized.
        cpptools::Session& session() { return session_; }

    private:
        cpptools::Session session_;
        mutable std::string untitledPath_;   // the stand-in file, once made
        mutable std::mutex mutex_;
        std::string path_;
        mutable bool flagsValid_ = false;
        mutable cpptools::CompileFlags flags_;
    };

    // What HighlightController's second pass runs: parses the text with libclang and returns
    // squiggles for its syntax errors, with the outline (a std::wstring from formatOutline()) as
    // the overlay's extra. Thread-safe.
    HighlightOverlay analyzeCppDiagnostics(const std::wstring& text, const std::shared_ptr<CppDocument>& document,
        const DiagnosticFilter& filter = {});
}
