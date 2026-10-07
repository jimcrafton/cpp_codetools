#include "NativeEditor.h"
#include "NativeToolWindow.h"
#include "Settings.h"
#include "TextEncoding.h"
#include "WorkspaceInfo.h"

#include <cpptools/compileflags.h>
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

void __stdcall NativeEditControl_SetHost(const HostServices* services)
{
    const HostServices host = copyHostServices(services);

    // The log sink. Called once, from the managed host's own init thread (never the edit thread) - so
    // it is always safe to call the sink from here directly, unlike log() calls made from the edit
    // thread (see Logging.h's own comment on why those queue instead). Reports this DLL's compiled-in
    // version (also in its VERSIONINFO resource, see NativeEditControl.rc) and newui's own (its generated
    // include/newui/version.h) - useful since 3rdparty/newui/ is a separately-versioned dependency pulled
    // via FetchContent (see the root CMakeLists.txt's "newui - Dependency" section), not something whose
    // version is otherwise visible anywhere in this extension.
    setManagedLogSink(host.logSink);
    if (host.logSink != nullptr) {
        CodeToolsVsix::log(cpptools::Severity::Note,
            std::string("NativeEditControls.dll version ") + CPPTOOLS_VERSION_STRING
            + " (newui " + NEWUI_VERSION_STRING + ")");
    }

    // Reading/editing files open in VS: set on the edit thread, where the bridge is used.
    NativeEditManager::startRunLoop();
    NativeEditManager::runLoop()->post([getText = host.getText, applyEdits = host.applyEdits]() {
        hostBridge().setCallbacks(getText, applyEdits);
        documentEditService().setHost(hostBridge().connected() ? &hostBridge() : nullptr);
    });

    HostLocationOpener::instance().setCallback(host.openLocation);
}

HWND __cdecl NativeToolWindow_Create(HWND hwndParent, int x, int y, int width, int height, ToolWindowType type)
{
    return NativeToolWindowManager::create(hwndParent, x, y, width, height, type);
}

BOOL __stdcall NativeToolWindow_SetBounds(HWND hwnd, int x, int y, int width, int height)
{
    return NativeToolWindowManager::setBounds(hwnd, x, y, width, height) ? TRUE : FALSE;
}

BOOL __stdcall NativeToolWindow_RequestClose(HWND hwnd)
{
    return NativeToolWindowManager::close(hwnd) ? TRUE : FALSE;
}

void __stdcall NativeEditControl_WorkspaceChanged(const wchar_t* roots, const wchar_t* configuration)
{
    // Listeners run on the edit thread, where the tool windows live.
    NativeEditManager::startRunLoop();
    WorkspaceInfo::instance().setRunLoop(NativeEditManager::runLoop());

    std::vector<std::string> folders;
    std::wstring all = roots != nullptr ? roots : L"";
    std::size_t from = 0;
    while (from <= all.size() && !all.empty()) {
        std::size_t to = all.find(L'\n', from);
        if (to == std::wstring::npos) to = all.size();
        if (to > from) folders.push_back(wideToUtf8(all.substr(from, to - from)));
        from = to + 1;
    }
    WorkspaceInfo::instance().set(std::move(folders), configuration != nullptr ? wideToUtf8(std::wstring(configuration)) : std::string());
}

void __stdcall NativeEditControl_SettingChanged(const wchar_t* settingName, const wchar_t* value)
{
    if (settingName == nullptr) {
        return;
    }
    // Listeners run on the edit thread, where the editors live. Set on every call, which keeps this
    // independent of whether the host table has been registered yet.
    NativeEditManager::startRunLoop();
    Settings::instance().setRunLoop(NativeEditManager::runLoop());

    std::string key;
    for (const wchar_t* c = settingName; *c != L'\0'; ++c) {
        key += static_cast<char>(*c);   // keys are ASCII
    }
    Settings::instance().set(key, value != nullptr ? std::wstring(value) : std::wstring());
    if (key == Settings::kCacheCompileDatabase.key) {   // a library setting, not a view's: handed down here
        const bool keep = Settings::instance().getBool(Settings::kCacheCompileDatabase);
        cpptools::setCompileDatabaseCacheEnabled(keep);
        // A Warning so it shows in the default log: lets the setting be seen arriving, as it is a switch for troubleshooting.
        CodeToolsVsix::log(cpptools::Severity::Warning, std::string("compile_commands.json kept in memory: ") + (keep ? "on" : "off"));
    }
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
