#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

// Global, like the other host callbacks (HostEditorBridge.h): it is part of the DLL's C ABI.
// path is only valid for the duration of the call - the host copies it before returning. line and
// column are 1-based, the column in UTF-16 units (what VS's text buffers count in, once it takes
// away the 1).
typedef void(__stdcall* HostOpenLocationCallback)(const wchar_t* path, std::size_t pathLength,
                                                  std::uint64_t line, std::uint64_t column);

namespace CodeToolsVsix
{
    // Where a native editor sends "open this file in an editor, at this line" when it is hosted in VS
    // (the Designer's Open in editor on a controller header problem): the managed host registers a
    // callback in the HostServices table (NativeEditControl_SetHost), and open() calls it. A fire-and-forget
    // request, like every call from the edit thread out to the host - the callback must return at once
    // and do the real work on VS's UI thread (see HostDocumentEditor.cs for why). With no host connected
    // (the testharness) open() reports false, and the caller falls back to something else.
    class HostLocationOpener
    {
    public:
        static HostLocationOpener& instance();

        // Null disconnects. From the host's own init thread.
        void setCallback(HostOpenLocationCallback callback) { callback_ = callback; }
        bool connected() const { return callback_.load() != nullptr; }

        // Asks the host to open path at line:column. False if no host is connected; true means the
        // request went out, not that the file opened.
        bool open(const std::wstring& path, std::size_t line, std::size_t column) const;

    private:
        std::atomic<HostOpenLocationCallback> callback_{ nullptr };
    };
}
