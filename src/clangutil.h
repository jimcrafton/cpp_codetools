#pragma once

// libclang -> cpptools conversions shared by the parser and the delegate-binding checker. Internal:
// pulls in clang-c, which no public cpptools header does.

#include <clang-c/Index.h>

#include <string>
#include <vector>

#include "cpptools/diagnostic.h"
#include "cpptools/symbol.h"

namespace cpptools::detail {

inline std::string toStdString(CXString clangString) {
    const char* text = clang_getCString(clangString);
    std::string result = text ? text : "";
    clang_disposeString(clangString);
    return result;
}

inline SourceLocation toSourceLocation(CXSourceLocation location) {
    CXFile file = nullptr;
    unsigned line = 0;
    unsigned column = 0;
    unsigned offset = 0;
    clang_getSpellingLocation(location, &file, &line, &column, &offset);

    SourceLocation result;
    result.file = file ? toStdString(clang_getFileName(file)) : std::string();
    result.line = line;
    result.column = column;
    result.offset = offset;
    return result;
}

inline Severity toSeverity(CXDiagnosticSeverity severity) {
    switch (severity) {
        case CXDiagnostic_Note: return Severity::Note;
        case CXDiagnostic_Warning: return Severity::Warning;
        case CXDiagnostic_Error: return Severity::Error;
        case CXDiagnostic_Fatal: return Severity::Fatal;
        default: return Severity::Note;
    }
}

inline std::vector<Diagnostic> collectDiagnostics(CXTranslationUnit tu) {
    std::vector<Diagnostic> diagnostics;
    unsigned count = clang_getNumDiagnostics(tu);
    diagnostics.reserve(count);

    for (unsigned i = 0; i < count; ++i) {
        CXDiagnostic diagnostic = clang_getDiagnostic(tu, i);

        Diagnostic entry;
        entry.severity = toSeverity(clang_getDiagnosticSeverity(diagnostic));
        entry.message = toStdString(clang_getDiagnosticSpelling(diagnostic));
        entry.location = toSourceLocation(clang_getDiagnosticLocation(diagnostic));
        entry.category = toStdString(clang_getDiagnosticCategoryText(diagnostic));
        entry.fromMainFile = clang_Location_isFromMainFile(clang_getDiagnosticLocation(diagnostic)) != 0;
        if (clang_getDiagnosticNumRanges(diagnostic) > 0) {
            entry.rangeEndOffset = toSourceLocation(clang_getRangeEnd(clang_getDiagnosticRange(diagnostic, 0))).offset;
        }
        diagnostics.push_back(std::move(entry));

        clang_disposeDiagnostic(diagnostic);
    }

    return diagnostics;
}

} // namespace cpptools::detail
