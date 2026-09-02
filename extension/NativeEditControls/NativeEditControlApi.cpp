#include "NativeEditor.h"
#include "TextEncoding.h"

#include <cpptools/version.h>
#include <newui/version.h>

#include <cwchar>






using namespace CodeToolsVsix;




BOOL __cdecl NativeEditControl_SetServiceProvider(IUnknown* svcProviderPtr)
{
 
    if (nullptr == svcProviderPtr) {
        NativeEditManager::setServiceProvider(nullptr);
    }
    else {
        IServiceProviderPtr svcProvPtr;
        svcProviderPtr->QueryInterface(&svcProvPtr);
        NativeEditManager::setServiceProvider(svcProvPtr);
    }

    return TRUE;
}




HWND __cdecl NativeEditControl_Create(HWND hwndParent, int x, int y, int width, int height, DocumentType documentType)
{
    HWND hwnd = NULL;

    auto editor = NativeEditManager::createEditor(hwndParent, x, y, width, height, documentType);
	if (nullptr != editor)
	{
		hwnd = editor->windowHandle();
	}

    return hwnd;
}

BOOL __stdcall NativeEditControl_RequestClose(HWND hwnd)
{
    return NativeEditManager::closeEditor(hwnd) ? TRUE:FALSE;
}

BOOL __stdcall NativeEditControl_Load(HWND hwnd, const wchar_t* filePath, size_t filePathLength)
{
    return NativeEditManager::loadFileForEditor(hwnd, filePath, filePathLength) ? TRUE : FALSE;
}

BOOL __stdcall NativeEditControl_Save(HWND hwnd, const wchar_t* filePath, size_t filePathLength)
{
    return NativeEditManager::saveFileForEditor(hwnd, filePath, filePathLength) ? TRUE : FALSE;
}

BOOL __stdcall NativeEditControl_IsDirty(HWND hwnd)
{    
    return NativeEditManager::isEditorDirty(hwnd) ? TRUE : FALSE;
}

BOOL __stdcall NativeEditControl_ExecCommand(HWND hwnd, EditorCommand command, uint32_t flags, const EditorCommandArgs* args)
{
    return NativeEditManager::execCmdForEditor(hwnd, command, flags, args) ? TRUE : FALSE;
}

void __stdcall NativeEditControl_SetLogSink(LogSinkCallback sink)
{
    setManagedLogSink(sink);

    // Called once, from the managed host's own init thread (never EditThreadHost's dedicated
    // one) - so this is always safe to call the sink from directly, unlike log() calls made from
    // CppEditorControl.cpp's dedicated-thread code (see Logging.h's own comment on why those
    // queue instead). This is the actual compiled-in NativeEditControls.dll version (also embedded
    // in the DLL's own VERSIONINFO resource, see NativeEditControl.rc) - distinct from
    // OutputWindowLogger's own managed-assembly version line, which reports CodeToolsVsix.dll's
    // version, not this native DLL's. Also reports newui's own version (its generated
    // include/newui/version.h, transitively reachable since NativeEditControls links newui) -
    // useful since 3rdparty/newui/ is a separately-versioned dependency pulled via FetchContent
    // (see root CMakeLists.txt's "newui - Dependency" section), not something whose version is
    // otherwise visible anywhere in this extension.
    CodeToolsVsix::log(cpptools::Severity::Note,
        std::string("NativeEditControls.dll version ") + CPPTOOLS_VERSION_STRING
        + " (newui " + NEWUI_VERSION_STRING + ")");
}
