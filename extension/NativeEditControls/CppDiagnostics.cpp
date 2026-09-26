#include "CppDiagnostics.h"
#include "TextEncoding.h"

#include <lex/cpp_lexer.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <numeric>

namespace CodeToolsVsix
{
    namespace
    {
        bool wanted(const cpptools::Diagnostic& diagnostic, const DiagnosticFilter& filter)
        {
            if (!diagnostic.fromMainFile) {
                return false;
            }
            const bool error = diagnostic.severity == cpptools::Severity::Error || diagnostic.severity == cpptools::Severity::Fatal;
            const bool warning = diagnostic.severity == cpptools::Severity::Warning && filter.warnings;
            if (!error && !warning) {
                return false;
            }
            if (!filter.syntaxOnly) {
                return true;
            }
            if (diagnostic.message.find("file not found") != std::string::npos) {
                return false;
            }
            return diagnostic.category == "Parse Issue" || diagnostic.category == "Lexical or Preprocessor Issue";
        }

        // Fixed strings for cpptools::SymbolKind - deliberately not libclang's own kind spelling
        // (cpptools::Symbol doesn't retain that), just a readable label for the outline display.
        const wchar_t* kindSpelling(cpptools::SymbolKind kind)
        {
            switch (kind)
            {
            case cpptools::SymbolKind::Namespace: return L"Namespace";
            case cpptools::SymbolKind::Class: return L"Class";
            case cpptools::SymbolKind::Struct: return L"Struct";
            case cpptools::SymbolKind::Union: return L"Union";
            case cpptools::SymbolKind::Enum: return L"Enum";
            case cpptools::SymbolKind::ClassTemplate: return L"ClassTemplate";
            case cpptools::SymbolKind::Function: return L"Function";
            case cpptools::SymbolKind::Method: return L"Method";
            case cpptools::SymbolKind::Constructor: return L"Constructor";
            case cpptools::SymbolKind::Destructor: return L"Destructor";
            case cpptools::SymbolKind::Field: return L"Field";
            case cpptools::SymbolKind::Variable: return L"Variable";
            case cpptools::SymbolKind::Typedef: return L"Typedef";
            default: return L"Other";
            }
        }

        void appendSymbols(const std::vector<cpptools::Symbol>& symbols, unsigned depth, std::wstring& out)
        {
            for (const cpptools::Symbol& symbol : symbols)
            {
                out += L"\r\n";
                out.append(static_cast<std::size_t>(depth) * 2, L' ');
                out += kindSpelling(symbol.kind);
                out += L' ';
                out += utf8ToWide(symbol.name);
                out += L" @ " + std::to_wstring(symbol.location.line) + L':' + std::to_wstring(symbol.location.column);

                appendSymbols(symbol.children, depth + 1, out);
            }
        }

        bool skippable(const lex::Token& token)
        {
            return lex::isTrivia(token.cls) || token.cls == lex::TokenClass::Comment;
        }
    }

    std::vector<std::size_t> utf8ToWideOffsets(const std::string& utf8, const std::wstring& wide,
        const std::vector<std::size_t>& byteOffsets)
    {
        std::vector<std::size_t> result(byteOffsets.size(), wide.size());
        std::vector<std::size_t> order(byteOffsets.size());
        std::iota(order.begin(), order.end(), std::size_t(0));
        std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return byteOffsets[a] < byteOffsets[b]; });

        std::size_t byte = 0;
        std::size_t units = 0;   // UTF-16 code units before `byte`
        std::size_t next = 0;
        while (next < order.size()) {
            while (next < order.size() && byteOffsets[order[next]] <= byte) {
                result[order[next]] = units < wide.size() ? units : wide.size();
                ++next;
            }
            if (byte >= utf8.size()) {
                break;
            }
            const unsigned char lead = static_cast<unsigned char>(utf8[byte]);
            std::size_t length = lead < 0x80 ? 1 : lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
            length = length < utf8.size() - byte ? length : utf8.size() - byte;
            byte += length;
            units += length == 4 ? 2 : 1;   // a supplementary character is a surrogate pair
        }
        return result;
    }

    std::vector<newui::text::TextStyleRange> diagnosticRanges(const std::wstring& wide, const std::string& utf8,
        const std::vector<cpptools::Diagnostic>& diagnostics, const DiagnosticFilter& filter)
    {
        std::vector<const cpptools::Diagnostic*> chosen;
        std::vector<std::size_t> byteOffsets;
        for (const cpptools::Diagnostic& diagnostic : diagnostics) {
            if (wanted(diagnostic, filter)) {
                chosen.push_back(&diagnostic);
                byteOffsets.push_back(diagnostic.location.offset);
                byteOffsets.push_back(diagnostic.rangeEndOffset);
            }
        }
        std::vector<newui::text::TextStyleRange> ranges;
        if (chosen.empty()) {
            return ranges;
        }
        const std::vector<std::size_t> offsets = utf8ToWideOffsets(utf8, wide, byteOffsets);

        lex::CppLexer lexer(wide);
        const std::vector<lex::Token> tokens = lexer.tokenize();

        for (std::size_t i = 0; i < chosen.size(); ++i) {
            const std::size_t start = offsets[2 * i];
            const std::size_t rangeEnd = chosen[i]->rangeEndOffset > chosen[i]->location.offset ? offsets[2 * i + 1] : 0;
            std::size_t from = start;
            std::size_t length = 0;
            if (rangeEnd > start) {
                length = rangeEnd - start;
            } else if (chosen[i]->message.find("extra tokens") != std::string::npos) {
                // Reported at the gap before the junk: the junk is the first significant token from
                // there to the end of its line.
                const auto first = std::find_if(std::lower_bound(tokens.begin(), tokens.end(), start,
                        [](const lex::Token& token, std::size_t offset) { return token.end() <= offset; }),
                    tokens.end(), [](const lex::Token& token) { return !skippable(token); });
                if (first != tokens.end()) {
                    from = first->offset;
                    std::size_t end = from;
                    while (end < wide.size() && wide[end] != L'\n' && wide[end] != L'\r') {
                        ++end;
                    }
                    length = end - from;
                }
            } else {
                // The token it points at; or, when that's a gap, the significant token before it.
                // The last token starting at or before the point; if it's whitespace or a comment
                // (or the point is past its end), the significant one before that.
                const auto after = std::upper_bound(tokens.begin(), tokens.end(), start,
                    [](std::size_t offset, const lex::Token& token) { return offset < token.offset; });
                std::ptrdiff_t at = (after - tokens.begin()) - 1;
                while (at >= 0 && skippable(tokens[static_cast<std::size_t>(at)])) {
                    --at;
                }
                if (at >= 0) {
                    from = tokens[static_cast<std::size_t>(at)].offset;
                    length = tokens[static_cast<std::size_t>(at)].length;
                }
            }
            const char* style = chosen[i]->severity == cpptools::Severity::Warning ? kWarningStyleName : kProblemStyleName;
            const auto same = std::find_if(ranges.begin(), ranges.end(), [&](const newui::text::TextStyleRange& range) {
                return range.start == from && range.length == (length != 0 ? length : 1);
            });
            if (same == ranges.end()) {
                addProblemRange(ranges, wide.size(), from, length, style);
            } else if (std::string(style) == kProblemStyleName) {
                same->style = style;   // an error and a warning on one spot: the error shows
            }
        }
        return ranges;
    }

    std::wstring formatOutline(const cpptools::ParseResult& result, const std::string& flagsOrigin)
    {
        std::wstring outline = result.symbols.empty() ? L"--- Outline (cpptools): no symbols found ---" : L"--- Outline (cpptools) ---";
        outline += L"\r\nCompile flags: ";
        outline += flagsOrigin.empty() ? std::wstring(L"defaults (no compile_commands.json or compile_flags.txt found)") : utf8ToWide(flagsOrigin);
        appendSymbols(result.symbols, 0, outline);
        return outline;
    }

    void CppDocument::setPath(std::string path)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        path_ = std::move(path);
        flagsValid_ = false;
    }

    bool CppDocument::hasPath() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return !path_.empty();
    }

    cpptools::CompileFlags CppDocument::flags() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!flagsValid_) {
            flags_ = path_.empty() ? cpptools::CompileFlags{ cpptools::defaultCompileArgs(), std::string() }
                                   : cpptools::compileFlagsFor(path_);
            flagsValid_ = true;
        }
        return flags_;
    }

    std::string CppDocument::path() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!path_.empty()) {
            return path_;
        }
        if (untitledPath_.empty()) {
            namespace fs = std::filesystem;
            std::error_code error;
            const fs::path file = fs::temp_directory_path(error) / ("codetools_untitled_" +
                std::to_string(reinterpret_cast<std::uintptr_t>(this)) + "_" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".cpp");
            if (!error) {
                std::ofstream(file, std::ios::binary);   // an empty file
            }
            untitledPath_ = file.string();
        }
        return untitledPath_;
    }

    CppDocument::~CppDocument()
    {
        if (!untitledPath_.empty()) {
            std::error_code ignored;
            std::filesystem::remove(untitledPath_, ignored);
        }
    }

    HighlightOverlay analyzeCppDiagnostics(const std::wstring& text, const std::shared_ptr<CppDocument>& document,
        const DiagnosticFilter& filter)
    {
        HighlightOverlay overlay;
        try {
            const std::string utf8 = wideToUtf8(text);
            const cpptools::CompileFlags flags = document->flags();
            const cpptools::ParseResult result = document->session().update(document->path(), utf8, flags.args);
            // With the project's own flags the includes resolve, so what's left is real.
            DiagnosticFilter effective = filter;
            if (flags.fromProject() && filter.followProjectFlags) {
                effective.syntaxOnly = false;
            }
            overlay.ranges = diagnosticRanges(text, utf8, result.diagnostics, effective);
            overlay.extra = formatOutline(result, flags.origin);
        } catch (...) {
            // A failed parse: no squiggles and no outline this time.
        }
        return overlay;
    }
}
