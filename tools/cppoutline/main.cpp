#include <cstdio>
#include <string>

#include "cpptools/parser.h"

namespace {

const char* symbolKindName(cpptools::SymbolKind kind) {
    switch (kind) {
        case cpptools::SymbolKind::Namespace: return "Namespace";
        case cpptools::SymbolKind::Class: return "Class";
        case cpptools::SymbolKind::Struct: return "Struct";
        case cpptools::SymbolKind::Union: return "Union";
        case cpptools::SymbolKind::Enum: return "Enum";
        case cpptools::SymbolKind::ClassTemplate: return "ClassTemplate";
        case cpptools::SymbolKind::Function: return "Function";
        case cpptools::SymbolKind::Method: return "Method";
        case cpptools::SymbolKind::Constructor: return "Constructor";
        case cpptools::SymbolKind::Destructor: return "Destructor";
        case cpptools::SymbolKind::Field: return "Field";
        case cpptools::SymbolKind::Variable: return "Variable";
        case cpptools::SymbolKind::Typedef: return "Typedef";
        default: return "Other";
    }
}

const char* severityName(cpptools::Severity severity) {
    switch (severity) {
        case cpptools::Severity::Note: return "note";
        case cpptools::Severity::Warning: return "warning";
        case cpptools::Severity::Error: return "error";
        case cpptools::Severity::Fatal: return "fatal";
        default: return "note";
    }
}

void printSymbols(const std::vector<cpptools::Symbol>& symbols, std::size_t depth) {
    for (const cpptools::Symbol& symbol : symbols) {
        std::fprintf(stdout, "%*s%s %s @ %u:%u\n",
                     static_cast<int>(depth * 2), "",
                     symbolKindName(symbol.kind),
                     symbol.name.c_str(),
                     static_cast<unsigned>(symbol.location.line),
                     static_cast<unsigned>(symbol.location.column));
        printSymbols(symbol.children, depth + 1);
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: cppoutline <file>\n");
        return 1;
    }

    std::string filePath = argv[1];

    cpptools::Parser parser;
    cpptools::ParseResult result = parser.parseFile(filePath);

    for (const cpptools::Diagnostic& diagnostic : result.diagnostics) {
        std::fprintf(stderr, "%s:%u:%u: %s: %s\n",
                     diagnostic.location.file.c_str(),
                     static_cast<unsigned>(diagnostic.location.line),
                     static_cast<unsigned>(diagnostic.location.column),
                     severityName(diagnostic.severity),
                     diagnostic.message.c_str());
    }

    printSymbols(result.symbols, 0);
    return 0;
}
