#pragma once

// What CMake knows about a project, as plain data: its targets, their sources and link edges, and the
// CMakeLists line that created each. Read from the CMake File API (fileapi.h); no CMake is run here.
// Std library only (plus lex's JSON parser in the reader).

#include <cstddef>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace cmakemodel
{
    // A line of a CMake file: the command call that made something.
    struct Location
    {
        std::string file;      // as CMake reports it: relative to the source dir, or absolute
        int line = 0;          // 1-based; 0 = unknown
        std::string command;   // "add_library", "target_link_libraries", ...

        bool valid() const { return line > 0 && !file.empty(); }
    };

    enum class TargetType
    {
        Executable,
        StaticLibrary,
        SharedLibrary,
        ModuleLibrary,
        ObjectLibrary,
        InterfaceLibrary,
        Utility,   // a custom target: no artifact of its own
        Unknown,
    };

    // What a target means to a user. Applications, Libraries, Tests and Tools are the products;
    // the rest only appear inside the products that use them.
    enum class ProductKind
    {
        Application,
        Library,
        Test,
        Tool,        // a custom target (a generator, a version stamp)
        Input,       // an OBJECT or INTERFACE library: shared inputs, not a deliverable
        Generated,   // added by the generator (ALL_BUILD, ZERO_CHECK, ...): never shown
    };

    struct SourceFile
    {
        std::string path;        // as CMake reports it
        bool generated = false;
        std::string group;       // CMake's source group ("Source Files", "Header Files", ...)
        std::string via;         // the OBJECT library this comes from; empty for the target's own
        Location defined;        // the call that listed it (the whole add_library, not the one line)
    };

    enum class LinkKind
    {
        Target,    // another target of this project
        Imported,  // a target CMake knows only by name (an imported library, libclang::libclang)
        File,      // a library file or flag
    };

    struct LinkItem
    {
        LinkKind kind = LinkKind::File;
        std::string name;        // Target / Imported: the target's name; File: the fragment
        Location defined;        // the target_link_libraries call
    };

    struct Target
    {
        std::string id;
        std::string name;
        TargetType type = TargetType::Unknown;
        ProductKind kind = ProductKind::Generated;
        std::string nameOnDisk;                // "cppoutline.exe"; empty for no artifact
        std::vector<std::string> artifacts;    // relative to the build dir
        Location defined;                      // its add_library / add_executable
        std::string directory;                 // source dir it was declared in, relative to the source root
        std::vector<SourceFile> sources;
        std::vector<LinkItem> links;
        std::vector<std::string> dependencies; // target names (build order), including implied ones
        std::vector<std::string> definitions;
        std::vector<std::string> includeDirs;
    };

    struct Model
    {
        std::string sourceDir;
        std::string buildDir;
        std::string configuration;   // "Debug"; empty for a single-config generator without one
        std::string generator;       // "Visual Studio 18 2026", "Ninja"
        std::string cmakeVersion;
        bool multiConfig = false;
        std::vector<Target> targets;

        const Target* find(const std::string& name) const;

        // The targets of a kind, in name order.
        std::vector<const Target*> byKind(ProductKind kind) const;

        // The targets that link `name` directly.
        std::vector<const Target*> usedBy(const std::string& name) const;

        // The target's own sources plus those of the OBJECT libraries it links (SourceFile::via names
        // the library), headers included, in the order CMake lists them.
        std::vector<SourceFile> allSources(const Target& target) const;
    };

    // The preprocessor settings a source is compiled with: those of the target that owns it.
    struct CompileSettings
    {
        std::string target;
        std::vector<std::string> includeDirs;
        std::vector<std::string> definitions;

        // "-I<dir>" and "-D<definition>" for each, ready to add to a parse command line.
        std::vector<std::string> toArgs() const;
    };

    // Finds the CompileSettings of a file. A source is found by the target that lists it. A header no target lists
    // (most are not) falls back to the target with one of its parent folders on its include path, the most specific
    // folder winning: that is how the header is meant to be reached. Keeps no reference to the model.
    class CompileSettingsIndex
    {
    public:
        explicit CompileSettingsIndex(const Model& model);

        // Null when no target claims the file.
        const CompileSettings* find(const std::string& path) const;

        // Makes `header` build with the settings of `from` (a file the index finds settings for), whatever find() said
        // of it before. For a header that no target lists or reaches, borrowing from a file that includes it. A no-op
        // if `from` has none.
        void borrow(const std::string& header, const std::string& from);

    private:
        std::vector<CompileSettings> settings_;
        std::map<std::string, std::size_t> bySource_;                        // canonical path -> settings_ index
        std::vector<std::pair<std::string, std::size_t>> includeRoots_;      // canonical include folder -> settings_ index
    };

    const char* toString(TargetType type);
    const char* toString(ProductKind kind);

    // Classifies one target. `linksTestFramework`: it links gtest, gtest_main or gmock.
    ProductKind classify(const Target& target, bool linksTestFramework);
}
