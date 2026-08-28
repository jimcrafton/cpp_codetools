#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <string>

#include "NativeEditControl.h"

namespace CodeToolsVsix
{
    // A minimal native Win32 window that stands in for a future, more capable text-editing
    // control. It registers its own window class, relies entirely on the OS's default
    // WM_SETTEXT/WM_GETTEXT handling (via DefWindowProc) as its text storage - no editing UI of
    // its own yet - and paints that text (plus an outline, when Load parsed one) on WM_PAINT.
    //
    // Load/Save own their own file I/O and, for Load, the cpptools::Parser call - see
    // StandInEditControl.cpp. No document content is ever pushed in from outside; the managed
    // host (NativeEditHost.cs) only ever calls Load(path)/Save(path)/IsDirty() and reads back a
    // pass/fail result, never text or symbols. Growing this into a real editor means adding
    // keyboard/mouse handling and an actual text buffer to HandleMessage()/HandlePaint(); Create()
    // and the rest of the window lifecycle are meant to keep working unchanged.
    class StandInEditControl
    {
    public:
        // Registers the window class if it hasn't been already. Safe to call more than once.
        static bool RegisterWindowClass(HINSTANCE hInstance);

        // Creates a stand-in control as a child of hwndParent filling (x, y, width, height).
        // Returns the new control's HWND, or nullptr on failure. The instance manages its own
        // lifetime (it deletes itself on WM_NCDESTROY), so callers just DestroyWindow() the HWND
        // like any other window when they're done with it.
        static HWND Create(HWND hwndParent, int x, int y, int width, int height, HINSTANCE hInstance);

        // Looks up the StandInEditControl instance behind hwnd, or nullptr if hwnd isn't one of
        // ours. Used both internally (WndProc) and by NativeEditControlApi.cpp's exported
        // Load/Save/IsDirty wrappers to reach the instance from a bare HWND.
        static StandInEditControl* FromHandle(HWND hwnd);

        // Reads filePath (UTF-8) and sets it as the control's text. If parsing it with
        // cpptools::Parser finds any symbols, an outline is appended to the display too. Returns
        // false only on I/O failure (missing/unreadable file); a parse failure just means no
        // outline, not an overall failure. filePathLength is filePath's length in wchar_t
        // characters, not counting a null terminator - filePath is never assumed to be
        // null-terminated (see NativeEditControl.h).
        bool Load(const wchar_t* filePath, std::size_t filePathLength);

        // Writes the control's current text buffer to filePath (UTF-8). Returns false on I/O
        // failure. Same filePathLength contract as Load.
        bool Save(const wchar_t* filePath, std::size_t filePathLength);

        bool IsDirty() const { return m_dirty; }

        // Generic stub dispatch for the editing commands in EditorCommand (see
        // NativeEditControl.h for the full contract). Every command today just logs via
        // OutputDebugStringW and returns true; real per-command behavior lands here once there's
        // something to act on. args may be null.
        bool ExecCommand(EditorCommand command, std::uint32_t flags, const EditorCommandArgs* args);

    private:
        StandInEditControl() = default;

        static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
        LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
        void HandlePaint(HWND hwnd);

        HWND m_hwnd = nullptr;
        std::wstring m_annotation;
        bool m_dirty = false;

        static const wchar_t kClassName[];
    };
}
