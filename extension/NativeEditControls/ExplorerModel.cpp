#include "ExplorerModel.h"

#include <cmakemodel/model.h>
#include <cpptools/projectindex.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <thread>

namespace fs = std::filesystem;

namespace CodeToolsVsix
{
    namespace
    {
        std::string lowered(std::string text)
        {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return text;
        }

        bool startsWith(const std::string& text, const std::string& prefix)
        {
            return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
        }

        // Folders a user does not browse: build output, fetched dependencies, tool caches.
        bool isSkippedFolder(const std::string& name)
        {
            const std::string n = lowered(name);
            static const std::set<std::string> kExact = { "build", "out", "3rdparty", "node_modules", "bin", "obj", "x64", "__pycache__" };
            return kExact.count(n) != 0 || startsWith(n, "build-") || startsWith(n, "build_") || startsWith(n, "cmake-build");
        }

        bool isSkippedFile(const std::string& name)
        {
            const std::string ext = lowered(fs::path(name).extension().string());
            static const std::set<std::string> kExt = { ".obj", ".pdb", ".ilk", ".tmp", ".user", ".suo", ".log", ".o" };
            return kExt.count(ext) != 0;
        }

        std::string sizeText(std::uintmax_t bytes)
        {
            char buffer[32];
            if (bytes < 1024) {
                std::snprintf(buffer, sizeof buffer, "%llu B", static_cast<unsigned long long>(bytes));
            } else if (bytes < 1024 * 1024) {
                std::snprintf(buffer, sizeof buffer, "%.1f KB", static_cast<double>(bytes) / 1024.0);
            } else {
                std::snprintf(buffer, sizeof buffer, "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
            }
            return buffer;
        }

        // 1204 -> "1,204"
        std::string countText(std::uint64_t n)
        {
            std::string digits = std::to_string(n);
            for (int at = static_cast<int>(digits.size()) - 3; at > 0; at -= 3) digits.insert(static_cast<std::size_t>(at), ",");
            return digits;
        }

        // Files whose line count means something to a C++ developer: source, headers, CMake.
        bool countsLines(const std::string& name)
        {
            const std::string lower = lowered(name);
            if (lower == "cmakelists.txt") return true;
            static const std::set<std::string> kExt = { ".h", ".hpp", ".hh", ".hxx", ".inl", ".c", ".cc", ".cpp", ".cxx", ".cmake" };
            return kExt.count(lowered(fs::path(name).extension().string())) != 0;
        }

        // Newlines in the file, plus one for a last line with none; 0 for an empty or unreadable file or one over
        // 4 MB (read on every folder open, so not unbounded).
        std::size_t countLines(const std::string& path, std::uintmax_t bytes)
        {
            if (bytes == 0 || bytes > 4u * 1024 * 1024) return 0;
            std::ifstream in(path, std::ios::binary);
            if (!in) return 0;
            std::vector<char> buffer(64 * 1024);
            std::size_t lines = 0;
            char last = 0;
            while (in.read(buffer.data(), static_cast<std::streamsize>(buffer.size())) || in.gcount() > 0) {
                const std::size_t got = static_cast<std::size_t>(in.gcount());
                lines += static_cast<std::size_t>(std::count(buffer.data(), buffer.data() + got, 0x0A));
                last = buffer[got - 1];
            }
            return last != 0x0A ? lines + 1 : lines;
        }

        bool lessNoCase(const ExplorerNode& a, const ExplorerNode& b)
        {
            return lowered(a.text) < lowered(b.text);
        }

        void fillFolder(ExplorerNode& folder, const std::string& path, const FileDetailProvider& detail, const FileBadgeProvider& badge)
        {
            std::vector<ExplorerNode> dirs;
            std::vector<ExplorerNode> files;
            std::error_code ec;
            fs::directory_iterator it(path, fs::directory_options::skip_permission_denied, ec);
            if (ec) return;
            for (const auto& entry : it) {
                const std::string name = entry.path().filename().string();
                if (name.empty() || name.front() == '.') continue;
                std::error_code typeEc;
                const bool isDir = entry.is_directory(typeEc);
                if (typeEc) continue;

                ExplorerNode child;
                child.text = name;
                child.search = lowered(name);
                const std::string full = entry.path().generic_string();
                if (isDir) {
                    if (isSkippedFolder(name)) continue;
                    child.kind = ExplorerNode::Kind::Folder;
                    child.detail = detail ? detail(full, true) : std::string();
                    child.badge = badge ? badge(full, true) : ExplorerNode::Badge::None;
                    child.loaded = false;
                    child.loader = [full, detail, badge](ExplorerNode& node) { fillFolder(node, full, detail, badge); };
                    dirs.push_back(std::move(child));
                } else {
                    if (isSkippedFile(name)) continue;
                    child.kind = ExplorerNode::Kind::File;
                    child.path = full;
                    std::error_code sizeEc;
                    const auto bytes = entry.file_size(sizeEc);
                    const std::string extra = detail ? detail(full, false) : std::string();
                    child.detail = extra;   // "cpptools", "not built": beside the name; the size and lines are columns
                    if (!sizeEc) {
                        const std::size_t lines = countsLines(name) ? countLines(full, bytes) : 0;
                        child.cells = { lines > 0 ? countText(lines) + (lines == 1 ? " line" : " lines") : std::string(), sizeText(bytes) };
                        child.cellTones = { lines >= 2000 ? ExplorerNode::Tone::Warn : ExplorerNode::Tone::Muted,
                                            bytes >= 1024 * 1024 ? ExplorerNode::Tone::Warn : ExplorerNode::Tone::Muted };
                        child.cellWidth = 68.0f;   // "1,268 lines" and "47.8 KB" with room to spare: names matter more
                    }
                    child.badge = badge ? badge(full, false) : ExplorerNode::Badge::None;
                    if (child.badge == ExplorerNode::Badge::None && extra == "not built") child.badge = ExplorerNode::Badge::NotBuilt;
                    files.push_back(std::move(child));
                }
            }
            std::sort(dirs.begin(), dirs.end(), lessNoCase);
            std::sort(files.begin(), files.end(), lessNoCase);
            folder.children = std::move(dirs);
            for (ExplorerNode& file : files) folder.children.push_back(std::move(file));
        }

        // Keeps a node whose text matches, with everything under it, or one with a match below it.
        bool prune(ExplorerNode& node, const std::string& needle)
        {
            if (node.search.find(needle) != std::string::npos) return true;
            std::vector<ExplorerNode> kept;
            for (ExplorerNode& child : node.children) {
                if (prune(child, needle)) kept.push_back(std::move(child));
            }
            node.children = std::move(kept);
            return !node.children.empty();
        }

        // A heading's "(n)" follows what is left of it after a filter.
        void refreshCounts(ExplorerNode& node)
        {
            if (node.kind == ExplorerNode::Kind::Group && !node.detail.empty() && node.detail.front() == '(') {
                node.detail = "(" + std::to_string(node.children.size()) + ")";
            }
            for (ExplorerNode& child : node.children) refreshCounts(child);
        }

        void pruneRoot(ExplorerNode& root, const std::string& filter)
        {
            const std::string needle = lowered(filter);
            if (needle.empty()) return;
            std::vector<ExplorerNode> kept;
            for (ExplorerNode& child : root.children) {
                if (prune(child, needle)) kept.push_back(std::move(child));
            }
            root.children = std::move(kept);
            refreshCounts(root);
        }

        ExplorerNode::Kind nodeKindOf(cpptools::SymbolKind kind)
        {
            using K = ExplorerNode::Kind;
            switch (kind) {
                case cpptools::SymbolKind::Namespace: return K::Namespace;
                case cpptools::SymbolKind::Class:
                case cpptools::SymbolKind::ClassTemplate: return K::Class;
                case cpptools::SymbolKind::Struct:
                case cpptools::SymbolKind::Union: return K::Struct;
                case cpptools::SymbolKind::Enum: return K::Enum;
                case cpptools::SymbolKind::Function:
                case cpptools::SymbolKind::Method:
                case cpptools::SymbolKind::Constructor:
                case cpptools::SymbolKind::Destructor: return K::Function;
                case cpptools::SymbolKind::Field: return K::Field;
                default: return K::Variable;
            }
        }

        bool isScopeKind(cpptools::SymbolKind kind)
        {
            return kind == cpptools::SymbolKind::Namespace || kind == cpptools::SymbolKind::Class || kind == cpptools::SymbolKind::Struct
                || kind == cpptools::SymbolKind::Union || kind == cpptools::SymbolKind::Enum || kind == cpptools::SymbolKind::ClassTemplate;
        }

        // Namespaces first, then types, then functions, then data; each by name.
        int rankOf(ExplorerNode::Kind kind)
        {
            using K = ExplorerNode::Kind;
            switch (kind) {
                case K::Namespace: return 0;
                case K::Class: case K::Struct: case K::Enum: return 1;
                case K::Function: return 2;
                default: return 3;
            }
        }

        std::string fileNameOf(const std::string& path) { return fs::path(path).filename().string(); }

        bool isAbsolutePath(const std::string& path)
        {
            return (path.size() > 1 && path[1] == ':') || (!path.empty() && (path[0] == '/' || path[0] == '\\'));
        }

        std::string absoluteIn(const std::string& dir, const std::string& file)
        {
            return isAbsolutePath(file) ? file : dir + "/" + file;
        }

        bool isExternalTarget(const cmakemodel::Target& target, const cmakemodel::Model& model)
        {
            if (startsWith(target.directory, "3rdparty/")) return true;
            const std::string& file = target.defined.file;
            return isAbsolutePath(file) && !startsWith(lowered(file), lowered(model.sourceDir));
        }

        const char* describe(cmakemodel::TargetType type) { return cmakemodel::toString(type); }
    }

    std::string ExplorerNode::display() const
    {
        std::string prefix;
        switch (kind) {
            case Kind::Namespace: prefix = "namespace "; break;
            case Kind::Class: prefix = "class "; break;
            case Kind::Struct: prefix = "struct "; break;
            case Kind::Enum: prefix = "enum "; break;
            default: break;
        }
        std::string shown = prefix + text + (kind == Kind::Folder ? "/" : "");
        if (!detail.empty()) shown += "   " + detail;
        for (const std::string& cell : cells) shown += "   " + cell;
        return shown;
    }

    static ExplorerIcon baseIconFor(const ExplorerNode& node)
    {
        using Kind = ExplorerNode::Kind;
        auto themed = [](const std::string& name) { return ExplorerIcon{ name, true }; };
        switch (node.kind) {
            case Kind::Folder: return themed("folders/folder");
            case Kind::File: {
                const std::string name = lowered(node.text);
                const std::string ext = lowered(fs::path(node.text).extension().string());
                if (name == "cmakelists.txt" || ext == ".cmake") return themed("files/cmake");
                if (ext == ".h" || ext == ".hpp" || ext == ".hh" || ext == ".hxx" || ext == ".inl") return themed("files-actions/header");
                if (ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".c") return themed("files-actions/source");
                if (ext == ".json") return themed("files/json");
                if (ext == ".md") return themed("files/markdown");
                if (ext == ".newui") return themed("files/newui");
                if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".gif" || ext == ".bmp" || ext == ".ico" || ext == ".svg") return themed("files/image");
                return themed("files/document");
            }
            case Kind::Namespace: return themed("symbols/namespace");
            case Kind::Class: return themed("symbols/class");
            case Kind::Struct: return themed("symbols/struct");
            case Kind::Enum: return themed("symbols/enum");
            case Kind::Function: return themed("symbols/function");
            case Kind::Field: return themed("symbols/field");
            case Kind::Variable: return themed("symbols/variable");
            case Kind::Product: {
                if (!node.iconHint.empty()) return themed("products/" + node.iconHint);
                auto has = [&](const char* word) { return node.detail.find(word) != std::string::npos; };
                if (has("executable")) return themed("products/executable");
                if (has("shared") || has("module")) return themed("products/shared-library");
                if (has("object")) return themed("products/object-library");
                if (has("interface")) return themed("products/interface-library");
                if (has("utility")) return themed("products/custom-command");
                return themed("products/static-library");
            }
            case Kind::Link: return themed("statements/include");
            case Kind::Note: return ExplorerIcon();
            case Kind::Group: return ExplorerIcon();
        }
        return ExplorerIcon();
    }

    ExplorerIcon explorerIconFor(const ExplorerNode& node)
    {
        ExplorerIcon icon = baseIconFor(node);
        if (node.badge == ExplorerNode::Badge::None || !icon.themed) return icon;

        // Which bases have a composed icon for which badge (see the set's readme).
        struct Composed { const char* base; const char* stem; const char* badges; };   // badges: e, w, n, u
        static const Composed kComposed[] = {
            { "files-actions/source", "source", "ewnu" },
            { "files-actions/header", "header", "ewu" },
            { "files/cmake", "cmake", "ewn" },
            { "folders/folder", "folder", "ew" },
        };
        using Badge = ExplorerNode::Badge;
        const char letter = node.badge == Badge::Error ? 'e' : node.badge == Badge::Warning ? 'w' : node.badge == Badge::NotBuilt ? 'n' : 'u';
        const char* suffix = node.badge == Badge::Error ? "error" : node.badge == Badge::Warning ? "warning"
                           : node.badge == Badge::NotBuilt ? "notbuilt" : "unreferenced";
        for (const Composed& composed : kComposed) {
            if (icon.name == composed.base && std::string(composed.badges).find(letter) != std::string::npos) {
                return ExplorerIcon{ std::string("badged/") + composed.stem + "-" + suffix, true };
            }
        }
        return icon;
    }

    std::string explorerIconPath(const ExplorerIcon& icon, bool dark)
    {
        if (icon.empty() || !icon.themed) return icon.name;
        return std::string("Images/icons/cpp-symbol-icons-32/") + (dark ? "dark/" : "light/") + icon.name + ".svg";
    }

    void ExplorerTreeModel::setRoot(ExplorerNode root)
    {
        root_ = std::move(root);
        onChanged(*this);
    }

    void ExplorerTreeModel::ensureLoaded(const ExplorerNode& nodeConst) const
    {
        ExplorerNode& node = const_cast<ExplorerNode&>(nodeConst);
        if (node.loaded || !node.loader) return;
        auto loader = std::move(node.loader);
        node.loader = nullptr;
        node.loaded = true;
        loader(node);
    }

    const ExplorerNode* ExplorerTreeModel::nodeAt(const std::vector<std::size_t>& path) const
    {
        const ExplorerNode* current = &root_;
        for (std::size_t index : path) {
            ensureLoaded(*current);
            if (index >= current->children.size()) return nullptr;
            current = &current->children[index];
        }
        return current;
    }

    bool ExplorerTreeModel::hasChildren(const std::vector<std::size_t>& path) const
    {
        const ExplorerNode* current = &root_;
        for (std::size_t index : path) {
            ensureLoaded(*current);
            if (index >= current->children.size()) return false;
            current = &current->children[index];
        }
        return (!current->loaded && current->loader) || !current->children.empty();
    }

    std::size_t ExplorerTreeModel::childCount(const std::vector<std::size_t>& path) const
    {
        const ExplorerNode* node = nodeAt(path);
        if (node == nullptr) return 0;
        ensureLoaded(*node);
        return node->children.size();
    }

    std::any ExplorerTreeModel::value(const std::any& key)
    {
        const auto path = std::any_cast<std::vector<std::size_t>>(key);
        const ExplorerNode* node = nodeAt(path);
        return node == nullptr ? std::string() : node->display();
    }

    ExplorerNode buildFilesTree(const std::string& root, const FileDetailProvider& detail, const FileBadgeProvider& badge)
    {
        ExplorerNode top;
        top.kind = ExplorerNode::Kind::Folder;
        top.text = fs::path(root).filename().string();
        const std::string path = fs::path(root).generic_string();
        top.loaded = false;
        top.loader = [path, detail, badge](ExplorerNode& node) { fillFolder(node, path, detail, badge); };
        return top;
    }

    ExplorerNode buildFileSearch(const std::string& root, const std::string& filter, std::size_t limit)
    {
        ExplorerNode top;
        const std::string needle = lowered(filter);
        if (needle.empty()) {
            return buildMessageTree("Type to search file names.");
        }
        std::error_code ec;
        fs::path base(root);
        bool cut = false;
        for (fs::recursive_directory_iterator it(base, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
            const std::string name = it->path().filename().string();
            std::error_code typeEc;
            if (name.empty() || name.front() == '.') {
                if (it->is_directory(typeEc)) it.disable_recursion_pending();
                continue;
            }
            if (it->is_directory(typeEc)) {
                if (isSkippedFolder(name)) it.disable_recursion_pending();
                continue;
            }
            if (isSkippedFile(name) || lowered(name).find(needle) == std::string::npos) continue;
            if (top.children.size() >= limit) { cut = true; break; }
            ExplorerNode file;
            file.kind = ExplorerNode::Kind::File;
            file.text = name;
            file.path = it->path().generic_string();
            std::error_code relEc;
            file.detail = fs::relative(it->path().parent_path(), base, relEc).generic_string();
            if (file.detail == ".") file.detail.clear();
            file.search = lowered(name);
            top.children.push_back(std::move(file));
        }
        std::sort(top.children.begin(), top.children.end(), lessNoCase);
        if (top.children.empty()) return buildMessageTree("No file name contains \"" + filter + "\".");
        if (cut) {
            ExplorerNode note;
            note.kind = ExplorerNode::Kind::Note;
            note.text = "Showing the first " + std::to_string(limit) + " matches; type more to narrow it.";
            top.children.push_back(std::move(note));
        }
        return top;
    }

    ExplorerNode buildSymbolsTree(const cpptools::ProjectIndex& index, const std::string& filter)
    {
        // One symbol per USR, whichever files declare it.
        std::map<std::string, cpptools::IndexedSymbol> chosen;
        for (const std::string& file : index.files()) {
            for (const cpptools::IndexedSymbol& symbol : index.symbolsIn(file)) {
                auto it = chosen.find(symbol.usr);
                if (it == chosen.end()) {
                    chosen.emplace(symbol.usr, symbol);
                    continue;
                }
                // A type or namespace is shown where it is defined; a member where it is declared.
                const bool wantDefinition = isScopeKind(symbol.kind);
                if (symbol.isDefinition == wantDefinition && it->second.isDefinition != wantDefinition) {
                    it->second = symbol;
                }
            }
        }

        std::vector<ExplorerNode> nodes;
        std::vector<std::string> parents;
        std::map<std::string, std::size_t> indexOf;
        for (const auto& entry : chosen) {
            const cpptools::IndexedSymbol& symbol = entry.second;
            ExplorerNode node;
            node.kind = nodeKindOf(symbol.kind);
            node.text = node.kind == ExplorerNode::Kind::Function ? symbol.name + "()" : symbol.name;
            // A namespace is declared all over the project: a double-click on it just expands it.
            if (symbol.kind != cpptools::SymbolKind::Namespace) {
                node.path = symbol.location.file;
                node.line = symbol.location.line;
            }
            node.search = lowered(symbol.qualifiedName);
            if (isScopeKind(symbol.kind) && symbol.kind != cpptools::SymbolKind::Namespace) node.detail = fileNameOf(symbol.location.file);
            indexOf[entry.first] = nodes.size();
            nodes.push_back(std::move(node));
            parents.push_back(symbol.parentUsr);
        }

        std::vector<std::vector<std::size_t>> kids(nodes.size());
        std::vector<std::size_t> top;
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            auto parent = indexOf.find(parents[i]);
            if (parents[i].empty() || parent == indexOf.end() || parent->second == i) top.push_back(i);
            else kids[parent->second].push_back(i);
        }

        auto order = [&nodes](std::vector<std::size_t>& list) {
            std::sort(list.begin(), list.end(), [&nodes](std::size_t a, std::size_t b) {
                const int ra = rankOf(nodes[a].kind);
                const int rb = rankOf(nodes[b].kind);
                return ra != rb ? ra < rb : lowered(nodes[a].text) < lowered(nodes[b].text);
            });
        };
        std::function<ExplorerNode(std::size_t, int)> assemble = [&](std::size_t i, int depth) {
            ExplorerNode node = std::move(nodes[i]);
            order(kids[i]);
            if (depth < 64) {   // a parent cycle in bad data must not recurse forever
                for (std::size_t child : kids[i]) node.children.push_back(assemble(child, depth + 1));
            }
            return node;
        };

        ExplorerNode root;
        order(top);
        for (std::size_t i : top) root.children.push_back(assemble(i, 0));
        pruneRoot(root, filter);
        if (root.children.empty()) {
            return buildMessageTree(filter.empty() ? "Nothing is indexed yet." : "No symbol contains \"" + filter + "\".");
        }
        return root;
    }

    ExplorerNode buildProductsTree(const cmakemodel::Model& model, const std::string& filter)
    {
        using K = ExplorerNode::Kind;
        struct Section { cmakemodel::ProductKind kind; const char* title; };
        static const Section kSections[] = {
            { cmakemodel::ProductKind::Application, "Applications" },
            { cmakemodel::ProductKind::Library, "Libraries" },
            { cmakemodel::ProductKind::Test, "Tests" },
            { cmakemodel::ProductKind::Tool, "Tools" },
        };

        auto group = [](const std::string& title, std::size_t count) {
            ExplorerNode node;
            node.kind = K::Group;
            node.text = title;
            if (count > 0) node.detail = "(" + std::to_string(count) + ")";
            node.search = lowered(title);
            return node;
        };

        ExplorerNode root;
        for (const Section& section : kSections) {
            ExplorerNode heading = group(section.title, 0);
            for (const cmakemodel::Target* target : model.byKind(section.kind)) {
                if (isExternalTarget(*target, model)) continue;

                ExplorerNode product;
                product.kind = K::Product;
                product.text = target->name;
                product.detail = describe(target->type);
                product.iconHint = section.kind == cmakemodel::ProductKind::Test ? "test"
                                 : section.kind == cmakemodel::ProductKind::Tool ? "custom-command" : std::string();
                product.path =target->defined.valid() ? absoluteIn(model.sourceDir, target->defined.file) : std::string();
                product.line = static_cast<std::size_t>(target->defined.line);
                product.search = lowered(target->name);

                // Build Time: what goes into it.
                ExplorerNode buildTime = group("Build Time", 0);
                const std::vector<cmakemodel::SourceFile> sources = model.allSources(*target);
                ExplorerNode sourceList = group("Sources", sources.size());
                for (const cmakemodel::SourceFile& source : sources) {
                    ExplorerNode file;
                    file.kind = K::File;
                    file.text = fileNameOf(source.path);
                    file.detail = source.via.empty() ? std::string() : "via " + source.via;
                    file.path = absoluteIn(model.sourceDir, source.path);
                    file.search = lowered(source.path);
                    sourceList.children.push_back(std::move(file));
                }
                std::sort(sourceList.children.begin(), sourceList.children.end(), lessNoCase);
                if (!sources.empty()) buildTime.children.push_back(std::move(sourceList));

                ExplorerNode linkList = group("Links", target->links.size());
                for (const cmakemodel::LinkItem& link : target->links) {
                    ExplorerNode node;
                    node.kind = K::Link;
                    node.text = link.name;
                    node.detail = link.kind == cmakemodel::LinkKind::Target ? "target"
                                : link.kind == cmakemodel::LinkKind::Imported ? "imported" : "file";
                    node.path = link.defined.valid() ? absoluteIn(model.sourceDir, link.defined.file) : std::string();
                    node.line = static_cast<std::size_t>(link.defined.line);
                    node.search = lowered(link.name);
                    linkList.children.push_back(std::move(node));
                }
                if (!target->links.empty()) buildTime.children.push_back(std::move(linkList));
                if (!buildTime.children.empty()) product.children.push_back(std::move(buildTime));

                // Runtime: what it is and what it needs to run with.
                ExplorerNode runtime = group("Runtime", 0);
                if (!target->nameOnDisk.empty()) {
                    ExplorerNode artifact;
                    artifact.kind = K::Note;
                    artifact.text = "Builds " + target->nameOnDisk;
                    runtime.children.push_back(std::move(artifact));
                }
                for (const cmakemodel::LinkItem& link : target->links) {
                    const cmakemodel::Target* linked = link.kind == cmakemodel::LinkKind::Target ? model.find(link.name) : nullptr;
                    if (linked != nullptr && linked->type == cmakemodel::TargetType::SharedLibrary) {
                        ExplorerNode need;
                        need.kind = K::Link;
                        need.text = linked->nameOnDisk.empty() ? linked->name : linked->nameOnDisk;
                        need.detail = "shared library";
                        need.search = lowered(need.text);
                        runtime.children.push_back(std::move(need));
                    }
                }
                ExplorerNode unread;
                unread.kind = ExplorerNode::Kind::Note;
                unread.text = "Copy steps and install rules are not read yet.";
                runtime.children.push_back(std::move(unread));
                product.children.push_back(std::move(runtime));

                heading.children.push_back(std::move(product));
            }
            if (!heading.children.empty()) {
                heading.detail = "(" + std::to_string(heading.children.size()) + ")";
                root.children.push_back(std::move(heading));
            }
        }
        pruneRoot(root, filter);
        if (root.children.empty()) {
            return buildMessageTree(filter.empty() ? "No products found." : "No product or file contains \"" + filter + "\".");
        }
        return root;
    }

    ExplorerNode buildMessageTree(const std::string& message)
    {
        ExplorerNode root;
        ExplorerNode note;
        note.kind = ExplorerNode::Kind::Note;
        note.text = message;
        root.children.push_back(std::move(note));
        return root;
    }

    bool isSkippedExplorerFolder(const std::string& name)
    {
        return isSkippedFolder(name);
    }

    std::string formatByteSize(std::uint64_t bytes)
    {
        return sizeText(bytes);
    }

    namespace
    {
        std::string millisText(double ms)
        {
            char buffer[32];
            if (ms < 1000.0) std::snprintf(buffer, sizeof buffer, "%.0f ms", ms);
            else std::snprintf(buffer, sizeof buffer, "%.1f s", ms / 1000.0);
            return buffer;
        }

        // Counts one top-level folder's files and bytes, all levels.
        void countFolder(const fs::path& path, FolderStat& stat, const std::atomic<bool>* cancel)
        {
            std::error_code ec;
            fs::recursive_directory_iterator it(path, fs::directory_options::skip_permission_denied, ec);
            const fs::recursive_directory_iterator end;
            for (; !ec && it != end; it.increment(ec)) {
                if (cancel != nullptr && cancel->load()) return;
                const std::string name = it->path().filename().string();
                std::error_code typeEc;
                const bool isDir = it->is_directory(typeEc);
                if (typeEc) continue;
                if (isDir) {
                    if (name.empty() || name.front() == '.' || isSkippedFolder(name)) {
                        it.disable_recursion_pending();
                    } else {
                        ++stat.folders;
                    }
                } else if (!name.empty() && name.front() != '.' && !isSkippedFile(name)) {
                    std::error_code sizeEc;
                    const auto bytes = it->file_size(sizeEc);
                    ++stat.files;
                    if (!sizeEc) stat.bytes += bytes;
                }
            }
        }

        // `path` below `root` (either slash, any case), or `path` unchanged when it is elsewhere.
        std::string relativeTo(std::string root, std::string path)
        {
            std::replace(root.begin(), root.end(), '\\', '/');
            std::replace(path.begin(), path.end(), '\\', '/');
            while (!root.empty() && root.back() == '/') root.pop_back();
            if (!root.empty() && path.size() > root.size() + 1 && path[root.size()] == '/' &&
                lowered(path.substr(0, root.size())) == lowered(root)) {
                return path.substr(root.size() + 1);
            }
            return path;
        }

        void addNote(ExplorerNode& parent, const std::string& text, const std::string& detail = std::string())
        {
            ExplorerNode note;
            note.kind = ExplorerNode::Kind::Note;
            note.text = text;
            note.detail = detail;
            note.search = lowered(text);
            parent.children.push_back(std::move(note));
        }

        using Tone = ExplorerNode::Tone;

        void addCells(ExplorerNode& parent, const std::string& text, std::vector<std::string> cells, std::vector<Tone> tones,
                      ExplorerNode::Kind kind = ExplorerNode::Kind::Note)
        {
            ExplorerNode row;
            row.kind = kind;
            row.text = text;
            row.search = lowered(text);
            row.cells = std::move(cells);
            row.cellTones = std::move(tones);
            parent.children.push_back(std::move(row));
        }

        // Quick steps read as good, slow ones as a warning.
        Tone timeTone(double ms)
        {
            return ms < 1000.0 ? Tone::Good : ms < 10000.0 ? Tone::Normal : ms < 30000.0 ? Tone::Warn : Tone::Bad;
        }

        // A folder that is most of the project is where the bloat is.
        Tone shareTone(std::uint64_t part, std::uint64_t whole)
        {
            if (whole == 0) return Tone::Normal;
            const double share = static_cast<double>(part) / static_cast<double>(whole);
            return share >= 0.5 ? Tone::Bad : share >= 0.25 ? Tone::Warn : Tone::Normal;
        }

        ExplorerNode group(const std::string& text)
        {
            ExplorerNode node;
            node.kind = ExplorerNode::Kind::Group;
            node.text = text;
            return node;
        }
    }

    FolderScan scanProjectFolders(const std::string& root, const ScanProgress& progress,
                                  const std::atomic<bool>* cancel, unsigned threads)
    {
        FolderScan scan;
        std::vector<fs::path> dirs;
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(root, fs::directory_options::skip_permission_denied, ec)) {
            const std::string name = entry.path().filename().string();
            if (name.empty() || name.front() == '.') continue;
            std::error_code typeEc;
            if (entry.is_directory(typeEc)) {
                dirs.push_back(entry.path());
            } else if (!typeEc && !isSkippedFile(name)) {
                std::error_code sizeEc;
                const auto bytes = entry.file_size(sizeEc);
                ++scan.rootFiles;
                if (!sizeEc) scan.rootBytes += bytes;
            }
        }
        std::sort(dirs.begin(), dirs.end(), [](const fs::path& a, const fs::path& b) {
            return lowered(a.filename().string()) < lowered(b.filename().string());
        });

        scan.folders.resize(dirs.size());
        for (std::size_t i = 0; i < dirs.size(); ++i) {
            scan.folders[i].name = dirs[i].filename().string();
            scan.folders[i].skipped = isSkippedFolder(scan.folders[i].name);
        }

        if (threads == 0) {
            const unsigned cores = std::thread::hardware_concurrency();
            threads = cores > 1 ? cores - 1 : 1;
        }
        threads = std::max(1u, std::min<unsigned>(threads, static_cast<unsigned>(std::max<std::size_t>(dirs.size(), 1))));

        std::atomic<std::size_t> next{ 0 };
        std::size_t done = 0;
        std::mutex reportMutex;
        auto worker = [&]() {
            for (;;) {
                const std::size_t at = next.fetch_add(1);
                if (at >= dirs.size() || (cancel != nullptr && cancel->load())) return;
                if (!scan.folders[at].skipped) countFolder(dirs[at], scan.folders[at], cancel);
                std::lock_guard<std::mutex> lock(reportMutex);
                ++done;
                if (progress) progress(done, dirs.size(), scan.folders[at].name);
            }
        };
        std::vector<std::thread> pool;
        for (unsigned i = 1; i < threads; ++i) pool.emplace_back(worker);
        worker();
        for (std::thread& t : pool) t.join();
        return scan;
    }

    ExplorerNode buildStatsTree(const ProjectStats& stats)
    {
        ExplorerNode root;

        ExplorerNode timing = group("Timing");
        auto addTime = [&](const std::string& text, double ms, const std::string& extra = std::string()) {
            addCells(timing, text, { millisText(ms) }, { timeTone(ms) });
            if (!extra.empty()) timing.children.back().detail = extra;
        };
        addTime("Scanning folders", stats.scanMs);
        addTime("Reading CMake's description", stats.cmakeMs);
        if (stats.cmakeRefreshMs > 0) addTime("Configuring CMake again", stats.cmakeRefreshMs);
        addTime("Loading the index cache", stats.cacheLoadMs);
        addTime("Indexing", stats.indexMs, stats.indexThreads > 0 ? "on " + std::to_string(stats.indexThreads) + " threads" : std::string());
        addTime(stats.complete ? "Total" : "Total so far", stats.totalMs);
        root.children.push_back(std::move(timing));

        std::uint64_t totalFiles = stats.scan.rootFiles;
        std::uint64_t totalBytes = stats.scan.rootBytes;
        std::vector<const FolderStat*> sorted;
        for (const FolderStat& folder : stats.scan.folders) {
            totalFiles += folder.files;
            totalBytes += folder.bytes;
            sorted.push_back(&folder);
        }
        std::stable_sort(sorted.begin(), sorted.end(), [](const FolderStat* a, const FolderStat* b) {
            if (a->skipped != b->skipped) return !a->skipped;
            return a->bytes > b->bytes;
        });
        ExplorerNode folders = group("Top-level folders");
        folders.detail = "(" + std::to_string(stats.scan.folders.size()) + ", " + countText(totalFiles) + " files, " + sizeText(totalBytes) + ")";
        for (const FolderStat* folder : sorted) {
            if (folder->skipped) {
                addCells(folders, folder->name, {}, {}, ExplorerNode::Kind::Folder);
                folders.children.back().detail = "not shown (build output or dependencies)";
                continue;
            }
            addCells(folders, folder->name,
                { countText(folder->files) + " files", sizeText(folder->bytes), countText(folder->folders) + " folders" },
                { Tone::Normal, shareTone(folder->bytes, totalBytes), Tone::Muted }, ExplorerNode::Kind::Folder);
            folders.children.back().dropRightFirst = true;   // files and size matter more than the subfolder count
        }
        if (stats.scan.rootFiles > 0) {
            addCells(folders, "(files in the root)", { countText(stats.scan.rootFiles) + " files", sizeText(stats.scan.rootBytes), "" },
                     { Tone::Muted, Tone::Muted, Tone::Muted });
        }
        root.children.push_back(std::move(folders));

        ExplorerNode index = group("Index");
        addCells(index, "Files", { countText(stats.filesToIndex) }, { Tone::Normal });
        addCells(index, "Symbols", { countText(stats.symbols) }, { Tone::Normal });
        addCells(index, "Read this time", { countText(stats.parsed) }, { stats.parsed > 0 ? Tone::Accent : Tone::Muted });
        addCells(index, "Up to date in the cache", { countText(stats.upToDate) }, { Tone::Good });
        if (stats.failed > 0) addCells(index, "Could not be read", { countText(stats.failed) }, { Tone::Bad });
        // The files behind a count, most problems first, each a row that opens the file.
        auto addFiles = [&](ExplorerNode& row, ExplorerNode::Badge badge, Tone tone, const std::string& noun,
                            const std::function<bool(const ProblemFile&)>& wanted,
                            const std::function<std::uint32_t(const ProblemFile&)>& count) {
            std::vector<const ProblemFile*> matching;
            for (const ProblemFile& file : stats.problemFiles) {
                if (wanted(file)) matching.push_back(&file);
            }
            std::stable_sort(matching.begin(), matching.end(), [&](const ProblemFile* a, const ProblemFile* b) {
                return count(*a) != count(*b) ? count(*a) > count(*b) : lowered(a->path) < lowered(b->path);
            });
            const std::size_t kMostShown = 300;   // a whole project of failures is a flags problem, not a list to read
            for (std::size_t i = 0; i < matching.size() && i < kMostShown; ++i) {
                ExplorerNode file;
                file.kind = ExplorerNode::Kind::File;
                file.text = relativeTo(stats.root, matching[i]->path);
                file.path = matching[i]->path;
                file.search = lowered(file.text);
                file.badge = badge;
                const std::uint32_t n = count(*matching[i]);
                file.detail = std::to_string(n) + " " + noun + (n == 1 ? "" : "s");
                file.detailTone = tone;
                // what the compiler actually said: each message opens its file at the line
                for (const ProblemSample& sample : matching[i]->samples) {
                    ExplorerNode said;
                    said.kind = ExplorerNode::Kind::Note;
                    said.text = sample.message;
                    said.detail = (sample.file.empty() || sample.file == matching[i]->path ? std::string() : relativeTo(stats.root, sample.file) + " ") +
                                  "line " + std::to_string(sample.line);
                    said.path = sample.file.empty() ? matching[i]->path : sample.file;
                    said.line = sample.line;
                    said.search = lowered(said.text);
                    file.children.push_back(std::move(said));
                }
                row.children.push_back(std::move(file));
            }
            if (matching.size() > kMostShown) {
                addNote(row, "and " + countText(matching.size() - kMostShown) + " more");
            }
        };

        addCells(index, "Files with errors", { countText(stats.filesWithErrors) }, { stats.filesWithErrors > 0 ? Tone::Bad : Tone::Muted });
        addFiles(index.children.back(), ExplorerNode::Badge::Error, Tone::Bad, "error",
                 [](const ProblemFile& f) { return f.unresolvedIncludes == 0 && f.errors > 0; },
                 [](const ProblemFile& f) { return f.errors; });
        addCells(index, "Files with warnings", { countText(stats.filesWithWarnings) }, { stats.filesWithWarnings > 0 ? Tone::Warn : Tone::Muted });
        addFiles(index.children.back(), ExplorerNode::Badge::Warning, Tone::Warn, "warning",
                 [](const ProblemFile& f) { return f.unresolvedIncludes == 0 && f.errors == 0 && f.warnings > 0; },
                 [](const ProblemFile& f) { return f.warnings; });
        addCells(index, "Files with unresolved includes", { countText(stats.filesWithUnresolvedIncludes) },
                 { stats.filesWithUnresolvedIncludes > 0 ? Tone::Warn : Tone::Muted });
        addFiles(index.children.back(), ExplorerNode::Badge::Warning, Tone::Warn, "unresolved include",
                 [](const ProblemFile& f) { return f.unresolvedIncludes > 0; },
                 [](const ProblemFile& f) { return f.unresolvedIncludes; });
        if (stats.filesWithUnresolvedIncludes > 0) {
            index.children.back().detail = countText(stats.unresolvedIncludes) + " includes";
            // a lot of these means the compile flags are missing, not that the code is wrong
            addNote(index, "Usually missing include paths: check compile_commands.json");
        }
        root.children.push_back(std::move(index));

        ExplorerNode cmake = group("CMake");
        if (stats.buildDir.empty()) {
            addNote(cmake, "No build tree found");
        } else {
            addNote(cmake, "Build folder", stats.buildDir);
            addNote(cmake, "Configuration", stats.configuration);
            addCells(cmake, "Targets", { countText(stats.targets) }, { Tone::Normal });
            if (stats.targetsMissing > 0) addCells(cmake, "Targets missing from the reply", { countText(stats.targetsMissing) }, { Tone::Warn });
        }
        root.children.push_back(std::move(cmake));
        return root;
    }
}
