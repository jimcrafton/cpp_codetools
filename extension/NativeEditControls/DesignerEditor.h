#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>

#include "NativeEditor.h"

namespace CodeToolsVsix
{
    // What NativeEditManager::createEditor constructs for DocumentType::Designer (see
    // NativeEditControlApi.h) - a real newui::RootView with no content yet, so that dispatch has
    // a genuinely distinct second NativeEditor subclass to hand off to rather than always falling
    // back to CppEditor. This is meant to grow into a visual UI designer surface (a View tree the
    // user edits directly, not a text buffer) - unlike CppEditor, it deliberately has no
    // TextControl of its own.
    class DesignerEditor : public NativeEditor
    {
    public:
        DesignerEditor(HWND hwndParent, int x, int y, int width, int height);

        DesignerEditor(newui::RootView* rootView);

        bool setupUI(newui::RootView* root);

        // filePath must be a real "<root>\Resources\<bundleName>.newui" -
        // derives bundleName/root from it (see resolveBundleNameAndRoot(),
        // DesignerEditor.cpp), points Bundle::instance() at root via
        // setExecutableDirOverride() (this DLL is hosted inside devenv.exe,
        // whose own exe dir has nothing to do with the user's project), then
        // Bundle::loadRootView()s just the "rootView" node into this
        // editor's own RootView. Returns false if the path isn't shaped
        // that way, or for any of loadRootView()'s own failure reasons.
        bool load(const wchar_t* filePath, std::size_t filePathLength) override;

        // Write-side counterpart to load() - Bundle::writeRootView(),
        // which preserves any other top-level keys (title, bounds,
        // animations, ...) an existing Frame-shaped file already has;
        // only "rootView" is replaced. Same path/root derivation and
        // failure contract as load().
        bool save(const wchar_t* filePath, std::size_t filePathLength) override;

        bool execCommand(EditorCommand command, std::uint32_t flags, const EditorCommandArgs* args) override;
    };
}
