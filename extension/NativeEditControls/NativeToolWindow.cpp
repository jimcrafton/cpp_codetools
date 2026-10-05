#include "NativeToolWindow.h"

#include "ExplorerToolWindow.h"
#include "NativeEditor.h"

namespace CodeToolsVsix
{
    std::map<HWND, std::unique_ptr<NativeToolWindow>>& NativeToolWindowManager::windows()
    {
        static std::map<HWND, std::unique_ptr<NativeToolWindow>> map;
        return map;
    }

    NativeToolWindow* NativeToolWindowManager::find(HWND hwnd)
    {
        auto it = windows().find(hwnd);
        return it == windows().end() ? nullptr : it->second.get();
    }

    HWND NativeToolWindowManager::create(HWND hwndParent, int x, int y, int width, int height, ToolWindowType type)
    {
        NativeEditManager::startRunLoop();
        return NativeEditManager::runOnEditThread([&]() -> HWND {
            std::unique_ptr<NativeToolWindow> window;
            switch (type) {
                case ToolWindowType::ProjectExplorer:
                    window = std::make_unique<ExplorerToolWindow>(hwndParent, x, y, width, height);
                    break;
                default:
                    return nullptr;
            }
            const HWND hwnd = window->windowHandle();
            if (hwnd == nullptr) {
                return nullptr;
            }
            windows().emplace(hwnd, std::move(window));
            return hwnd;
        });
    }

    bool NativeToolWindowManager::setBounds(HWND hwnd, int x, int y, int width, int height)
    {
        return NativeEditManager::runOnEditThread([&]() -> bool {
            NativeToolWindow* window = find(hwnd);
            if (window == nullptr) {
                return false;
            }
            window->setWindowBounds(newui::Rect(static_cast<float>(x), static_cast<float>(y),
                static_cast<float>(width), static_cast<float>(height)));
            return true;
        });
    }

    bool NativeToolWindowManager::close(HWND hwnd)
    {
        return NativeEditManager::runOnEditThread([&]() -> bool {
            auto it = windows().find(hwnd);
            if (it == windows().end()) {
                return false;
            }
            windows().erase(it);
            return true;
        });
    }
}
