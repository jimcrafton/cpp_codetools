// A plain newui::Application/newui::Frame app with a File menu (Open C++/Save C++, Open
// Designer/Save Designer) that loads and saves through a real NativeEditor (CppEditor or
// DesignerEditor - whichever the menu item names), hosted directly inside frame.rootView() via
// NativeEditManager::createEditor(newui::RootView*, DocumentType) - the non-threaded path, added
// specifically so a harness like this doesn't have to deal with the dedicated-background-thread
// hosting model NativeEditControlApi.cpp uses for VS. The editor's own View tree lives as plain
// View children of frame.rootView() itself (no second native window, no run loop marshaling), so
// Open/Save just call load()/save() directly on the NativeEditor pointer this returns - never
// NativeEditManager's HWND-keyed wrapper methods (loadFileForEditor/saveFileForEditor/
// closeEditor), which assume a run loop this harness never starts.
//
// Only one NativeEditor's View tree is ever hosted per run - it's built into frame.rootView()
// once, on the first Open, and NativeEditor doesn't support tearing that content back out again.
// Picking "Open Designer" after already opening a C++ file (or vice versa) is refused with a
// message rather than silently mixing both editors' content into the same RootView.

#include "newui/newui.h"
#include "newui/application.h"
#include "newui/frame.h"
#include "newui/layout.h"
#include "newui/menus.h"
#include "newui/rootview.h"
#include "newui/uicolormanager.h"

#include "../NativeEditor.h"

#include <commdlg.h>
#include <cstdio>
#include <memory>
#include <vector>

using namespace CodeToolsVsix;

namespace
{
    // Returns an empty string if the user cancels.
    std::wstring showFileDialog(HWND hwndOwner, bool forSave)
    {
        wchar_t path[MAX_PATH] = L"";

        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hwndOwner;
        ofn.lpstrFilter = L"All Files\0*.*\0";
        ofn.lpstrFile = path;
        ofn.nMaxFile = static_cast<DWORD>(std::size(path));
        ofn.Flags = forSave ? (OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST)
                             : (OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST);

        BOOL ok = forSave ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
        return ok ? std::wstring(path) : std::wstring();
    }

    const wchar_t* documentTypeName(DocumentType type)
    {
        return type == DocumentType::Designer ? L"Designer" : L"C++";
    }
}

int main()
{
    newui::Frame frame;
    NativeEditor* editor = nullptr;
    DocumentType editorType = DocumentType::CppSource;

    newui::Application& app = newui::Application::instance();
    app.setName("codetools++ testharness");
    app.setFrame(&frame);

    frame.setTitle("codetools++ NativeEditor test harness");
    frame.setBounds(newui::Rect(100, 100, 1000, 700));

    frame.onClosed += [&editor](newui::Frame& frame) {
        // Not NativeEditManager::closeEditor() - that marshals through runLoop()->postAndWait(),
        // and this harness never starts a run loop (see this file's own top comment).
        // unregisterEditor() just erases the map entry directly, no marshaling needed.
        if (nullptr != editor)
        {
            NativeEditManager::unregisterEditor(editor->windowHandle());
            editor = nullptr;
        }
        printf("Frame (%p, hwnd: %p) closed, exiting application.\n", &frame, frame.frameHandle());
        return newui::SyncReturn::Handled;
    };

    newui::RootView& root = frame.rootView();
    root.style().setBackgroundColor(newui::UIColorManager::colorFor(newui::UIColorRole::WindowBackground));

    auto rootLayout = std::make_unique<newui::FlexLayout>(newui::Orientation::Vertical);
    rootLayout->setSpacing(0.0f);
    rootLayout->setPadding(0.0f);
    root.setLayout(std::move(rootLayout));

    // Shared by all four menu items below - open picks or reuses the editor for type; save just
    // reuses whatever's already open, refusing if it doesn't match type.
    auto openAs = [&frame, &root, &editor, &editorType](DocumentType type) {
        std::wstring path = showFileDialog(frame.frameHandle(), false);
        if (path.empty())
        {
            return;
        }

        if (nullptr == editor)
        {
            editor = NativeEditManager::createEditor(&root, type);
            editorType = type;
            if (nullptr == editor)
            {
                printf("testharness: failed to create %ls editor\n", documentTypeName(type));
                return;
            }
        }
        else if (type != editorType)
        {
            printf("testharness: already hosting a %ls editor - restart to open a %ls document\n",
                   documentTypeName(editorType), documentTypeName(type));
            return;
        }

        if (!editor->load(path.c_str(), path.size()))
        {
            printf("testharness: load failed\n");
        }
    };

    auto saveAs = [&frame, &editor, &editorType](DocumentType type) {
        if (nullptr == editor)
        {
            printf("testharness: nothing open yet\n");
            return;
        }
        if (type != editorType)
        {
            printf("testharness: the open editor is %ls, not %ls\n",
                   documentTypeName(editorType), documentTypeName(type));
            return;
        }

        std::wstring path = showFileDialog(frame.frameHandle(), true);
        if (!path.empty() && !editor->save(path.c_str(), path.size()))
        {
            printf("testharness: save failed\n");
        }
    };

    std::vector<std::unique_ptr<newui::MenuItem>> menuItems;

    auto fileMenu = std::make_unique<newui::MenuItem>("File");
    fileMenu->addChild(std::make_unique<newui::MenuItem>("Open C++"))->onClick.add(
        [openAs](newui::MenuItem&) {
            openAs(DocumentType::CppSource);
            return newui::SyncReturn::Handled;
        });
    fileMenu->addChild(std::make_unique<newui::MenuItem>("Save C++"))->onClick.add(
        [saveAs](newui::MenuItem&) {
            saveAs(DocumentType::CppSource);
            return newui::SyncReturn::Handled;
        });
    fileMenu->addChild(std::make_unique<newui::MenuItem>("Open Designer"))->onClick.add(
        [openAs](newui::MenuItem&) {
            openAs(DocumentType::Designer);
            return newui::SyncReturn::Handled;
        });
    fileMenu->addChild(std::make_unique<newui::MenuItem>("Save Designer"))->onClick.add(
        [saveAs](newui::MenuItem&) {
            saveAs(DocumentType::Designer);
            return newui::SyncReturn::Handled;
        });
    menuItems.push_back(std::move(fileMenu));

    auto* menuBar = new newui::MenuBar();
    menuBar->setMenuItems(std::move(menuItems));
    menuBar->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(0.0f));
    root.addChild(menuBar);

    app.run();

    return 0;
}
