#pragma once

#include <cpptools/diagnostic.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "HostEditorBridge.h"
#include "HostLocationOpener.h"

// Matches Logging.h's LogSinkCallback. message is never assumed to be null-terminated -
// messageLength is authoritative.
using LogSinkCallback = void(__stdcall*)(cpptools::Severity severity, const wchar_t* message, size_t messageLength);

// Everything the host (the managed VS side) offers the native editors, handed over in ONE call -
// NativeEditControl_SetHost() - instead of an export per capability. Each member is a function pointer
// the host keeps alive for as long as it is registered, and may be null: that capability is then simply
// not available (the native side falls back, or does nothing).
//
// A versioned table, so a capability can be added without breaking an older host (or an older DLL):
//  * Members are only ever APPENDED, never reordered or changed.
//  * `size` is sizeof(HostServices) as the host was built. The DLL reads only that many bytes, so a host
//    built before a member existed leaves it null, and a newer host's extra members are ignored by an
//    older DLL.
//
// Every callback runs on the native edit thread and must return at once, doing any real work on VS's UI
// thread and answering later if it has to answer at all (see HostDocumentEditor.cs for why).
struct HostServices
{
    std::uint32_t size;

    // Receives this DLL's log lines (see Logging.h). Optional: without one, logging goes to stdout.
    LogSinkCallback logSink;

    // Read, or edit, a file that is open in VS's own text editor; answered later through
    // NativeEditControl_HostGetTextReply / HostApplyEditsReply (see HostEditorBridge.h). Both or neither.
    HostGetTextCallback getText;
    HostApplyEditsCallback applyEdits;

    // Open a file in an editor at a line - fire and forget (see HostLocationOpener.h).
    HostOpenLocationCallback openLocation;
};

namespace CodeToolsVsix
{
    // The table as this DLL understands it from whatever the host passed: a null pointer, or a `size`
    // of 0, gives everything null (disconnected); a `size` smaller than ours leaves the newer members
    // null; a larger one has its extra members ignored.
    inline HostServices copyHostServices(const HostServices* fromHost)
    {
        HostServices table{};
        if (fromHost != nullptr) {
            const std::size_t hostSize = fromHost->size;
            const std::size_t known = hostSize < sizeof(HostServices) ? hostSize : sizeof(HostServices);
            std::memcpy(&table, fromHost, known);
            table.size = static_cast<std::uint32_t>(known);
        }
        return table;
    }
}
