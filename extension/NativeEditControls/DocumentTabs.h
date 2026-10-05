#pragma once

#include "NativeEditor.h"

#include <newui/controls.h>
#include <newui/subview.h>

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

        // Where a tab's label and dirty marker come from - exposed for tests.
        static std::string titleFor(const std::wstring& path, bool dirty);

    private:
        struct Tab
        {
            std::wstring path;
            DocumentType type = DocumentType::CppSource;
            std::unique_ptr<NativeEditor> editor;
            EditorPage* page = nullptr;   // owned by tabControl_
        };

        newui::SyncReturn handleTabChanged(newui::TabControl& sender, std::size_t index);
        void syncWindows();
        void notifyActiveSource();

        EditorFactory factory_;
        ActiveSourceHandler activeSourceHandler_;
        newui::TabControl* tabControl_ = nullptr;
        std::vector<Tab> tabs_;   // index-aligned with tabControl_'s tabs
    };
}
