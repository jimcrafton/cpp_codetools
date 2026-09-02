#include "Logging.h"
#include "TextEncoding.h"

#include <newui/runloop.h>

#include <mutex>
#include <utility>
#include <vector>
#include <sstream>

#include "NativeEditor.h"


#include <initguid.h>

#pragma comment(lib, "C:/Program Files/Microsoft Visual Studio/18/Community/VSSDK/VisualStudioIntegration/Common/Lib/x64/vsguids.lib")

DEFINE_GUID(GUID_OutWindowDebugPane,
    0xfc076020, 0x078a, 0x11d1, 0xa7, 0xdf, 0x00, 0xa0, 0xc9, 0x11, 0x00, 0x51);

// General Pane: {65482C72-DEFA-41B7-902C-11C091889C83}
DEFINE_GUID(GUID_OutWindowGeneralPane,
    0x65482c72, 0xdefa, 0x41b7, 0x90, 0x2c, 0x11, 0xc0, 0x91, 0x88, 0x9c, 0x83);

// Build Pane: {1BD8A850-02D1-11D1-BEE7-00A0C913D1F8}
DEFINE_GUID(GUID_OutWindowBuildPane,
    0x1bd8a850, 0x02d1, 0x11d1, 0xbe, 0xe7, 0x00, 0xa0, 0xc9, 0x13, 0xd1, 0xf8);


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

        if (newui::RunLoop::current())
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



    void logToDebugOut(const std::wstring& s)
    {

        auto svcProvPtr = NativeEditManager::serviceProvider();

		if (!svcProvPtr) {
			return;
		}

        IVsOutputWindowPtr pOutputWindow;
        if (SUCCEEDED(svcProvPtr->QueryService(SID_SVsOutputWindow, IID_IVsOutputWindow, (void**)&pOutputWindow))) {
            IVsOutputWindowPanePtr pLogPane;

            // 2. Target the specific system Debug Pane via its GUID
            // (Use GUID_OutWindowGeneralPane for the standard 'General' pane)
            if (SUCCEEDED(pOutputWindow->GetPane(GUID_OutWindowDebugPane, &pLogPane))) {
                std::wstringstream  ss;
                ss << L"[" << "codetools++" << L"][TID: " << std::this_thread::get_id() << "] " << s << L"\r\n";

                std::wstring msg = ss.str();

                pLogPane->OutputString(msg.c_str());
            }
        }
    }
}
