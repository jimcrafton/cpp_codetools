#pragma once

#include <newui/controllers.h>
#include <newui/controls.h>
#include <newui/delegate.h>
#include <newui/filewatcher.h>
#include <newui/runloop.h>
#include <newui/segmentedcontrol.h>

#include <atomic>
#include <optional>
#include <mutex>
#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "ExplorerAnalysis.h"
#include "ExplorerModel.h"

namespace cmakemodel { struct Model; }
namespace cpptools { class ProjectIndex; class IncludeGraph; }

namespace CodeToolsVsix
{
    enum class ExplorerMode
    {
        Files = 0,
        Symbols = 1,
        Products = 2,
        Analysis = 3,
    };

    // The C++ project explorer: a root bar, a Files / Symbols / Products / Analysis switch, a filter box, a
    // tree and a status line. Its chrome is explorer.newui; the switch and the tree are added in code. Given a
    // folder (setRoot) it reads the CMake build tree and indexes the project's files on a background thread,
    // refreshing the views as results arrive. It belongs on one thread (the edit thread) and talks to the
    // host only through the open handler - it knows nothing of VS.
    class ProjectExplorer
    {
    public:
        // Called when a row is activated: open `path`, at `line` (1-based) or just the file when 0. Return
        // whether anything opened.
        using OpenHandler = std::function<bool(const std::string& path, std::size_t line)>;

        // Loads explorer.newui into `host`, which owns it and lays it out filling itself. loaded() says whether
        // that worked; nothing else is usable if it did not.
        explicit ProjectExplorer(newui::View& host);
        ~ProjectExplorer();
        ProjectExplorer(const ProjectExplorer&) = delete;
        ProjectExplorer& operator=(const ProjectExplorer&) = delete;

        bool loaded() const { return view_ != nullptr; }

        void setOpenHandler(OpenHandler handler) { open_ = std::move(handler); }

        // The folder to explore ("" - nothing open). Starts reading the build tree and indexing, in the
        // background unless setBackground(false).
        void setRoot(const std::string& folder);
        const std::string& root() const { return root_; }

        void setMode(ExplorerMode mode);
        ExplorerMode mode() const { return mode_; }

        // Analysis's sub-tab, and the Includes tab's two ways of looking: by cost, or file by file.
        void setAnalysisTab(AnalysisTab tab);
        AnalysisTab analysisTab() const { return analysisTab_; }
        void setIncludeView(IncludeView view);
        IncludeView includeView() const { return includeView_; }

        // The file the user is working in; Macros shows its macros. "" - none open.
        void setActiveFile(const std::string& path);
        const std::string& activeFile() const { return activeFile_; }
        // What Macros covers: the active file, or every file in its folder (the project folder with none open).
        void setMacroScope(MacroScope scope);
        MacroScope macroScope() const { return macroScope_; }

        void setFilter(const std::wstring& text);
        std::wstring filter() const;

        // The info view (what the "i" button toggles): timings, folder sizes and counts in place of the tree.
        void setInfoShown(bool shown);
        bool infoShown() const { return showInfo_; }
        const ProjectStats& stats() const { return stats_; }

        // What a batch of disk changes asks of the explorer: `full` re-reads everything (a file was added,
        // removed or renamed, CMake inputs changed, or events were lost); otherwise `modified` are the
        // source and header files whose contents changed.
        struct DiskChanges
        {
            bool full = false;
            std::set<std::string> modified;
            bool any() const { return full || !modified.empty(); }
        };
        // Where the index caches are kept: the folder given here, else %LOCALAPPDATA%/codetools++/index. Tests point it
        // somewhere they delete after.
        static void setCacheFolder(const std::string& folder);
        static DiskChanges summarizeChanges(const newui::FileWatcher::Changes& changes);
        // Whether a change at `path` under `root` is of no interest: inside a dot, build or dependency
        // folder, or a build-output or temp file.
        static bool watchIgnores(const std::string& root, const std::string& path);
        // The newest CMake File API index in `replyDir` (its "reply" folder's parent: <build>/.cmake/api/v1), by
        // file name; "" if there is none. A new one means CMake configured again.
        static std::string latestReplyIndex(const std::string& replyDir);

        // What a double-click on the row at `path` does. False if the row opens nothing.
        bool activate(const std::vector<std::size_t>& path);

        // Index and CMake work on the calling thread instead of a background one - for tests, which have no
        // run loop to deliver results.
        void setBackground(bool background) { background_ = background; }
        // Waits for background work to finish and applies what it found.
        void waitForIndexing();

        // The line under the tree ("253 files, 5091 symbols").
        std::string statusText() const;
        std::string rootName() const;
        std::string rootPath() const;

        ExplorerTreeModel* model() const { return model_; }
        newui::TreeView* treeView() const { return tree_; }
        newui::SegmentedControl* modeControl() const { return modeControl_; }
        newui::SegmentedControl* analysisControl() const { return analysisControl_; }
        newui::SegmentedControl* includeViewControl() const { return includeViewControl_; }
        newui::SegmentedControl* macroScopeControl() const { return macroScopeControl_; }
        newui::SegmentedControl* cardStepsControl() const { return cardSteps_; }
        newui::View* cardHost() const { return cardHost_; }
        newui::TextControl* cardText() const { return cardText_; }
        newui::TextField* filterField() const { return filter_; }
        const cpptools::ProjectIndex& index() const { return *index_; }
        const cmakemodel::Model* cmake() const { return cmake_.get(); }

    private:
        struct Alive;
        struct Found;
        struct FileProducts;
        struct FileProblems;
        struct TreeState;

        newui::SyncReturn handleModeChanged(newui::SegmentedControl& sender);
        newui::SyncReturn handleAnalysisTabChanged(newui::SegmentedControl& sender);
        newui::SyncReturn handleIncludeViewChanged(newui::SegmentedControl& sender);
        newui::SyncReturn handleMacroScopeChanged(newui::SegmentedControl& sender);
        newui::SyncReturn handleSelectionChanged(newui::TreeView& sender);
        newui::SyncReturn handleCardStepChanged(newui::SegmentedControl& sender);
        void showCard(const std::shared_ptr<const ExplorerCard>& card);
        void showCardStep(std::size_t index);
        void showCardSpans(const ExplorerCard& card);
        void openIncludeGraph();
        void expandAll(const std::vector<std::size_t>& path, const ExplorerNode& node);
        // Starts analyzing the files for `key` on a worker (or here, with no run loop); rebuild() runs when it is done.
        // True when the result is already there (no worker: it ran here).
        bool requestMacros(const std::string& key, const std::vector<std::string>& files);
        ExplorerNode buildMacroView(const std::string& filter);
        void stopMacroWork();
        void showAnalysisControls();   // the sub-tab rows only while Analysis is the mode
        newui::SyncReturn handleInfoToggled(newui::Button& sender);
        newui::SyncReturn handleDoubleClick(newui::View& sender, const newui::Point& pt, std::uint32_t buttons, std::uint32_t keys);

        ExplorerNode buildAnalysisTree(const std::string& filter);
        void rebuild();
        void scheduleRebuild();
        void setStatus(const std::string& text);
        // The bar under the mode switch: a fraction, or Found::kBusy for a sweep with no known end.
        void setProgress(float progress);
        void stopProgressSweep();
        void startWork();
        void stopWork();
        void applyFound(const std::shared_ptr<Found>& found);
        void collectProblems();   // which files, and the folders above them, have errors or warnings

        // Watching the root for changes on disk: a batch lands in handleDiskChanges() on the loop thread.
        void startWatching();
        newui::SyncReturn handleDiskChanges(newui::FileWatcher& sender, const newui::FileWatcher::Changes& changes);
        // Brings the views up to date: `full` re-reads everything (startWork), otherwise only the
        // `modified` files are re-parsed. Called when the batch is in and no load is running.
        void refresh(bool full, std::set<std::string> modified);
        void refreshModified(std::set<std::string> modified);
        void watchReply(const std::string& replyDir);   // so a configure done elsewhere is noticed

        // The rows open and the one selected, by name, so a rebuild of the same view can put them back.
        TreeState saveTreeState();
        void restoreTreeState(const TreeState& state, bool sameView);
        // The tree for the current mode, filter and data. Empty while it is being built on a worker (Symbols) and the
        // tree shown is already this view's: it stays until the new one is in, rather than flash a message.
        std::optional<ExplorerNode> buildTree(bool sameView);
        // The Symbols tree for `filter`: from the cache, else built on a worker (rebuild() runs when it is in; nullptr
        // meanwhile), else here when there is no run loop.
        std::shared_ptr<const ExplorerNode> symbolsTree(const std::string& filter);
        void stopSymbolsWork();
        void dropTrees();   // the index changed: forget the trees built from it
        // The tree for view `key`: the one built before from this index, or `build()`'s (kept, unless a load is running).
        ExplorerNode cachedTree(const std::string& key, const std::function<ExplorerNode()>& build);

        newui::View* view_ = nullptr;                    // owned by the host
        newui::Label* rootName_ = nullptr;
        newui::Label* rootPath_ = nullptr;
        newui::Label* status_ = nullptr;
        newui::TextField* filter_ = nullptr;
        newui::SegmentedControl* modeControl_ = nullptr;
        newui::SegmentedControl* analysisControl_ = nullptr;
        newui::SegmentedControl* includeViewControl_ = nullptr;
        newui::SegmentedControl* macroScopeControl_ = nullptr;
        newui::View* macroScopeHost_ = nullptr;
        newui::View* cardHost_ = nullptr;
        newui::Label* cardTitle_ = nullptr;
        newui::SegmentedControl* cardSteps_ = nullptr;
        newui::Button* cardGraph_ = nullptr;
        newui::Connection cardGraphConnection_;
        newui::TextControl* cardText_ = nullptr;
        newui::Label* cardWarning_ = nullptr;
        std::shared_ptr<const ExplorerCard> card_;
        newui::View* analysisHost_ = nullptr;
        newui::View* includeViewHost_ = nullptr;
        newui::TreeView* tree_ = nullptr;
        newui::Button* infoButton_ = nullptr;
        bool showInfo_ = false;
        ProjectStats stats_;
        newui::Progress* progress_ = nullptr;
        newui::RunLoop::TimerHandle progressTimer_ = newui::RunLoop::kInvalidTimerHandle;
        float progressPhase_ = 0.0f;
        ExplorerTreeModel* model_ = nullptr;             // owned by tree_'s controller

        std::string root_;
        ExplorerMode mode_ = ExplorerMode::Symbols;
        AnalysisTab analysisTab_ = AnalysisTab::Includes;
        IncludeView includeView_ = IncludeView::Impact;
        std::string activeFile_;
        MacroScope macroScope_ = MacroScope::ActiveFile;
        std::shared_ptr<std::vector<MacroFile>> macroResult_;   // the analysis of what macroKey_ names
        std::string macroKey_;
        std::string macroRunningKey_;
        std::shared_ptr<std::atomic<bool>> macroCancel_;
        std::thread macroThread_;
        std::shared_ptr<const cpptools::IncludeGraph> includeGraph_;   // built when Analysis first needs it; dropped when the index changes
        OpenHandler open_;

        std::shared_ptr<cpptools::ProjectIndex> index_;
        std::shared_ptr<cmakemodel::Model> cmake_;
        std::shared_ptr<FileProblems> problems_;
        std::shared_ptr<FileProducts> products_;         // which products each file is in; read by lazily loaded folders
        std::string buildDir_;
        std::string note_;                               // why Products is empty, when it is
        std::string status_text_;

        bool background_ = true;
        std::unique_ptr<newui::FileWatcher> watcher_;
        newui::Connection watcherConnection_;
        std::string pendingReplyDir_;          // the build's reply folder, found by the load and watched once it is done
        std::string replyDir_;                 // <build>/.cmake/api/v1, watched for a new index
        newui::FileWatcher::WatchId replyWatch_ = newui::FileWatcher::kInvalidWatch;
        std::string loadedReplyIndex_;         // the index the model in cmake_ was read from
        std::string treeViewKey_;              // which view the tree last showed (mode, filter, info)
        // Trees built from a finished index, by view: showing one again costs a copy, not a rebuild. Dropped whenever the
        // index changes.
        std::map<std::string, std::shared_ptr<const ExplorerNode>> treeCache_;
        static constexpr long long kSaveEveryMs = 15000;   // how often a long index is written out while it runs
        unsigned treeEpoch_ = 0;              // bumped by dropTrees(), so a tree built from an older index is not kept
        std::string symbolsRunningKey_;        // the Symbols tree being built on symbolsThread_, if any
        std::shared_ptr<std::atomic<bool>> symbolsCancel_;
        std::thread symbolsThread_;
        bool loading_ = false;                 // startWork()'s job has not delivered its final result
        bool refreshFull_ = false;             // changes that came in during a load, applied when it ends
        std::set<std::string> refreshModified_;
        std::shared_ptr<Alive> alive_;
        std::thread worker_;
        newui::RunLoop::TimerHandle filterTimer_ = newui::RunLoop::kInvalidTimerHandle;
        newui::RunLoop* loop_ = nullptr;
        newui::Connection modeConnection_;
        newui::Connection analysisConnection_;
        newui::Connection includeViewConnection_;
        newui::Connection macroScopeConnection_;
        newui::Connection cardStepConnection_;
    };
}
