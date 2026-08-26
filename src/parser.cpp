#include "cpptools/parser.h"

#include <clang-c/Index.h>

#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace cpptools {

namespace {

struct TranslationUnitDeleter {
    void operator()(CXTranslationUnitImpl* tu) const {
        clang_disposeTranslationUnit(tu);
    }
};

using UniqueTranslationUnit = std::unique_ptr<CXTranslationUnitImpl, TranslationUnitDeleter>;

std::string toStdString(CXString clangString) {
    const char* text = clang_getCString(clangString);
    std::string result = text ? text : "";
    clang_disposeString(clangString);
    return result;
}

SourceLocation toSourceLocation(CXSourceLocation location) {
    CXFile file = nullptr;
    unsigned line = 0;
    unsigned column = 0;
    clang_getSpellingLocation(location, &file, &line, &column, nullptr);

    SourceLocation result;
    result.file = file ? toStdString(clang_getFileName(file)) : std::string();
    result.line = line;
    result.column = column;
    return result;
}

// Kinds worth surfacing in a symbol outline - a filter over libclang's own cursor kinds, not a
// reclassification of them (mirrors CppNativeEditorVsix's CppParser::IsOutlineWorthy).
bool isOutlineWorthy(CXCursorKind kind) {
    switch (kind) {
        case CXCursor_Namespace:
        case CXCursor_ClassDecl:
        case CXCursor_StructDecl:
        case CXCursor_UnionDecl:
        case CXCursor_EnumDecl:
        case CXCursor_ClassTemplate:
        case CXCursor_FunctionDecl:
        case CXCursor_CXXMethod:
        case CXCursor_Constructor:
        case CXCursor_Destructor:
        case CXCursor_FieldDecl:
        case CXCursor_VarDecl:
        case CXCursor_TypedefDecl:
            return true;
        default:
            return false;
    }
}

SymbolKind toSymbolKind(CXCursorKind kind) {
    switch (kind) {
        case CXCursor_Namespace: return SymbolKind::Namespace;
        case CXCursor_ClassDecl: return SymbolKind::Class;
        case CXCursor_StructDecl: return SymbolKind::Struct;
        case CXCursor_UnionDecl: return SymbolKind::Union;
        case CXCursor_EnumDecl: return SymbolKind::Enum;
        case CXCursor_ClassTemplate: return SymbolKind::ClassTemplate;
        case CXCursor_FunctionDecl: return SymbolKind::Function;
        case CXCursor_CXXMethod: return SymbolKind::Method;
        case CXCursor_Constructor: return SymbolKind::Constructor;
        case CXCursor_Destructor: return SymbolKind::Destructor;
        case CXCursor_FieldDecl: return SymbolKind::Field;
        case CXCursor_VarDecl: return SymbolKind::Variable;
        case CXCursor_TypedefDecl: return SymbolKind::Typedef;
        default: return SymbolKind::Other;
    }
}

Severity toSeverity(CXDiagnosticSeverity severity) {
    switch (severity) {
        case CXDiagnostic_Note: return Severity::Note;
        case CXDiagnostic_Warning: return Severity::Warning;
        case CXDiagnostic_Error: return Severity::Error;
        case CXDiagnostic_Fatal: return Severity::Fatal;
        default: return Severity::Note;
    }
}

struct VisitContext {
    std::vector<Symbol>* symbols;
};

CXChildVisitResult visitCursor(CXCursor cursor, CXCursor /*parent*/, CXClientData clientData) {
    auto* context = static_cast<VisitContext*>(clientData);
    CXSourceLocation location = clang_getCursorLocation(cursor);

    // Skip anything pulled in from #include'd headers - only symbols physically in the parsed
    // file itself.
    if (clang_Location_isFromMainFile(location) == 0) {
        return CXChildVisit_Recurse;
    }

    CXCursorKind kind = clang_getCursorKind(cursor);
    if (!isOutlineWorthy(kind)) {
        return CXChildVisit_Recurse;
    }

    std::string name = toStdString(clang_getCursorSpelling(cursor));
    if (name.empty()) {
        return CXChildVisit_Recurse;
    }

    Symbol symbol;
    symbol.name = name;
    symbol.kind = toSymbolKind(kind);
    symbol.location = toSourceLocation(location);

    VisitContext childContext{&symbol.children};
    clang_visitChildren(cursor, &visitCursor, &childContext);

    context->symbols->push_back(std::move(symbol));
    return CXChildVisit_Continue;
}

std::vector<Diagnostic> collectDiagnostics(CXTranslationUnit tu) {
    std::vector<Diagnostic> diagnostics;
    unsigned count = clang_getNumDiagnostics(tu);
    diagnostics.reserve(count);

    for (unsigned i = 0; i < count; ++i) {
        CXDiagnostic diagnostic = clang_getDiagnostic(tu, i);

        Diagnostic entry;
        entry.severity = toSeverity(clang_getDiagnosticSeverity(diagnostic));
        entry.message = toStdString(clang_getDiagnosticSpelling(diagnostic));
        entry.location = toSourceLocation(clang_getDiagnosticLocation(diagnostic));
        diagnostics.push_back(std::move(entry));

        clang_disposeDiagnostic(diagnostic);
    }

    return diagnostics;
}

std::vector<const char*> toCStrings(const std::vector<std::string>& args) {
    std::vector<const char*> result;
    result.reserve(args.size());
    for (const std::string& arg : args) {
        result.push_back(arg.c_str());
    }
    return result;
}

} // namespace

ClangIndex::ClangIndex() {
    index_ = clang_createIndex(/*excludeDeclarationsFromPCH*/ 0, /*displayDiagnostics*/ 0);
    if (!index_) {
        throw std::runtime_error("ClangIndex::ClangIndex: clang_createIndex returned null");
    }
}

ClangIndex::~ClangIndex() {
    if (index_) {
        clang_disposeIndex(index_);
    }
}

ClangIndex::ClangIndex(ClangIndex&& other) noexcept : index_(other.index_) {
    other.index_ = nullptr;
}

ClangIndex& ClangIndex::operator=(ClangIndex&& other) noexcept {
    if (this != &other) {
        if (index_) {
            clang_disposeIndex(index_);
        }
        index_ = other.index_;
        other.index_ = nullptr;
    }
    return *this;
}

std::vector<std::string> defaultCompileArgs() {
    return {"-std=c++17", "-xc++"};
}

Parser::Parser() = default;

ParseResult Parser::parseBuffer(const std::string& filePath, const std::string& content,
                                 const std::vector<std::string>& compileArgs) {
    CXUnsavedFile unsavedFile;
    unsavedFile.Filename = filePath.c_str();
    unsavedFile.Contents = content.c_str();
    unsavedFile.Length = static_cast<unsigned long>(content.size());

    std::vector<const char*> args = toCStrings(compileArgs);

    CXTranslationUnit rawTu = nullptr;
    CXErrorCode error = clang_parseTranslationUnit2(
        index_.get(),
        filePath.c_str(),
        args.data(), static_cast<int>(args.size()),
        &unsavedFile, 1,
        clang_defaultEditingTranslationUnitOptions(),
        &rawTu);

    ParseResult result;
    if (error != CXError_Success || !rawTu) {
        Diagnostic diagnostic;
        diagnostic.severity = Severity::Fatal;
        diagnostic.message = "failed to parse translation unit (libclang error code " +
            std::to_string(static_cast<int>(error)) + ")";
        diagnostic.location.file = filePath;
        result.diagnostics.push_back(std::move(diagnostic));
        return result;
    }

    UniqueTranslationUnit tu(rawTu);

    result.diagnostics = collectDiagnostics(tu.get());

    VisitContext context{&result.symbols};
    CXCursor rootCursor = clang_getTranslationUnitCursor(tu.get());
    clang_visitChildren(rootCursor, &visitCursor, &context);

    return result;
}

ParseResult Parser::parseFile(const std::string& filePath,
                               const std::vector<std::string>& compileArgs) {
    // A thin wrapper: read the file ourselves and forward to parseBuffer, which stays the one
    // place that actually talks to libclang - keeps both entry points independently testable.
    std::ifstream stream(filePath, std::ios::binary);
    if (!stream) {
        ParseResult result;
        Diagnostic diagnostic;
        diagnostic.severity = Severity::Fatal;
        diagnostic.message = "failed to open file";
        diagnostic.location.file = filePath;
        result.diagnostics.push_back(std::move(diagnostic));
        return result;
    }

    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return parseBuffer(filePath, buffer.str(), compileArgs);
}

} // namespace cpptools
