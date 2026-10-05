#include <cmakemodel/fileapi.h>

#include <lex/json5_parser.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>

namespace fs = std::filesystem;

namespace cmakemodel
{
    namespace
    {
        using lex::json5::ASTNode;
        using lex::json5::ASTNodeType;
        using lex::json5::PrimitiveKind;

        std::wstring widen(const std::string& utf8)
        {
            std::wstring out;
            out.reserve(utf8.size());
            for (std::size_t i = 0; i < utf8.size();) {
                const unsigned char c = static_cast<unsigned char>(utf8[i]);
                std::uint32_t cp = 0;
                int extra = 0;
                if (c < 0x80) { cp = c; }
                else if ((c >> 5) == 0x6) { cp = c & 0x1F; extra = 1; }
                else if ((c >> 4) == 0xE) { cp = c & 0x0F; extra = 2; }
                else if ((c >> 3) == 0x1E) { cp = c & 0x07; extra = 3; }
                else { cp = 0xFFFD; }
                ++i;
                for (int k = 0; k < extra && i < utf8.size(); ++k, ++i) {
                    cp = (cp << 6) | (static_cast<unsigned char>(utf8[i]) & 0x3F);
                }
                if (cp >= 0x10000) {
                    cp -= 0x10000;
                    out += static_cast<wchar_t>(0xD800 + (cp >> 10));
                    out += static_cast<wchar_t>(0xDC00 + (cp & 0x3FF));
                } else {
                    out += static_cast<wchar_t>(cp);
                }
            }
            return out;
        }

        std::string narrow(std::wstring_view wide)
        {
            std::string out;
            out.reserve(wide.size());
            for (std::size_t i = 0; i < wide.size(); ++i) {
                std::uint32_t cp = wide[i];
                if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < wide.size() && wide[i + 1] >= 0xDC00 && wide[i + 1] < 0xE000) {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (wide[i + 1] - 0xDC00);
                    ++i;
                }
                if (cp < 0x80) {
                    out += static_cast<char>(cp);
                } else if (cp < 0x800) {
                    out += static_cast<char>(0xC0 | (cp >> 6));
                    out += static_cast<char>(0x80 | (cp & 0x3F));
                } else if (cp < 0x10000) {
                    out += static_cast<char>(0xE0 | (cp >> 12));
                    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                    out += static_cast<char>(0x80 | (cp & 0x3F));
                } else {
                    out += static_cast<char>(0xF0 | (cp >> 18));
                    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                    out += static_cast<char>(0x80 | (cp & 0x3F));
                }
            }
            return out;
        }

        // A read-only view of a node of the parsed JSON; a missing member is an invalid view that
        // answers everything with a default, so a reply from a newer or older CMake never crashes this.
        class Json
        {
        public:
            Json() = default;
            explicit Json(const ASTNode* node) : node_(node) {}

            bool valid() const { return node_ != nullptr; }

            Json get(std::wstring_view key) const
            {
                if (node_ == nullptr || node_->type != ASTNodeType::Object) {
                    return Json();
                }
                for (const auto& child : node_->children()) {
                    if (child && child->type == ASTNodeType::Property && child->key == key) {
                        return Json(child->valueNode());
                    }
                }
                return Json();
            }

            std::vector<Json> items() const
            {
                std::vector<Json> items;
                if (node_ != nullptr && node_->type == ASTNodeType::Array) {
                    for (const auto& child : node_->children()) {
                        if (child && !child->isComment()) {
                            items.emplace_back(child.get());
                        }
                    }
                }
                return items;
            }

            std::string str() const
            {
                if (node_ == nullptr || node_->type != ASTNodeType::Primitive || node_->primitive != PrimitiveKind::String) {
                    return std::string();
                }
                return narrow(lex::json5::decodeString(node_->text()));
            }

            long long integer(long long fallback = 0) const
            {
                if (node_ == nullptr || node_->type != ASTNodeType::Primitive || node_->primitive != PrimitiveKind::Number) {
                    return fallback;
                }
                return std::strtoll(narrow(node_->text()).c_str(), nullptr, 10);
            }

            bool boolean(bool fallback = false) const
            {
                if (node_ == nullptr || node_->type != ASTNodeType::Primitive || node_->primitive != PrimitiveKind::Boolean) {
                    return fallback;
                }
                return node_->text() == L"true";
            }

        private:
            const ASTNode* node_ = nullptr;
        };

        // A parsed file; keeps the tree alive for the Json views taken from it.
        struct Document
        {
            lex::json5::ParseResult parsed;
            Json root() const { return Json(parsed.root.get()); }
        };

        bool readJson(const fs::path& path, Document& out, std::string& error)
        {
            std::ifstream in(path, std::ios::binary);
            if (!in) {
                error = "cannot read " + path.string();
                return false;
            }
            const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            out.parsed = lex::json5::parse(widen(bytes));
            if (!out.parsed.ok()) {
                error = "cannot parse " + path.string();
                return false;
            }
            return true;
        }

        // The backtraceGraph of a target: node index to the CMake call it stands for.
        struct Backtrace
        {
            struct Node
            {
                int file = -1;
                int command = -1;
                int line = 0;
            };
            std::vector<std::string> files;
            std::vector<std::string> commands;
            std::vector<Node> nodes;

            explicit Backtrace(const Json& graph)
            {
                for (const Json& f : graph.get(L"files").items()) {
                    files.push_back(f.str());
                }
                for (const Json& c : graph.get(L"commands").items()) {
                    commands.push_back(c.str());
                }
                for (const Json& n : graph.get(L"nodes").items()) {
                    Node node;
                    node.file = static_cast<int>(n.get(L"file").integer(-1));
                    node.command = static_cast<int>(n.get(L"command").integer(-1));
                    node.line = static_cast<int>(n.get(L"line").integer(0));
                    nodes.push_back(node);
                }
            }

            Location at(long long index) const
            {
                Location location;
                if (index < 0 || index >= static_cast<long long>(nodes.size())) {
                    return location;
                }
                const Node& node = nodes[static_cast<std::size_t>(index)];
                if (node.file >= 0 && node.file < static_cast<int>(files.size())) {
                    location.file = files[static_cast<std::size_t>(node.file)];
                }
                if (node.command >= 0 && node.command < static_cast<int>(commands.size())) {
                    location.command = commands[static_cast<std::size_t>(node.command)];
                }
                location.line = node.line;
                return location;
            }
        };

        TargetType typeFromString(const std::string& type)
        {
            if (type == "EXECUTABLE") return TargetType::Executable;
            if (type == "STATIC_LIBRARY") return TargetType::StaticLibrary;
            if (type == "SHARED_LIBRARY") return TargetType::SharedLibrary;
            if (type == "MODULE_LIBRARY") return TargetType::ModuleLibrary;
            if (type == "OBJECT_LIBRARY") return TargetType::ObjectLibrary;
            if (type == "INTERFACE_LIBRARY") return TargetType::InterfaceLibrary;
            if (type == "UTILITY") return TargetType::Utility;
            return TargetType::Unknown;
        }

        // "libclang::libclang::@6890..." -> "libclang::libclang"
        std::string nameFromId(const std::string& id)
        {
            const std::size_t at = id.rfind("::@");
            return at == std::string::npos ? id : id.substr(0, at);
        }

        bool linksTestFramework(const Target& target)
        {
            for (const LinkItem& link : target.links) {
                if (link.kind != LinkKind::File && (link.name.find("gtest") != std::string::npos || link.name.find("gmock") != std::string::npos)) {
                    return true;
                }
            }
            return false;
        }

        bool readTarget(const fs::path& replyDir, const Json& entry, const std::map<std::string, std::string>& idToName,
                        const std::vector<std::string>& directories, Target& target, std::string& error)
        {
            Document doc;
            if (!readJson(replyDir / entry.get(L"jsonFile").str(), doc, error)) {
                return false;
            }
            const Json t = doc.root();
            const Backtrace backtrace(t.get(L"backtraceGraph"));

            target.id = t.get(L"id").str();
            target.name = t.get(L"name").str();
            target.type = typeFromString(t.get(L"type").str());
            target.nameOnDisk = t.get(L"nameOnDisk").str();
            for (const Json& artifact : t.get(L"artifacts").items()) {
                target.artifacts.push_back(artifact.str().empty() ? artifact.get(L"path").str() : artifact.str());
            }
            target.defined = backtrace.at(t.get(L"backtrace").integer(-1));
            const long long dirIndex = entry.get(L"directoryIndex").integer(-1);
            if (dirIndex >= 0 && dirIndex < static_cast<long long>(directories.size())) {
                target.directory = directories[static_cast<std::size_t>(dirIndex)];
            }

            std::map<long long, std::string> groupOfSource;
            for (const Json& group : t.get(L"sourceGroups").items()) {
                for (const Json& index : group.get(L"sourceIndexes").items()) {
                    groupOfSource[index.integer(-1)] = group.get(L"name").str();
                }
            }
            long long sourceIndex = 0;
            for (const Json& s : t.get(L"sources").items()) {
                SourceFile source;
                source.path = s.get(L"path").str();
                source.generated = s.get(L"isGenerated").boolean(false);
                source.group = groupOfSource[sourceIndex];
                source.defined = backtrace.at(s.get(L"backtrace").integer(-1));
                ++sourceIndex;
                if (source.group == "Object Libraries") {
                    continue;   // the object files of a linked OBJECT library, not sources of this target
                }
                target.sources.push_back(std::move(source));
            }

            for (const Json& l : t.get(L"linkLibraries").items()) {
                LinkItem link;
                link.defined = backtrace.at(l.get(L"backtrace").integer(-1));
                if (l.get(L"id").valid()) {
                    const std::string id = l.get(L"id").str();
                    auto known = idToName.find(id);
                    link.kind = known != idToName.end() ? LinkKind::Target : LinkKind::Imported;
                    link.name = known != idToName.end() ? known->second : nameFromId(id);
                } else {
                    link.kind = LinkKind::File;
                    link.name = l.get(L"fragment").str();
                }
                target.links.push_back(std::move(link));
            }

            for (const Json& d : t.get(L"dependencies").items()) {
                const std::string id = d.get(L"id").str();
                auto known = idToName.find(id);
                const std::string name = known != idToName.end() ? known->second : nameFromId(id);
                if (name != "ZERO_CHECK" && name != "ALL_BUILD") {
                    target.dependencies.push_back(name);
                }
            }

            std::set<std::string> seenIncludes;
            std::set<std::string> seenDefinitions;
            for (const Json& group : t.get(L"compileGroups").items()) {
                for (const Json& d : group.get(L"defines").items()) {
                    const std::string define = d.get(L"define").str();
                    if (seenDefinitions.insert(define).second) {
                        target.definitions.push_back(define);
                    }
                }
                for (const Json& i : group.get(L"includes").items()) {
                    const std::string path = i.get(L"path").str();
                    if (seenIncludes.insert(path).second) {
                        target.includeDirs.push_back(path);
                    }
                }
            }

            target.kind = classify(target, linksTestFramework(target));
            return true;
        }
    }

    bool requestQueries(const std::string& buildDir, std::string* error)
    {
        std::error_code ec;
        if (!fs::is_directory(buildDir, ec)) {
            if (error != nullptr) {
                *error = "no build directory: " + buildDir;
            }
            return false;
        }
        const fs::path query = fs::path(buildDir) / ".cmake" / "api" / "v1" / "query";
        fs::create_directories(query, ec);
        std::ofstream file(query / "codemodel-v2");   // an empty file is the request
        if (ec || !file) {
            if (error != nullptr) {
                *error = "cannot write " + (query / "codemodel-v2").string();
            }
            return false;
        }
        return true;
    }

    LoadResult loadFileApi(const std::string& buildDir, const std::string& configuration)
    {
        LoadResult result;
        const fs::path replyDir = fs::path(buildDir) / ".cmake" / "api" / "v1" / "reply";
        std::error_code ec;
        if (!fs::is_directory(replyDir, ec)) {
            result.error = "no File API reply: configure the project once after requestQueries()";
            return result;
        }

        // index-<timestamp>.json: the last one by name is the newest.
        fs::path indexPath;
        for (const auto& entry : fs::directory_iterator(replyDir, ec)) {
            const std::string name = entry.path().filename().string();
            if (name.rfind("index-", 0) == 0 && name.size() > 5 && (indexPath.empty() || name > indexPath.filename().string())) {
                indexPath = entry.path();
            }
        }
        if (indexPath.empty()) {
            result.error = "no File API reply index in " + replyDir.string();
            return result;
        }

        Document index;
        if (!readJson(indexPath, index, result.error)) {
            return result;
        }
        std::string codemodelFile;
        for (const Json& object : index.root().get(L"objects").items()) {
            if (object.get(L"kind").str() == "codemodel") {
                codemodelFile = object.get(L"jsonFile").str();
            }
        }
        if (codemodelFile.empty()) {
            result.error = "the reply has no codemodel: request it with requestQueries() and configure again";
            return result;
        }

        Model& model = result.model;
        const Json cmake = index.root().get(L"cmake");
        result.cmakeExe = cmake.get(L"paths").get(L"cmake").str();
        model.cmakeVersion = cmake.get(L"version").get(L"string").str();
        model.generator = cmake.get(L"generator").get(L"name").str();
        model.multiConfig = cmake.get(L"generator").get(L"multiConfig").boolean(false);

        Document codemodel;
        if (!readJson(replyDir / codemodelFile, codemodel, result.error)) {
            return result;
        }
        model.sourceDir = codemodel.root().get(L"paths").get(L"source").str();
        model.buildDir = codemodel.root().get(L"paths").get(L"build").str();

        const std::vector<Json> configurations = codemodel.root().get(L"configurations").items();
        if (configurations.empty()) {
            result.error = "the codemodel has no configurations";
            return result;
        }
        const Json* chosen = nullptr;
        std::string have;
        for (const Json& c : configurations) {
            const std::string name = c.get(L"name").str();
            have += (have.empty() ? "" : ", ") + name;
            if (!configuration.empty() && name == configuration) {
                chosen = &c;
            }
        }
        if (chosen == nullptr) {
            if (!configuration.empty() && configurations.size() > 1) {
                result.error = "no configuration " + configuration + " (have: " + have + ")";
                return result;
            }
            chosen = &configurations.front();
            for (const Json& c : configurations) {
                if (c.get(L"name").str() == "Debug") {
                    chosen = &c;
                }
            }
        }
        model.configuration = chosen->get(L"name").str();

        std::vector<std::string> directories;
        for (const Json& d : chosen->get(L"directories").items()) {
            directories.push_back(d.get(L"source").str());
        }
        const std::vector<Json> entries = chosen->get(L"targets").items();
        std::map<std::string, std::string> idToName;
        for (const Json& e : entries) {
            idToName[e.get(L"id").str()] = e.get(L"name").str();
        }
        // A target file the codemodel names but the reply lacks (a configure rewrote the folder under us) is
        // skipped, not fatal; if none can be read it is an error.
        std::string firstError;
        for (const Json& e : entries) {
            Target target;
            std::string error;
            if (!readTarget(replyDir, e, idToName, directories, target, error)) {
                if (firstError.empty()) firstError = error;
                ++result.skipped;
                continue;
            }
            model.targets.push_back(std::move(target));
        }
        if (model.targets.empty() && !firstError.empty()) {
            result.error = firstError;
        }
        return result;
    }
}
