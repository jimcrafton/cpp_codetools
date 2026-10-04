#include "DocumentTabs.h"
#include "TextEncoding.h"

#include <newui/layout.h>
#include <newui/rootview.h>

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
        std::string title = wideToUtf8(std::filesystem::path(path).filename().wstring());
        if (title.empty()) {
            title = wideToUtf8(path);
        }
        return dirty ? title + " *" : title;
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
        refreshTitles();
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
        std::unique_ptr<NativeEditor> editor = std::move(tabs_[index].editor);
        tabs_.erase(tabs_.begin() + static_cast<std::ptrdiff_t>(index));
        newui::SubView* page = tabControl_->removeTab(index);

        editor.reset();   // destroys the editor's window first - the page is only a placeholder for it
        if (page != nullptr) {
            page->destroy();
            delete page;
        }
        syncWindows();
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
            if (!current.empty()) {
                tab.path = current;
            }
            tab.page->setTitle(titleFor(tab.path, tab.editor != nullptr && tab.editor->isDirty()));
        }
    }

    newui::SyncReturn DocumentTabs::handleTabChanged(newui::TabControl&, std::size_t)
    {
        syncWindows();
        return newui::SyncReturn::Handled;
    }

    void DocumentTabs::syncWindows()
    {
        for (Tab& tab : tabs_) {
            tab.page->syncWindow();
        }
    }
}
