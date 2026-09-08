// A plain newui::Application/newui::Frame app with a File menu (Open Folder..., Open C++/Save
// C++, Open Designer/Save Designer) that loads and saves through a real NativeEditor (CppEditor
// or DesignerEditor - whichever the menu item names), hosted inside a content pane to the right
// of a directory tree pane (a stand-in for what a real IDE's Solution Explorer would show -
// testharness has no VS host providing that, but knowing what directory/project is being worked
// on is still useful here) via NativeEditManager::createEditor(newui::RootView*, DocumentType,
// newui::SubView*) - the non-threaded path, added specifically so a harness like this doesn't
// have to deal with the dedicated-background-thread hosting model NativeEditControlApi.cpp uses
// for VS. The editor's own View tree lives as plain View children of contentHost (a child of
// frame.rootView(), not the RootView itself - see CppEditor::setupUI()/DesignerEditor::setupUI()'s
// own comments for why that split exists), so Open/Save just call load()/save() directly on the
// NativeEditor pointer this returns - never NativeEditManager's HWND-keyed wrapper methods
// (loadFileForEditor/saveFileForEditor/closeEditor), which assume a run loop this harness never
// starts.
//
// Only one NativeEditor's View tree is ever hosted per run - it's built into contentHost once, on
// the first Open, and NativeEditor doesn't support tearing that content back out again. Picking
// "Open Designer" after already opening a C++ file (or vice versa) is refused with a message
// rather than silently mixing both editors' content into the same contentHost.

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

#include "../DirectoryTree.h"
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

    // mainRow: directoryTree | contentHost, a horizontal split - fixedPane(First) is Splitter's
    // own default (directoryTree pinned at kDirectoryTreePaneWidth, contentHost grows), matching
    // the standard docking-IDE convention (Workspace::mainRow, Workspace.cpp, follows the same
    // convention for its own Toolbox pane). contentHost is deliberately a plain, empty SubView
    // until an editor is actually opened - CppEditor::setupUI()/DesignerEditor::setupUI() build
    // their own content straight into it (see NativeEditManager::createEditor()'s own comment).
    auto* directoryTree = new DirectoryTree();
    directoryTree->setName("testharnessDirectoryTree");

    auto* contentHost = new newui::SubView();
    contentHost->setName("testharnessContentHost");
    contentHost->setVisible(true);
    contentHost->style().setBackgroundColor(newui::UIColorManager::colorFor(newui::UIColorRole::WindowBackground));

    auto* mainRow = new newui::Splitter(newui::Orientation::Horizontal);
    mainRow->setName("testharnessMainRow");
    mainRow->setSplitPosition(kDirectoryTreePaneWidth);
    mainRow->setDividerThickness(kDividerThickness);
    mainRow->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
    mainRow->addChild(directoryTree);
    mainRow->addChild(contentHost);

    // Shared by all four Open/Save menu items below, and by directoryTree's own double-click -
    // open picks or reuses the editor for type; save just reuses whatever's already open,
    // refusing if it doesn't match type.
    auto openPath = [&root, &editor, &editorType, contentHost](const std::wstring& path, DocumentType type) {
        if (path.empty())
        {
            return;
        }

        if (nullptr == editor)
        {
            editor = NativeEditManager::createEditor(&root, type, contentHost);
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

    auto openAs = [&frame, openPath](DocumentType type) {
        std::wstring path = showFileDialog(frame.frameHandle(), false);
        openPath(path, type);
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
            if (newui::Dialog::ShowBrowseForFolder(frame.frameHandle(), options, selectedPath))
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
    root.addChild(mainRow);

    app.run();

    return 0;
}
