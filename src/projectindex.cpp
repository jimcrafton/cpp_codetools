#include "cpptools/projectindex.h"

#include "cpptools/compileflags.h"
#include "cpptools/log.h"
#include "cpptools/parser.h"
#include "clangutil.h"

#include <clang-c/Index.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <thread>

namespace cpptools {

namespace fs = std::filesystem;
using detail::toStdString;

namespace {

std::string lowered(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

// The key a path is compared by: case-folded, since the files are on Windows.
std::string keyOf(const std::string& normalizedPath) { return lowered(normalizedPath); }

std::uint64_t hashArgs(const std::vector<std::string>& args) {
    std::uint64_t hash = 1469598103934665603ull;   // FNV-1a
    for (const std::string& arg : args) {
        for (unsigned char c : arg) {
            hash = (hash ^ c) * 1099511628211ull;
        }
        hash = (hash ^ 0x1F) * 1099511628211ull;
    }
    return hash;
}

bool statFile(const std::string& path, std::int64_t& modified, std::uint64_t& size) {
    std::error_code ec;
    const auto time = fs::last_write_time(path, ec);
    if (ec) return false;
    const auto bytes = fs::file_size(path, ec);
    if (ec) return false;
    modified = static_cast<std::int64_t>(time.time_since_epoch().count());
    size = static_cast<std::uint64_t>(bytes);
    return true;
}

SymbolKind kindOf(CXCursorKind kind) {
    switch (kind) {
        case CXCursor_Namespace: return SymbolKind::Namespace;
        case CXCursor_ClassDecl: return SymbolKind::Class;
        case CXCursor_StructDecl: return SymbolKind::Struct;
        case CXCursor_UnionDecl: return SymbolKind::Union;
        case CXCursor_EnumDecl: return SymbolKind::Enum;
        case CXCursor_ClassTemplate: return SymbolKind::ClassTemplate;
        case CXCursor_FunctionDecl:
        case CXCursor_FunctionTemplate: return SymbolKind::Function;
        case CXCursor_CXXMethod:
        case CXCursor_ConversionFunction: return SymbolKind::Method;
        case CXCursor_Constructor: return SymbolKind::Constructor;
        case CXCursor_Destructor: return SymbolKind::Destructor;
        case CXCursor_FieldDecl: return SymbolKind::Field;
        case CXCursor_VarDecl: return SymbolKind::Variable;
        case CXCursor_TypedefDecl:
        case CXCursor_TypeAliasDecl: return SymbolKind::Typedef;
        default: return SymbolKind::Other;
    }
}

bool isScope(CXCursorKind kind) {
    switch (kind) {
        case CXCursor_Namespace:
        case CXCursor_ClassDecl:
        case CXCursor_StructDecl:
        case CXCursor_UnionDecl:
        case CXCursor_ClassTemplate:
        case CXCursor_EnumDecl:
            return true;
        default:
            return false;
    }
}

// "outer::inner::name" - the enclosing namespaces and types, left out when anonymous.
std::string qualifiedNameOf(CXCursor cursor) {
    std::vector<std::string> parts;
    parts.push_back(toStdString(clang_getCursorSpelling(cursor)));
    for (CXCursor parent = clang_getCursorSemanticParent(cursor);
         !clang_Cursor_isNull(parent) && isScope(clang_getCursorKind(parent));
         parent = clang_getCursorSemanticParent(parent)) {
        const std::string name = toStdString(clang_getCursorSpelling(parent));
        if (!name.empty()) parts.push_back(name);
    }
    std::string qualified;
    for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
        if (!qualified.empty()) qualified += "::";
        qualified += *it;
    }
    return qualified;
}

struct Roots {
    std::vector<std::string> keys;   // normalized, case-folded, each ending in '/'

    bool empty() const { return keys.empty(); }
    bool contains(const std::string& path) const {
        const std::string key = keyOf(ProjectIndex::normalizePath(path));
        for (const std::string& root : keys) {
            if (key.compare(0, root.size(), root) == 0) return true;
        }
        return false;
    }
};

struct ExtractContext {
    IndexedFile* file;
    const Roots* roots;
    std::string mainPath;
};

bool referenceIsProject(const ExtractContext& context, CXCursor referenced) {
    CXSourceLocation location = clang_getCursorLocation(referenced);
    if (context.roots->empty()) {
        return clang_Location_isInSystemHeader(location) == 0;
    }
    CXFile file = nullptr;
    clang_getSpellingLocation(location, &file, nullptr, nullptr, nullptr);
    return file != nullptr && context.roots->contains(toStdString(clang_getFileName(file)));
}

CXChildVisitResult collectSymbols(CXCursor cursor, CXCursor /*parent*/, CXClientData data) {
    auto* context = static_cast<ExtractContext*>(data);
    if (clang_Location_isFromMainFile(clang_getCursorLocation(cursor)) == 0) {
        return CXChildVisit_Continue;
    }
    const CXCursorKind kind = clang_getCursorKind(cursor);
    if (kind == CXCursor_LinkageSpec) {
        return CXChildVisit_Recurse;
    }
    const SymbolKind symbolKind = kindOf(kind);
    if (symbolKind == SymbolKind::Other) {
        return CXChildVisit_Continue;
    }

    const std::string name = toStdString(clang_getCursorSpelling(cursor));
    const std::string usr = toStdString(clang_getCursorUSR(cursor));
    if (!name.empty() && !usr.empty()) {
        IndexedSymbol symbol;
        symbol.usr = usr;
        symbol.name = name;
        symbol.qualifiedName = qualifiedNameOf(cursor);
        symbol.kind = symbolKind;
        symbol.location = detail::toSourceLocation(clang_getCursorLocation(cursor));
        symbol.location.file = context->mainPath;
        symbol.isDefinition = clang_isCursorDefinition(cursor) != 0;
        CXCursor parent = clang_getCursorSemanticParent(cursor);
        if (!clang_Cursor_isNull(parent) && isScope(clang_getCursorKind(parent))) {
            symbol.parentUsr = toStdString(clang_getCursorUSR(parent));
        }
        if (symbolKind == SymbolKind::Class || symbolKind == SymbolKind::Struct || symbolKind == SymbolKind::ClassTemplate) {
            clang_visitChildren(cursor, [](CXCursor child, CXCursor, CXClientData bases) {
                if (clang_getCursorKind(child) == CXCursor_CXXBaseSpecifier) {
                    // The base's own declaration gives its qualified name; a base that is not a plain
                    // class (Base<int>, a dependent type) is kept as written.
                    CXCursor declaration = clang_getCursorReferenced(child);
                    const bool plain = !clang_Cursor_isNull(declaration) && isScope(clang_getCursorKind(declaration));
                    static_cast<std::vector<std::string>*>(bases)->push_back(plain
                        ? qualifiedNameOf(declaration)
                        : toStdString(clang_getTypeSpelling(clang_getCursorType(child))));
                }
                return CXChildVisit_Continue;
            }, &symbol.bases);
        }
        context->file->symbols.push_back(std::move(symbol));
    }

    // Namespaces and types hold more declarations; nothing else is worth entering (a function's body has
    // no declarations anyone looks up).
    return (isScope(kind) && kind != CXCursor_EnumDecl) || name.empty() ? CXChildVisit_Recurse : CXChildVisit_Continue;
}

CXChildVisitResult collectReferences(CXCursor cursor, CXCursor /*parent*/, CXClientData data) {
    auto* context = static_cast<ExtractContext*>(data);
    if (clang_Location_isFromMainFile(clang_getCursorLocation(cursor)) == 0) {
        return CXChildVisit_Continue;
    }
    const CXCursorKind kind = clang_getCursorKind(cursor);
    if (clang_isReference(kind) != 0 || kind == CXCursor_DeclRefExpr || kind == CXCursor_MemberRefExpr) {
        CXCursor referenced = clang_getCursorReferenced(cursor);
        if (!clang_Cursor_isNull(referenced) && referenceIsProject(*context, referenced)) {
            const std::string usr = toStdString(clang_getCursorUSR(referenced));
            if (!usr.empty()) ++context->file->references[usr];
        }
    }
    return CXChildVisit_Recurse;
}

struct InclusionContext {
    ExtractContext* extract;
};

void collectInclusion(CXFile included, CXSourceLocation* stack, unsigned length, CXClientData data) {
    auto* context = static_cast<InclusionContext*>(data);
    if (length != 1 || included == nullptr) {
        return;   // the main file itself, or an include made by an included file
    }
    unsigned line = 0;
    clang_getSpellingLocation(stack[0], nullptr, &line, nullptr, nullptr);
    IncludeEdge edge;
    edge.includer = context->extract->mainPath;
    edge.included = ProjectIndex::normalizePath(toStdString(clang_getFileName(included)));
    edge.line = line;
    edge.external = !context->extract->roots->empty() ? !context->extract->roots->contains(edge.included)
                                                       : false;
    context->extract->file->includes.push_back(std::move(edge));
}

// Parses one file and reads everything the index keeps from it.
IndexedFile indexOne(CXIndex index, const std::string& path, const std::vector<std::string>& args,
                     const Roots& roots, const std::string* content) {
    IndexedFile file;
    file.path = path;
    file.flagsHash = hashArgs(args);
    statFile(path, file.modified, file.size);

    std::vector<const char*> argv;
    argv.reserve(args.size());
    for (const std::string& arg : args) argv.push_back(arg.c_str());

    CXUnsavedFile unsaved;
    unsaved.Filename = path.c_str();
    if (content != nullptr) {
        unsaved.Contents = content->c_str();
        unsaved.Length = static_cast<unsigned long>(content->size());
    }

    CXTranslationUnit unit = nullptr;
    const CXErrorCode error = clang_parseTranslationUnit2(index, path.c_str(), argv.data(), static_cast<int>(argv.size()),
        content != nullptr ? &unsaved : nullptr, content != nullptr ? 1u : 0u, CXTranslationUnit_KeepGoing, &unit);
    if (error != CXError_Success || unit == nullptr) {
        file.error = "libclang could not parse the file (error " + std::to_string(static_cast<int>(error)) + ")";
        return file;
    }

    ExtractContext context{ &file, &roots, path };
    CXCursor root = clang_getTranslationUnitCursor(unit);
    clang_visitChildren(root, &collectSymbols, &context);
    clang_visitChildren(root, &collectReferences, &context);
    InclusionContext inclusions{ &context };
    clang_getInclusions(unit, &collectInclusion, &inclusions);

    // Only the diagnostics located in this file itself: a header's own problems belong to the header's entry.
    const unsigned diagnosticCount = clang_getNumDiagnostics(unit);
    for (unsigned i = 0; i < diagnosticCount; ++i) {
        CXDiagnostic diagnostic = clang_getDiagnostic(unit, i);
        const CXDiagnosticSeverity severity = clang_getDiagnosticSeverity(diagnostic);
        if (severity == CXDiagnostic_Warning || severity == CXDiagnostic_Error || severity == CXDiagnostic_Fatal) {
            CXFile where = nullptr;
            clang_getSpellingLocation(clang_getDiagnosticLocation(diagnostic), &where, nullptr, nullptr, nullptr);
            if (where != nullptr) {
                CXString name = clang_getFileName(where);
                const char* text = clang_getCString(name);
                if (text != nullptr && keyOf(ProjectIndex::normalizePath(text)) == keyOf(path)) {
                    if (severity == CXDiagnostic_Warning) {
                        ++file.warnings;
                    } else {
                        // "'x.h' file not found": the include path is missing, which is the flags' problem, not the code's
                        const std::string message = toStdString(clang_getDiagnosticSpelling(diagnostic));
                        if (message.find("file not found") != std::string::npos) ++file.unresolvedIncludes; else ++file.errors;
                    }
                }
                clang_disposeString(name);
            }
        }
        clang_disposeDiagnostic(diagnostic);
    }
    clang_disposeTranslationUnit(unit);
    file.parsed = true;
    return file;
}

// --- persistence helpers -------------------------------------------------------------------------

constexpr char kMagic[8] = { 'C', 'P', 'T', 'O', 'O', 'L', 'I', 'X' };
constexpr std::uint32_t kFormatVersion = 3;   // 2: per-file error and warning counts; 3: and unresolved includes

void put(std::ostream& out, std::uint64_t value) { out.write(reinterpret_cast<const char*>(&value), sizeof value); }
void put(std::ostream& out, const std::string& text) {
    put(out, static_cast<std::uint64_t>(text.size()));
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

struct Reader {
    std::istream& in;
    bool good = true;

    std::uint64_t number() {
        std::uint64_t value = 0;
        in.read(reinterpret_cast<char*>(&value), sizeof value);
        if (!in) good = false;
        return value;
    }
    std::string text() {
        const std::uint64_t length = number();
        if (!good || length > (1u << 28)) { good = false; return std::string(); }
        std::string value(static_cast<std::size_t>(length), '\0');
        in.read(value.data(), static_cast<std::streamsize>(length));
        if (!in) good = false;
        return value;
    }
};

} // namespace

struct ProjectIndex::Impl {
    mutable std::mutex mutex;
    std::map<std::string, IndexedFile> files;   // keyOf(path) -> file
    Roots roots;
    FlagsProvider flags = [](const std::string& file) { return compileFlagsFor(file).args; };
};

ProjectIndex::ProjectIndex() : impl_(std::make_unique<Impl>()) {}
ProjectIndex::~ProjectIndex() = default;

std::string ProjectIndex::normalizePath(const std::string& path) {
    std::error_code ec;
    fs::path absolute = fs::absolute(fs::path(path), ec);
    if (ec) absolute = fs::path(path);
    return absolute.lexically_normal().generic_string();
}

void ProjectIndex::setRoots(std::vector<std::string> roots) {
    Roots built;
    for (const std::string& root : roots) {
        std::string key = keyOf(normalizePath(root));
        if (!key.empty() && key.back() != '/') key += '/';
        built.keys.push_back(std::move(key));
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->roots = std::move(built);
}

void ProjectIndex::setFlagsProvider(FlagsProvider provider) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->flags = provider ? std::move(provider) : [](const std::string& file) { return compileFlagsFor(file).args; };
}

IndexProgress ProjectIndex::indexFiles(const std::vector<std::string>& requested, const ProgressCallback& progress, unsigned threads) {
    std::vector<std::string> paths;
    {
        std::set<std::string> seen;
        for (const std::string& file : requested) {
            std::string path = normalizePath(file);
            if (seen.insert(keyOf(path)).second) paths.push_back(std::move(path));
        }
    }

    Roots roots;
    FlagsProvider flags;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        roots = impl_->roots;
        flags = impl_->flags;
    }

    IndexProgress summary;
    summary.total = paths.size();
    if (threads == 0) {
        const unsigned cores = std::thread::hardware_concurrency();
        threads = cores > 1 ? cores - 1 : 1;
    }
    threads = std::max(1u, std::min<unsigned>(threads, static_cast<unsigned>(std::max<std::size_t>(paths.size(), 1))));

    std::atomic<std::size_t> next{0};
    std::atomic<bool> stop{false};
    std::mutex reportMutex;

    auto worker = [&]() {
        ClangIndex index;
        for (;;) {
            const std::size_t at = next.fetch_add(1);
            if (at >= paths.size() || stop.load()) return;
            const std::string& path = paths[at];

            enum class Outcome { Parsed, Skipped, Failed } outcome = Outcome::Skipped;
            std::int64_t modified = 0;
            std::uint64_t size = 0;
            const bool exists = statFile(path, modified, size);
            std::vector<std::string> args;
            if (exists) {
                try { args = flags(path); } catch (...) { args = defaultCompileArgs(); }
                const std::uint64_t flagsHash = hashArgs(args);
                bool fresh = false;
                {
                    std::lock_guard<std::mutex> lock(impl_->mutex);
                    auto it = impl_->files.find(keyOf(path));
                    fresh = it != impl_->files.end() && it->second.parsed && it->second.modified == modified
                            && it->second.size == size && it->second.flagsHash == flagsHash;
                }
                if (!fresh) {
                    IndexedFile result = indexOne(index.get(), path, args, roots, nullptr);
                    outcome = result.parsed ? Outcome::Parsed : Outcome::Failed;
                    std::lock_guard<std::mutex> lock(impl_->mutex);
                    impl_->files[keyOf(path)] = std::move(result);
                }
            } else {
                outcome = Outcome::Failed;
                std::lock_guard<std::mutex> lock(impl_->mutex);
                impl_->files.erase(keyOf(path));   // gone from disk
            }

            std::lock_guard<std::mutex> report(reportMutex);
            ++summary.done;
            summary.current = path;
            if (outcome == Outcome::Parsed) ++summary.parsed;
            else if (outcome == Outcome::Skipped) ++summary.skipped;
            else ++summary.failed;
            if (progress && !progress(summary)) stop.store(true);
        }
    };

    std::vector<std::thread> pool;
    for (unsigned i = 1; i < threads; ++i) pool.emplace_back(worker);
    worker();
    for (std::thread& t : pool) t.join();
    return summary;
}

bool ProjectIndex::updateFile(const std::string& rawPath, const std::string* content) {
    const std::string path = normalizePath(rawPath);
    Roots roots;
    FlagsProvider flags;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        roots = impl_->roots;
        flags = impl_->flags;
    }
    std::vector<std::string> args;
    try { args = flags(path); } catch (...) { args = defaultCompileArgs(); }

    ClangIndex index;
    IndexedFile result = indexOne(index.get(), path, args, roots, content);
    if (content != nullptr) {
        result.modified = 0;   // not what is on disk: the next indexFiles() parses it again
        result.size = 0;
    }
    const bool parsed = result.parsed;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->files[keyOf(path)] = std::move(result);
    return parsed;
}

void ProjectIndex::removeFile(const std::string& path) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->files.erase(keyOf(normalizePath(path)));
}

std::size_t ProjectIndex::fileCount() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->files.size();
}

std::vector<std::string> ProjectIndex::files() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    std::vector<std::string> paths;
    for (const auto& entry : impl_->files) paths.push_back(entry.second.path);
    return paths;
}

bool ProjectIndex::hasFile(const std::string& path) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->files.count(keyOf(normalizePath(path))) != 0;
}

std::vector<IndexedSymbol> ProjectIndex::symbolsIn(const std::string& path) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto it = impl_->files.find(keyOf(normalizePath(path)));
    return it == impl_->files.end() ? std::vector<IndexedSymbol>() : it->second.symbols;
}

ProjectIndex::Problems ProjectIndex::problemsIn(const std::string& path) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto it = impl_->files.find(keyOf(normalizePath(path)));
    Problems problems;
    if (it != impl_->files.end()) {
        problems.errors = it->second.errors;
        problems.warnings = it->second.warnings;
        problems.unresolvedIncludes = it->second.unresolvedIncludes;
    }
    return problems;
}

std::vector<IncludeEdge> ProjectIndex::includesOf(const std::string& path) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto it = impl_->files.find(keyOf(normalizePath(path)));
    return it == impl_->files.end() ? std::vector<IncludeEdge>() : it->second.includes;
}

std::vector<IndexedSymbol> ProjectIndex::findClasses(const std::string& filter) const {
    const std::string needle = lowered(filter);
    std::map<std::string, IndexedSymbol> byUsr;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        for (const auto& entry : impl_->files) {
            for (const IndexedSymbol& symbol : entry.second.symbols) {
                if (!symbol.isDefinition) continue;
                if (symbol.kind != SymbolKind::Class && symbol.kind != SymbolKind::Struct && symbol.kind != SymbolKind::ClassTemplate) continue;
                if (!needle.empty() && lowered(symbol.qualifiedName).find(needle) == std::string::npos) continue;
                byUsr.emplace(symbol.usr, symbol);
            }
        }
    }
    std::vector<IndexedSymbol> found;
    for (auto& entry : byUsr) found.push_back(std::move(entry.second));
    std::sort(found.begin(), found.end(), [&needle](const IndexedSymbol& a, const IndexedSymbol& b) {
        const bool aPrefix = !needle.empty() && lowered(a.name).compare(0, needle.size(), needle) == 0;
        const bool bPrefix = !needle.empty() && lowered(b.name).compare(0, needle.size(), needle) == 0;
        if (aPrefix != bPrefix) return aPrefix;
        return a.qualifiedName < b.qualifiedName;
    });
    return found;
}

std::vector<IndexedSymbol> ProjectIndex::declarationsOf(const std::string& usr) const {
    std::vector<IndexedSymbol> found;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    for (const auto& entry : impl_->files) {
        for (const IndexedSymbol& symbol : entry.second.symbols) {
            if (symbol.usr == usr) found.push_back(symbol);
        }
    }
    return found;
}

std::vector<std::string> ProjectIndex::includers(const std::string& path, bool transitive) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    // included file -> the files that include it directly
    std::map<std::string, std::vector<std::string>> directly;
    for (const auto& entry : impl_->files) {
        for (const IncludeEdge& edge : entry.second.includes) {
            directly[keyOf(edge.included)].push_back(entry.second.path);
        }
    }
    std::vector<std::string> found;
    std::set<std::string> seen;
    std::vector<std::string> pending{ keyOf(normalizePath(path)) };
    while (!pending.empty()) {
        const std::string current = pending.back();
        pending.pop_back();
        auto it = directly.find(current);
        if (it == directly.end()) continue;
        for (const std::string& includer : it->second) {
            if (!seen.insert(keyOf(includer)).second) continue;
            found.push_back(includer);
            if (transitive) pending.push_back(keyOf(includer));
        }
    }
    std::sort(found.begin(), found.end());
    return found;
}

std::size_t ProjectIndex::referenceCount(const std::string& usr) const {
    std::size_t total = 0;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    for (const auto& entry : impl_->files) {
        auto it = entry.second.references.find(usr);
        if (it != entry.second.references.end()) total += it->second;
    }
    return total;
}

std::size_t ProjectIndex::referencingFileCount(const std::string& usr) const {
    std::size_t files = 0;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    for (const auto& entry : impl_->files) {
        files += entry.second.references.count(usr);
    }
    return files;
}

bool ProjectIndex::save(const std::string& cachePath) const {
    std::ofstream out(cachePath, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(kMagic, sizeof kMagic);
    put(out, kFormatVersion);
    std::lock_guard<std::mutex> lock(impl_->mutex);
    put(out, static_cast<std::uint64_t>(impl_->files.size()));
    for (const auto& entry : impl_->files) {
        const IndexedFile& file = entry.second;
        put(out, file.path);
        put(out, static_cast<std::uint64_t>(file.modified));
        put(out, file.size);
        put(out, file.flagsHash);
        put(out, static_cast<std::uint64_t>(file.parsed ? 1 : 0));
        put(out, file.error);
        put(out, static_cast<std::uint64_t>(file.errors));
        put(out, static_cast<std::uint64_t>(file.warnings));
        put(out, static_cast<std::uint64_t>(file.unresolvedIncludes));
        put(out, static_cast<std::uint64_t>(file.symbols.size()));
        for (const IndexedSymbol& s : file.symbols) {
            put(out, s.usr);
            put(out, s.name);
            put(out, s.qualifiedName);
            put(out, static_cast<std::uint64_t>(s.kind));
            put(out, s.location.file);
            put(out, static_cast<std::uint64_t>(s.location.line));
            put(out, static_cast<std::uint64_t>(s.location.column));
            put(out, static_cast<std::uint64_t>(s.location.offset));
            put(out, static_cast<std::uint64_t>(s.isDefinition ? 1 : 0));
            put(out, s.parentUsr);
            put(out, static_cast<std::uint64_t>(s.bases.size()));
            for (const std::string& base : s.bases) put(out, base);
        }
        put(out, static_cast<std::uint64_t>(file.includes.size()));
        for (const IncludeEdge& e : file.includes) {
            put(out, e.includer);
            put(out, e.included);
            put(out, static_cast<std::uint64_t>(e.line));
            put(out, static_cast<std::uint64_t>(e.external ? 1 : 0));
        }
        put(out, static_cast<std::uint64_t>(file.references.size()));
        for (const auto& ref : file.references) {
            put(out, ref.first);
            put(out, static_cast<std::uint64_t>(ref.second));
        }
    }
    return static_cast<bool>(out);
}

bool ProjectIndex::load(const std::string& cachePath) {
    std::ifstream in(cachePath, std::ios::binary);
    if (!in) return false;
    char magic[sizeof kMagic] = {};
    in.read(magic, sizeof magic);
    if (!in || !std::equal(std::begin(magic), std::end(magic), std::begin(kMagic))) return false;
    Reader reader{ in };
    if (reader.number() != kFormatVersion) return false;

    std::map<std::string, IndexedFile> loaded;
    const std::uint64_t fileCount = reader.number();
    for (std::uint64_t f = 0; f < fileCount && reader.good; ++f) {
        IndexedFile file;
        file.path = reader.text();
        file.modified = static_cast<std::int64_t>(reader.number());
        file.size = reader.number();
        file.flagsHash = reader.number();
        file.parsed = reader.number() != 0;
        file.error = reader.text();
        file.errors = static_cast<std::uint32_t>(reader.number());
        file.warnings = static_cast<std::uint32_t>(reader.number());
        file.unresolvedIncludes = static_cast<std::uint32_t>(reader.number());
        const std::uint64_t symbols = reader.number();
        for (std::uint64_t i = 0; i < symbols && reader.good; ++i) {
            IndexedSymbol s;
            s.usr = reader.text();
            s.name = reader.text();
            s.qualifiedName = reader.text();
            s.kind = static_cast<SymbolKind>(reader.number());
            s.location.file = reader.text();
            s.location.line = static_cast<std::size_t>(reader.number());
            s.location.column = static_cast<std::size_t>(reader.number());
            s.location.offset = static_cast<std::size_t>(reader.number());
            s.isDefinition = reader.number() != 0;
            s.parentUsr = reader.text();
            const std::uint64_t bases = reader.number();
            for (std::uint64_t b = 0; b < bases && reader.good; ++b) s.bases.push_back(reader.text());
            file.symbols.push_back(std::move(s));
        }
        const std::uint64_t includes = reader.number();
        for (std::uint64_t i = 0; i < includes && reader.good; ++i) {
            IncludeEdge e;
            e.includer = reader.text();
            e.included = reader.text();
            e.line = static_cast<std::size_t>(reader.number());
            e.external = reader.number() != 0;
            file.includes.push_back(std::move(e));
        }
        const std::uint64_t references = reader.number();
        for (std::uint64_t i = 0; i < references && reader.good; ++i) {
            std::string usr = reader.text();
            file.references[usr] = static_cast<std::uint32_t>(reader.number());
        }
        loaded[keyOf(file.path)] = std::move(file);
    }
    if (!reader.good) return false;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->files = std::move(loaded);
    return true;
}

} // namespace cpptools
