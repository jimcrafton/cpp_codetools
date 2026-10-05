#pragma once

#include <newui/delegate.h>

#include <cpptools/compileflags.h>

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace newui { class RunLoop; }
namespace cmakemodel { class CompileSettingsIndex; }

namespace CodeToolsVsix
{
    // cpptools::compileFlagsFor(path) with the build's own include folders and definitions for the target that owns the
    // file added (settings may be null, or not know the file). compile_commands.json may not cover a target - the tests,
    // a library that needs LLVM - and the file then misses a -D or a -I; CMake's File API has them all. The origin names
    // the target when it contributed.
    cpptools::CompileFlags compileFlagsWithBuild(const std::string& path, const cmakemodel::CompileSettingsIndex* settings);

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

        // What the project explorer read from CMake about how each file is compiled (null: nothing known). Any
        // thread. Each change bumps compileSettingsVersion(), so something that cached flags can tell they are stale.
        void setCompileSettings(std::shared_ptr<const cmakemodel::CompileSettingsIndex> settings);
        unsigned compileSettingsVersion() const;

        // compileFlagsWithBuild() with the settings set above.
        cpptools::CompileFlags compileFlagsFor(const std::string& path) const;

    private:
        mutable std::mutex mutex_;
        std::vector<std::string> roots_;
        std::string configuration_;
        newui::RunLoop* loop_ = nullptr;
        std::shared_ptr<const cmakemodel::CompileSettingsIndex> settings_;
        unsigned settingsVersion_ = 0;
    };
}
