#include "DocumentTabs.h"
#include "TextEncoding.h"

#include <newui/layout.h>
#include <newui/rootview.h>

#include <newui/runloop.h>
#include <newui/utils.h>

#include <cwchar>
#include <filesystem>

namespace CodeToolsVsix
{
    // A tab's page: an empty SubView whose only job is to keep its editor's child window exactly on
    // the page's rectangle (in the RootView's client coordinates) - shown while the page is the
    // selected one, hidden otherwise. A page can be moved without being resized (a Splitter divider
    // dragged), which no setBounds() would report, so it re-checks when painted too.
    class EditorPage : public newui::TabPage
    {
    public:
        void attach(NativeEditor* editor)
        {
            editor_ = editor;
            window_ = editor != nullptr ? editor->windowHandle() : nullptr;
            shown_ = false;
            if (window_ != nullptr) {
                ::ShowWindow(window_, SW_HIDE);   // a new child window starts at the origin, visible
            }
            syncWindow();
        }

        void setBounds(const newui::Rect& bounds) override
        {
            newui::TabPage::setBounds(bounds);
            syncWindow();
        }

        void setVisible(bool visible) override
        {
            newui::TabPage::setVisible(visible);
            syncWindow();
        }

        void paint(BLContext& ctx) override
        {
            newui::TabPage::paint(ctx);
            syncWindow();
        }

        void syncWindow()
        {
            if (window_ == nullptr) {
                return;
            }
            newui::RootView* root = rootView();
            const newui::Size size = bounds().size();
            if (root == nullptr || !isVisible() || size.width <= 0.0f || size.height <= 0.0f) {
                if (shown_) {
                    ::ShowWindow(window_, SW_HIDE);
                    shown_ = false;
                }
                return;
            }

            const newui::Point origin = root->accumulatedOffset(this);
            const newui::Rect target(origin, size);
            if (!shown_) {
                shown_ = true;
                ::ShowWindow(window_, SW_SHOWNOACTIVATE);
            }
            if (target == last_) {
                return;
            }
            last_ = target;
            editor_->setWindowBounds(target);
        }

    private:
        NativeEditor* editor_ = nullptr;
        HWND window_ = nullptr;
        bool shown_ = false;
        newui::Rect last_;
    };

    namespace
    {
        bool samePath(const std::wstring& a, const std::wstring& b)
        {
            const std::wstring left = std::filesystem::path(a).lexically_normal().wstring();
            const std::wstring right = std::filesystem::path(b).lexically_normal().wstring();
            return ::_wcsicmp(left.c_str(), right.c_str()) == 0;
        }
    }

    DocumentTabs::DocumentTabs(EditorFactory factory)
        : factory_(std::move(factory))
    {
        setVisible(true);

        auto layout = std::make_unique<newui::FlexLayout>(newui::Orientation::Vertical);
        layout->setSpacing(0.0f);
        layout->setPadding(0.0f);
        setLayout(std::move(layout));

        tabControl_ = new newui::TabControl();
        tabControl_->setName("documentTabs");
        tabControl_->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
        tabControl_->onTabChanged.add(this, &DocumentTabs::handleTabChanged);
        addChild(tabControl_);
    }

    DocumentTabs::~DocumentTabs()
    {
        closeAll();
    }

    std::string DocumentTabs::titleFor(const std::wstring& path, bool dirty)
    {
        return titleFor(path, dirty, DiskState::InSync);
    }

    std::string DocumentTabs::titleFor(const std::wstring& path, bool dirty, DiskState state)
    {
        std::string title = wideToUtf8(std::filesystem::path(path).filename().wstring());
        if (title.empty()) {
            title = wideToUtf8(path);
        }
        if (dirty) title += " *";
        if (state == DiskState::Changed) title += " (changed on disk)";
        if (state == DiskState::Deleted) title += " (deleted)";
        return title;
    }

    DocumentTabs::DiskStamp DocumentTabs::stampOf(const std::wstring& path)
    {
        DiskStamp stamp;
        std::error_code ec;
        const std::filesystem::path file(path);
        if (!std::filesystem::is_regular_file(file, ec)) return stamp;
        stamp.size = std::filesystem::file_size(file, ec);
        stamp.time = static_cast<std::int64_t>(std::filesystem::last_write_time(file, ec).time_since_epoch().count());
        stamp.exists = !ec;
        return stamp;
    }

    DocumentTabs::DiskState DocumentTabs::diskStateAt(std::size_t index) const
    {
        return index < tabs_.size() ? tabs_[index].state : DiskState::InSync;
    }

    void DocumentTabs::watchTab(Tab& tab)
    {
        if (watcher_ == nullptr) {
            newui::RunLoop& loop = newui::RunLoop::current();
            if (!loop) return;   // no loop pumping (a test): nothing to deliver to
            watcher_ = std::make_unique<newui::FileWatcher>(loop);
            watcher_->onChanged.add(this, &DocumentTabs::handleDiskChanges);
        }
        newui::FileWatcher::Options options;
        options.debounce = std::chrono::milliseconds(150);
        tab.watchId = watcher_->watchFile(newui::wideToUtf8(tab.path), options);
    }

    void DocumentTabs::unwatchTab(Tab& tab)
    {
        if (watcher_ != nullptr && tab.watchId != newui::FileWatcher::kInvalidWatch) watcher_->unwatch(tab.watchId);
        tab.watchId = newui::FileWatcher::kInvalidWatch;
    }

    newui::SyncReturn DocumentTabs::handleDiskChanges(newui::FileWatcher&, const newui::FileWatcher::Changes& changes)
    {
        applyDiskChanges(changes);
        return newui::SyncReturn::Handled;
    }

    void DocumentTabs::applyDiskChanges(const newui::FileWatcher::Changes& changes)
    {
        if (applyingDiskChanges_) return;
        applyingDiskChanges_ = true;
        for (const newui::FileWatcher::Change& change : changes) {
            const std::wstring path = newui::utf8ToWide(change.path);
            for (Tab& tab : tabs_) {
                if (samePath(tab.path, path)) {
                    reactToDiskChange(tab);
                    break;
                }
            }
        }
        applyingDiskChanges_ = false;
        refreshTitles();
    }

    void DocumentTabs::editorSaved(NativeEditor* editor)
    {
        for (Tab& tab : tabs_) {
            if (tab.editor.get() != editor) continue;
            const std::wstring current = editor->currentPath();
            if (!current.empty()) tab.path = current;   // a Save As: this is the file just written
            tab.stamp = stampOf(tab.path);
            tab.state = DiskState::InSync;
        }
        refreshTitles();
    }

    void DocumentTabs::reactToDiskChange(Tab& tab)
    {
        const DiskStamp now = stampOf(tab.path);
        if (now == tab.stamp) return;   // the editor's own save, or already dealt with
        if (!now.exists) {
            tab.stamp = now;
            tab.state = DiskState::Deleted;   // the text stays; saving writes it out again
            return;
        }
        if (tab.editor->isDirty()) {
            const bool reload = changedOnDiskHandler_ && changedOnDiskHandler_(tab.path);
            if (!reload) {
                tab.stamp = now;
                tab.state = DiskState::Changed;
                return;
            }
        }
        const bool loaded = tab.editor->load(tab.path.c_str(), tab.path.size());
        tab.stamp = now;
        tab.state = loaded ? DiskState::InSync : DiskState::Changed;
    }

    NativeEditor* DocumentTabs::open(const std::wstring& path, DocumentType type)
    {
        if (path.empty()) {
            return nullptr;
        }
        for (std::size_t i = 0; i < tabs_.size(); ++i) {
            if (samePath(tabs_[i].path, path)) {
                tabControl_->selectTab(i);
                return tabs_[i].editor.get();
            }
        }

        newui::RootView* root = rootView();
        std::unique_ptr<NativeEditor> editor = factory_(type, root != nullptr ? root->windowHandle() : nullptr);
        if (!editor) {
            return nullptr;
        }

        auto* page = new EditorPage();
        page->setTitle(titleFor(path, false));
        page->attach(editor.get());
        NativeEditor* raw = editor.get();
        raw->setStateChangedHandler([this]() { refreshTitles(); });
        tabs_.push_back(Tab{ path, type, std::move(editor), page });
        tabControl_->addTab(titleFor(path, false), page);
        tabControl_->selectTab(tabs_.size() - 1);

        // Loaded once the tab is in place and laid out, so an editor that sizes itself from its window
        // sees its real size.
        if (!raw->load(path.c_str(), path.size())) {
            close(tabs_.size() - 1);
            return nullptr;
        }
        tabs_.back().stamp = stampOf(path);
        watchTab(tabs_.back());
        refreshTitles();
        notifyActiveSource();
        return raw;
    }

    NativeEditor* DocumentTabs::openAt(const std::wstring& path, DocumentType type, std::size_t line, std::size_t column)
    {
        NativeEditor* editor = open(path, type);
        if (editor == nullptr) {
            return nullptr;
        }
        const std::wstring position = std::to_wstring(line) + L":" + std::to_wstring(column > 0 ? column : 1);
        EditorCommandArgs args{};
        args.text1 = position.c_str();
        args.text1Length = position.size();
        editor->execCommand(EditorCommand::GotoLine, 0, &args);
        return editor;
    }

    NativeEditor* DocumentTabs::activeEditor() const
    {
        return editorAt(tabControl_->selectedIndex());
    }

    NativeEditor* DocumentTabs::editorAt(std::size_t index) const
    {
        return index < tabs_.size() ? tabs_[index].editor.get() : nullptr;
    }

    std::optional<DocumentType> DocumentTabs::activeType() const
    {
        const std::size_t index = tabControl_->selectedIndex();
        if (index >= tabs_.size()) {
            return std::nullopt;
        }
        return tabs_[index].type;
    }

    const std::wstring& DocumentTabs::activePath() const
    {
        static const std::wstring none;
        const std::size_t index = tabControl_->selectedIndex();
        return index < tabs_.size() ? tabs_[index].path : none;
    }

    bool DocumentTabs::close(std::size_t index)
    {
        if (index >= tabs_.size()) {
            return false;
        }
        unwatchTab(tabs_[index]);
        std::unique_ptr<NativeEditor> editor = std::move(tabs_[index].editor);
        tabs_.erase(tabs_.begin() + static_cast<std::ptrdiff_t>(index));
        newui::SubView* page = tabControl_->removeTab(index);

        editor.reset();   // destroys the editor's window first - the page is only a placeholder for it
        if (page != nullptr) {
            page->destroy();
            delete page;
        }
        syncWindows();
        notifyActiveSource();
        return true;
    }

    bool DocumentTabs::closeActive()
    {
        return close(tabControl_->selectedIndex());
    }

    void DocumentTabs::closeAll()
    {
        while (!tabs_.empty()) {
            close(tabs_.size() - 1);
        }
    }

    void DocumentTabs::refreshTitles()
    {
        for (Tab& tab : tabs_) {
            // A Save As inside the editor moves the document to a new file; the tab follows it.
            const std::wstring current = tab.editor != nullptr ? tab.editor->currentPath() : std::wstring();
            if (!current.empty() && !samePath(current, tab.path)) {
                unwatchTab(tab);
                tab.path = current;
                tab.stamp = stampOf(tab.path);
                tab.state = DiskState::InSync;
                watchTab(tab);
            }
            const bool dirty = tab.editor != nullptr && tab.editor->isDirty();
            if (tab.wasDirty && !dirty) {   // saved (or reloaded): the file is now what the editor holds
                tab.stamp = stampOf(tab.path);
                tab.state = DiskState::InSync;
            }
            tab.wasDirty = dirty;
            tab.page->setTitle(titleFor(tab.path, dirty, tab.state));
        }
    }

    newui::SyncReturn DocumentTabs::handleTabChanged(newui::TabControl&, std::size_t)
    {
        syncWindows();
        notifyActiveSource();
        return newui::SyncReturn::Handled;
    }

    void DocumentTabs::notifyActiveSource()
    {
        if (!activeSourceHandler_ || tabs_.empty()) return;
        const std::optional<DocumentType> type = activeType();
        if (type.has_value() && *type == DocumentType::CppSource) activeSourceHandler_(activePath());
    }

    void DocumentTabs::syncWindows()
    {
        for (Tab& tab : tabs_) {
            tab.page->syncWindow();
        }
    }
}
