#include "ProjectExplorer.h"
#include "HeaderFlags.h"

#include <Windows.h>

#include "ExplorerItem.h"
#include "IncludeGraphPopup.h"
#include "Logging.h"
#include "Settings.h"
#include "TextEncoding.h"
#include "WorkspaceInfo.h"

#include <cmakemodel/fileapi.h>
#include <cmakemodel/model.h>
#include <cpptools/compileflags.h>
#include <cpptools/includeanalysis.h>
#include <cpptools/projectindex.h>

#include <newui/bundle.h>
#include <newui/layout.h>
#include <newui/uicolormanager.h>
#include <newui/utils.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>

namespace fs = std::filesystem;

namespace CodeToolsVsix
{
    namespace
    {
        // The UI thread should spend only moments in any one step of the explorer; one that takes long is logged.
        class StallLog
        {
        public:
            explicit StallLog(std::string what) : what_(std::move(what)), start_(std::chrono::steady_clock::now()) {}
            ~StallLog()
            {
                const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_).count();
                if (ms >= kThresholdMs) log(cpptools::Severity::Warning, "ProjectExplorer: " + what_ + " took " + std::to_string(ms) + " ms on the UI thread");
            }

        private:
            static constexpr long long kThresholdMs = 20;
            std::string what_;
            std::chrono::steady_clock::time_point start_;
        };
    }

    struct ProjectExplorer::Alive
    {
        std::atomic<bool> alive{ true };
        std::atomic<bool> cancel{ false };
    };

    struct ProjectExplorer::FileProducts
    {
        bool known = false;
        std::map<std::string, std::vector<std::string>> byPath;   // lower-case normalized path -> product names
    };

    // The worst problem of each file with one, and of each folder above such a file (lower-case normalized path).
    struct ProjectExplorer::FileProblems
    {
        std::map<std::string, ExplorerNode::Badge> worst;
    };

    // What the background work hands the UI thread: the CMake model once it is read, then progress, then
    // the final counts.
    struct ProjectExplorer::Found
    {
        std::shared_ptr<cmakemodel::Model> model;
        std::string buildDir;
        std::string replyDir;  // <build>/.cmake/api/v1 of the build tree found, with or without a model
        std::string note;      // why there is no model, when there is none
        std::string status;
        bool final = false;
        // The progress bar: kKeep leaves it, kBusy sweeps (no known end), 0..1 is a fraction.
        std::shared_ptr<ProjectStats> stats;   // a snapshot, when this carries new numbers
        std::shared_ptr<const cmakemodel::CompileSettingsIndex> settings;   // how each file is compiled, once known
        static constexpr float kKeep = -3.0f;
        static constexpr float kBusy = -2.0f;
        float progress = kKeep;
    };

    namespace
    {
        // Runs `cmake -S root -B buildDir` hidden, waiting for it (and abandoning it when cancel is set). It
        // rewrites the File API reply. True if it exited 0.
        bool runCMakeConfigure(const std::string& cmakeExe, const std::string& root, const std::string& buildDir,
                               const std::atomic<bool>& cancel)
        {
            std::wstring command = L"\"" + utf8ToWide(cmakeExe) + L"\" -S \"" + utf8ToWide(root) + L"\" -B \"" + utf8ToWide(buildDir) + L"\"";
            STARTUPINFOW startup{};
            startup.cb = sizeof startup;
            PROCESS_INFORMATION process{};
            if (!::CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
                return false;
            }
            DWORD code = 1;
            while (::WaitForSingleObject(process.hProcess, 200) == WAIT_TIMEOUT) {
                if (cancel.load()) {
                    ::TerminateProcess(process.hProcess, 1);
                    break;
                }
            }
            ::GetExitCodeProcess(process.hProcess, &code);
            ::CloseHandle(process.hThread);
            ::CloseHandle(process.hProcess);
            return code == 0;
        }

        std::string lowered(std::string text)
        {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return text;
        }

        bool isCppSourceExtension(const std::string& ext)
        {
            return ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".c";
        }

        bool isIndexedExtension(const std::string& ext)
        {
            return isCppSourceExtension(ext) || ext == ".h" || ext == ".hpp" || ext == ".hxx" || ext == ".inl";
        }

        std::string extensionOf(const std::string& path) { return lowered(fs::path(path).extension().string()); }

        bool isAbsolute(const std::string& path)
        {
            return (path.size() > 1 && path[1] == ':') || (!path.empty() && (path[0] == '/' || path[0] == '\\'));
        }

        // A target that is not the project's own: a fetched dependency, or something a package defined.
        bool isExternalTarget(const cmakemodel::Target& target, const cmakemodel::Model& model)
        {
            if (target.directory.compare(0, 9, "3rdparty/") == 0) return true;
            const std::string& file = target.defined.file;
            return isAbsolute(file) && lowered(file).compare(0, model.sourceDir.size(), lowered(model.sourceDir)) != 0;
        }

        struct BuildTree
        {
            std::string dir;
            bool hasReply = false;
        };

        // The folder CMake configured into: one with a File API reply, else one that has been configured
        // (so a query can be left for its next configure).
        BuildTree findBuildTree(const std::string& root)
        {
            std::vector<fs::path> candidates;
            const fs::path base(root);
            candidates.push_back(base / "build-ninja");
            candidates.push_back(base / "build");
            std::error_code ec;
            for (const char* parent : { "out/build", "cmake-build-debug", "cmake-build-release" }) {
                const fs::path folder = base / parent;
                if (!fs::is_directory(folder, ec)) continue;
                if (std::string(parent) == "out/build") {
                    for (const auto& entry : fs::directory_iterator(folder, ec)) {
                        if (entry.is_directory(ec)) candidates.push_back(entry.path());
                    }
                } else {
                    candidates.push_back(folder);
                }
            }
            BuildTree configured;
            for (const fs::path& candidate : candidates) {
                if (fs::is_directory(candidate / ".cmake" / "api" / "v1" / "reply", ec)) {
                    return { candidate.generic_string(), true };
                }
                if (configured.dir.empty() && fs::is_regular_file(candidate / "CMakeCache.txt", ec)) {
                    configured = { candidate.generic_string(), false };
                }
            }
            return configured;
        }

        std::string cachePathFor(const std::string& root)
        {
            const char* local = std::getenv("LOCALAPPDATA");
            if (local == nullptr || *local == '\0') return std::string();
            const std::size_t hash = std::hash<std::string>()(lowered(root));
            char name[32];
            std::snprintf(name, sizeof name, "%016llx.bin", static_cast<unsigned long long>(hash));
            const fs::path folder = fs::path(local) / "codetools++" / "index";
            std::error_code ec;
            fs::create_directories(folder, ec);
            return ec ? std::string() : (folder / name).string();
        }

        // The files worth indexing: what the build compiles, or failing that whatever is under the root.
        std::vector<std::string> filesToIndex(const std::string& root, const cmakemodel::Model* model)
        {
            std::vector<std::string> files;
            std::error_code ec;
            if (model != nullptr) {
                for (const cmakemodel::Target& target : model->targets) {
                    if (target.kind == cmakemodel::ProductKind::Generated || isExternalTarget(target, *model)) continue;
                    for (const cmakemodel::SourceFile& source : model->allSources(target)) {
                        if (!isIndexedExtension(extensionOf(source.path))) continue;
                        const std::string path = isAbsolute(source.path) ? source.path : model->sourceDir + "/" + source.path;
                        if (fs::is_regular_file(path, ec)) files.push_back(path);
                    }
                }
                if (!files.empty()) {
                    // A target lists the headers it happens to name; the rest of the project's headers hold classes
                    // too (and the definitions of their methods live in the .cpp files above).
                    std::set<std::string> known;
                    for (const std::string& file : files) known.insert(lowered(cpptools::ProjectIndex::normalizePath(file)));
                    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
                        const std::string name = it->path().filename().string();
                        std::error_code typeEc;
                        if (it->is_directory(typeEc)) {
                            if (name.empty() || name.front() == '.' || isSkippedExplorerFolder(name)) it.disable_recursion_pending();
                        } else if (isIndexedExtension(extensionOf(name)) && !isCppSourceExtension(extensionOf(name)) && files.size() < 20000) {
                            const std::string path = it->path().generic_string();
                            if (known.insert(lowered(cpptools::ProjectIndex::normalizePath(path))).second) files.push_back(path);
                        }
                    }
                    return files;
                }
            }
            for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
                const std::string name = it->path().filename().string();
                std::error_code typeEc;
                if (it->is_directory(typeEc)) {
                    if (name.empty() || name.front() == '.' || isSkippedExplorerFolder(name)) it.disable_recursion_pending();
                } else if (isIndexedExtension(extensionOf(name)) && files.size() < 20000) {
                    files.push_back(it->path().generic_string());
                }
            }
            return files;
        }

        ExplorerMode modeFromName(const std::wstring& name)
        {
            const std::string n = lowered(wideToUtf8(name));
            if (n == "files") return ExplorerMode::Files;
            if (n == "products") return ExplorerMode::Products;
            if (n == "analysis") return ExplorerMode::Analysis;
            return ExplorerMode::Symbols;
        }
    }

    ProjectExplorer::ProjectExplorer(newui::View& host)
        : index_(std::make_shared<cpptools::ProjectIndex>()), products_(std::make_shared<FileProducts>()),
          alive_(std::make_shared<Alive>())
    {
        newui::RunLoop& loop = newui::RunLoop::current();
        loop_ = loop ? &loop : nullptr;

        newui::View* loadedView = newui::Bundle::instance().loadView("explorer");
        auto* sub = dynamic_cast<newui::SubView*>(loadedView);
        if (sub == nullptr) {
            if (loadedView != nullptr) {
                loadedView->destroy();
                delete loadedView;
            }
            logToDebugOut(L"ProjectExplorer: could not load explorer.newui");
            return;
        }
        sub->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
        host.addChild(sub);
        view_ = sub;

        rootName_ = dynamic_cast<newui::Label*>(sub->findView("rootName"));
        rootPath_ = dynamic_cast<newui::Label*>(sub->findView("rootPath"));
        status_ = dynamic_cast<newui::Label*>(sub->findView("statusLabel"));
        filter_ = dynamic_cast<newui::TextField*>(sub->findView("filterInput"));
        infoButton_ = dynamic_cast<newui::Button*>(sub->findView("infoButton"));
        auto* modeHost = dynamic_cast<newui::SubView*>(sub->findView("modeHost"));
        auto* analysisHost = dynamic_cast<newui::SubView*>(sub->findView("analysisHost"));
        auto* includeViewHost = dynamic_cast<newui::SubView*>(sub->findView("includeViewHost"));
        auto* macroScopeHost = dynamic_cast<newui::SubView*>(sub->findView("macroScopeHost"));
        auto* cardHost = dynamic_cast<newui::SubView*>(sub->findView("cardHost"));
        auto* treeHost = dynamic_cast<newui::SubView*>(sub->findView("treeHost"));
        if (rootName_ == nullptr || rootPath_ == nullptr || status_ == nullptr || filter_ == nullptr || infoButton_ == nullptr ||
            modeHost == nullptr || analysisHost == nullptr || includeViewHost == nullptr || macroScopeHost == nullptr || cardHost == nullptr || treeHost == nullptr) {
            logToDebugOut(L"ProjectExplorer: explorer.newui is missing a named view");
            view_ = nullptr;
            return;
        }

        // The mode switch: a custom control, so built here rather than loaded.
        modeControl_ = new newui::SegmentedControl();
        modeControl_->setName("explorerModes");
        modeControl_->setVisible(true);
        modeControl_->setSegments({ "Files", "Symbols", "Products", "Analysis" });
        modeControl_->setDesiredSize(modeControl_->naturalSize());   // it never sizes itself
        modeHost->addChild(modeControl_);

        // Analysis's sub-tabs, and Includes' two views; each row shows only when it applies.
        analysisControl_ = new newui::SegmentedControl();
        analysisControl_->setName("analysisTabs");
        analysisControl_->setVisible(true);
        analysisControl_->setSegments({ "Includes", "Macros", "Templates" });
        analysisControl_->setDesiredSize(analysisControl_->naturalSize());
        analysisHost->addChild(analysisControl_);
        includeViewControl_ = new newui::SegmentedControl();
        includeViewControl_->setName("includeViews");
        includeViewControl_->setVisible(true);
        includeViewControl_->setSegments({ "Impact", "Per file" });
        includeViewControl_->setDesiredSize(includeViewControl_->naturalSize());
        includeViewHost->addChild(includeViewControl_);
        macroScopeControl_ = new newui::SegmentedControl();
        macroScopeControl_->setName("macroScope");
        macroScopeControl_->setVisible(true);
        macroScopeControl_->setSegments({ "This file", "Folder" });
        macroScopeControl_->setDesiredSize(macroScopeControl_->naturalSize());
        macroScopeHost->addChild(macroScopeControl_);
        macroScopeHost_ = macroScopeHost;

        // The detail pane under the tree: what the selected row has to say (an expansion's steps).
        cardTitle_ = new newui::Label();
        cardTitle_->setName("cardTitle");
        cardTitle_->setVisible(true);
        cardTitle_->setDesiredSize(newui::Size(1.0f, 20.0f));
        cardHost->addChild(cardTitle_);
        cardGraph_ = new newui::Button();
        cardGraph_->setName("cardGraph");
        cardGraph_->setText("Open include graph");
        cardGraph_->setVisible(false);
        cardGraph_->setDesiredSize(newui::Size(1.0f, 28.0f));
        cardHost->addChild(cardGraph_);
        cardSteps_ = new newui::SegmentedControl();
        cardSteps_->setName("cardSteps");
        cardSteps_->setVisible(false);
        cardSteps_->setSegments({ "0" });
        cardSteps_->setDesiredSize(cardSteps_->naturalSize());
        cardHost->addChild(cardSteps_);
        cardText_ = new newui::TextControl();
        cardText_->setName("cardText");
        cardText_->setVisible(true);
        cardText_->inputTraits().setReadOnly(true);
        cardText_->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
        cardHost->addChild(cardText_);
        cardWarning_ = new newui::Label();
        cardWarning_->setName("cardWarning");
        cardWarning_->setVisible(false);
        cardWarning_->setDesiredSize(newui::Size(1.0f, 32.0f));
        cardHost->addChild(cardWarning_);
        cardHost_ = cardHost;

        analysisHost_ = analysisHost;
        includeViewHost_ = includeViewHost;

        // A thin progress bar above the tree: sweeps while CMake is read, fills while files are indexed.
        progress_ = new newui::Progress();
        progress_->setName("explorerProgress");
        progress_->setVisible(true);
        progress_->setDesiredSize(newui::Size(1.0f, 6.0f));
        treeHost->addChild(progress_);

        // The tree, in a scroll view - same shape as DirectoryTree.
        auto* scroll = new newui::ScrollView();
        scroll->setName("explorerScroll");
        scroll->setVisible(true);
        scroll->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
        treeHost->addChild(scroll);
        tree_ = new newui::TreeView();
        tree_->setName("explorerTree");
        tree_->setVisible(true);
        tree_->setController(std::make_unique<ExplorerController>());   // makes the rows and names their icons
        auto model = std::make_unique<ExplorerTreeModel>();
        model_ = model.get();
        tree_->setModel(std::move(model));
        scroll->addChild(tree_);

        modeConnection_ = modeControl_->onSelectionChanged.add(this, &ProjectExplorer::handleModeChanged);
        analysisConnection_ = analysisControl_->onSelectionChanged.add(this, &ProjectExplorer::handleAnalysisTabChanged);
        includeViewConnection_ = includeViewControl_->onSelectionChanged.add(this, &ProjectExplorer::handleIncludeViewChanged);
        macroScopeConnection_ = macroScopeControl_->onSelectionChanged.add(this, &ProjectExplorer::handleMacroScopeChanged);
        cardStepConnection_ = cardSteps_->onSelectionChanged.add(this, &ProjectExplorer::handleCardStepChanged);
        cardGraphConnection_ = cardGraph_->onClick.add([this](newui::Control&) {
            openIncludeGraph();
            return newui::SyncReturn::Handled;
        });
        tree_->onSelectionChanged.add(this, &ProjectExplorer::handleSelectionChanged);
        tree_->onMouseDblClick.add(this, &ProjectExplorer::handleDoubleClick);
        infoButton_->onCheckedChanged.add(this, &ProjectExplorer::handleInfoToggled);
        filter_->model().onChanged.add([this, alive = alive_](newui::Model&) {
            if (alive->alive.load()) scheduleRebuild();
            return newui::SyncReturn::Ignored;
        });

        mode_ = modeFromName(Settings::instance().getString(Settings::kExplorerDefaultView));
        modeControl_->setSelectedIndex(static_cast<std::size_t>(mode_));
        showAnalysisControls();
        setRoot(std::string());
    }

    ProjectExplorer::~ProjectExplorer()
    {
        alive_->alive.store(false);
        watcher_.reset();   // stops its workers before anything they post can go stale
        stopWork();
        stopProgressSweep();
        if (loop_ != nullptr && filterTimer_ != newui::RunLoop::kInvalidTimerHandle) {
            loop_->cancelDelayed(filterTimer_);
        }
        if (modeControl_ != nullptr) {
            modeControl_->onSelectionChanged.remove(modeConnection_);
        }
        if (analysisControl_ != nullptr) analysisControl_->onSelectionChanged.remove(analysisConnection_);
        if (includeViewControl_ != nullptr) includeViewControl_->onSelectionChanged.remove(includeViewConnection_);
        if (macroScopeControl_ != nullptr) macroScopeControl_->onSelectionChanged.remove(macroScopeConnection_);
        if (cardSteps_ != nullptr) cardSteps_->onSelectionChanged.remove(cardStepConnection_);
        if (cardGraph_ != nullptr) cardGraph_->onClick.remove(cardGraphConnection_);
        stopMacroWork();
    }

    std::wstring ProjectExplorer::filter() const
    {
        return filter_ != nullptr ? filter_->text() : std::wstring();
    }

    std::string ProjectExplorer::statusText() const { return status_text_; }
    std::string ProjectExplorer::rootName() const { return rootName_ != nullptr ? rootName_->text() : std::string(); }
    std::string ProjectExplorer::rootPath() const { return rootPath_ != nullptr ? rootPath_->text() : std::string(); }

    void ProjectExplorer::setStatus(const std::string& text)
    {
        status_text_ = text;
        if (status_ != nullptr) status_->setText(text);
    }

    void ProjectExplorer::stopProgressSweep()
    {
        if (loop_ != nullptr && progressTimer_ != newui::RunLoop::kInvalidTimerHandle) {
            loop_->cancelDelayed(progressTimer_);
        }
        progressTimer_ = newui::RunLoop::kInvalidTimerHandle;
    }

    void ProjectExplorer::setProgress(float progress)
    {
        if (progress_ == nullptr) return;
        if (progress != Found::kBusy) {
            stopProgressSweep();
            progress_->setValue(progress);
            return;
        }
        if (progressTimer_ != newui::RunLoop::kInvalidTimerHandle || loop_ == nullptr) return;
        progressTimer_ = loop_->postDelayed(std::chrono::milliseconds(30), [this, alive = alive_]() {
            if (!alive->alive.load()) return false;
            progressPhase_ += 0.08f;
            progress_->setValue(0.5f + 0.5f * std::sin(progressPhase_));
            return false;   // repeats until stopProgressSweep(), as CppEditor's load animation does
        });
    }

    void ProjectExplorer::setRoot(const std::string& folder)
    {
        if (!loaded()) return;
        stopWork();
        watcher_.reset();
        pendingReplyDir_.clear();
        treeCache_.clear();
        replyDir_.clear();
        replyWatch_ = newui::FileWatcher::kInvalidWatch;
        loadedReplyIndex_.clear();
        treeViewKey_.clear();
        loading_ = false;
        refreshFull_ = false;
        refreshModified_.clear();
        setProgress(0.0f);
        root_ = folder;
        cmake_.reset();
        products_ = std::make_shared<FileProducts>();
        problems_.reset();
        includeGraph_.reset();
        WorkspaceInfo::instance().setCompileSettings(nullptr);   // the last folder's targets say nothing about this one
        buildDir_.clear();
        note_.clear();
        stats_ = ProjectStats();
        stats_.root = folder;
        index_ = std::make_shared<cpptools::ProjectIndex>();

        fs::path path(folder);
        path.make_preferred();   // backslashes on Windows, as the user knows the path
        rootName_->setText(folder.empty() ? std::string() : (path.filename().empty() ? path.string() : path.filename().string()));
        rootPath_->setText(folder.empty() ? std::string() : path.string());
        setStatus(folder.empty() ? "No folder or solution is open." : "Reading the project...");
        rebuild();
        if (!folder.empty()) startWork();
    }

    void ProjectExplorer::setMode(ExplorerMode mode)
    {
        const bool controlRebuilds = modeControl_ != nullptr && modeControl_->selectedIndex() != static_cast<std::size_t>(mode);
        mode_ = mode;
        if (modeControl_ != nullptr) modeControl_->setSelectedIndex(static_cast<std::size_t>(mode));
        showAnalysisControls();
        if (!controlRebuilds) rebuild();   // otherwise handleModeChanged() just did
    }

    void ProjectExplorer::setAnalysisTab(AnalysisTab tab)
    {
        analysisTab_ = tab;
        if (analysisControl_ != nullptr) analysisControl_->setSelectedIndex(static_cast<std::size_t>(tab));
        showAnalysisControls();
        rebuild();
    }

    void ProjectExplorer::setIncludeView(IncludeView view)
    {
        includeView_ = view;
        if (includeViewControl_ != nullptr) includeViewControl_->setSelectedIndex(static_cast<std::size_t>(view));
        rebuild();
    }

    void ProjectExplorer::showAnalysisControls()
    {
        const bool analysis = mode_ == ExplorerMode::Analysis;
        if (analysisHost_ != nullptr) analysisHost_->setVisible(analysis);
        if (includeViewHost_ != nullptr) includeViewHost_->setVisible(analysis && analysisTab_ == AnalysisTab::Includes);
        if (macroScopeHost_ != nullptr) macroScopeHost_->setVisible(analysis && analysisTab_ == AnalysisTab::Macros);
    }

    newui::SyncReturn ProjectExplorer::handleAnalysisTabChanged(newui::SegmentedControl& sender)
    {
        analysisTab_ = static_cast<AnalysisTab>(sender.selectedIndex());
        showAnalysisControls();
        if (!showInfo_) rebuild();
        return newui::SyncReturn::Handled;
    }

    newui::SyncReturn ProjectExplorer::handleIncludeViewChanged(newui::SegmentedControl& sender)
    {
        includeView_ = static_cast<IncludeView>(sender.selectedIndex());
        if (!showInfo_) rebuild();
        return newui::SyncReturn::Handled;
    }

    void ProjectExplorer::setFilter(const std::wstring& text)
    {
        if (filter_ == nullptr) return;
        filter_->setText(text);
        if (loop_ != nullptr && filterTimer_ != newui::RunLoop::kInvalidTimerHandle) {
            loop_->cancelDelayed(filterTimer_);
            filterTimer_ = newui::RunLoop::kInvalidTimerHandle;
        }
        rebuild();
    }

    newui::SyncReturn ProjectExplorer::handleModeChanged(newui::SegmentedControl& sender)
    {
        mode_ = static_cast<ExplorerMode>(sender.selectedIndex());
        showAnalysisControls();
        if (showInfo_) {
            setInfoShown(false);   // rebuilds
        } else {
            rebuild();
        }
        return newui::SyncReturn::Handled;
    }

    void ProjectExplorer::setInfoShown(bool shown)
    {
        if (infoButton_ == nullptr || shown == showInfo_) return;
        infoButton_->setChecked(shown);   // handleInfoToggled does the rest
    }

    newui::SyncReturn ProjectExplorer::handleInfoToggled(newui::Button& sender)
    {
        showInfo_ = sender.isChecked();
        rebuild();
        return newui::SyncReturn::Handled;
    }

    void ProjectExplorer::scheduleRebuild()
    {
        // A pause in typing, not every keystroke: the Files search reads the disk.
        if (loop_ == nullptr) {
            rebuild();
            return;
        }
        if (filterTimer_ != newui::RunLoop::kInvalidTimerHandle) {
            loop_->cancelDelayed(filterTimer_);
        }
        filterTimer_ = loop_->postDelayed(std::chrono::milliseconds(150), [this, alive = alive_]() {
            if (alive->alive.load()) {
                filterTimer_ = newui::RunLoop::kInvalidTimerHandle;
                rebuild();
            }
            return true;
        });
    }

    // The tree's open rows and its selection as names from the top, and where the open ones were by position:
    // positions mean nothing once the rows are rebuilt, so those are closed and the names are looked up afresh.
    struct ProjectExplorer::TreeState
    {
        std::vector<std::vector<std::string>> expandedNames;   // parents before children
        std::vector<std::vector<std::size_t>> expandedPaths;
        std::vector<std::string> selectedNames;
    };

    ProjectExplorer::TreeState ProjectExplorer::saveTreeState()
    {
        TreeState state;
        if (model_ == nullptr || tree_ == nullptr) return state;
        newui::TreeController& controller = tree_->controller();
        std::function<void(const std::vector<std::size_t>&, const std::vector<std::string>&)> walk =
            [&](const std::vector<std::size_t>& parent, const std::vector<std::string>& parentNames) {
                const std::size_t count = model_->childCount(parent);
                for (std::size_t i = 0; i < count; ++i) {
                    std::vector<std::size_t> path = parent;
                    path.push_back(i);
                    if (!controller.isExpanded(path)) continue;
                    const ExplorerNode* node = model_->nodeAt(path);
                    if (node == nullptr) continue;
                    std::vector<std::string> names = parentNames;
                    names.push_back(node->text);
                    state.expandedNames.push_back(names);
                    state.expandedPaths.push_back(path);
                    walk(path, names);
                }
            };
        walk({}, {});
        if (auto selected = tree_->selectedPath()) {
            std::vector<std::size_t> path;
            for (std::size_t index : *selected) {
                path.push_back(index);
                const ExplorerNode* node = model_->nodeAt(path);
                if (node == nullptr) {
                    state.selectedNames.clear();
                    break;
                }
                state.selectedNames.push_back(node->text);
            }
        }
        return state;
    }

    void ProjectExplorer::restoreTreeState(const TreeState& state, bool sameView)
    {
        if (model_ == nullptr || tree_ == nullptr || !sameView) return;
        newui::TreeController& controller = tree_->controller();
        // The path of the row named `names` from the top, if the tree still has it.
        auto find = [&](const std::vector<std::string>& names, std::vector<std::size_t>& path) {
            path.clear();
            for (const std::string& name : names) {
                const std::size_t count = model_->childCount(path);
                bool found = false;
                for (std::size_t i = 0; i < count && !found; ++i) {
                    path.push_back(i);
                    const ExplorerNode* node = model_->nodeAt(path);
                    if (node != nullptr && node->text == name) found = true;
                    else path.pop_back();
                }
                if (!found) return false;
            }
            return true;
        };
        std::vector<std::size_t> path;
        for (const std::vector<std::string>& names : state.expandedNames) {
            if (find(names, path)) controller.setExpanded(path, true);
        }
        if (!state.selectedNames.empty() && find(state.selectedNames, path)) tree_->setSelectedPath(path);
    }

    void ProjectExplorer::rebuild()
    {
        if (model_ == nullptr) return;
        StallLog stall("rebuild(mode " + std::to_string(static_cast<int>(mode_)) + ")");
        showCard(nullptr);   // the rows it described are about to go
        const TreeState state = saveTreeState();
        for (const std::vector<std::size_t>& path : state.expandedPaths) tree_->controller().setExpanded(path, false);
        const std::string viewKey = std::to_string(static_cast<int>(mode_)) + "|" + wideToUtf8(filter()) + (showInfo_ ? "|info" : "");
        buildTree();
        restoreTreeState(state, viewKey == treeViewKey_);
        treeViewKey_ = viewKey;
    }

    void ProjectExplorer::buildTree()
    {
        if (showInfo_ && !root_.empty()) {
            model_->setRoot(buildStatsTree(stats_));
            for (std::size_t i = 0; i < 4; ++i) tree_->controller().setExpanded({ i }, true);   // its groups, open
            return;
        }
        const std::string text = wideToUtf8(filter());
        ExplorerNode top;
        switch (mode_) {
            case ExplorerMode::Files:
                if (root_.empty()) {
                    top = buildMessageTree("Open a folder or solution.");
                } else if (text.empty()) {
                    std::shared_ptr<FileProducts> products = products_;
                    top = buildFilesTree(root_, [products](const std::string& path, bool isDirectory) -> std::string {
                        if (isDirectory || !products->known) return std::string();
                        const std::string ext = extensionOf(path);
                        if (!isIndexedExtension(ext)) return std::string();
                        auto it = products->byPath.find(lowered(cpptools::ProjectIndex::normalizePath(path)));
                        if (it == products->byPath.end()) return isCppSourceExtension(ext) ? "not built" : std::string();
                        return it->second.front() + (it->second.size() > 1 ? " +" + std::to_string(it->second.size() - 1) : std::string());
                    }, [problems = problems_](const std::string& path, bool) {
                        if (problems == nullptr) return ExplorerNode::Badge::None;
                        auto it = problems->worst.find(lowered(cpptools::ProjectIndex::normalizePath(path)));
                        return it == problems->worst.end() ? ExplorerNode::Badge::None : it->second;
                    });
                } else {
                    top = buildFileSearch(root_, text);
                }
                break;
            case ExplorerMode::Symbols:
                if (root_.empty()) {
                    top = buildMessageTree("Open a folder or solution.");
                } else {
                    top = cachedTree("symbols|" + text, [&]() { return buildSymbolsTree(*index_, text); });
                }
                break;
            case ExplorerMode::Products:
                if (cmake_ != nullptr) {
                    top = buildProductsTree(*cmake_, text);
                } else {
                    top = buildMessageTree(root_.empty() ? "Open a folder or solution."
                                         : note_.empty() ? "Reading the CMake build tree..." : note_);
                }
                break;
            case ExplorerMode::Analysis:
                top = buildAnalysisTree(text);
                break;
        }
        model_->setRoot(std::move(top));
    }

    void ProjectExplorer::setActiveFile(const std::string& path)
    {
        const std::string normalized = path.empty() ? std::string() : cpptools::ProjectIndex::normalizePath(path);
        if (normalized == activeFile_) return;
        activeFile_ = normalized;
        if (mode_ == ExplorerMode::Analysis && analysisTab_ == AnalysisTab::Macros && !showInfo_) rebuild();
    }

    void ProjectExplorer::setMacroScope(MacroScope scope)
    {
        macroScope_ = scope;
        if (macroScopeControl_ != nullptr) macroScopeControl_->setSelectedIndex(static_cast<std::size_t>(scope));
        if (mode_ == ExplorerMode::Analysis && analysisTab_ == AnalysisTab::Macros && !showInfo_) rebuild();
    }

    newui::SyncReturn ProjectExplorer::handleMacroScopeChanged(newui::SegmentedControl& sender)
    {
        macroScope_ = static_cast<MacroScope>(sender.selectedIndex());
        if (!showInfo_) rebuild();
        return newui::SyncReturn::Handled;
    }

    newui::SyncReturn ProjectExplorer::handleSelectionChanged(newui::TreeView&)
    {
        const std::optional<std::vector<std::size_t>> selected = tree_->selectedPath();
        const ExplorerNode* node = selected.has_value() && model_ != nullptr ? model_->nodeAt(*selected) : nullptr;
        showCard(node != nullptr ? node->card : nullptr);
        if (node != nullptr && node->swatch >= 0 && !node->children.empty()) expandAll(*selected, *node);   // a use: its macros, open
        if (node != nullptr && node->openOnSelect) activate(*selected);
        return newui::SyncReturn::Handled;
    }

    void ProjectExplorer::expandAll(const std::vector<std::size_t>& path, const ExplorerNode& node)
    {
        tree_->controller().setExpanded(path, true);
        std::vector<std::size_t> child = path;
        child.push_back(0);
        for (std::size_t i = 0; i < node.children.size(); ++i) {
            child.back() = i;
            if (!node.children[i].children.empty()) expandAll(child, node.children[i]);
        }
    }

    newui::SyncReturn ProjectExplorer::handleCardStepChanged(newui::SegmentedControl& sender)
    {
        showCardStep(sender.selectedIndex());
        return newui::SyncReturn::Handled;
    }

    void ProjectExplorer::showCard(const std::shared_ptr<const ExplorerCard>& card)
    {
        card_ = card;
        if (cardHost_ == nullptr) return;
        cardHost_->setVisible(card != nullptr);
        if (card == nullptr) return;

        cardTitle_->setText(card->title);
        std::string warnings;
        for (const std::string& warning : card->warnings) warnings += (warnings.empty() ? "" : "  ") + warning;
        cardWarning_->setText(warnings);
        cardWarning_->setVisible(!warnings.empty());
        cardGraph_->setVisible(!card->graphFile.empty());

        if (!card->spans.empty()) {
            cardSteps_->setVisible(false);
            showCardSpans(*card);
            return;
        }
        cardSteps_->setVisible(!card->steps.empty());
        if (card->steps.empty()) {
            showCardStep(0);
            return;
        }
        std::vector<std::string> labels;
        for (const auto& step : card->steps) labels.push_back(step.first);
        cardSteps_->setSegments(labels);
        cardSteps_->setDesiredSize(cardSteps_->naturalSize());
        cardSteps_->setSelectedIndex(labels.size() - 1);   // what it became first
        showCardStep(labels.size() - 1);
    }

    void ProjectExplorer::openIncludeGraph()
    {
        if (card_ == nullptr || card_->graphFile.empty() || includeGraph_ == nullptr || cardGraph_ == nullptr) return;
        const newui::Rect anchor = cardGraph_->localToScreen(cardGraph_->getClientBounds());
        const std::string file = card_->graphFile;
        // Posted, never inline: this runs inside the button's mouse dispatch, and the mouse-down tail would take
        // focus straight back from a popup created now.
        auto show = [this, alive = alive_, anchor, file]() {
            if (!alive->alive.load() || cardGraph_ == nullptr) return;
            IncludeGraphPopup::show(*cardGraph_, anchor, includeGraph_, file, [this, alive](const std::string& path) {
                if (alive->alive.load() && open_) open_(path, 0);
            });
        };
        if (loop_ != nullptr) loop_->post(std::move(show));
        else show();
    }

    // The card's text in pieces, each tinted like the row with its swatch.
    void ProjectExplorer::showCardSpans(const ExplorerCard& card)
    {
        const bool dark = newui::UIColorManager::isDarkMode();
        auto sheet = std::make_shared<newui::TextStyleSheet>();
        std::wstring text;
        std::vector<newui::text::TextStyleRange> ranges;
        for (const ExplorerCard::Span& span : card.spans) {
            const std::wstring piece = utf8ToWide(span.text);
            if (span.swatch >= 0) {
                const std::string name = "swatch" + std::to_string(span.swatch % 6);
                if (sheet->style(name) == nullptr) {
                    auto* style = new newui::TextStyle(name);
                    const std::uint32_t argb = explorerSwatchColor(span.swatch, dark);
                    newui::Color tint(float((argb >> 16) & 0xFF) / 255.0f, float((argb >> 8) & 0xFF) / 255.0f, float(argb & 0xFF) / 255.0f, 0.30f);
                    style->setBackgroundColor(tint);
                    sheet->addStyle(style);
                }
                // a leading space stays outside the tint
                const std::size_t lead = piece.empty() || piece.front() != L' ' ? 0 : 1;
                ranges.push_back({ text.size() + lead, piece.size() - lead, name });
            }
            text += piece;
        }
        cardText_->inputTraits().setReadOnly(false);
        cardText_->setText(text);
        cardText_->setStyleSheet(sheet);
        cardText_->setStyledRanges(std::move(ranges));
        cardText_->inputTraits().setReadOnly(true);
    }

    void ProjectExplorer::showCardStep(std::size_t index)
    {
        if (cardText_ == nullptr) return;
        std::string text;
        if (card_ != nullptr && index < card_->steps.size()) text = card_->steps[index].second;
        else if (card_ != nullptr && card_->steps.empty()) text = card_->body;
        cardText_->inputTraits().setReadOnly(false);   // read-only also stops this program from setting it
        cardText_->setText(utf8ToWide(text));
        cardText_->inputTraits().setReadOnly(true);
    }

    void ProjectExplorer::stopMacroWork()
    {
        if (macroCancel_ != nullptr) macroCancel_->store(true);
        if (macroThread_.joinable()) macroThread_.join();
        macroRunningKey_.clear();
    }

    bool ProjectExplorer::requestMacros(const std::string& key, const std::vector<std::string>& files)
    {
        if (macroRunningKey_ == key) return false;
        stopMacroWork();

        struct Job
        {
            std::string path;
            std::vector<std::string> args;
        };
        std::vector<Job> jobs;
        for (const std::string& file : files) jobs.push_back(Job{ file, WorkspaceInfo::instance().compileFlagsFor(file).args });
        auto result = std::make_shared<std::vector<MacroFile>>();
        auto cancel = std::make_shared<std::atomic<bool>>(false);
        macroCancel_ = cancel;
        auto run = [jobs, result, cancel]() {   // each file as the editor would parse it: its flags, its text on disk
            for (const Job& job : jobs) {
                if (cancel->load()) return;
                std::ifstream in(job.path, std::ios::binary);
                const std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                MacroFile file;
                file.path = job.path;
                file.analysis = cpptools_analysis::analyzeMacros(content, job.path, job.args);
                result->push_back(std::move(file));
            }
        };

        newui::RunLoop& current = newui::RunLoop::current();
        newui::RunLoop* loop = current ? &current : nullptr;
        if (!background_ || loop == nullptr) {
            run();
            macroKey_ = key;
            macroResult_ = result;
            return true;
        }
        macroRunningKey_ = key;
        macroThread_ = std::thread([this, run, loop, cancel, key, result, alive = alive_]() {
            run();
            if (cancel->load()) return;
            loop->post([this, alive, key, result, cancel]() {
                if (!alive->alive.load() || cancel->load()) return;
                macroKey_ = key;
                macroResult_ = result;
                macroRunningKey_.clear();
                rebuild();
            });
        });
        return false;
    }

    ExplorerNode ProjectExplorer::buildMacroView(const std::string& filter)
    {
        if (!stats_.complete) return buildMessageTree("Reading the project; macros appear when indexing is done.");
        const bool folder = macroScope_ == MacroScope::Folder;
        std::string target = activeFile_;
        if (folder) target = activeFile_.empty() ? cpptools::ProjectIndex::normalizePath(root_) : fs::path(activeFile_).parent_path().generic_string();
        if (target.empty()) return buildMessageTree("Open a C++ file in the editor to see its macros.");

        std::vector<std::string> files;
        std::string key = std::string(folder ? "folder:" : "file:") + lowered(target) + "|" +
                          std::to_string(WorkspaceInfo::instance().compileSettingsVersion());
        if (folder) {
            for (const std::string& file : index_->files()) {
                if (lowered(fs::path(file).parent_path().generic_string()) == lowered(target)) files.push_back(file);
            }
        } else {
            files.push_back(target);
            std::error_code ec;
            const auto stamp = fs::last_write_time(target, ec);
            key += "|" + std::to_string(ec ? 0 : stamp.time_since_epoch().count());
        }
        if (files.empty()) return buildMessageTree("No C++ file is indexed in this folder.");

        if ((macroKey_ != key || macroResult_ == nullptr) && !requestMacros(key, files)) {
            return buildMessageTree("Analyzing " + fs::path(target).filename().string() + "...");
        }
        return buildMacroTree(*macroResult_, root_, folder, filter);
    }

    ExplorerNode ProjectExplorer::cachedTree(const std::string& key, const std::function<ExplorerNode()>& build)
    {
        if (!loading_) {
            auto found = treeCache_.find(key);
            if (found != treeCache_.end()) return *found->second;
        }
        ExplorerNode tree = build();
        if (!loading_) {
            if (treeCache_.size() >= 8) treeCache_.clear();   // filters come and go: keep it small
            treeCache_[key] = std::make_shared<const ExplorerNode>(tree);
        }
        return tree;
    }

    ExplorerNode ProjectExplorer::buildAnalysisTree(const std::string& filter)
    {
        if (root_.empty()) return buildMessageTree("Open a folder or solution.");
        switch (analysisTab_) {
            case AnalysisTab::Includes:
                if (!stats_.complete) return buildMessageTree("Reading the project; includes appear when indexing is done.");
                if (includeGraph_ == nullptr) includeGraph_ = std::make_shared<const cpptools::IncludeGraph>(*index_);
                return cachedTree(std::string("includes|") + (includeView_ == IncludeView::Impact ? "impact|" : "files|") + filter, [&]() {
                    return includeView_ == IncludeView::Impact ? buildIncludeImpactTree(includeGraph_, root_, filter)
                                                               : buildIncludeFileTree(includeGraph_, root_, filter);
                });
            case AnalysisTab::Macros:
                return buildMacroView(filter);
            case AnalysisTab::Templates:
                return buildMessageTree("Template instantiation analysis needs the Clang AST library, which is not built yet.");
        }
        return ExplorerNode();
    }

    bool ProjectExplorer::activate(const std::vector<std::size_t>& path)
    {
        if (model_ == nullptr) return false;
        const ExplorerNode* node = model_->nodeAt(path);
        if (node == nullptr || node->path.empty() || node->kind == ExplorerNode::Kind::Folder) return false;
        return open_ ? open_(node->path, node->line) : false;
    }

    newui::SyncReturn ProjectExplorer::handleDoubleClick(newui::View&, const newui::Point&, std::uint32_t, std::uint32_t)
    {
        auto path = tree_->selectedPath();
        if (!path) return newui::SyncReturn::Ignored;
        return activate(*path) ? newui::SyncReturn::Handled : newui::SyncReturn::Ignored;
    }

    void ProjectExplorer::stopWork()
    {
        if (alive_) alive_->cancel.store(true);
        if (worker_.joinable()) worker_.join();
    }

    void ProjectExplorer::waitForIndexing()
    {
        if (worker_.joinable()) worker_.join();
    }

    void ProjectExplorer::applyFound(const std::shared_ptr<Found>& found)
    {
        StallLog stall(std::string("applyFound(") + (found->final ? "final" : found->model != nullptr ? "model" : found->stats != nullptr ? "stats" : "progress") + ")");
        if (!found->replyDir.empty()) pendingReplyDir_ = found->replyDir;
        if (found->model != nullptr) {
            cmake_ = found->model;
            buildDir_ = found->buildDir;
            loadedReplyIndex_ = latestReplyIndex(found->replyDir);
            note_.clear();
            auto products = std::make_shared<FileProducts>();
            products->known = true;
            for (const cmakemodel::Target& target : cmake_->targets) {
                if (target.kind == cmakemodel::ProductKind::Generated || target.kind == cmakemodel::ProductKind::Input) continue;
                if (isExternalTarget(target, *cmake_)) continue;
                for (const cmakemodel::SourceFile& source : cmake_->allSources(target)) {
                    const std::string path = isAbsolute(source.path) ? source.path : cmake_->sourceDir + "/" + source.path;
                    std::vector<std::string>& names = products->byPath[lowered(cpptools::ProjectIndex::normalizePath(path))];
                    if (std::find(names.begin(), names.end(), target.name) == names.end()) names.push_back(target.name);
                }
            }
            products_ = std::move(products);
            rebuild();
        } else if (!found->note.empty()) {
            note_ = found->note;
            rebuild();
        }

        if (found->settings != nullptr) {
            WorkspaceInfo::instance().setCompileSettings(found->settings);   // the open editors take their flags from it too
        }
        if (found->stats != nullptr) {
            stats_ = *found->stats;
            if (showInfo_) rebuild();
        }
        if (found->progress != Found::kKeep) setProgress(found->progress);
        if (found->final) setProgress(0.0f);

        if (!found->status.empty()) {
            std::string text = found->status;
            if (cmake_ != nullptr && found->final) {
                text += "   CMake: " + fs::path(buildDir_).filename().string() +
                        (cmake_->configuration.empty() ? std::string() : " (" + cmake_->configuration + ")");
            } else if (found->final) {
                text += "   No CMake build tree";
            }
            setStatus(text);
        }
        if (found->final) {
            loading_ = false;
            treeCache_.clear();   // the index changed
            // Watching starts once the first load is done, so indexing is never competing with (or redone for) events.
            startWatching();
            if (!pendingReplyDir_.empty()) watchReply(pendingReplyDir_);
            includeGraph_.reset();   // the index is complete: the next Analysis view reads it afresh
            collectProblems();
            rebuild();
            if (refreshFull_ || !refreshModified_.empty()) {   // changes that came in during the load
                const bool full = refreshFull_;
                std::set<std::string> modified = std::move(refreshModified_);
                refreshFull_ = false;
                refreshModified_.clear();
                refresh(full, std::move(modified));
            }
        }
    }

    ProjectExplorer::DiskChanges ProjectExplorer::summarizeChanges(const newui::FileWatcher::Changes& changes)
    {
        using Action = newui::FileWatcher::Action;
        DiskChanges summary;
        for (const newui::FileWatcher::Change& change : changes) {
            if (change.action != Action::Modified) {
                summary.full = true;   // a file came, went or moved (or events were lost): the listings changed
                continue;
            }
            const std::string ext = extensionOf(change.path);
            if (lowered(fs::path(change.path).filename().string()) == "cmakelists.txt" || ext == ".cmake") {
                summary.full = true;   // the description of the build may be out of date
            } else if (isIndexedExtension(ext)) {
                summary.modified.insert(change.path);
            }   // any other file's contents are not shown
        }
        return summary;
    }

    bool ProjectExplorer::watchIgnores(const std::string& root, const std::string& path)
    {
        const std::string base = newui::normalizePath(root);
        if (path.size() <= base.size() + 1) return true;
        std::size_t start = base.size() + 1;
        while (start < path.size()) {
            const std::size_t slash = path.find('/', start);
            const std::string name = path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
            if (!name.empty() && (name.front() == '.' || isSkippedExplorerFolder(name))) return true;
            if (slash == std::string::npos) return isSkippedExplorerFile(name);
            start = slash + 1;
        }
        return false;
    }

    std::string ProjectExplorer::latestReplyIndex(const std::string& replyDir)
    {
        std::string latest;
        std::error_code ec;
        for (fs::directory_iterator it(fs::path(replyDir) / "reply", ec), end; !ec && it != end; it.increment(ec)) {
            const std::string name = it->path().filename().string();
            if (name.compare(0, 6, "index-") == 0 && extensionOf(name) == ".json" && name > latest) latest = name;   // stamped names sort by time
        }
        return latest;
    }

    void ProjectExplorer::watchReply(const std::string& replyDir)
    {
        if (watcher_ == nullptr || replyDir == replyDir_) return;
        if (replyWatch_ != newui::FileWatcher::kInvalidWatch) watcher_->unwatch(replyWatch_);
        replyDir_ = replyDir;
        newui::FileWatcher::Options options;
        options.debounce = std::chrono::milliseconds(400);
        replyWatch_ = watcher_->watch(replyDir, options);
    }

    void ProjectExplorer::startWatching()
    {
        if (watcher_ != nullptr || !background_ || loop_ == nullptr || root_.empty()) return;
        watcher_ = std::make_unique<newui::FileWatcher>(*loop_);
        watcherConnection_ = watcher_->onChanged.add(this, &ProjectExplorer::handleDiskChanges);
        newui::FileWatcher::Options options;
        options.debounce = std::chrono::milliseconds(400);   // a build or checkout is one batch, not hundreds
        options.ignore = [root = root_](const std::string& path) { return watchIgnores(root, path); };
        if (watcher_->watch(root_, options) == newui::FileWatcher::kInvalidWatch) watcher_.reset();
    }

    newui::SyncReturn ProjectExplorer::handleDiskChanges(newui::FileWatcher&, const newui::FileWatcher::Changes& changes)
    {
        newui::FileWatcher::Changes projectChanges;
        bool replyTouched = false;
        for (const newui::FileWatcher::Change& change : changes) {
            if (!replyDir_.empty() && change.path.compare(0, replyDir_.size(), replyDir_) == 0) replyTouched = true;
            else projectChanges.push_back(change);
        }
        DiskChanges summary = summarizeChanges(projectChanges);
        // CMake configured again (here or elsewhere): its description of the build has a new index. Our own
        // load rewrites it too, which is why nothing is done during one.
        if (replyTouched && !loading_ && latestReplyIndex(replyDir_) != loadedReplyIndex_) summary.full = true;
        if (!summary.any()) return newui::SyncReturn::Ignored;
        if (loading_) {   // the load in progress may or may not have seen these: redo them once it ends
            refreshFull_ = refreshFull_ || summary.full;
            refreshModified_.insert(summary.modified.begin(), summary.modified.end());
        } else {
            refresh(summary.full, std::move(summary.modified));
        }
        return newui::SyncReturn::Handled;
    }

    void ProjectExplorer::refresh(bool full, std::set<std::string> modified)
    {
        if (full) startWork();   // re-reads the CMake description and the folders; a file that did not change costs a stat
        else refreshModified(std::move(modified));
    }

    void ProjectExplorer::refreshModified(std::set<std::string> modified)
    {
        std::vector<std::string> files;
        for (const std::string& path : modified) {
            if (index_->hasFile(path)) files.push_back(path);   // a stray file is not part of the project
        }
        if (files.empty() || loop_ == nullptr) return;
        stopWork();
        alive_->cancel.store(false);
        loading_ = true;
        worker_ = std::thread([this, files, index = index_, alive = alive_, loop = loop_]() {
            for (const std::string& file : files) {
                if (alive->cancel.load()) return;
                index->updateFile(file);
            }
            auto done = std::make_shared<Found>();
            done->final = true;
            loop->post([this, alive, done]() {
                if (alive->alive.load()) applyFound(done);
            });
        });
    }

    void ProjectExplorer::collectProblems()
    {
        StallLog stall("collectProblems");
        using Badge = ExplorerNode::Badge;
        auto problems = std::make_shared<FileProblems>();
        const std::string rootKey = lowered(cpptools::ProjectIndex::normalizePath(root_));
        // Marks a file and each folder above it up to the root with the worst badge seen.
        auto mark = [&](std::string key, Badge badge) {
            while (key.size() >= rootKey.size()) {
                Badge& slot = problems->worst[key];
                if (badge == Badge::Error || slot == Badge::None) slot = badge;
                const std::size_t slash = key.find_last_of('/');
                if (slash == std::string::npos) break;
                key.resize(slash);
            }
        };
        const std::vector<std::string> files = index_->files();
        std::set<std::string> included;   // every file some indexed file includes
        for (const std::string& file : files) {
            for (const cpptools::IncludeEdge& edge : index_->includesOf(file)) {
                included.insert(lowered(cpptools::ProjectIndex::normalizePath(edge.included)));
            }
        }
        for (const std::string& file : files) {
            const cpptools::ProjectIndex::Problems found = index_->problemsIn(file);
            const std::string key = lowered(cpptools::ProjectIndex::normalizePath(file));
            if (found.errors == 0 && found.warnings == 0 && found.unresolvedIncludes == 0) {
                // a header nothing includes; only the file is marked, a folder of them is not a problem
                const std::string ext = extensionOf(file);
                if ((ext == ".h" || ext == ".hpp" || ext == ".hxx") && included.count(key) == 0) {
                    problems->worst[key] = Badge::Unreferenced;
                }
                continue;
            }
            // A header libclang cannot find leaves its names undeclared, and every use of one is another error, so
            // a file with an unresolved include is only a warning: the cause is the flags, not the code.
            mark(key, found.errors > 0 && found.unresolvedIncludes == 0 ? Badge::Error : Badge::Warning);
        }

        // CMake's side: a CMakeLists.txt whose target names a source that is not on disk.
        if (cmake_ != nullptr) {
            std::error_code ec;
            for (const cmakemodel::Target& target : cmake_->targets) {
                if (target.kind == cmakemodel::ProductKind::Generated || target.kind == cmakemodel::ProductKind::Input) continue;
                if (isExternalTarget(target, *cmake_)) continue;
                for (const cmakemodel::SourceFile& source : target.sources) {
                    if (source.generated) continue;
                    const std::string path = isAbsolute(source.path) ? source.path : cmake_->sourceDir + "/" + source.path;
                    if (fs::exists(path, ec)) continue;
                    const std::string list = cmake_->sourceDir + (target.directory.empty() || target.directory == "." ? "" : "/" + target.directory) + "/CMakeLists.txt";
                    mark(lowered(cpptools::ProjectIndex::normalizePath(list)), Badge::Error);
                    break;
                }
            }
        }
        problems_ = std::move(problems);
    }

    void ProjectExplorer::startWork()
    {
        stopWork();
        alive_->cancel.store(false);
        loading_ = true;

        // The loop may not have been pumping when this was constructed (a plain newui app builds its views
        // first), so look again now; with none running the work falls back to this thread.
        newui::RunLoop& current = newui::RunLoop::current();
        loop_ = current ? &current : nullptr;

        const std::string root = root_;
        const std::string configuration = WorkspaceInfo::instance().configuration();
        std::shared_ptr<Alive> alive = alive_;
        std::shared_ptr<cpptools::ProjectIndex> index = index_;
        newui::RunLoop* loop = loop_;
        const bool inBackground = background_ && loop != nullptr;

        auto deliver = [this, alive, loop, inBackground](std::shared_ptr<Found> found) {
            auto apply = [this, alive, found]() {
                if (alive->alive.load()) applyFound(found);
            };
            if (inBackground) loop->post(apply);
            else apply();
        };

        auto job = [root, configuration, alive, index, deliver]() {   // all on the worker
            // The CMake build tree first: the product views and the Files badges need only that.
            using Clock = std::chrono::steady_clock;
            auto since = [](Clock::time_point start) {
                return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
            };
            const Clock::time_point began = Clock::now();
            ProjectStats stats;
            stats.root = root;
            auto publish = [&](Found& found) {
                stats.totalMs = since(began);
                found.stats = std::make_shared<ProjectStats>(stats);
            };

            // Pass 1: the top-level folders, counted a folder per task on several threads.
            {
                const Clock::time_point start = Clock::now();
                stats.scan = scanProjectFolders(root,
                    [&](std::size_t done, std::size_t total, const std::string& folder) {
                        auto update = std::make_shared<Found>();
                        update->status = "Scanning folders (" + std::to_string(done) + " of " + std::to_string(total) + "): " + folder + "/";
                        update->progress = total > 0 ? static_cast<float>(done) / static_cast<float>(total) : 0.0f;
                        deliver(update);
                    },
                    &alive->cancel);
                stats.scanMs = since(start);
            }
            if (alive->cancel.load()) return;

            // Pass 2: what CMake says is built, then the index.
            auto first = std::make_shared<Found>();
            {
                auto reading = std::make_shared<Found>();
                reading->status = "Reading the CMake build...";
                reading->progress = Found::kBusy;
                publish(*reading);
                deliver(reading);
            }
            const Clock::time_point cmakeStart = Clock::now();
            const BuildTree build = findBuildTree(root);
            if (!build.dir.empty()) first->replyDir = build.dir + "/.cmake/api/v1";
            std::string buildNote;   // said beside the counts when the description is incomplete
            if (build.dir.empty()) {
                first->note = "No CMake build folder found in this workspace.";
            } else if (!build.hasReply) {
                std::string ignored;
                cmakemodel::requestQueries(build.dir, &ignored);
                first->note = "CMake has not described this build yet: configure the project once and it will appear here.";
            } else {
                cmakemodel::LoadResult loaded = cmakemodel::loadFileApi(build.dir, configuration);
                // A reply with files missing (a configure rewrote the folder) is repaired by configuring again.
                if ((!loaded.ok() || loaded.skipped > 0) && !loaded.cmakeExe.empty() && !alive->cancel.load()) {
                    auto refreshing = std::make_shared<Found>();
                    refreshing->status = "Refreshing CMake's description of the build (configuring)...";
                    deliver(refreshing);
                    const Clock::time_point refreshStart = Clock::now();
                    if (runCMakeConfigure(loaded.cmakeExe, root, build.dir, alive->cancel)) {
                        loaded = cmakemodel::loadFileApi(build.dir, configuration);
                    }
                    stats.cmakeRefreshMs = since(refreshStart);
                }
                if (loaded.ok()) {
                    stats.targets = loaded.model.targets.size();
                    stats.targetsMissing = static_cast<std::size_t>(loaded.skipped);
                    stats.buildDir = build.dir;
                    stats.configuration = loaded.model.configuration;
                    first->model = std::make_shared<cmakemodel::Model>(std::move(loaded.model));
                    first->buildDir = build.dir;
                    if (loaded.skipped > 0) buildNote = std::to_string(loaded.skipped) + " targets missing from CMake's reply";
                } else {
                    first->note = loaded.error;
                    buildNote = loaded.error;
                }
            }
            stats.cmakeMs = since(cmakeStart) - stats.cmakeRefreshMs;
            first->status = "Loading the index cache...";
            first->progress = Found::kBusy;
            publish(*first);
            deliver(first);
            if (alive->cancel.load()) return;

            const std::vector<std::string> files = filesToIndex(root, first->model.get());
            stats.filesToIndex = files.size();
            for (const std::string& known : index->files()) {   // a file deleted since the last pass
                std::error_code existsEc;
                if (!fs::exists(known, existsEc)) index->removeFile(known);
            }
            index->setRoots({ root });
            std::shared_ptr<const cmakemodel::CompileSettingsIndex> settings;   // how the build compiles each file
            if (first->model != nullptr) {
                // What the build really compiles a file with: its target's include folders and definitions, added to the
                // flags found the usual way (compile_commands.json), which may not cover a target (it did not cover
                // the tests, the codegen library, or the generated headers) and so miss a -D or a -I.
                settings = std::make_shared<const cmakemodel::CompileSettingsIndex>(*first->model);
                index->setFlagsProvider([settings](const std::string& file) { return compileFlagsWithBuild(file, settings.get()).args; });
                // Also handed to the editors, so their squiggles agree with this index. A delivery of its own: `first`
                // went to the UI thread already, and is not to be written to from here.
                auto flagsSource = std::make_shared<Found>();
                flagsSource->settings = settings;
                deliver(flagsSource);
            }
            const std::string cache = cachePathFor(root);
            {
                const Clock::time_point start = Clock::now();
                if (!cache.empty()) index->load(cache);
                stats.cacheLoadMs = since(start);
            }

            // A header no target lists is compiled as part of what includes it: give it that file's settings. Needs the
            // include edges, so once from the cache (a header parsed with these settings last time is then up to date)
            // and again after the files are parsed, for the ones that were new or changed.
            std::set<std::string> borrowedHeaders;
            auto borrowHeaders = [&]() -> std::vector<std::string> {
                if (settings == nullptr) return {};
                auto withHeaders = std::make_shared<cmakemodel::CompileSettingsIndex>(*settings);
                std::vector<std::string> all;
                borrowHeaderSettings(*index, *withHeaders, &all);
                std::vector<std::string> fresh;
                for (const std::string& header : all) {
                    if (borrowedHeaders.insert(header).second) fresh.push_back(header);
                }
                if (fresh.empty()) return fresh;
                index->setFlagsProvider([withHeaders](const std::string& file) { return compileFlagsWithBuild(file, withHeaders.get()).args; });
                auto flagsSource = std::make_shared<Found>();
                flagsSource->settings = withHeaders;
                deliver(flagsSource);
                return fresh;
            };
            borrowHeaders();

            const unsigned cores = std::thread::hardware_concurrency();
            stats.indexThreads = std::max(1u, std::min<unsigned>(cores > 1 ? cores - 1 : 1, static_cast<unsigned>(std::max<std::size_t>(files.size(), 1))));
            const Clock::time_point indexStart = Clock::now();
            cpptools::IndexProgress result = index->indexFiles(files,
                [&](const cpptools::IndexProgress& progress) {
                    if (alive->cancel.load() || !alive->alive.load()) return false;
                    if (progress.done % 5 == 0 && progress.done != progress.total) {
                        auto update = std::make_shared<Found>();
                        update->status = "Indexing " + std::to_string(progress.done) + " of " + std::to_string(progress.total) +
                                         ": " + fs::path(progress.current).filename().string();
                        update->progress = progress.total > 0 ? static_cast<float>(progress.done) / static_cast<float>(progress.total) : 0.0f;
                        deliver(update);
                    }
                    return true;
                },
                stats.indexThreads);
            if (alive->cancel.load()) return;
            const std::vector<std::string> reread = borrowHeaders();
            if (!reread.empty()) {   // their flags changed, so the index parses them again
                const cpptools::IndexProgress again = index->indexFiles(reread,
                    [&](const cpptools::IndexProgress& progress) {
                        if (alive->cancel.load() || !alive->alive.load()) return false;
                        auto update = std::make_shared<Found>();
                        update->status = "Reading headers with the settings of what includes them (" + std::to_string(progress.done) + " of " +
                                         std::to_string(progress.total) + ")";
                        update->progress = progress.total > 0 ? static_cast<float>(progress.done) / static_cast<float>(progress.total) : 0.0f;
                        deliver(update);
                        return true;
                    },
                    stats.indexThreads);
                result.parsed += again.parsed;
                result.failed += again.failed;
                if (alive->cancel.load()) return;
            }
            stats.indexMs = since(indexStart);
            stats.filesToIndex = result.total;   // CMake lists a shared source once per target; the index counts it once
            if (!cache.empty()) index->save(cache);

            std::size_t symbols = 0;
            for (const std::string& file : index->files()) symbols += index->symbolsIn(file).size();
            stats.symbols = symbols;
            for (const std::string& file : index->files()) {
                const cpptools::ProjectIndex::Problems found = index->problemsIn(file);
                if (found.errors > 0 || found.warnings > 0 || found.unresolvedIncludes > 0) {
                    ProblemFile problem{ file, found.errors, found.warnings, found.unresolvedIncludes, {} };
                    for (const cpptools::IndexedDiagnostic& sample : found.samples) {
                        problem.samples.push_back({ sample.file, sample.line, sample.message });
                    }
                    stats.problemFiles.push_back(std::move(problem));
                }
                if (found.unresolvedIncludes > 0) {   // its other errors are most likely knock-on, so they count here
                    ++stats.filesWithUnresolvedIncludes;
                    stats.unresolvedIncludes += found.unresolvedIncludes;
                } else if (found.errors > 0) {
                    ++stats.filesWithErrors;
                } else if (found.warnings > 0) {
                    ++stats.filesWithWarnings;
                }
            }
            stats.parsed = result.parsed;
            stats.upToDate = result.skipped;
            stats.failed = result.failed;
            stats.complete = true;
            auto done = std::make_shared<Found>();
            publish(*done);
            done->final = true;
            done->status = std::to_string(result.total) + " files, " + std::to_string(symbols) + " symbols";
            if (result.failed > 0) done->status += ", " + std::to_string(result.failed) + " not read";
            if (!buildNote.empty()) done->status += " (" + buildNote + ")";
            deliver(done);
        };

        if (inBackground) worker_ = std::thread(job);
        else job();
    }
}
