#include "ExplorerToolWindow.h"

#include "HostLocationOpener.h"
#include "Logging.h"
#include "NativeEditor.h"
#include "TextEncoding.h"

#include <newui/layout.h>
#include <newui/uicolormanager.h>

namespace CodeToolsVsix
{
    ExplorerToolWindow::ExplorerToolWindow(HWND hwndParent, int x, int y, int width, int height)
    {
        auto root = std::make_unique<newui::RootView>(
            hwndParent, NativeEditManager::moduleHandle(),
            newui::Rect(static_cast<float>(x), static_cast<float>(y), static_cast<float>(width), static_cast<float>(height)),
            "explorerRoot");

        root->style().setBackgroundColor(newui::UIColorManager::colorFor(newui::UIColorRole::ControlBackground));
        auto layout = std::make_unique<newui::FlexLayout>(newui::Orientation::Vertical);
        layout->setSpacing(0.0f);
        layout->setPadding(0.0f);
        root->setLayout(std::move(layout));

        explorer_ = std::make_unique<ProjectExplorer>(*root);
        if (!explorer_->loaded() || !root->initialize()) {
            logToDebugOut(L"ExplorerToolWindow: could not build the explorer");
            return;
        }

        explorer_->setOpenHandler([](const std::string& path, std::size_t line) {
            return HostLocationOpener::instance().open(utf8ToWide(path), line > 0 ? line : 1, 1);
        });
        workspaceConnection_ = WorkspaceInfo::instance().onChanged.add(this, &ExplorerToolWindow::handleWorkspaceChanged);
        explorer_->setRoot(WorkspaceInfo::instance().primaryRoot());

        setRootView(std::move(root));
    }

    ExplorerToolWindow::~ExplorerToolWindow()
    {
        WorkspaceInfo::instance().onChanged.remove(workspaceConnection_);
        explorer_.reset();   // before the RootView (and the views it owns) goes
    }

    newui::SyncReturn ExplorerToolWindow::handleWorkspaceChanged(WorkspaceInfo& sender)
    {
        explorer_->setRoot(sender.primaryRoot());
        return newui::SyncReturn::Handled;
    }
}
