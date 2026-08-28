#include "NativeEditControl.h"
#include "StandInEditControl.h"
#include "Logging.h"

using namespace CodeToolsVsix;

HWND __stdcall NativeEditControl_Create(HWND hwndParent, int x, int y, int width, int height, HINSTANCE hInstance)
{
    return StandInEditControl::Create(hwndParent, x, y, width, height, hInstance);
}

BOOL __stdcall NativeEditControl_Load(HWND hwnd, const wchar_t* filePath, size_t filePathLength)
{
    StandInEditControl* self = StandInEditControl::FromHandle(hwnd);
    return (self && self->Load(filePath, filePathLength)) ? TRUE : FALSE;
}

BOOL __stdcall NativeEditControl_Save(HWND hwnd, const wchar_t* filePath, size_t filePathLength)
{
    StandInEditControl* self = StandInEditControl::FromHandle(hwnd);
    return (self && self->Save(filePath, filePathLength)) ? TRUE : FALSE;
}

BOOL __stdcall NativeEditControl_IsDirty(HWND hwnd)
{
    StandInEditControl* self = StandInEditControl::FromHandle(hwnd);
    return (self && self->IsDirty()) ? TRUE : FALSE;
}

BOOL __stdcall NativeEditControl_ExecCommand(HWND hwnd, EditorCommand command, uint32_t flags, const EditorCommandArgs* args)
{
    StandInEditControl* self = StandInEditControl::FromHandle(hwnd);
    return (self && self->ExecCommand(command, flags, args)) ? TRUE : FALSE;
}

void __stdcall NativeEditControl_SetLogSink(LogSinkCallback sink)
{
    SetManagedLogSink(sink);
}
