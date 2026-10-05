#pragma once
#include <Windows.h>

#include <map>
#include <memory>

#include <newui/rootview.h>

#include "NativeEditControlApi.h"

namespace CodeToolsVsix
{
    // A native pane that lives in a VS tool window (the project explorer): a newui::RootView hosted as a
    // child of the HWND the managed tool window gives it. Unlike a NativeEditor it is not a document - no
    // file, no load/save, no dirty state, no editing commands - and there is one of it per tool window,
    // not one per open file. It shares the edit thread (and its run loop) with the editors, so every call
    // here is made through NativeEditManager::runOnEditThread(), as NativeToolWindowManager does.
    class NativeToolWindow
    {
    public:
        virtual ~NativeToolWindow()
        {
            if (rootView_) {
                rootView_->destroy();
            }
        }

        HWND windowHandle() const { return rootView_ ? rootView_->windowHandle() : nullptr; }

        // Moves and sizes the window within its parent (client coordinates). Through the RootView, never a
        // raw SetWindowPos(): the RootView re-applies the position it remembers on every WM_SIZE.
        void setWindowBounds(const newui::Rect& bounds)
        {
            if (rootView_) {
                rootView_->setBounds(bounds);
            }
        }

    protected:
        NativeToolWindow() = default;

        // A subclass calls this once its RootView and everything under it is built. Left unset if
        // construction failed, which windowHandle() reports as null.
        void setRootView(std::unique_ptr<newui::RootView> rootView) { rootView_ = std::move(rootView); }
        newui::RootView* getRootView() const { return rootView_.get(); }

    private:
        std::unique_ptr<newui::RootView> rootView_;
    };

    // Creates, finds and closes the tool-window panes. The edit thread owns them: create(), setBounds()
    // and close() marshal onto it and wait.
    class NativeToolWindowManager
    {
    public:
        // The pane's window handle, or null if it could not be created.
        static HWND create(HWND hwndParent, int x, int y, int width, int height, ToolWindowType type);
        static bool setBounds(HWND hwnd, int x, int y, int width, int height);
        static bool close(HWND hwnd);

        // Edit thread only.
        static NativeToolWindow* find(HWND hwnd);

    private:
        static std::map<HWND, std::unique_ptr<NativeToolWindow>>& windows();
    };
}
