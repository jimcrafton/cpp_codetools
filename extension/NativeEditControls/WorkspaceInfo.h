#pragma once

#include <newui/delegate.h>

#include <mutex>
#include <string>
#include <vector>

namespace newui { class RunLoop; }

namespace CodeToolsVsix
{
    // What the host says is open: the workspace's root folder(s) - the solution's folder, or the folder
    // opened in Open Folder mode - and the active build configuration. Pushed by the managed host
    // (NativeEditControl_WorkspaceChanged) as VS opens and closes solutions and folders; empty roots
    // mean nothing is open. set() may be called from any thread; onChanged fires on the run loop given to
    // setRunLoop() - the edit thread in the DLL - so UI code can react directly.
    class WorkspaceInfo
    {
    public:
        static WorkspaceInfo& instance();

        // Fires when the roots or the configuration changed.
        newui::Delegate<WorkspaceInfo> onChanged;

        // Where onChanged runs. Unset: on the thread that called set(). Must outlive this object.
        void setRunLoop(newui::RunLoop* loop);

        // Any thread. Roots are absolute folder paths, UTF-8.
        void set(std::vector<std::string> roots, std::string configuration);

        std::vector<std::string> roots() const;
        std::string primaryRoot() const;     // the first root, or empty
        std::string configuration() const;   // "Debug"; empty when the host did not say

    private:
        mutable std::mutex mutex_;
        std::vector<std::string> roots_;
        std::string configuration_;
        newui::RunLoop* loop_ = nullptr;
    };
}
