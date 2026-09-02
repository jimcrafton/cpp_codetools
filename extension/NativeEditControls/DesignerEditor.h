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

        // No document model yet - always returns true (there is nothing that can fail to load).
        bool load(const wchar_t* filePath, std::size_t filePathLength) override;

        // No document model yet - always returns false, matching the "not implemented" theme of
        // this placeholder rather than silently pretending a save succeeded.
        bool save(const wchar_t* filePath, std::size_t filePathLength) override;

        bool execCommand(EditorCommand command, std::uint32_t flags, const EditorCommandArgs* args) override;
    };
}
