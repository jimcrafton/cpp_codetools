#include "cpptools/parser.h"
#include "cpptools/log.h"

#include <clang-c/Index.h>

#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace cpptools {

void TranslationUnitDeleter::operator()(CXTranslationUnitImpl* tu) const {
    clang_disposeTranslationUnit(tu);
}

namespace {

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
    unsigned offset = 0;
    clang_getSpellingLocation(location, &file, &line, &column, &offset);

    SourceLocation result;
    result.file = file ? toStdString(clang_getFileName(file)) : std::string();
    result.line = line;
    result.column = column;
    result.offset = offset;
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
    // file itself. And don't look inside it: a declaration written in a header can't contain
    // anything written in this file, and walking all of <vector>, <string>, ... on every parse was
    // most of what parsing a file with heavy includes cost.
    if (clang_Location_isFromMainFile(location) == 0) {
        return CXChildVisit_Continue;
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
    std::vector<std::string> args{"-std=c++17", "-xc++"};
#ifdef _WIN32
    // The MSVC STL refuses a compiler it doesn't know (VS 2026's wants Clang 20; this libclang may
    // be older) with a static_assert in <yvals_core.h> - this is its own switch for that.
    args.push_back("-D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH");
#endif
    // A header is the main file here: don't warn about "#pragma once" in it.
    args.push_back("-Wno-pragma-once-outside-header");
    return args;
}

namespace {

// Editing options (which include the precompiled preamble a reparse reuses). KeepGoing: carry on after
// a fatal error (a missing #include is one), like an IDE - or every diagnostic after the first missing
// header would be lost.
unsigned parseOptions() {
    // Not clang_defaultEditingTranslationUnitOptions(): that includes CacheCompletionResults, which
    // rebuilds a code-completion cache over every top-level declaration - all of <vector>, <string>, ...
    // - on every reparse, and nothing here completes code. The preamble (the includes at the top) is
    // precompiled on the first parse and reused by every reparse, with the bodies of the functions
    // in it skipped (nobody looks at those).
    return CXTranslationUnit_PrecompiledPreamble | CXTranslationUnit_CreatePreambleOnFirstParse |
           CXTranslationUnit_SkipFunctionBodies | CXTranslationUnit_LimitSkipFunctionBodiesToPreamble |
           CXTranslationUnit_KeepGoing;
}

// A parse that produced no translation unit, as a result.
ParseResult failedParse(const std::string& filePath, int errorCode) {
    ParseResult result;
    Diagnostic diagnostic;
    diagnostic.severity = Severity::Fatal;
    diagnostic.message = "failed to parse translation unit (libclang error code " + std::to_string(errorCode) + ")";
    diagnostic.location.file = filePath;
    log(Severity::Fatal, diagnostic.message + " [" + filePath + "]");
    result.diagnostics.push_back(std::move(diagnostic));
    return result;
}

ParseResult readTranslationUnit(CXTranslationUnit tu) {
    ParseResult result;
    result.diagnostics = collectDiagnostics(tu);

    VisitContext context{&result.symbols};
    CXCursor rootCursor = clang_getTranslationUnitCursor(tu);
    clang_visitChildren(rootCursor, &visitCursor, &context);
    return result;
}

CXUnsavedFile unsavedFileFor(const std::string& filePath, const std::string& content) {
    CXUnsavedFile unsavedFile;
    unsavedFile.Filename = filePath.c_str();
    unsavedFile.Contents = content.c_str();
    unsavedFile.Length = static_cast<unsigned long>(content.size());
    return unsavedFile;
}

// Parses from scratch; null (and *error set) when libclang gives nothing back.
UniqueTranslationUnit parseFresh(CXIndex index, const std::string& filePath, const std::string& content,
                                 const std::vector<std::string>& compileArgs, CXErrorCode* error) {
    CXUnsavedFile unsavedFile = unsavedFileFor(filePath, content);
    std::vector<const char*> args = toCStrings(compileArgs);
    CXTranslationUnit rawTu = nullptr;
    *error = clang_parseTranslationUnit2(index, filePath.c_str(), args.data(), static_cast<int>(args.size()),
                                         &unsavedFile, 1, parseOptions(), &rawTu);
    if (*error != CXError_Success || !rawTu) {
        return nullptr;
    }
    return UniqueTranslationUnit(rawTu);
}

} // namespace

Parser::Parser() = default;

ParseResult Parser::parseBuffer(const std::string& filePath, const std::string& content,
                                 const std::vector<std::string>& compileArgs) {
    CXErrorCode error = CXError_Success;
    UniqueTranslationUnit tu = parseFresh(index_.get(), filePath, content, compileArgs, &error);
    if (!tu) {
        return failedParse(filePath, static_cast<int>(error));
    }
    return readTranslationUnit(tu.get());
}

Session::Session() = default;

ParseResult Session::update(const std::string& filePath, const std::string& content,
                            const std::vector<std::string>& compileArgs) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (unit_ && path_ == filePath && args_ == compileArgs) {
        CXUnsavedFile unsavedFile = unsavedFileFor(filePath, content);
        if (clang_reparseTranslationUnit(unit_.get(), 1, &unsavedFile, clang_defaultReparseOptions(unit_.get())) == 0) {
            ++reparses_;
            return readTranslationUnit(unit_.get());
        }
        unit_.reset();   // a failed reparse leaves the unit unusable: start over
    }

    unit_.reset();
    CXErrorCode error = CXError_Success;
    unit_ = parseFresh(index_.get(), filePath, content, compileArgs, &error);
    if (!unit_) {
        return failedParse(filePath, static_cast<int>(error));
    }
    path_ = filePath;
    args_ = compileArgs;
    ++parses_;
    return readTranslationUnit(unit_.get());
}

std::size_t Session::parseCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return parses_;
}

std::size_t Session::reparseCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return reparses_;
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
        log(Severity::Fatal, diagnostic.message + " [" + filePath + "]");
        result.diagnostics.push_back(std::move(diagnostic));
        return result;
    }

    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return parseBuffer(filePath, buffer.str(), compileArgs);
}


} // namespace cpptools
