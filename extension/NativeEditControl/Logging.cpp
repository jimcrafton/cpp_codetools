#include "Logging.h"
#include "TextEncoding.h"

#include <newui/runloop.h>

#include <mutex>
#include <utility>
#include <vector>

namespace CodeToolsVsix
{
    namespace
    {
        LogSinkCallback g_managedSink = nullptr;

        // Guards g_queuedLogs - log() can queue into it from the dedicated thread while
        // flushQueuedLogs() drains it from the calling thread; each individual call is quick
        // (append/swap a small vector), so a plain mutex is fine, no need for anything fancier.
        std::mutex g_queueMutex;
        std::vector<std::pair<cpptools::Severity, std::wstring>> g_queuedLogs;

        const char* SeverityLabel(cpptools::Severity severity)
        {
            switch (severity)
            {
            case cpptools::Severity::Note: return "NOTE";
            case cpptools::Severity::Warning: return "WARNING";
            case cpptools::Severity::Error: return "ERROR";
            case cpptools::Severity::Fatal: return "FATAL";
            default: return "UNKNOWN";
            }
        }
    }

    void setManagedLogSink(LogSinkCallback sink)
    {
        g_managedSink = sink;
    }

    void log(cpptools::Severity severity, const std::string& message)
    {
        std::string line = std::string("[codetools++] [") + SeverityLabel(severity) + "] " + message;

        // NOT calling OutputDebugStringA here - contrary to what this function's doc comment
        // used to claim ("always safe, any thread"), it actually serializes through a
        // process-wide Win32 mutex (DBWIN_BUFFER_READY/DBWIN_DATA_READY) shared by every
        // OutputDebugString caller on the system, and is a documented real deadlock vector when
        // called from a background thread while the VS UI/STA thread is simultaneously blocked
        // pumping for something else (exactly EditThreadHost::runAndWait's own nested pump-wait,
        // itself nested inside VS's own JoinableTaskFactory.Run()) - confirmed as the live cause
        // of the NativeEditControl_Create hang investigated 2026-08-29. The managed-sink path
        // below (IVsOutputWindowPane::OutputStringThreadSafe, via OutputWindowLogger.cs) is VS's
        // own documented non-blocking alternative and is kept as the only delivery mechanism.

        if (!g_managedSink)
        {
            return;
        }

        std::wstring wide = utf8ToWide(line);

        if (newui::RunLoop::current() != nullptr)
        {
            // On the dedicated thread - queue rather than calling the managed sink directly here
            // (see this function's own doc comment for why). flushQueuedLogs() delivers it once
            // back on a safe thread.
            std::lock_guard<std::mutex> lock(g_queueMutex);
            g_queuedLogs.emplace_back(severity, std::move(wide));
            return;
        }

        g_managedSink(severity, wide.c_str(), wide.size());
    }

    void flushQueuedLogs()
    {
        if (!g_managedSink)
        {
            return;
        }

        std::vector<std::pair<cpptools::Severity, std::wstring>> pending;
        {
            std::lock_guard<std::mutex> lock(g_queueMutex);
            pending.swap(g_queuedLogs);
        }

        for (const auto& entry : pending)
        {
            g_managedSink(entry.first, entry.second.c_str(), entry.second.size());
        }
    }
}
