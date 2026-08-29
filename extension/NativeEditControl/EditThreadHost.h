#pragma once
#include <Windows.h>

#include <newui/runloop.h>

#include <atomic>
#include <mutex>
#include <thread>
#include <type_traits>

namespace CodeToolsVsix
{
    // Owns the one dedicated background thread + newui::RunLoop every hosted CppEditorControl's
    // RootView lives on - see "win32 loop in VSIX.docx" (D:\code\newui), the reference this
    // design follows: a native control gets its own thread with its own independent message
    // loop, with its HWND handed back to the host cross-thread. Started lazily on the first
    // NativeEditControl_Create() call; every subsequent call reuses the same thread rather than
    // spinning up a new one, matching how VS itself hosts many editor tabs on one UI thread.
    // Stopped once, on DLL unload (see dllmain.cpp's DLL_PROCESS_DETACH).
    class EditThreadHost
    {
    public:
        static EditThreadHost& Instance();

        // Records this DLL's own module handle - captured once, in DllMain's DLL_PROCESS_ATTACH.
        // NativeEditControl_Create's own hInstance parameter is the *host* process's
        // (devenv.exe's) module handle (see NativeEditHost.cs's CreateChildWindow -
        // GetModuleHandle(null) from managed code) - the wrong one for a window class whose
        // WndProc lives inside this DLL. This is the correct one to use instead.
        static void SetModuleHandle(HINSTANCE hInstance);
        static HINSTANCE ModuleHandle();

        // Starts the dedicated thread if it isn't already running - safe to call more than once,
        // every call after the first is a no-op. Blocks until the thread's RunLoop is actually
        // pumping (RunLoop::waitUntilStarted()) before returning, so a caller can safely
        // RunAndWait() immediately afterward.
        void EnsureStarted();

        // Runs fn() on the dedicated thread and blocks the calling thread until it completes,
        // returning fn()'s own result (or nothing, if fn() returns void). The only place raw
        // RunLoop::post() is used directly - every native export
        // (Create/Load/Save/IsDirty/ExecCommand/RequestClose) marshals through this, since VS
        // calls them from its own UI thread but the RootView/TextControl state they touch only
        // ever lives on the dedicated thread.
        //
        // Waits by pumping the CALLING thread's own message queue (MsgWaitForMultipleObjects +
        // PeekMessage/DispatchMessage), not a plain blocking wait - the same reentrant-pump
        // technique newui's own RunLoop::runModal() uses (runloop.h) for the same underlying
        // reason: creating (or destroying) a window whose parent lives on this calling thread
        // sends messages (e.g. WM_PARENTNOTIFY) to that parent *synchronously*, cross-thread -
        // the parent's own thread has to still be pumping to receive them, or both threads
        // deadlock (the dedicated thread blocked inside CreateWindowEx waiting for the parent
        // thread to service the sent message; the calling/parent thread blocked here waiting for
        // the dedicated thread's task to finish). Confirmed live: an earlier plain
        // condition_variable-based version of this function hung Visual Studio's UI thread solid
        // on the very first NativeEditControl_Create() call for exactly this reason.
        template <typename Func>
        auto RunAndWait(Func&& fn) -> decltype(fn())
        {
            using ReturnType = decltype(fn());

            HANDLE doneEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);

            if constexpr (std::is_void_v<ReturnType>)
            {
                runLoop_.post([&fn, doneEvent]() {
                    fn();
                    ::SetEvent(doneEvent);
                    });

                PumpUntilSignaled(doneEvent);
                ::CloseHandle(doneEvent);
            }
            else
            {
                ReturnType result{};

                runLoop_.post([&fn, &result, doneEvent]() {
                    result = fn();
                    ::SetEvent(doneEvent);
                    });

                PumpUntilSignaled(doneEvent);
                ::CloseHandle(doneEvent);
                return result;
            }
        }

        // Tracked for completeness/future use - not currently used to auto-shutdown the thread
        // early (see Shutdown()'s own comment on why teardown is deliberately kept to DLL-unload
        // time only for this phase).
        void ControlCreated();
        void ControlClosed();

        // Stops the RunLoop and joins the thread - called once, from DllMain's
        // DLL_PROCESS_DETACH. A thread still running when this DLL is unloaded is a crash risk
        // (its own code pages become invalid mid-unload), so this must run before unload
        // completes. Safe to call even if the thread was never started.
        void Shutdown();

    private:
        EditThreadHost() = default;

        // Pumps the calling thread's own message queue until doneEvent is signaled - see
        // RunAndWait()'s own comment for why this can't be a plain blocking wait.
        static void PumpUntilSignaled(HANDLE doneEvent)
        {
            for (;;)
            {
                DWORD waitResult = ::MsgWaitForMultipleObjects(1, &doneEvent, FALSE, INFINITE, QS_ALLINPUT);
                if (waitResult == WAIT_OBJECT_0)
                {
                    return;
                }

                MSG msg;
                while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
                {
                    ::TranslateMessage(&msg);
                    ::DispatchMessageW(&msg);
                }
            }
        }

        newui::RunLoop runLoop_;
        std::thread thread_;
        std::once_flag startOnce_;
        std::atomic<int> liveControlCount_{0};

        static HINSTANCE s_moduleHandle;
    };
}
