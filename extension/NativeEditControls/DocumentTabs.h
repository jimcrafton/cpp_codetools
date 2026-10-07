#pragma once

#include "NativeEditor.h"

#include <newui/controls.h>
#include <newui/filewatcher.h>
#include <newui/subview.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace CodeToolsVsix
{
    class EditorPage;

    // Several documents open at once, one tab each (the testharness's stand-in for VS's own document
    // tabs). Every tab's editor lives in a RootView of its own, in a child window of this control's
    // RootView - the way VS hosts each editor - rather than all of them sharing one root: the editors
    // hook their root's mouse/key delegates and the Designer owns its root's one Overlay, so two
    // sharing a root would trample each other (and a closed one would leave handlers behind).
    //
    // Each tab's page keeps its editor's window on its own rectangle (see EditorPage in the .cpp);
    // that is native child-window hosting, so it is sized when the page is laid out or painted.
    class DocumentTabs : public newui::SubView
    {
    public:
        // Makes the editor for one tab. The real one (testharness.cpp) builds a CppEditor/DesignerEditor
        // in a child window of `parent`; a test passes one that needs no window. Null = couldn't.
        using EditorFactory = std::function<std::unique_ptr<NativeEditor>(DocumentType type, HWND parent)>;

        explicit DocumentTabs(EditorFactory factory);
        ~DocumentTabs() override;

        // Opens `path` in a new tab and selects it - or just selects the tab already showing it
        // (compared without regard to case or "." / ".." segments). Null if the editor couldn't be
        // created or the file couldn't be loaded (no tab is left behind then).
        NativeEditor* open(const std::wstring& path, DocumentType type);

        // open(), then puts the caret at line:column (1-based) - what a jump to a problem does. A
        // file that is already open is just selected and moved. The editor is still returned if it
        // couldn't place the caret (a position past the end of the text, an editor with no such
        // command); null only when the file couldn't be opened at all.
        NativeEditor* openAt(const std::wstring& path, DocumentType type, std::size_t line, std::size_t column);

        std::size_t count() const { return tabs_.size(); }
        // The selected tab's editor/type/path - null / nullopt / empty when no tab is open.
        NativeEditor* activeEditor() const;
        std::optional<DocumentType> activeType() const;
        const std::wstring& activePath() const;
        NativeEditor* editorAt(std::size_t index) const;

        // Closes a tab (its editor is destroyed, the neighbour is selected). False if out of range.
        bool close(std::size_t index);
        bool closeActive();
        void closeAll();

        // Re-labels every tab: the file name, plus " *" while its editor has unsaved changes. Call
        // after anything that can change that (a save, an edit's dirty flag turning on).
        void refreshTitles();

        // Called with the path of a C++ source whenever it becomes the selected tab (opened, switched to, or what is left
        // after a close); a designer tab does not call it. What the explorer's Macros view follows.
        using ActiveSourceHandler = std::function<void(const std::wstring& path)>;
        void setActiveSourceHandler(ActiveSourceHandler handler) { activeSourceHandler_ = std::move(handler); }

        newui::TabControl* tabControl() const { return tabControl_; }

        // A tab's file against what is on disk: the editor shows it as loaded or saved (InSync), the file has
        // since been changed by something else and the editor kept its own text (Changed), or it is gone (Deleted).
        enum class DiskState { InSync, Changed, Deleted };
        DiskState diskStateAt(std::size_t index) const;

        // Asked when a tab's file changes on disk while its editor has unsaved edits: true reloads it (the edits are
        // lost), false keeps them. With no handler they are kept. A tab without edits is simply reloaded.
        using ChangedOnDiskHandler = std::function<bool(const std::wstring& path)>;
        void setChangedOnDiskHandler(ChangedOnDiskHandler handler) { changedOnDiskHandler_ = std::move(handler); }

        // What the file watcher reports, applied to the tabs. The watcher (one for every open file, once a run
        // loop is pumping) calls this itself; it is public so a test can hand it changes. A change is acted on
        // only if the file really differs from what the tab last loaded or saved, so the editor's own save is not
        // taken for someone else's.
        void applyDiskChanges(const newui::FileWatcher::Changes& changes);

        // Tells the tabs `editor` has just written its file, so the change that write causes on disk is its own.
        // Call it after a save (the editors do not all report one), then the titles are refreshed too.
        void editorSaved(NativeEditor* editor);

        // Where a tab's label and dirty marker come from - exposed for tests.
        static std::string titleFor(const std::wstring& path, bool dirty);
        static std::string titleFor(const std::wstring& path, bool dirty, DiskState state);

    private:
        // What a file looked like on disk (modification time and size); two equal stamps mean it did not change.
        struct DiskStamp
        {
            bool exists = false;
            std::uintmax_t size = 0;
            std::int64_t time = 0;
            bool operator==(const DiskStamp& other) const { return exists == other.exists && size == other.size && time == other.time; }
        };

        struct Tab
        {
            std::wstring path;
            DocumentType type = DocumentType::CppSource;
            std::unique_ptr<NativeEditor> editor;
            EditorPage* page = nullptr;   // owned by tabControl_
            newui::FileWatcher::WatchId watchId = newui::FileWatcher::kInvalidWatch;
            DiskStamp stamp;              // the file as the editor last loaded or saved it
            DiskState state = DiskState::InSync;
            bool wasDirty = false;
        };

        static DiskStamp stampOf(const std::wstring& path);
        void watchTab(Tab& tab);          // starts following the tab's file (once a run loop is pumping)
        void unwatchTab(Tab& tab);
        void reactToDiskChange(Tab& tab);
        newui::SyncReturn handleDiskChanges(newui::FileWatcher& sender, const newui::FileWatcher::Changes& changes);

        newui::SyncReturn handleTabChanged(newui::TabControl& sender, std::size_t index);
        void syncWindows();
        void notifyActiveSource();

        EditorFactory factory_;
        ActiveSourceHandler activeSourceHandler_;
        ChangedOnDiskHandler changedOnDiskHandler_;
        std::unique_ptr<newui::FileWatcher> watcher_;   // created with the first open file, if a loop is pumping
        bool applyingDiskChanges_ = false;              // a prompt is up: its own events are not nested inside it
        newui::TabControl* tabControl_ = nullptr;
        std::vector<Tab> tabs_;   // index-aligned with tabControl_'s tabs
    };
}
