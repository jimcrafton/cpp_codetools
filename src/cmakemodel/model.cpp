#include <cmakemodel/model.h>

#include <algorithm>
#include <cctype>

namespace cmakemodel
{
    const Target* Model::find(const std::string& name) const
    {
        for (const Target& target : targets) {
            if (target.name == name) {
                return &target;
            }
        }
        return nullptr;
    }

    std::vector<const Target*> Model::byKind(ProductKind kind) const
    {
        std::vector<const Target*> found;
        for (const Target& target : targets) {
            if (target.kind == kind) {
                found.push_back(&target);
            }
        }
        std::sort(found.begin(), found.end(), [](const Target* a, const Target* b) { return a->name < b->name; });
        return found;
    }

    std::vector<const Target*> Model::usedBy(const std::string& name) const
    {
        std::vector<const Target*> found;
        for (const Target& target : targets) {
            for (const LinkItem& link : target.links) {
                if (link.kind != LinkKind::File && link.name == name) {
                    found.push_back(&target);
                    break;
                }
            }
        }
        return found;
    }

    std::vector<SourceFile> Model::allSources(const Target& target) const
    {
        std::vector<SourceFile> all = target.sources;
        for (const LinkItem& link : target.links) {
            if (link.kind != LinkKind::Target) {
                continue;
            }
            const Target* linked = find(link.name);
            if (linked == nullptr || linked->type != TargetType::ObjectLibrary) {
                continue;
            }
            for (SourceFile source : linked->sources) {
                source.via = linked->name;
                all.push_back(std::move(source));
            }
        }
        return all;
    }

    namespace
    {
        // Lower case, forward slashes: one spelling for a path however CMake or a caller wrote it.
        std::string canonical(std::string path)
        {
            std::replace(path.begin(), path.end(), '\\', '/');
            std::transform(path.begin(), path.end(), path.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return path;
        }

        bool isAbsolutePath(const std::string& path)
        {
            return (path.size() > 1 && path[1] == ':') || (!path.empty() && (path[0] == '/' || path[0] == '\\'));
        }
    }

    std::vector<std::string> CompileSettings::toArgs() const
    {
        std::vector<std::string> args;
        for (const std::string& dir : includeDirs) args.push_back("-I" + dir);
        for (const std::string& definition : definitions) args.push_back("-D" + definition);
        return args;
    }

    CompileSettingsIndex::CompileSettingsIndex(const Model& model)
    {
        for (const Target& target : model.targets) {
            if (target.type == TargetType::Utility || target.type == TargetType::Unknown) continue;   // ALL_BUILD, ZERO_CHECK...
            const std::size_t at = settings_.size();
            settings_.push_back({ target.name, target.includeDirs, target.definitions });
            for (const SourceFile& source : target.sources) {
                const std::string full = isAbsolutePath(source.path) ? source.path : model.sourceDir + "/" + source.path;
                bySource_.emplace(canonical(full), at);   // the first target to list a file keeps it
            }
            for (std::string dir : target.includeDirs) {
                std::replace(dir.begin(), dir.end(), '\\', '/');
                while (!dir.empty() && dir.back() == '/') dir.pop_back();
                if (!dir.empty()) includeRoots_.emplace_back(canonical(dir), at);
            }
        }
    }

    const CompileSettings* CompileSettingsIndex::find(const std::string& path) const
    {
        const std::string key = canonical(path);
        auto source = bySource_.find(key);
        if (source != bySource_.end()) return &settings_[source->second];

        // not listed by any target: the target whose include path reaches it, by the most specific folder
        const std::pair<std::string, std::size_t>* best = nullptr;
        for (const auto& root : includeRoots_) {
            if (key.size() > root.first.size() + 1 && key.compare(0, root.first.size(), root.first) == 0 && key[root.first.size()] == '/' &&
                (best == nullptr || root.first.size() > best->first.size())) {
                best = &root;
            }
        }
        return best != nullptr ? &settings_[best->second] : nullptr;
    }

    const char* toString(TargetType type)
    {
        switch (type) {
        case TargetType::Executable: return "executable";
        case TargetType::StaticLibrary: return "static library";
        case TargetType::SharedLibrary: return "shared library";
        case TargetType::ModuleLibrary: return "module library";
        case TargetType::ObjectLibrary: return "object library";
        case TargetType::InterfaceLibrary: return "interface library";
        case TargetType::Utility: return "utility";
        case TargetType::Unknown: break;
        }
        return "unknown";
    }

    const char* toString(ProductKind kind)
    {
        switch (kind) {
        case ProductKind::Application: return "Application";
        case ProductKind::Library: return "Library";
        case ProductKind::Test: return "Test";
        case ProductKind::Tool: return "Tool";
        case ProductKind::Input: return "Input";
        case ProductKind::Generated: return "Generated";
        }
        return "";
    }

    ProductKind classify(const Target& target, bool linksTestFramework)
    {
        switch (target.type) {
        case TargetType::Executable: {
            const std::string& name = target.name;
            auto endsWith = [&name](const char* suffix) {
                const std::string s = suffix;
                return name.size() >= s.size() && name.compare(name.size() - s.size(), s.size(), s) == 0;
            };
            return (linksTestFramework || endsWith("_tests") || endsWith("_test")) ? ProductKind::Test : ProductKind::Application;
        }
        case TargetType::StaticLibrary:
        case TargetType::SharedLibrary:
        case TargetType::ModuleLibrary:
            return ProductKind::Library;
        case TargetType::ObjectLibrary:
        case TargetType::InterfaceLibrary:
            return ProductKind::Input;
        case TargetType::Utility:
            // What a generator adds to every project, versus a target someone wrote.
            if (target.name == "ALL_BUILD" || target.name == "ZERO_CHECK" || target.name == "INSTALL"
                || target.name == "PACKAGE" || target.name == "RUN_TESTS" || target.name == "all"
                || target.name == "install" || target.name == "package" || target.name == "test") {
                return ProductKind::Generated;
            }
            return ProductKind::Tool;
        case TargetType::Unknown:
            break;
        }
        return ProductKind::Generated;
    }
}
