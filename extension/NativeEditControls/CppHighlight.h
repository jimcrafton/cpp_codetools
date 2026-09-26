#pragma once

#include "HighlightController.h"

#include <string>
#include <vector>

namespace CodeToolsVsix
{
    // Folds for a C++ text, none collapsed: every multi-line { } block (hiding what's between the
    // braces), multi-line block comment (between the delimiters), and #if / #ifdef / #ifndef group
    // (from the end of its directive line to the next #else / #elif / #endif, so an #if with an
    // #else is two folds). Braces are matched by the lexer alone, without regard for #if branches -
    // an unmatched one just doesn't fold. Thread-safe.
    std::vector<newui::text::TextFold> cppFoldsFor(const std::wstring& text);

    // What HighlightController runs for a C++ text: lex's C++ colors, squiggles for malformed and
    // unterminated tokens, and cppFoldsFor(). Thread-safe.
    HighlightResult analyzeCpp(const std::wstring& text);
}
