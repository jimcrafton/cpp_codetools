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
};

} // namespace cpptools
