#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>

#include <newui/controls.h>
#include <newui/textfolding.h>

#include <memory>

#include "CppDiagnostics.h"
#include "DocumentEditService.h"
#include "FindReplaceController.h"
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
    class CppEditor : public NativeEditor, public IEditableDocument
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
        ~CppEditor() override;

        // Reads filePath (UTF-8), sets it as the editable TextControl's text, and separately
        // populates the read-only outline pane with a cpptools outline if parsing finds any
        // symbols - same file-I/O/outline logic StandInEditControl.cpp had, just writing into
        // two real newui::TextControls instead of one raw Win32 edit control with the outline
        // appended as trailing text. Returns false only on I/O failure.
        bool load(const wchar_t* filePath, std::size_t filePathLength) override;

        // Writes the editable TextControl's current text to filePath (UTF-8) - never the outline
        // pane, which is a separate, read-only control. Returns false on I/O failure.
        bool save(const wchar_t* filePath, std::size_t filePathLength) override;

        // Editing commands act on the source pane (false when there is nothing to undo / redo / copy /
        // paste). Find / Replace / GotoLine open the Find and Go to line overlays (a GotoLine that names a
        // line goes straight there; text the command carries seeds the search). Anything else is logged.
        bool execCommand(EditorCommand command, std::uint32_t flags, const EditorCommandArgs* args) override;

		// contentHost: see the constructor's own comment above.
		bool setupUI(newui::RootView* root, newui::SubView* contentHost = nullptr);

        // IEditableDocument, so other editors (the Designer) can edit this file's live text through
        // documentEditService() - registered under the path of the last load()/save(). Text and edit
        // offsets are UTF-16, the model's own. All edits of one call are a single undo step; the
        // snapshot's version changes on every change to the text.
        DocumentSnapshot snapshot() const override;
        EditStatus applyEdits(std::uint64_t expectedVersion, const std::vector<TextEdit>& edits) override;

        // The editable source's control (a TextFoldingControl: line numbers, folding, colors), the
        // ScrollView that scrolls it, and the read-only outline's control - for tests.
        newui::TextFoldingControl* textControl() const { return textControl_; }
        newui::ScrollView* scrollView() const { return scrollView_; }
        newui::TextControl* outlineControl() const { return outlineControl_; }
        // Find / Replace / Go to line (null if construction failed) - for tests.
        FindReplaceController* findReplace() const { return find_.get(); }
    private:
        std::uint64_t editVersion_ = 1;
        std::filesystem::path registeredPath_;
        void registerForEdits(const std::wstring& path);

        // Each pane is a text control hosted by a ScrollView, which scrolls it (a TextControl has no
        // scrollbar of its own). All owned by the base's RootView child tree.
        newui::ScrollView* scrollView_ = nullptr;
        newui::TextFoldingControl* textControl_ = nullptr;   // editable source; keeps its edits for undo
        newui::ScrollView* outlineScroll_ = nullptr;
        newui::TextControl* outlineControl_ = nullptr;       // read-only outline
        // Colors, folds and syntax-error squiggles for textControl_, computed off the UI thread.
        std::unique_ptr<HighlightController> highlight_;
        std::shared_ptr<CppDocument> document_;   // what the parse thinks the text is (its path), shared with the worker

        // host as passed to setupUI() (root itself, or a caller-supplied contentHost - see
        // setupUI()'s own comment) - kept so load() can size/position loadingProgress_ against it;
        // nothing else here previously needed to remember it.
        newui::View* host_ = nullptr;

        // A thin, indeterminate progress bar across the top of the editor, shown for however long
        // the first background parse of a freshly loaded file takes (see load()'s own comment on
        // why that can't run synchronously) and hidden once it lands - there's no way to know real
        // percent-complete from an opaque libclang parse, so this just animates to say "working",
        // the same "at least show it's not locked up" ask this exists for in the first place.
        newui::Progress* loadingProgress_ = nullptr;
        newui::RunLoop::TimerHandle loadingProgressTimer_ = newui::RunLoop::kInvalidTimerHandle;
        float loadingProgressPhase_ = 0.0f;   // radians; drives a smooth 0<->1 back-and-forth sweep
        void startLoadingAnimation();
        void stopLoadingAnimation();
        // Find / Replace / Go to line overlays over the editor. Declared after highlight_ (which it feeds
        // matches to) so it is destroyed first.
        std::unique_ptr<FindReplaceController> find_;

        // Shows a new outline in the read-only pane (unchanged: left alone).
        void setOutlineText(const std::wstring& outline);

        // Ctrl+F / Ctrl+H / Ctrl+G, for a host that doesn't route those commands to execCommand() itself
        // (VS does, through the managed Exec; a plain newui window such as testharness does not). Subscribed
        // to the root's onKeyDown, which fires for every key before the focused control sees it - the same
        // way the designer's own hot keys are hooked.
        newui::SyncReturn handleKeyDown(newui::View& sender, std::uint32_t keyMask, int keyCharVal,
            int repeatCount, std::uint32_t VKeyCode);
        newui::Connection keyConnection_;

        // Re-resolves the symbol-occurrence highlight (semantic, via cpptools) for wherever the
        // caret now is, every time it moves. Subscribed to textControl_'s own caret, not any
        // higher-level "the document changed" event - this needs to react to a plain caret move
        // with no edit at all (e.g. arrow keys, clicking around).
        newui::SyncReturn handleCaretMoved(newui::text::Caret& sender);
        newui::Connection caretConnection_;
    };
}
