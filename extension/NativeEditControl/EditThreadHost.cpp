#include "EditThreadHost.h"

namespace CodeToolsVsix
{
    HINSTANCE EditThreadHost::s_moduleHandle = nullptr;

    EditThreadHost& EditThreadHost::instance()
    {
        static EditThreadHost instance;
        return instance;
    }

    void EditThreadHost::setModuleHandle(HINSTANCE hInstance)
    {
        s_moduleHandle = hInstance;
    }

    HINSTANCE EditThreadHost::moduleHandle()
    {
        return s_moduleHandle;
    }

    void EditThreadHost::ensureStarted()
    {
        std::call_once(startOnce_, [this]() {
            thread_ = std::thread([this]() { runLoop_.run(); });
            runLoop_.waitUntilStarted();
            });
    }

    void EditThreadHost::controlCreated()
    {
        ++liveControlCount_;
    }

    void EditThreadHost::controlClosed()
    {
        --liveControlCount_;
    }

    void EditThreadHost::shutdown()
    {
        if (!thread_.joinable())
        {
            return;
        }

        runLoop_.quit();
        thread_.join();
    }
}
