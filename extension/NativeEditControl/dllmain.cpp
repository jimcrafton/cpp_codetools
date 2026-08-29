#include <Windows.h>

#include "EditThreadHost.h"

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ulReasonForCall, LPVOID /*lpReserved*/)
{
    switch (ulReasonForCall)
    {
    case DLL_PROCESS_ATTACH:
        // Captured here, not from NativeEditControl_Create's own hInstance parameter - that's
        // the *host* process's (devenv.exe's) module handle, not this DLL's own, and the
        // dedicated-thread RootView's window class needs the one whose WndProc actually lives in
        // this DLL. See EditThreadHost.h's own comment.
        CodeToolsVsix::EditThreadHost::SetModuleHandle(reinterpret_cast<HINSTANCE>(hModule));
        break;

    case DLL_PROCESS_DETACH:
        // A thread still running when this DLL is unloaded is a crash risk (its own code pages
        // become invalid mid-unload) - stop it and join before returning.
        CodeToolsVsix::EditThreadHost::Instance().Shutdown();
        break;
    }

    return TRUE;
}
