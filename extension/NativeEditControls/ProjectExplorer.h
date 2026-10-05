#pragma once

#include <newui/controllers.h>
#include <newui/controls.h>
#include <newui/delegate.h>
#include <newui/runloop.h>
#include <newui/segmentedcontrol.h>

#include <atomic>
#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "ExplorerModel.h"

namespace cmakemodel { struct Model; }
namespace cpptools { class ProjectIndex; }

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

        void setFilter(const std::wstring& text);
        std::wstring filter() const;

        // The info view (what the "i" button toggles): timings, folder sizes and counts in place of the tree.
        void setInfoShown(bool shown);
        bool infoShown() const { return showInfo_; }
        const ProjectStats& stats() const { return stats_; }

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
        newui::TextField* filterField() const { return filter_; }
        const cpptools::ProjectIndex& index() const { return *index_; }
        const cmakemodel::Model* cmake() const { return cmake_.get(); }

    private:
        struct Alive;
        struct Found;
        struct FileProducts;

        newui::SyncReturn handleModeChanged(newui::SegmentedControl& sender);
        newui::SyncReturn handleInfoToggled(newui::Button& sender);
        newui::SyncReturn handleDoubleClick(newui::View& sender, const newui::Point& pt, std::uint32_t buttons, std::uint32_t keys);

        void rebuild();
        void scheduleRebuild();
        void setStatus(const std::string& text);
        // The bar under the mode switch: a fraction, or Found::kBusy for a sweep with no known end.
        void setProgress(float progress);
        void stopProgressSweep();
        void startWork();
        void stopWork();
        void applyFound(const std::shared_ptr<Found>& found);

        newui::View* view_ = nullptr;                    // owned by the host
        newui::Label* rootName_ = nullptr;
        newui::Label* rootPath_ = nullptr;
        newui::Label* status_ = nullptr;
        newui::TextField* filter_ = nullptr;
        newui::SegmentedControl* modeControl_ = nullptr;
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
        OpenHandler open_;

        std::shared_ptr<cpptools::ProjectIndex> index_;
        std::shared_ptr<cmakemodel::Model> cmake_;
        std::shared_ptr<FileProducts> products_;         // which products each file is in; read by lazily loaded folders
        std::string buildDir_;
        std::string note_;                               // why Products is empty, when it is
        std::string status_text_;

        bool background_ = true;
        std::shared_ptr<Alive> alive_;
        std::thread worker_;
        newui::RunLoop::TimerHandle filterTimer_ = newui::RunLoop::kInvalidTimerHandle;
        newui::RunLoop* loop_ = nullptr;
        newui::Connection modeConnection_;
    };
}
