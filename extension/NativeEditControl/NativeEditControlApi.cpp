#include "NativeEditControl.h"
#include "CppEditorControl.h"
#include "EditThreadHost.h"
#include "Logging.h"

#include <unordered_map>
#include <mutex>

using namespace CodeToolsVsix;

namespace
{
    // Maps a live control's HWND back to the CppEditorControl instance behind it - all access
    // (including the map itself) only ever happens on EditThreadHost's own dedicated thread
    // (every function below reaches it exclusively through EditThreadHost::RunAndWait()), so no
    // separate locking is needed here despite the exported entry points themselves being callable
    // from VS's own UI thread at any time.
    std::unordered_map<HWND, std::unique_ptr<CppEditorControl>>& ControlMap()
    {
        static std::unordered_map<HWND, std::unique_ptr<CppEditorControl>> map;
        return map;
    }
}

HWND __stdcall NativeEditControl_Create(HWND hwndParent, int x, int y, int width, int height, HINSTANCE /*hInstance*/)
{
    EditThreadHost::Instance().EnsureStarted();

    return EditThreadHost::Instance().RunAndWait([&]() -> HWND {
        auto control = std::make_unique<CppEditorControl>(hwndParent, x, y, width, height);
        HWND hwnd = control->windowHandle();
        if (!hwnd)
        {
            return nullptr;
        }

        ControlMap().emplace(hwnd, std::move(control));
        EditThreadHost::Instance().ControlCreated();
        return hwnd;
        });
}

BOOL __stdcall NativeEditControl_RequestClose(HWND hwnd)
{
    return EditThreadHost::Instance().RunAndWait([&]() -> BOOL {
        auto it = ControlMap().find(hwnd);
        if (it == ControlMap().end())
        {
            return FALSE;
        }

        ControlMap().erase(it);
        EditThreadHost::Instance().ControlClosed();
        return TRUE;
        });
}

BOOL __stdcall NativeEditControl_Load(HWND hwnd, const wchar_t* filePath, size_t filePathLength)
{
    return EditThreadHost::Instance().RunAndWait([&]() -> BOOL {
        auto it = ControlMap().find(hwnd);
        return (it != ControlMap().end() && it->second->Load(filePath, filePathLength)) ? TRUE : FALSE;
        });
}

BOOL __stdcall NativeEditControl_Save(HWND hwnd, const wchar_t* filePath, size_t filePathLength)
{
    return EditThreadHost::Instance().RunAndWait([&]() -> BOOL {
        auto it = ControlMap().find(hwnd);
        return (it != ControlMap().end() && it->second->Save(filePath, filePathLength)) ? TRUE : FALSE;
        });
}

BOOL __stdcall NativeEditControl_IsDirty(HWND hwnd)
{
    return EditThreadHost::Instance().RunAndWait([&]() -> BOOL {
        auto it = ControlMap().find(hwnd);
        return (it != ControlMap().end() && it->second->IsDirty()) ? TRUE : FALSE;
        });
}

BOOL __stdcall NativeEditControl_ExecCommand(HWND hwnd, EditorCommand command, uint32_t flags, const EditorCommandArgs* args)
{
    return EditThreadHost::Instance().RunAndWait([&]() -> BOOL {
        auto it = ControlMap().find(hwnd);
        return (it != ControlMap().end() && it->second->ExecCommand(command, flags, args)) ? TRUE : FALSE;
        });
}

void __stdcall NativeEditControl_SetLogSink(LogSinkCallback sink)
{
    SetManagedLogSink(sink);
}
