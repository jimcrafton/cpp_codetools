#include "WorkspaceInfo.h"

#include <cmakemodel/model.h>

#include <newui/runloop.h>

namespace CodeToolsVsix
{
    cpptools::CompileFlags compileFlagsWithBuild(const std::string& path, const cmakemodel::CompileSettingsIndex* settings)
    {
        cpptools::CompileFlags flags = cpptools::compileFlagsFor(path);
        const cmakemodel::CompileSettings* found = settings != nullptr ? settings->find(path) : nullptr;
        if (found != nullptr) {
            for (std::string& extra : found->toArgs()) flags.args.push_back(std::move(extra));
            const std::string target = "CMake target " + found->target;
            flags.origin = flags.origin.empty() ? target : flags.origin + " + " + target;
        }
        return flags;
    }

    void WorkspaceInfo::setCompileSettings(std::shared_ptr<const cmakemodel::CompileSettingsIndex> settings)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        settings_ = std::move(settings);
        ++settingsVersion_;
    }

    unsigned WorkspaceInfo::compileSettingsVersion() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return settingsVersion_;
    }

    cpptools::CompileFlags WorkspaceInfo::compileFlagsFor(const std::string& path) const
    {
        std::shared_ptr<const cmakemodel::CompileSettingsIndex> settings;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            settings = settings_;
        }
        return compileFlagsWithBuild(path, settings.get());
    }

    WorkspaceInfo& WorkspaceInfo::instance()
    {
        static WorkspaceInfo info;
        return info;
    }

    void WorkspaceInfo::setRunLoop(newui::RunLoop* loop)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        loop_ = loop;
    }

    void WorkspaceInfo::set(std::vector<std::string> roots, std::string configuration)
    {
        newui::RunLoop* loop = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (roots == roots_ && configuration == configuration_) {
                return;
            }
            roots_ = std::move(roots);
            configuration_ = std::move(configuration);
            loop = loop_;
        }
        // The listeners are read when this runs, on the loop's thread (where they are also added and
        // removed), so one disconnected since this call is not called.
        if (loop != nullptr) {
            loop->post([this]() { onChanged.syncCall(*this); });
        } else {
            onChanged.syncCall(*this);
        }
    }

    std::vector<std::string> WorkspaceInfo::roots() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return roots_;
    }

    std::string WorkspaceInfo::primaryRoot() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return roots_.empty() ? std::string() : roots_.front();
    }

    std::string WorkspaceInfo::configuration() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return configuration_;
    }
}
