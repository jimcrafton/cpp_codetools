#include "EditThreadHost.h"

namespace CodeToolsVsix
{
    HINSTANCE EditThreadHost::s_moduleHandle = nullptr;

    EditThreadHost& EditThreadHost::Instance()
    {
        static EditThreadHost instance;
        return instance;
    }

    void EditThreadHost::SetModuleHandle(HINSTANCE hInstance)
    {
        s_moduleHandle = hInstance;
    }

    HINSTANCE EditThreadHost::ModuleHandle()
    {
        return s_moduleHandle;
    }

    void EditThreadHost::EnsureStarted()
    {
        std::call_once(startOnce_, [this]() {
            thread_ = std::thread([this]() { runLoop_.run(); });
            runLoop_.waitUntilStarted();
            });
    }

    void EditThreadHost::ControlCreated()
    {
        ++liveControlCount_;
    }

    void EditThreadHost::ControlClosed()
    {
        --liveControlCount_;
    }

    void EditThreadHost::Shutdown()
    {
        if (!thread_.joinable())
        {
            return;
        }

        runLoop_.quit();
        thread_.join();
    }
}
