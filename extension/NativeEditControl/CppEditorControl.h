#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

// runloop.h must come before rootview.h/controls.h: some of their own inline code instantiates
// Delegate<...>::postCall(RunLoop&, ...) (delegate.h), which needs RunLoop's full definition,
// not just delegate.h's own forward declaration of it.
#include <newui/runloop.h>
#include <newui/rootview.h>
#include <newui/controls.h>

#include "NativeEditControl.h"

namespace CodeToolsVsix
{
    // Hosts a real newui::RootView (standalone - no Application/Frame, see newui's HANDOFF.md
    // Part 91) with one newui::TextControl child filling it, as a child of hwndParent. Replaces
    // StandInEditControl's hand-rolled Win32 window - see "win32 loop in VSIX.docx"
    // (D:\code\newui) for the hosting architecture this follows: this control's RootView lives
    // entirely on EditThreadHost's dedicated background thread, never on the caller's (VS's UI)
    // thread.
    //
    // Every public method here (aside from the constructor/destructor, which are only ever
    // called from EditThreadHost::RunAndWait() - see CppEditorControlApi's own wrappers in
    // NativeEditControlApi.cpp for where that marshaling actually happens) touches
    // RootView/TextControl state directly with no thread-safety of its own - callers are
    // responsible for only ever reaching this class from the edit thread.
    class CppEditorControl
    {
    public:
        // Constructs the RootView as a child of hwndParent filling (x, y, width, height). Must
        // be called on EditThreadHost's own thread. On success, windowHandle() returns the new
        // control's real HWND; on failure it stays null (check before use - the constructor
        // itself can't fail loudly, matching this DLL's existing "return null/false, don't
        // throw across the P/Invoke boundary" convention).
        CppEditorControl(HWND hwndParent, int x, int y, int width, int height);

        // Tears down the RootView's real HWND - must be called on EditThreadHost's own thread
        // (same as the constructor). Safe even if construction failed (windowHandle() was
        // already null).
        ~CppEditorControl();

        HWND windowHandle() const { return rootView_ ? rootView_->windowHandle() : nullptr; }

        // Reads filePath (UTF-8), sets it as the TextControl's text, and appends a cpptools
        // outline if parsing finds any symbols - same file-I/O/outline logic
        // StandInEditControl.cpp had, just writing into a real newui::TextControl instead of a
        // raw Win32 edit control. Returns false only on I/O failure.
        bool Load(const wchar_t* filePath, std::size_t filePathLength);

        // Writes the TextControl's current text to filePath (UTF-8). Returns false on I/O
        // failure. Note: this writes out the outline appended by Load() too if it's still
        // present in the buffer - same as the stand-in control's own pre-existing behavior
        // (WM_SETTEXT-backed storage had the same property) - not addressed by this phase, real
        // editing (which would let a user delete the outline like any other text) is what
        // ultimately makes this moot.
        bool Save(const wchar_t* filePath, std::size_t filePathLength);

        bool IsDirty() const { return dirty_; }

        // Generic stub dispatch, unchanged from StandInEditControl - every command just logs and
        // returns true; real per-command behavior is out of scope for this phase.
        bool ExecCommand(EditorCommand command, std::uint32_t flags, const EditorCommandArgs* args);

    private:
        std::unique_ptr<newui::RootView> rootView_;
        newui::TextControl* textControl_ = nullptr;  // owned by rootView_'s own child tree
        bool dirty_ = false;
    };
}
