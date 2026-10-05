#include "ProjectExplorer.h"

#include <Windows.h>

#include "ExplorerItem.h"
#include "Logging.h"
#include "Settings.h"
#include "TextEncoding.h"
#include "WorkspaceInfo.h"

#include <cmakemodel/fileapi.h>
#include <cmakemodel/model.h>
#include <cpptools/compileflags.h>
#include <cpptools/projectindex.h>

#include <newui/bundle.h>
#include <newui/layout.h>
#include <newui/uicolormanager.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <set>

namespace fs = std::filesystem;

namespace CodeToolsVsix
{
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
                if (!files.empty()) return files;
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
        auto* treeHost = dynamic_cast<newui::SubView*>(sub->findView("treeHost"));
        if (rootName_ == nullptr || rootPath_ == nullptr || status_ == nullptr || filter_ == nullptr || infoButton_ == nullptr ||
            modeHost == nullptr || treeHost == nullptr) {
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
        tree_->onMouseDblClick.add(this, &ProjectExplorer::handleDoubleClick);
        infoButton_->onCheckedChanged.add(this, &ProjectExplorer::handleInfoToggled);
        filter_->model().onChanged.add([this, alive = alive_](newui::Model&) {
            if (alive->alive.load()) scheduleRebuild();
            return newui::SyncReturn::Ignored;
        });

        mode_ = modeFromName(Settings::instance().getString(Settings::kExplorerDefaultView));
        modeControl_->setSelectedIndex(static_cast<std::size_t>(mode_));
        setRoot(std::string());
    }

    ProjectExplorer::~ProjectExplorer()
    {
        alive_->alive.store(false);
        stopWork();
        stopProgressSweep();
        if (loop_ != nullptr && filterTimer_ != newui::RunLoop::kInvalidTimerHandle) {
            loop_->cancelDelayed(filterTimer_);
        }
        if (modeControl_ != nullptr) {
            modeControl_->onSelectionChanged.remove(modeConnection_);
        }
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
        setProgress(0.0f);
        root_ = folder;
        cmake_.reset();
        products_ = std::make_shared<FileProducts>();
        problems_.reset();
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
        mode_ = mode;
        if (modeControl_ != nullptr) modeControl_->setSelectedIndex(static_cast<std::size_t>(mode));
        rebuild();
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

    void ProjectExplorer::rebuild()
    {
        if (model_ == nullptr) return;
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
                top = root_.empty() ? buildMessageTree("Open a folder or solution.") : buildSymbolsTree(*index_, text);
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
                top = buildMessageTree("Include, macro and template analysis are not available yet.");
                break;
        }
        model_->setRoot(std::move(top));
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
        if (found->model != nullptr) {
            cmake_ = found->model;
            buildDir_ = found->buildDir;
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
            collectProblems();
            rebuild();
        }
    }

    void ProjectExplorer::collectProblems()
    {
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
            index->setRoots({ root });
            if (first->model != nullptr) {
                // What the build really compiles a file with: its target's include folders and definitions, added to the
                // flags found the usual way (compile_commands.json), which may not cover a target (it did not cover
                // the tests, the codegen library, or the generated headers) and so miss a -D or a -I.
                auto settings = std::make_shared<const cmakemodel::CompileSettingsIndex>(*first->model);
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

            const unsigned cores = std::thread::hardware_concurrency();
            stats.indexThreads = std::max(1u, std::min<unsigned>(cores > 1 ? cores - 1 : 1, static_cast<unsigned>(std::max<std::size_t>(files.size(), 1))));
            const Clock::time_point indexStart = Clock::now();
            const cpptools::IndexProgress result = index->indexFiles(files,
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
