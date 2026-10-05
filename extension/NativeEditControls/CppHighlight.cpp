#include "CppHighlight.h"

#include <lex/cmake_language.h>
#include <lex/cmake_parser.h>
#include <lex/cpp_language.h>
#include <lex/cpp_lexer.h>

#include <cstdint>
#include <cwctype>
#include <string_view>

namespace CodeToolsVsix
{
    namespace
    {
        struct Open
        {
            std::size_t offset;    // where the fold would start
            std::uint32_t line;    // the line the opener is on
        };

        // The name after the '#' of a Directive token: "if" for "#if", "# if".
        std::wstring_view directiveName(const std::wstring& text, const lex::Token& token)
        {
            std::wstring_view spelling(text.data() + token.offset, token.length);
            std::size_t i = spelling.empty() ? 0 : 1;   // past '#'
            while (i < spelling.size() && std::iswspace(static_cast<wint_t>(spelling[i])) != 0) {
                ++i;
            }
            return spelling.substr(i);
        }

        std::size_t lineEnd(const std::wstring& text, std::size_t from)
        {
            while (from < text.size() && text[from] != L'\n' && text[from] != L'\r') {
                ++from;
            }
            return from;
        }
    }

    std::vector<newui::text::TextFold> cppFoldsFor(const std::wstring& text)
    {
        std::vector<newui::text::TextFold> folds;
        auto add = [&](std::size_t start, std::size_t end, const wchar_t* placeholder) {
            if (end > start) {
                newui::text::TextFold fold;
                fold.start = start;
                fold.length = end - start;
                fold.placeholder = placeholder;
                folds.push_back(fold);
            }
        };

        std::vector<Open> braces;
        std::vector<Open> groups;   // open #if groups; offset = the end of the directive's line
        lex::CppLexer lexer(text);
        for (lex::Token t = lexer.next(); !t.isEof(); t = lexer.next()) {
            switch (t.kind) {
            case lex::cpp::LBrace:
                braces.push_back({ t.end(), t.line });
                break;
            case lex::cpp::RBrace:
                if (!braces.empty()) {
                    const Open open = braces.back();
                    braces.pop_back();
                    if (t.line > open.line) {
                        add(open.offset, t.offset, L"...");
                    }
                }
                break;
            case lex::kind::BlockComment:
                if (t.endLine > t.line) {   // an unterminated one hides to the end
                    add(t.offset + 2, t.has(lex::TokenFlag_Unterminated) ? t.end() : t.end() - 2, L"...");
                }
                break;
            case lex::cpp::Directive: {
                const std::wstring_view name = directiveName(text, t);
                const bool opens = name == L"if" || name == L"ifdef" || name == L"ifndef";
                const bool splits = name == L"else" || name == L"elif" || name == L"elifdef" || name == L"elifndef";
                if (name == L"endif" || splits) {
                    if (!groups.empty()) {
                        const Open group = groups.back();
                        groups.pop_back();
                        if (t.line > group.line) {
                            add(group.offset, t.offset, L" ... ");
                        }
                    }
                }
                if (opens || splits) {
                    groups.push_back({ lineEnd(text, t.end()), t.line });
                }
                break;
            }
            default:
                break;
            }
        }
        return folds;
    }

    std::vector<newui::text::TextFold> cmakeFoldsFor(const std::wstring& text)
    {
        struct Section { std::wstring opener; std::size_t offset; std::uint32_t line; };
        std::vector<newui::text::TextFold> folds;
        std::vector<Section> open;
        auto close = [&](const Section& section, std::size_t end, std::uint32_t endLine) {
            if (endLine > section.line && end > section.offset) {
                newui::text::TextFold fold;
                fold.start = section.offset;
                fold.length = end - section.offset;
                fold.placeholder = L"...";
                folds.push_back(fold);
            }
        };

        const lex::cmake::ParseResult parsed = lex::cmake::parse(text);
        for (const lex::cmake::Command& command : parsed.commands) {
            std::wstring lower = command.name;
            for (wchar_t& c : lower) {
                if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c - L'A' + L'a');
            }
            if (command.opensBlock()) {
                open.push_back({ lower, lineEnd(text, command.end), command.endLine });
            } else if ((lower == L"elseif" || lower == L"else") && !open.empty() && open.back().opener == L"if") {
                close(open.back(), command.offset, command.line);
                open.back() = { L"if", lineEnd(text, command.end), command.endLine };
            } else if (command.closesBlock()) {
                const std::wstring wanted = lower.substr(3);
                for (std::size_t i = open.size(); i-- > 0;) {
                    if (open[i].opener == wanted) {
                        close(open[i], command.offset, command.line);
                        open.resize(i);
                        break;
                    }
                }
            }
        }
        return folds;
    }

    HighlightResult analyzeCMake(const std::wstring& text)
    {
        HighlightResult result;
        appendStyleRanges(lex::cmakeLanguage(), text, result.ranges);
        result.folds = cmakeFoldsFor(text);
        return result;
    }

    HighlightResult analyzePlainText(const std::wstring&)
    {
        return HighlightResult();
    }

    HighlightResult analyzeCpp(const std::wstring& text)
    {
        HighlightResult result;
        appendStyleRanges(lex::cppLanguage(), text, result.ranges);
        result.folds = cppFoldsFor(text);
        return result;
    }
}
