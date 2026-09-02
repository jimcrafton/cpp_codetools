#include "DesignerEditor.h"
#include "Logging.h"

#include <newui/rootview.h>
#include <newui/layout.h>
#include <newui/uicolormanager.h>

namespace CodeToolsVsix
{
    DesignerEditor::DesignerEditor(newui::RootView* rootView)
    {
        rootViewOwned_ = true;
        if (!setupUI(rootView))
        {
            return;
        }

        setRootView(std::unique_ptr<newui::RootView>(rootView));
    }

    DesignerEditor::DesignerEditor(HWND hwndParent, int x, int y, int width, int height)
    {
        logToDebugOut(L"DesignerEditor");

        auto root = std::make_unique<newui::RootView>(
            hwndParent, NativeEditManager::moduleHandle(),
            newui::Rect(static_cast<float>(x), static_cast<float>(y), static_cast<float>(width), static_cast<float>(height)),
            "designerEditorRoot");

        if (!setupUI(root.get()))
        {
            return;
        }

        setRootView(std::move(root));
        logToDebugOut(L"DesignerEditor completed");
    }

    bool DesignerEditor::setupUI(newui::RootView* root)
    {
        root->style().setBackgroundColor(newui::UIColorManager::colorFor(newui::UIColorRole::WindowBackground));

        auto rootLayout = std::make_unique<newui::FlexLayout>(newui::Orientation::Vertical);
        rootLayout->setSpacing(0.0f);
        rootLayout->setPadding(0.0f);
        root->setLayout(std::move(rootLayout));

        if (!this->rootViewOwned_) {
            if (!root->initialize())
            {
                // Leave the base's RootView null - windowHandle() reports failure the same way
                // CppEditor's own constructor does on this same failure path.
                logToDebugOut(L"!root->initialize()");
                return false;
            }
        }
        return true;
    }

    bool DesignerEditor::load(const wchar_t* /*filePath*/, std::size_t /*filePathLength*/)
    {
        logToDebugOut(L"DesignerEditor::load: no document model yet, nothing to load");
        return true;
    }

    bool DesignerEditor::save(const wchar_t* /*filePath*/, std::size_t /*filePathLength*/)
    {
        logToDebugOut(L"DesignerEditor::save: no document model yet, nothing to save");
        return false;
    }

    bool DesignerEditor::execCommand(EditorCommand /*command*/, std::uint32_t /*flags*/, const EditorCommandArgs* /*args*/)
    {
        logToDebugOut(L"DesignerEditor::execCommand: no document model yet, stub");
        return true;
    }
}
