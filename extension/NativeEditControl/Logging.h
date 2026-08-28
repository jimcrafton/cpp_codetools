#pragma once
#include <Windows.h>
#include <cpptools/diagnostic.h>
#include <cstddef>
#include <string>

namespace CodeToolsVsix
{
    // This DLL's own logging entry point - always available via OutputDebugStringA (visible in
    // VS's own Output window's "Debug" pane whenever this process is being debugged, e.g. F5'd
    // from another VS instance - the same mechanism System.Diagnostics.Debug.WriteLine uses on
    // the managed side), and optionally also relayed to a managed sink (see SetManagedLogSink)
    // for when nothing is attached to catch OutputDebugString. cpptools::setLogSink is wired
    // (see EnsureCpptoolsLogSinkRegistered in StandInEditControl.cpp) to call this too, so
    // cpptools's own internal logging (e.g. a parse failure) ends up here alongside this DLL's
    // own log calls.
    void Log(cpptools::Severity severity, const std::string& message);

    // Matches NativeEditControl.h's LogSinkCallback. Registered once by the managed host
    // (typically to relay Log() calls into a real VS Output window pane) - optional; a host that
    // never calls NativeEditControl_SetLogSink just gets OutputDebugStringA-only logging.
    using LogSinkCallback = void(__stdcall*)(cpptools::Severity severity, const wchar_t* message, std::size_t messageLength);
    void SetManagedLogSink(LogSinkCallback sink);
}
