#pragma once
#include <Windows.h>
#include <cpptools/diagnostic.h>
#include <cstddef>
#include <string>

namespace CodeToolsVsix
{
    // This DLL's own logging entry point - relayed to a managed sink (see setManagedLogSink),
    // typically a real VS Output window pane (OutputWindowLogger.cs). cpptools::setLogSink is
    // wired (see ensureCpptoolsLogSinkRegistered in CppEditorControl.cpp) to call this too, so
    // cpptools's own internal logging (e.g. a parse failure) ends up here alongside this DLL's
    // own log calls. A no-op (besides the OutputDebugStringA... no, see below) if no sink is
    // registered yet.
    //
    // Deliberately does NOT call OutputDebugStringA, despite that looking like an "always safe,
    // any thread" baseline - it isn't. OutputDebugString(A/W) serializes through a process-wide
    // Win32 mutex (DBWIN_BUFFER_READY/DBWIN_DATA_READY) shared by every OutputDebugString caller
    // on the system, and is a documented real deadlock vector when called from a background
    // thread while the calling/UI thread is simultaneously blocked pumping messages for something
    // else - exactly EditThreadHost::runAndWait's own pump-wait, itself nested inside VS's own
    // JoinableTaskFactory.Run(). A real contributor to a NativeEditControl_Create hang
    // investigated 2026-08-29 - removing this call reduced but did not by itself eliminate the
    // hang; the other real contributor was EditThreadHost::pumpUntilSignaled() only draining
    // messages when MsgWaitForMultipleObjects claimed new input arrived, which isn't fully
    // trustworthy (see that function's own comment). Both fixed together: 10/10 Create calls
    // clean across a real stress test, 0/10 before.
    //
    // Safe to call from any thread, including EditThreadHost's dedicated one - but not by calling
    // the managed sink directly from there either. Calling a managed delegate from a thread the
    // CLR/VS runtime has never seen, while that same thread's caller is also pumping messages
    // waiting on the dedicated thread's own work, is its own separate, real deadlock risk
    // (confirmed live, 2026-08-29, a different incident from the OutputDebugStringA one above).
    // So: when called while newui::RunLoop::current() is non-null (i.e. on the dedicated thread),
    // this queues the managed sink call for later instead - see flushQueuedLogs(), which
    // runAndWait() itself calls right after rejoining the safe calling thread, so queued messages
    // still reach the Output window pane, just slightly deferred, from a thread that's always
    // safe to call the sink from.
    void log(cpptools::Severity severity, const std::string& message);

    // Dispatches every log message queued by a log() call made while on the dedicated thread
    // (see log()'s own comment) to the managed sink, then clears the queue. Must be called from
    // a thread that's safe to invoke the managed sink from - EditThreadHost::runAndWait() is the
    // only caller. A no-op if nothing is queued or no sink is registered.
    void flushQueuedLogs();

    // Matches NativeEditor.h's LogSinkCallback. Registered once by the managed host
    // (typically to relay log() calls into a real VS Output window pane) - optional; a host that
    // never calls NativeEditControl_SetLogSink just gets no logging at all (see log()'s own
    // comment on why this doesn't fall back to OutputDebugStringA).
    using LogSinkCallback = void(__stdcall*)(cpptools::Severity severity, const wchar_t* message, std::size_t messageLength);
    void setManagedLogSink(LogSinkCallback sink);

    void logToDebugOut(const std::wstring& message);
}
