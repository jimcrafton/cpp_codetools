#include "NativeEditor.h"
#include "TextEncoding.h"

#include <cpptools/version.h>
#include <newui/version.h>

#include <cwchar>






using namespace CodeToolsVsix;

namespace
{
    // The bridge lives for the process; replies hop onto the edit thread through its run loop.
    HostEditorBridge& hostBridge()
    {
        static HostEditorBridge bridge([](std::function<void()> task) {
            NativeEditManager::startRunLoop();
            NativeEditManager::runLoop()->post(std::move(task));
        });
        return bridge;
    }
}




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

void __stdcall NativeEditControl_SetHostEditor(HostGetTextCallback getText, HostApplyEditsCallback applyEdits)
{
    NativeEditManager::startRunLoop();
    NativeEditManager::runLoop()->post([getText, applyEdits]() {
        hostBridge().setCallbacks(getText, applyEdits);
        documentEditService().setHost(hostBridge().connected() ? &hostBridge() : nullptr);
    });
}

void __stdcall NativeEditControl_HostGetTextReply(
    uint64_t requestId, int32_t status, const wchar_t* text, size_t textLength, uint64_t version)
{
    // Copies before returning - text is only valid for this call.
    hostBridge().replyGetText(requestId, static_cast<EditStatus>(status),
                              text != nullptr ? std::wstring(text, textLength) : std::wstring(), version);
}

void __stdcall NativeEditControl_HostApplyEditsReply(uint64_t requestId, int32_t status)
{
    hostBridge().replyApply(requestId, static_cast<EditStatus>(status));
}
