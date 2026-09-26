#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>

#include <newui/controls.h>
#include <newui/textfolding.h>

#include <memory>

#include "CppDiagnostics.h"
#include "HighlightController.h"
#include "NativeEditor.h"

namespace CodeToolsVsix
{
    // Hosts a real newui::RootView (standalone - no Application/Frame, see newui's HANDOFF.md
    // Part 91) with two scrolled text panes stacked vertically, as a child of hwndParent:
    // the editable source buffer (textControl_, most of the space - syntax highlighted, foldable,
    // with undo) and a read-only outline pane
    // below it (outlineControl_) - kept as two separate controls, not one buffer with the outline
    // text appended, specifically so save() only ever writes real source text (see load()'s own
    // comment). Replaces StandInEditControl's hand-rolled Win32 window - see "win32 loop in
    // VSIX.docx" (D:\code\newui) for the hosting architecture this follows: this control's
    // RootView lives entirely on EditThreadHost's dedicated background thread, never on the
    // caller's (VS's UI) thread.
    //
    // The VS-communication contract itself (windowHandle()/isDirty()/RootView storage/teardown)
    // lives in EditorControlBase - shared with whatever a future visual designer editor type
    // ends up being; this class only adds the C++-source-specific content (the two TextControls)
    // and the load/save/execCommand behavior that goes with them.
    //
    // Every public method here (aside from the constructor, which is only ever called from
    // EditThreadHost::runAndWait() - see NativeEditControlApi.cpp's own wrappers for where that
    // marshaling actually happens) touches RootView/TextControl state directly with no
    // thread-safety of its own - callers are responsible for only ever reaching this class from
    // the edit thread.
    class CppEditor : public NativeEditor
    {
    public:
        // Constructs the RootView as a child of hwndParent filling (x, y, width, height). Must
        // be called on EditThreadHost's own thread. On success, windowHandle() returns the new
        // control's real HWND; on failure it stays null (check before use - the constructor
        // itself can't fail loudly, matching this DLL's existing "return null/false, don't
        // throw across the P/Invoke boundary" convention).
        CppEditor(HWND hwndParent, int x, int y, int width, int height);

        // contentHost: see NativeEditManager::createEditor()'s own comment - nullptr (default)
        // means the two TextControls below are added directly to rootView, matching every
        // pre-existing caller.
        CppEditor(newui::RootView* rootView, newui::SubView* contentHost = nullptr);

        // Reads filePath (UTF-8), sets it as the editable TextControl's text, and separately
        // populates the read-only outline pane with a cpptools outline if parsing finds any
        // symbols - same file-I/O/outline logic StandInEditControl.cpp had, just writing into
        // two real newui::TextControls instead of one raw Win32 edit control with the outline
        // appended as trailing text. Returns false only on I/O failure.
        bool load(const wchar_t* filePath, std::size_t filePathLength) override;

        // Writes the editable TextControl's current text to filePath (UTF-8) - never the outline
        // pane, which is a separate, read-only control. Returns false on I/O failure.
        bool save(const wchar_t* filePath, std::size_t filePathLength) override;

        // Generic stub dispatch, unchanged from StandInEditControl - every command just logs and
        // returns true; real per-command behavior is out of scope for this phase.
        bool execCommand(EditorCommand command, std::uint32_t flags, const EditorCommandArgs* args) override;

		// contentHost: see the constructor's own comment above.
		bool setupUI(newui::RootView* root, newui::SubView* contentHost = nullptr);

        // The editable source's control (a TextFoldingControl: line numbers, folding, colors), the
        // ScrollView that scrolls it, and the read-only outline's control - for tests.
        newui::TextFoldingControl* textControl() const { return textControl_; }
        newui::ScrollView* scrollView() const { return scrollView_; }
        newui::TextControl* outlineControl() const { return outlineControl_; }
    private:
        // Each pane is a text control hosted by a ScrollView, which scrolls it (a TextControl has no
        // scrollbar of its own). All owned by the base's RootView child tree.
        newui::ScrollView* scrollView_ = nullptr;
        newui::TextFoldingControl* textControl_ = nullptr;   // editable source; keeps its edits for undo
        newui::ScrollView* outlineScroll_ = nullptr;
        newui::TextControl* outlineControl_ = nullptr;       // read-only outline
        // Colors, folds and syntax-error squiggles for textControl_, computed off the UI thread.
        std::unique_ptr<HighlightController> highlight_;
        std::shared_ptr<CppDocument> document_;   // what the parse thinks the text is (its path), shared with the worker

        // Shows a new outline in the read-only pane (unchanged: left alone).
        void setOutlineText(const std::wstring& outline);
    };
}
