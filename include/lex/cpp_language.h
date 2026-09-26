#pragma once

#include "highlight.h"

namespace lex {

// C++ highlighting: the default TokenClass styles (keywords, constants, strings and character
// literals, numbers, comments, preprocessor directives and <header> names, operators). Multi-line
// block comments, raw strings, continued strings / // comments and directives carry across lines in
// LexState (see cpp_lexer.h).
const Language& cppLanguage();

}  // namespace lex
