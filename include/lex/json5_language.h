#pragma once

#include "highlight.h"

namespace lex {

// JSON5 highlighting: the default TokenClass styles, plus PropertyName for object
// keys. A key is a String / identifier-name token whose next significant token on
// the same line is ':' (a key and its colon split across lines keep the plain
// String / Identifier style; the AST-based diff view knows keys exactly).
const Language& json5Language();

}  // namespace lex
