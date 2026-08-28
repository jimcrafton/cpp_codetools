#pragma once
#include <Windows.h>
#include <cpptools/diagnostic.h>
#include <cstddef>
#include <cstdint>

#ifdef NATIVEEDITCONTROL_EXPORTS
#define NATIVEEDITCONTROL_API extern "C" __declspec(dllexport)
#else
#define NATIVEEDITCONTROL_API extern "C" __declspec(dllimport)
#endif

// The whole native<->managed boundary for this control: create it, tell it a path to load from
// or save to, ask if it's dirty, destroy it. No document content (text, symbols, or otherwise)
// ever crosses this boundary - the control owns its own file I/O and, for C/C++ source files,
// its own call into cpptools::Parser (see StandInEditControl.cpp) entirely on the native side.
// The managed host (NativeEditHost.cs) only ever sees an HWND and pass/fail booleans.
//
// filePath/filePathLength: filePathLength is the number of wchar_t characters in filePath, not
// counting a null terminator - required explicitly rather than relying on filePath being
// null-terminated. .NET's P/Invoke marshaler does null-terminate a `string` parameter, but this
// is an exported native entry point another caller could reach without that guarantee, so nothing
// here scans for a terminator in an untrusted buffer.

// Creates a new StandInEditControl as a child of hwndParent, filling (x, y, width, height).
// Returns the control's HWND, or nullptr on failure.
NATIVEEDITCONTROL_API HWND __stdcall NativeEditControl_Create(
    HWND hwndParent, int x, int y, int width, int height, HINSTANCE hInstance);

// Reads filePath from disk (UTF-8), parses it with cpptools::Parser if it looks like a C/C++
// source file, and populates the control's display - all in native code. Returns FALSE if the
// file couldn't be read; a parse failure alone doesn't fail this (it just means no outline).
NATIVEEDITCONTROL_API BOOL __stdcall NativeEditControl_Load(
    HWND hwnd, const wchar_t* filePath, size_t filePathLength);

// Writes the control's current text buffer to filePath (UTF-8). Returns FALSE on I/O failure.
NATIVEEDITCONTROL_API BOOL __stdcall NativeEditControl_Save(
    HWND hwnd, const wchar_t* filePath, size_t filePathLength);

// TRUE if the control's content has changed since the last Load/Save. Always FALSE today - this
// stand-in control has no keyboard/mouse editing yet (see StandInEditControl.h) - but the export
// exists now so IVsPersistDocData.IsDocDataDirty (CodeToolsEditorPane.cs) never needs to change
// once real editing is added.
NATIVEEDITCONTROL_API BOOL __stdcall NativeEditControl_IsDirty(HWND hwnd);

// Generic dispatch for editor-level editing commands that VS's IOleCommandTarget world routes
// through - see CodeToolsEditorPane.Exec (managed) for where these actually come from. One shared
// entry point rather than a separate native export per command: the command set is expected to
// grow, and what differs between commands is mainly which of EditorCommandArgs's fields matter,
// not the call shape itself.
enum class EditorCommand : uint32_t
{
    Undo = 0,
    Redo = 1,
    Cut = 2,
    Copy = 3,
    Paste = 4,
    Find = 5,
    Replace = 6,
    GotoLine = 7,
};

// A deliberately generic, fixed-shape payload - the closest a P/Invoke-crossable C ABI gets to
// something like std::any (which itself cannot cross this boundary at all: it has no fixed
// layout, so the managed side would have no way to know what's in it). Which fields are
// meaningful depends on command: Find uses text1(Length) (the search text); Replace uses both
// text1(Length) (search) and text2(Length) (replacement); GotoLine uses number (the target line,
// 1-based); Undo/Redo/Cut/Copy/Paste use none. text1/text2 are never assumed to be
// null-terminated - same contract as NativeEditControl_Load/Save's filePath - the length fields
// are authoritative.
struct EditorCommandArgs
{
    const wchar_t* text1;
    size_t text1Length;
    const wchar_t* text2;
    size_t text2Length;
    long long number;
};

// Dispatches command to the control behind hwnd. Returns FALSE if hwnd isn't one of ours;
// otherwise TRUE, regardless of whether the specific command has a real implementation yet - see
// StandInEditControl::ExecCommand, where today every command is a logged no-op stub. args may be
// null (equivalent to an all-zero EditorCommandArgs); flags is a reserved bitmask, currently
// unused by any command (a real future use: MatchCase/WholeWord modifiers on Find).
NATIVEEDITCONTROL_API BOOL __stdcall NativeEditControl_ExecCommand(
    HWND hwnd, EditorCommand command, uint32_t flags, const EditorCommandArgs* args);

// Registers a sink this DLL relays its own log lines to, in addition to the always-on
// OutputDebugStringA baseline (see Logging.h) - typically used by the managed host to surface
// logging in a real UI (e.g. a VS Output window pane) for the case where nothing is attached to
// catch OutputDebugString. Optional: never calling this just means OutputDebugStringA-only
// logging. message is never assumed to be null-terminated (see this header's own note on
// filePath above) - messageLength is authoritative. Pass nullptr to unregister.
using LogSinkCallback = void(__stdcall*)(cpptools::Severity severity, const wchar_t* message, size_t messageLength);
NATIVEEDITCONTROL_API void __stdcall NativeEditControl_SetLogSink(LogSinkCallback sink);
