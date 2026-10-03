// A plain newui::Application/newui::Frame app with a File menu (Open Folder..., Open C++, Open
// Designer, Save, Close Tab) that loads and saves through real NativeEditors
// (CppEditor or DesignerEditor - whichever the menu item names), hosted in document tabs to the
// right of a directory tree pane (a stand-in for what a real IDE's Solution Explorer would show -
// testharness has no VS host providing that, but knowing what directory/project is being worked
// on is still useful here).
//
// Any number of documents can be open at once, one tab each (DocumentTabs). Each tab's editor has a
// RootView of its own in a child window, like VS hosts them - see DocumentTabs.h for why they
// don't share the frame's root. Open/Save call load()/save() directly on the NativeEditor, never
// NativeEditManager's HWND-keyed wrapper methods (loadFileForEditor/saveFileForEditor/
// closeEditor), which assume a run loop this harness never starts.

#include "newui/newui.h"
#include "newui/application.h"
#include "newui/dialogs.h"
#include "newui/frame.h"
#include "newui/layout.h"
#include "newui/menus.h"
#include "newui/rootview.h"
#include "newui/splitter.h"
#include "newui/subview.h"
#include "newui/uicolormanager.h"

#include "../CppEditor.h"
#include "../DesignerEditor.h"
#include "../DirectoryTree.h"
#include "../DocumentTabs.h"
#include "../NativeEditor.h"

#include <commdlg.h>
#include <cstdio>
#include <memory>
#include <vector>

// Defined in the reflectgen-generated .cpp (compiled into the `newui` target, global namespace) -
// self-guarding at the source, so calling it here is harmless even though NativeEditManager's own
// constructor also calls it (NativeEditor.cpp) once an editor is actually opened. Every real newui
// example app calls this once at startup; testharness never needed to before because everything it
// built (Toolbox/DocumentOutline/PropertiesGrid) only ever painted after "Open C++"/"Open Designer"
// had already constructed a NativeEditManager first - DirectoryTree is the first thing here that
// can paint (a plain newui::TreeController's own createItem(), which instantiates "TreeItem" via
// reflection) before any editor is ever opened, so it needs this registered unconditionally too.
extern void registerReflectionData();

using namespace CodeToolsVsix;

namespace
{
    // Fixed dock width for directoryTree - no particular design reasoning behind the number
    // (unlike Workspace::kToolboxPaneWidth's own Main.dc.html-derived value), just a reasonable
    // default for a file-name-only tree; the divider is a real, user-draggable newui::Splitter,
    // so this is only ever the starting position.
    constexpr float kDirectoryTreePaneWidth = 240.0f;
    constexpr float kDividerThickness = 4.0f;

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

    // A new editor in a child window of `parent`, built here on the UI thread (not through
    // NativeEditManager::createEditor(), which hops to its own thread). Sized properly by its tab
    // page once that is laid out.
    std::unique_ptr<NativeEditor> makeEditor(DocumentType type, HWND parent)
    {
        constexpr int kInitialSize = 100;
        std::unique_ptr<NativeEditor> editor;
        if (type == DocumentType::Designer)
        {
            auto designer = std::make_unique<DesignerEditor>(parent, 0, 0, kInitialSize, kInitialSize);
            designer->installDialogPrompts();
            editor = std::move(designer);
        }
        else
        {
            editor = std::make_unique<CppEditor>(parent, 0, 0, kInitialSize, kInitialSize);
        }
        return editor->windowHandle() != nullptr ? std::move(editor) : nullptr;
    }

    std::wstring toWide(const std::string& utf8)
    {
        if (utf8.empty())
        {
            return std::wstring();
        }
        int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
        std::wstring wide(static_cast<std::size_t>(len), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), wide.data(), len);
        return wide;
    }
}

int main()
{
    registerReflectionData();

    newui::Frame frame;
    // The editors build their root views with the module handle NativeEditManager holds (set by
    // DllMain in the VSIX); here it is the exe itself.
    NativeEditManager::setModuleHandle(::GetModuleHandleW(nullptr));
    DocumentTabs* tabs = nullptr;   // owned by the frame's view tree, built below

    newui::Application& app = newui::Application::instance();
    app.setName("codetools++ testharness");
    app.setFrame(&frame);

    frame.setTitle("codetools++ NativeEditor test harness");
    frame.setBounds(newui::Rect(100, 100, 1000, 700));

    frame.onClosed += [&tabs](newui::Frame& frame) {
        // The editors' windows are children of the frame's: close them while those still exist.
        if (nullptr != tabs)
        {
            tabs->closeAll();
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

    // mainRow: directoryTree | document tabs, a horizontal split - fixedPane(First) is Splitter's
    // own default (directoryTree pinned at kDirectoryTreePaneWidth, the tabs grow), matching the
    // standard docking-IDE convention (Workspace::mainRow, Workspace.cpp, follows the same
    // convention for its own Toolbox pane).
    auto* directoryTree = new DirectoryTree();
    directoryTree->setName("testharnessDirectoryTree");

    tabs = new DocumentTabs(&makeEditor);
    tabs->setName("testharnessDocumentTabs");
    tabs->style().setBackgroundColor(newui::UIColorManager::colorFor(newui::UIColorRole::WindowBackground));

    auto* mainRow = new newui::Splitter(newui::Orientation::Horizontal);
    mainRow->setName("testharnessMainRow");
    mainRow->setSplitPosition(kDirectoryTreePaneWidth);
    mainRow->setDividerThickness(kDividerThickness);
    mainRow->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
    mainRow->addChild(directoryTree);
    mainRow->addChild(tabs);

    // Shared by the Open menu items and by directoryTree's own double-click: a new tab, or the one
    // that already shows the file.
    auto openPath = [&root, &tabs](const std::wstring& path, DocumentType type) {
        if (path.empty())
        {
            return;
        }
        if (nullptr == tabs->open(path, type))
        {
            printf("testharness: couldn't open %ls as a %ls document\n", path.c_str(), documentTypeName(type));
            MessageBoxW(root.windowHandle(), (L"Couldn't open " + path).c_str(), L"codetools++ testharness",
                        MB_OK | MB_ICONWARNING);
        }
    };

    auto openAs = [&frame, openPath](DocumentType type) {
        std::wstring path = showFileDialog(frame.frameHandle(), false);
        openPath(path, type);
    };

    // Saves the selected tab to its own file.
    auto saveActive = [&tabs]() {
        NativeEditor* editor = tabs->activeEditor();
        if (nullptr == editor)
        {
            printf("testharness: nothing open yet\n");
            return;
        }
        const std::wstring& path = tabs->activePath();
        if (!editor->save(path.c_str(), path.size()))
        {
            printf("testharness: save failed\n");
        }
        tabs->refreshTitles();
    };

    // A file in directoryTree is always a C/C++ source/header (DirectoryTreeModel's own
    // isSourceFile() filter, DirectoryTree.cpp) - always CppSource, no extension sniffing needed.
    directoryTree->onFileActivated.add([openPath](DirectoryTree&, const std::string& path) {
        openPath(toWide(path), DocumentType::CppSource);
        return newui::SyncReturn::Handled;
    });

    std::vector<std::unique_ptr<newui::MenuItem>> menuItems;

    auto fileMenu = std::make_unique<newui::MenuItem>("File");
    fileMenu->addChild(std::make_unique<newui::MenuItem>("Open Folder..."))->onClick.add(
        [&frame, directoryTree](newui::MenuItem&) {
            newui::FileDialogOptions options;
            options.title = "Select a project folder";
            std::string selectedPath;
            if (newui::Dialog::showBrowseForFolder(frame.frameHandle(), options, selectedPath))
            {
                directoryTree->setRootPath(selectedPath);
                // "we need to know what directory/project we're working on" even without a real
                // IDE host to show it - the frame's own title is the simplest place testharness
                // has for that.
                frame.setTitle("codetools++ NativeEditor test harness - " + selectedPath);
            }
            return newui::SyncReturn::Handled;
        });
    fileMenu->addChild(std::make_unique<newui::MenuItem>("Open C++"))->onClick.add(
        [openAs](newui::MenuItem&) {
            openAs(DocumentType::CppSource);
            return newui::SyncReturn::Handled;
        });
    fileMenu->addChild(std::make_unique<newui::MenuItem>("Open Designer"))->onClick.add(
        [openAs](newui::MenuItem&) {
            openAs(DocumentType::Designer);
            return newui::SyncReturn::Handled;
        });
    fileMenu->addChild(std::make_unique<newui::MenuItem>("Save"))->onClick.add(
        [saveActive](newui::MenuItem&) {
            saveActive();
            return newui::SyncReturn::Handled;
        });
    fileMenu->addChild(std::make_unique<newui::MenuItem>("Close Tab"))->onClick.add(
        [&tabs](newui::MenuItem&) {
            tabs->closeActive();
            return newui::SyncReturn::Handled;
        });
    menuItems.push_back(std::move(fileMenu));

    auto* menuBar = new newui::MenuBar();
    menuBar->setMenuItems(std::move(menuItems));
    menuBar->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(0.0f));
    root.addChild(menuBar);
    root.addChild(mainRow);

    app.run();

    return 0;
}
