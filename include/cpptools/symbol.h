#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace cpptools {

enum class SymbolKind {
    Namespace,
    Class,
    Struct,
    Union,
    Enum,
    ClassTemplate,
    Function,
    Method,
    Constructor,
    Destructor,
    Field,
    Variable,
    Typedef,
    Other
};

struct SourceLocation {
    std::string file;
    std::size_t line = 0;
    std::size_t column = 0;
};

struct Symbol {
    std::string name;
    SymbolKind kind = SymbolKind::Other;
    SourceLocation location;
    std::vector<Symbol> children;
};

} // namespace cpptools
