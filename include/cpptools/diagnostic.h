#pragma once

#include <string>

#include "cpptools/symbol.h"

namespace cpptools {

enum class Severity {
    Note,
    Warning,
    Error,
    Fatal
};

struct Diagnostic {
    Severity severity = Severity::Note;
    std::string message;
    SourceLocation location;
    // libclang's category text: "Parse Issue" (a syntax error), "Semantic Issue" (types, names,
    // overloads - what a missing header cascades into), "Lexical or Preprocessor Issue" (a bad
    // token, a missing #include), ... Empty for diagnostics cpptools makes up itself.
    std::string category;
    bool fromMainFile = true;            // false: located in an #include'd file
    std::size_t rangeEndOffset = 0;      // end (byte offset) of its first source range; 0 when it has none
};

} // namespace cpptools
