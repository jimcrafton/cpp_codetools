#include "NativeEditControl.h"
#include "CppEditorControl.h"
#include "EditThreadHost.h"
#include "Logging.h"
#include "TextEncoding.h"

#include <cpptools/version.h>
#include <newui/version.h>

#include <cwchar>
#include <unordered_map>
#include <mutex>
#include <sstream>

#include <wil/com.h>
#include <vsshell.h>


using namespace CodeToolsVsix;

namespace
{
    // Maps a live control's HWND back to the EditorControlBase instance behind it - held through
    // the base, not the concrete CppEditorControl, so this dispatch layer stays editor-type-
    // agnostic (ready for a future second editor type, e.g. a visual designer, with no changes
    // needed here - see EditorControlBase.h). All access (including the map itself) only ever
    // happens on EditThreadHost's own dedicated thread (every function below reaches it
    // exclusively through EditThreadHost::runAndWait()), so no separate locking is needed here
    // despite the exported entry points themselves being callable from VS's own UI thread at any
    // time.
    std::unordered_map<HWND, std::unique_ptr<EditorControlBase>>& controlMap()
    {
        static std::unordered_map<HWND, std::unique_ptr<EditorControlBase>> map;
        return map;
    }
}

HWND __stdcall NativeEditControl_Create(HWND hwndParent, int x, int y, int width, int height, HINSTANCE /*hInstance*/)
{
    EditThreadHost::instance().ensureStarted();


    EditThreadHost::instance().runAndWait([&]() -> HWND {
        

        CComPtr<IVsOutputWindow> pOutputWindow;
        if (SUCCEEDED(pServiceProvider->QueryService(SID_SVsOutputWindow, IID_IVsOutputWindow, (void**)&pOutputWindow)))
        {
            // Use a unique GUID for your extension pane
            GUID guidPane = { 0xXXXX, 0xXX, 0xXX, { 0xXX, 0xXX, 0xXX, 0xXX, 0xXX, 0xXX, 0xXX, 0xXX } };
            CComPtr<IVsOutputWindowPane> pPane;

            if (FAILED(pOutputWindow->GetPane(guidPane, &pPane)))
            {
                pOutputWindow->CreatePane(guidPane, L"My Extension Logs", TRUE, FALSE);
                pOutputWindow->GetPane(guidPane, &pPane);
            }

            if (pPane)
            {
                pPane->OutputStringThreadSafe(pszText); // Automatically handles cross-thread calls safely
            }
        }



        //auto control = std::make_unique<CppEditorControl>(hwndParent, x, y, width, height);
        //HWND hwnd = control->windowHandle();
        //if (!hwnd)
        //{
        //    return nullptr;
        //}

        //controlMap().emplace(hwnd, std::move(control));
        //EditThreadHost::instance().controlCreated();
        //return hwnd;

        return NULL;
        });



    return NULL;

        /*
        EditThreadHost::instance().runAndWait([&]() -> HWND {
        auto control = std::make_unique<CppEditorControl>(hwndParent, x, y, width, height);
        HWND hwnd = control->windowHandle();
        if (!hwnd)
        {
            return nullptr;
        }

        controlMap().emplace(hwnd, std::move(control));
        EditThreadHost::instance().controlCreated();
        return hwnd;
        });
        */
}

BOOL __stdcall NativeEditControl_RequestClose(HWND hwnd)
{
    return EditThreadHost::instance().runAndWait([&]() -> BOOL {
        auto it = controlMap().find(hwnd);
        if (it == controlMap().end())
        {
            return FALSE;
        }

        controlMap().erase(it);
        EditThreadHost::instance().controlClosed();
        return TRUE;
        });
}

BOOL __stdcall NativeEditControl_Load(HWND hwnd, const wchar_t* filePath, size_t filePathLength)
{
    return EditThreadHost::instance().runAndWait([&]() -> BOOL {
        auto it = controlMap().find(hwnd);
        return (it != controlMap().end() && it->second->load(filePath, filePathLength)) ? TRUE : FALSE;
        });
}

BOOL __stdcall NativeEditControl_Save(HWND hwnd, const wchar_t* filePath, size_t filePathLength)
{
    return EditThreadHost::instance().runAndWait([&]() -> BOOL {
        auto it = controlMap().find(hwnd);
        return (it != controlMap().end() && it->second->save(filePath, filePathLength)) ? TRUE : FALSE;
        });
}

BOOL __stdcall NativeEditControl_IsDirty(HWND hwnd)
{
    return EditThreadHost::instance().runAndWait([&]() -> BOOL {
        auto it = controlMap().find(hwnd);
        return (it != controlMap().end() && it->second->isDirty()) ? TRUE : FALSE;
        });
}

BOOL __stdcall NativeEditControl_ExecCommand(HWND hwnd, EditorCommand command, uint32_t flags, const EditorCommandArgs* args)
{
    return EditThreadHost::instance().runAndWait([&]() -> BOOL {
        auto it = controlMap().find(hwnd);
        return (it != controlMap().end() && it->second->execCommand(command, flags, args)) ? TRUE : FALSE;
        });
}

void __stdcall NativeEditControl_SetLogSink(LogSinkCallback sink)
{
    setManagedLogSink(sink);

    // Called once, from the managed host's own init thread (never EditThreadHost's dedicated
    // one) - so this is always safe to call the sink from directly, unlike log() calls made from
    // CppEditorControl.cpp's dedicated-thread code (see Logging.h's own comment on why those
    // queue instead). This is the actual compiled-in NativeEditControl.dll version (also embedded
    // in the DLL's own VERSIONINFO resource, see NativeEditControl.rc) - distinct from
    // OutputWindowLogger's own managed-assembly version line, which reports CodeToolsVsix.dll's
    // version, not this native DLL's. Also reports newui's own version (its generated
    // include/newui/version.h, transitively reachable since NativeEditControl links newui) -
    // useful since 3rdparty/newui/ is a separately-versioned dependency pulled via FetchContent
    // (see root CMakeLists.txt's "newui - Dependency" section), not something whose version is
    // otherwise visible anywhere in this extension.
    CodeToolsVsix::log(cpptools::Severity::Note,
        std::string("NativeEditControl.dll version ") + CPPTOOLS_VERSION_STRING
        + " (newui " + NEWUI_VERSION_STRING + ")");
}
