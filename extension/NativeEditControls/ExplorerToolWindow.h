#pragma once

#include <newui/delegate.h>

#include <memory>

#include "NativeToolWindow.h"
#include "ProjectExplorer.h"
#include "WorkspaceInfo.h"

namespace CodeToolsVsix
{
    // The project explorer as a VS tool-window pane: a RootView holding a ProjectExplorer, following the
    // folder the host says is open (WorkspaceInfo) and opening rows in the host's own editor
    // (HostLocationOpener).
    class ExplorerToolWindow : public NativeToolWindow
    {
    public:
        // Builds its RootView as a child of hwndParent filling (x, y, width, height). Edit thread only.
        ExplorerToolWindow(HWND hwndParent, int x, int y, int width, int height);
        ~ExplorerToolWindow() override;

        ProjectExplorer* explorer() const { return explorer_.get(); }

    private:
        newui::SyncReturn handleWorkspaceChanged(WorkspaceInfo& sender);

        // Declared before the RootView's own storage in the base: this is destroyed first.
        std::unique_ptr<ProjectExplorer> explorer_;
        newui::Connection workspaceConnection_;
    };
}
