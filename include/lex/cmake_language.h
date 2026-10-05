#pragma once

#include "highlight.h"

namespace lex {

// CMake highlighting: the default TokenClass styles - command names as keywords, quoted and bracket
// arguments as strings, unquoted arguments as identifiers, # and #[[ ]] comments. A quoted argument,
// bracket argument or bracket comment that spans lines carries across them in LexState (see cmake_lexer.h).
const Language& cmakeLanguage();

}  // namespace lex
