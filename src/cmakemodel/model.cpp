#include <cmakemodel/model.h>

#include <algorithm>

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
