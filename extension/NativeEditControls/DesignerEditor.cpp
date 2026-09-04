#include "DesignerEditor.h"
#include "Logging.h"
#include "TextEncoding.h"

#include <newui/rootview.h>
#include <newui/layout.h>
#include <newui/uicolormanager.h>
#include <newui/bundle.h>

#include <utility>

namespace CodeToolsVsix
{
    namespace
    {
        // filePath is never assumed to be null-terminated (see
        // NativeEditor.h) - same convention CppEditor.cpp's own copyPath()
        // uses.
        std::wstring copyPath(const wchar_t* filePath, std::size_t filePathLength)
        {
            if (!filePath || filePathLength == 0)
            {
                return std::wstring();
            }
            return std::wstring(filePath, filePathLength);
        }

        // Splits at the final \ or / into (everything before, everything
        // after) - empty first element if there's no separator at all.
        std::pair<std::wstring, std::wstring> splitLastComponent(const std::wstring& path)
        {
            std::size_t pos = path.find_last_of(L"\\/");
            if (pos == std::wstring::npos)
            {
                return { std::wstring(), path };
            }
            return { path.substr(0, pos), path.substr(pos + 1) };
        }

        std::wstring stripExtension(const std::wstring& fileName)
        {
            std::size_t pos = fileName.find_last_of(L'.');
            return pos == std::wstring::npos ? fileName : fileName.substr(0, pos);
        }

        // DesignerEditor edits "<root>\Resources\<bundleName>.newui" files -
        // the same convention bundle.h itself documents and Frame Map's own
        // naming already uses. Bundle::instance() has no idea where the
        // user's actual project lives (it's hosted inside devenv.exe, whose
        // own exe directory means nothing here) - this derives Bundle's
        // executableDir()-shaped root (the "<root>" above) plus the bare
        // bundleName from the real absolute path VS hands load()/save(),
        // to feed Bundle::setExecutableDirOverride() before every real call.
        // Returns false if path doesn't look like it's under a \Resources\
        // folder at all.
        bool resolveBundleNameAndRoot(const std::wstring& path, std::wstring& outRoot, std::string& outBundleName)
        {
            auto [fileDir, fileName] = splitLastComponent(path);
            if (fileDir.empty() || fileName.empty())
            {
                return false;
            }

            auto [root, resourcesFolder] = splitLastComponent(fileDir);
            if (root.empty() || _wcsicmp(resourcesFolder.c_str(), L"Resources") != 0)
            {
                return false;
            }

            outRoot = root;
            outBundleName = wideToUtf8(stripExtension(fileName));
            return true;
        }
    }

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

    bool DesignerEditor::load(const wchar_t* filePath, std::size_t filePathLength)
    {
        newui::RootView* root = getRootView();
        if (!root)
        {
            logToDebugOut(L"DesignerEditor::load: root view is null (construction must have failed)");
            return false;
        }

        std::wstring path = copyPath(filePath, filePathLength);
        std::wstring overrideRoot;
        std::string bundleName;
        if (!resolveBundleNameAndRoot(path, overrideRoot, bundleName))
        {
            logToDebugOut(L"DesignerEditor::load: path is not under a \\Resources\\ folder, can't resolve a bundle name");
            return false;
        }

        newui::Bundle::instance().setExecutableDirOverride(wideToUtf8(overrideRoot));
        if (!newui::Bundle::instance().loadRootView(*root, bundleName))
        {
            logToDebugOut(L"DesignerEditor::load: Bundle::loadRootView failed");
            return false;
        }

        clearDirty();
        return true;
    }

    bool DesignerEditor::save(const wchar_t* filePath, std::size_t filePathLength)
    {
        newui::RootView* root = getRootView();
        if (!root)
        {
            logToDebugOut(L"DesignerEditor::save: root view is null (construction must have failed)");
            return false;
        }

        std::wstring path = copyPath(filePath, filePathLength);
        std::wstring overrideRoot;
        std::string bundleName;
        if (!resolveBundleNameAndRoot(path, overrideRoot, bundleName))
        {
            logToDebugOut(L"DesignerEditor::save: path is not under a \\Resources\\ folder, can't resolve a bundle name");
            return false;
        }

        newui::Bundle::instance().setExecutableDirOverride(wideToUtf8(overrideRoot));
        if (!newui::Bundle::instance().writeRootView(*root, bundleName))
        {
            logToDebugOut(L"DesignerEditor::save: Bundle::writeRootView failed");
            return false;
        }

        clearDirty();
        return true;
    }

    bool DesignerEditor::execCommand(EditorCommand /*command*/, std::uint32_t /*flags*/, const EditorCommandArgs* /*args*/)
    {
        logToDebugOut(L"DesignerEditor::execCommand: no document model yet, stub");
        return true;
    }
}
