#include "cpptools/parser.h"
#include "cpptools/log.h"
#include "clangutil.h"

#include <clang-c/Index.h>

#include <algorithm>
#include <cctype>
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

using detail::collectDiagnostics;
using detail::toSourceLocation;
using detail::toStdString;

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

// Cursor kinds worth renaming - the same idea as isOutlineWorthy, plus the local/parameter kinds
// that never show up there but are common rename targets.
bool isRenamableCursorKind(CXCursorKind kind) {
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
        case CXCursor_ParmDecl:
        case CXCursor_TypedefDecl:
        case CXCursor_TypeAliasDecl:
        case CXCursor_EnumConstantDecl:
        case CXCursor_NonTypeTemplateParameter:
        case CXCursor_TemplateTypeParameter:
        case CXCursor_TemplateTemplateParameter:
            return true;
        default:
            return false;
    }
}

// What cursor stands for the same symbol everywhere it's spelled: a reference's target, or the
// cursor itself when it isn't a reference (clang_getCursorReferenced already returns a declaration
// cursor unchanged).
CXCursor resolveReferenced(CXCursor cursor) {
    CXCursor referenced = clang_getCursorReferenced(cursor);
    return clang_Cursor_isNull(referenced) ? cursor : referenced;
}

struct OccurrenceContext {
    CXCursor target;   // canonical
    std::vector<Occurrence>* result;
};

CXChildVisitResult visitForOccurrences(CXCursor cursor, CXCursor /*parent*/, CXClientData clientData) {
    auto* context = static_cast<OccurrenceContext*>(clientData);
    CXCursor referenced = resolveReferenced(cursor);
    if (!clang_Cursor_isNull(referenced) && clang_equalCursors(clang_getCanonicalCursor(referenced), context->target)) {
        CXSourceLocation location = clang_getCursorLocation(cursor);
        if (clang_Location_isFromMainFile(location) != 0) {
            // Just the name, not (say) a VarDecl's whole "int x = 1" extent.
            CXSourceRange nameRange = clang_Cursor_getSpellingNameRange(cursor, 0, 0);
            CXFile file = nullptr;
            unsigned start = 0;
            unsigned end = 0;
            clang_getSpellingLocation(clang_getRangeStart(nameRange), &file, nullptr, nullptr, &start);
            clang_getSpellingLocation(clang_getRangeEnd(nameRange), &file, nullptr, nullptr, &end);
            if (file != nullptr && end > start) {
                context->result->push_back(Occurrence{ start, end - start });
            }
        }
    }
    // Always recurse: a match can be nested inside another (e.g. a call's callee reference).
    return CXChildVisit_Recurse;
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

namespace {

// The declaration the symbol spelled at byte `offset` stands for, or a null cursor when the offset isn't on a renamable one.
CXCursor renamableTargetAt(CXTranslationUnit unit, const std::string& path, std::size_t offset) {
    const CXCursor none = clang_getNullCursor();
    CXFile file = clang_getFile(unit, path.c_str());
    if (file == nullptr) {
        return none;
    }
    CXSourceLocation location = clang_getLocationForOffset(unit, file, static_cast<unsigned>(offset));
    CXCursor cursor = clang_getCursor(unit, location);
    if (clang_Cursor_isNull(cursor) || clang_isInvalid(clang_getCursorKind(cursor))) {
        return none;
    }
    // clang_getCursor snaps to the cursor whose EXTENT contains the location, which for a
    // declaration is the whole "int count = 0;" - so also require offset be within the symbol's
    // own spelling name range (just "count"), not merely somewhere inside its declaration.
    CXSourceRange ownName = clang_Cursor_getSpellingNameRange(cursor, 0, 0);
    CXFile ownFile = nullptr;
    unsigned ownStart = 0;
    unsigned ownEnd = 0;
    clang_getSpellingLocation(clang_getRangeStart(ownName), &ownFile, nullptr, nullptr, &ownStart);
    clang_getSpellingLocation(clang_getRangeEnd(ownName), nullptr, nullptr, nullptr, &ownEnd);
    if (ownFile == nullptr || offset < ownStart || offset >= ownEnd) {
        return none;
    }
    CXCursor referenced = resolveReferenced(cursor);
    if (clang_Cursor_isNull(referenced) || !isRenamableCursorKind(clang_getCursorKind(referenced))) {
        return none;
    }
    return referenced;
}

bool isFunctionLike(CXCursorKind kind) {
    return kind == CXCursor_FunctionDecl || kind == CXCursor_CXXMethod || kind == CXCursor_FunctionTemplate ||
           kind == CXCursor_Constructor || kind == CXCursor_Destructor;
}

struct ConflictContext {
    CXCursor target;   // canonical
    std::string name;
    bool targetIsFunction;
    bool found;
};

CXChildVisitResult visitForConflict(CXCursor cursor, CXCursor /*parent*/, CXClientData clientData) {
    auto* context = static_cast<ConflictContext*>(clientData);
    if (toStdString(clang_getCursorSpelling(cursor)) != context->name) {
        return CXChildVisit_Continue;
    }
    if (clang_equalCursors(clang_getCanonicalCursor(cursor), context->target)) {
        return CXChildVisit_Continue;
    }
    // Functions may share a name (overloads); anything else with the name is a clash.
    if (context->targetIsFunction && isFunctionLike(clang_getCursorKind(cursor))) {
        return CXChildVisit_Continue;
    }
    context->found = true;
    return CXChildVisit_Break;
}

} // namespace

std::string Session::renameConflict(std::size_t offset, const std::string& newName) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!unit_) {
        return std::string();
    }
    const bool validName = !newName.empty() && (std::isalpha(static_cast<unsigned char>(newName[0])) || newName[0] == '_') &&
                           std::all_of(newName.begin(), newName.end(), [](char c) {
                               return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
                           });
    if (!validName) {
        return "\"" + newName + "\" is not a valid name";
    }
    const CXCursor target = renamableTargetAt(unit_.get(), path_, offset);
    if (clang_Cursor_isNull(target)) {
        return std::string();
    }
    const CXCursorKind kind = clang_getCursorKind(target);
    // Names in the scope the symbol is declared in. A local or a parameter lives in a function body whose
    // nested scopes can't be told apart here, so those are not checked.
    if (kind == CXCursor_VarDecl || kind == CXCursor_ParmDecl) {
        CXCursor parent = clang_getCursorSemanticParent(target);
        const CXCursorKind parentKind = clang_getCursorKind(parent);
        if (isFunctionLike(parentKind)) {
            return std::string();
        }
    }
    const CXCursor scope = clang_getCursorSemanticParent(target);
    if (clang_Cursor_isNull(scope) || clang_isInvalid(clang_getCursorKind(scope))) {
        return std::string();
    }
    ConflictContext context{ clang_getCanonicalCursor(target), newName, isFunctionLike(kind), false };
    clang_visitChildren(scope, &visitForConflict, &context);
    if (context.found) {
        const std::string scopeName = toStdString(clang_getCursorSpelling(scope));
        return "\"" + newName + "\" already exists" + (scopeName.empty() ? std::string() : " in " + scopeName);
    }
    return std::string();
}

std::vector<Occurrence> Session::findOccurrences(std::size_t offset) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Occurrence> result;
    if (!unit_) {
        return result;
    }
    const CXCursor referenced = renamableTargetAt(unit_.get(), path_, offset);
    if (clang_Cursor_isNull(referenced)) {
        return result;
    }

    OccurrenceContext context{ clang_getCanonicalCursor(referenced), &result };
    CXCursor rootCursor = clang_getTranslationUnitCursor(unit_.get());
    clang_visitChildren(rootCursor, &visitForOccurrences, &context);

    std::sort(result.begin(), result.end(),
              [](const Occurrence& a, const Occurrence& b) { return a.offset < b.offset; });
    result.erase(std::unique(result.begin(), result.end(),
                             [](const Occurrence& a, const Occurrence& b) { return a.offset == b.offset; }),
                 result.end());
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
        log(Severity::Fatal, diagnostic.message + " [" + filePath + "]");
        result.diagnostics.push_back(std::move(diagnostic));
        return result;
    }

    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return parseBuffer(filePath, buffer.str(), compileArgs);
}


} // namespace cpptools
