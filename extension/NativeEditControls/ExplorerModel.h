#pragma once

#include <newui/delegate.h>
#include <newui/models.h>

#include <any>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace cmakemodel { struct Model; }
namespace cpptools { class ProjectIndex; }

namespace CodeToolsVsix
{
    // What the project explorer's tree shows, for any of its views: a tree of rows, each with what a
    // double-click should open. Plain data, built by the functions below from the file system, the
    // project index or the CMake model - none of which this knows about.
    struct ExplorerNode
    {
        enum class Kind
        {
            Folder, File, Namespace, Class, Struct, Enum, Function, Field, Variable,
            Group,       // a heading: "Applications", "Build Time", "Sources"
            Product,     // an output: an exe, a library
            Link,        // a library or target a product links
            Note,        // explanatory text, not something to open
        };

        // How a piece of secondary text is colored. Good / Warn / Bad are for measurements (a quick step, a
        // folder that is most of the project); the rest are the theme's own colors.
        enum class Tone { Normal, Muted, Accent, Good, Warn, Bad };

        Kind kind = Kind::Note;
        std::string text;        // the name
        std::string detail;      // muted text after it ("parser.h", "1.2 KB", "static library")
        Tone detailTone = Tone::Muted;
        // Instead of `detail`: values drawn in right-aligned columns at the row's right edge (the last cell
        // is the rightmost), so numbers line up down the tree. cellTones[i] colors cells[i].
        std::vector<std::string> cells;
        std::vector<Tone> cellTones;
        std::string path;        // what a double-click opens; empty for nothing
        std::size_t line = 0;    // 1-based; 0 = open the file without moving the caret
        std::string search;      // lower case text a filter is matched against (a qualified name, a path)
        std::vector<ExplorerNode> children;

        // For a node whose children are found when it is first opened (a folder on disk): fills
        // `children` and is then dropped. Null for a node whose children are already there.
        std::function<void(ExplorerNode&)> loader;
        bool loaded = true;

        // The row's text: "class Session   parser.h".
        std::string display() const;
    };

    // A newui tree over an ExplorerNode tree. The node passed to setRoot() is invisible: its children are
    // the top-level rows.
    class ExplorerTreeModel : public newui::TreeModel
    {
    public:
        void setRoot(ExplorerNode root);

        std::size_t childCount(const std::vector<std::size_t>& path) const override;
        std::any value(const std::any& key) override;

        // The node a tree path names, or null if it no longer exists. Loads a lazy folder on the way.
        const ExplorerNode* nodeAt(const std::vector<std::size_t>& path) const;

    private:
        void ensureLoaded(const ExplorerNode& node) const;

        ExplorerNode root_;
    };

    // An icon for a row. A themed one is in the colored set (Images/icons/cpp-symbol-icons-32), which has a
    // light and a dark folder: `name` is "symbols/class". Otherwise `name` is a full resource path to an
    // icon drawn in currentColor, to be tinted by whoever paints it.
    struct ExplorerIcon
    {
        std::string name;
        bool themed = false;
        bool empty() const { return name.empty(); }
    };

    // The icon for a row, from its kind, a file's extension, and what a product is.
    ExplorerIcon explorerIconFor(const ExplorerNode& node);

    // The resource path of a themed icon in the light or dark folder; an unthemed icon's own path.
    std::string explorerIconPath(const ExplorerIcon& icon, bool dark);

    // --- builders ---------------------------------------------------------------------------------

    // Extra text for a file row ("cpptools +2", "not built"), or empty. Called when a folder is opened.
    using FileDetailProvider = std::function<std::string(const std::string& path, bool isDirectory)>;

    // The folder `root` on disk, one level at a time as folders are opened. Skips what a user does not
    // browse: dot-files and folders, build output, fetched dependencies. Folders first, then files, each
    // in name order (any case).
    ExplorerNode buildFilesTree(const std::string& root, const FileDetailProvider& detail = {});

    // Every file under `root` whose name contains `filter` (any case), as a flat list, relative path as
    // the detail. At most `limit` of them (the list says so if it was cut).
    ExplorerNode buildFileSearch(const std::string& root, const std::string& filter, std::size_t limit = 500);

    // Namespaces, types and their members from everything the index knows, each once however many files
    // declare it. `filter` (any case) keeps a row whose qualified name contains it, with the rows
    // around it that lead there.
    ExplorerNode buildSymbolsTree(const cpptools::ProjectIndex& index, const std::string& filter = std::string());

    // Applications, Libraries, Tests and Tools; under each product its Build Time (sources, links) and
    // Runtime (what is built) sections. Rows open the file, or for a target or a link the CMake line that
    // made it.
    ExplorerNode buildProductsTree(const cmakemodel::Model& model, const std::string& filter = std::string());

    // ---- project statistics: the info view ----------------------------------------------------------

    struct FolderStat
    {
        std::string name;
        std::size_t files = 0;
        std::size_t folders = 0;       // below it, all levels
        std::uint64_t bytes = 0;
        bool skipped = false;          // a folder no view shows (build output...): named, not counted
    };

    struct FolderScan
    {
        std::vector<FolderStat> folders;   // the top-level ones, in name order
        std::size_t rootFiles = 0;
        std::uint64_t rootBytes = 0;
    };

    // Called as each top-level folder finishes: how many are done of how many, and the one just done.
    using ScanProgress = std::function<void(std::size_t done, std::size_t total, const std::string& folder)>;

    // Counts files and bytes under each top-level folder of root, a folder per task on `threads` workers
    // (0: all but one core). Stops early when *cancel becomes true. The same folders and files the Files
    // view hides are left out. Call from a background thread.
    FolderScan scanProjectFolders(const std::string& root, const ScanProgress& progress = {},
                                  const std::atomic<bool>* cancel = nullptr, unsigned threads = 0);

    // Everything the info view reports. Times are milliseconds; 0 for a step that did not run.
    struct ProjectStats
    {
        std::string root;
        FolderScan scan;
        double scanMs = 0;
        double cmakeMs = 0;
        double cmakeRefreshMs = 0;
        double cacheLoadMs = 0;
        double indexMs = 0;
        double totalMs = 0;

        std::string buildDir;
        std::string configuration;
        std::size_t targets = 0;
        std::size_t targetsMissing = 0;

        std::size_t filesToIndex = 0;
        std::size_t parsed = 0;        // read by libclang this time
        std::size_t upToDate = 0;      // found current in the cache
        std::size_t failed = 0;
        std::size_t symbols = 0;
        unsigned indexThreads = 0;
        bool complete = false;         // every step has run
    };

    // "1.2 KB" / "38.0 MB".
    std::string formatByteSize(std::uint64_t bytes);

    // The info view as a tree: Timing, Top-level folders (largest first), Index and CMake groups of notes.
    ExplorerNode buildStatsTree(const ProjectStats& stats);

    // A single explanatory row, for a view that has nothing to show (yet).
    ExplorerNode buildMessageTree(const std::string& message);

    // Folders no explorer view shows: build output, fetched dependencies, tool caches (by folder name).
    bool isSkippedExplorerFolder(const std::string& name);
}
