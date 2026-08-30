#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <memory>

// runloop.h must come before rootview.h/controls.h - see CppEditorControl.h's own comment on why
// (Delegate<...>::postCall(RunLoop&, ...) needs RunLoop's full definition).
#include <newui/runloop.h>
#include <newui/rootview.h>

#include "NativeEditControl.h"

namespace CodeToolsVsix
{
    // The shared VS-communication contract every native editor type (the C++ source editor
    // today, a future visual designer) hosts behind NativeEditControl_Create/RequestClose/load/
    // save/isDirty/execCommand - see NativeEditControlApi.cpp, which only ever talks to instances
    // through this interface, never a concrete subclass. Deliberately does NOT unify what gets
    // loaded/saved or displayed - a future designer's content model (file format, View tree) is
    // its own concern; this only owns the pieces that are genuinely about being a
    // newui::RootView-hosted control VS can create/destroy/ask about, not what's inside it.
    //
    // Every public method here is only ever called from EditThreadHost::runAndWait() - see
    // NativeEditControlApi.cpp's own wrappers for where that marshaling actually happens - so,
    // like CppEditorControl, this has no thread-safety of its own; callers are responsible for
    // only ever reaching an instance from the edit thread.
    class EditorControlBase
    {
    public:
        virtual ~EditorControlBase()
        {
            if (rootView_)
            {
                rootView_->destroy();
            }
        }

        HWND windowHandle() const { return rootView_ ? rootView_->windowHandle() : nullptr; }

        virtual bool load(const wchar_t* filePath, std::size_t filePathLength) = 0;
        virtual bool save(const wchar_t* filePath, std::size_t filePathLength) = 0;
        bool isDirty() const { return dirty_; }
        virtual bool execCommand(EditorCommand command, std::uint32_t flags, const EditorCommandArgs* args) = 0;

    protected:
        EditorControlBase() = default;

        // A subclass constructor calls this once its own RootView (and whatever child View tree
        // it builds, and initialize()) is fully built - see this class's own comment on why the
        // base constructor doesn't build the RootView itself (calling a virtual "build my
        // content" hook from a base constructor wouldn't reach the derived override yet).
        // Ownership transfers in; left null (the default) if construction failed, matching
        // windowHandle()'s own "null means failure" contract.
        void setRootView(std::unique_ptr<newui::RootView> rootView) { rootView_ = std::move(rootView); }
        newui::RootView* getRootView() const { return rootView_.get(); }

        void markDirty() { dirty_ = true; }
        void clearDirty() { dirty_ = false; }

    private:
        std::unique_ptr<newui::RootView> rootView_;
        bool dirty_ = false;
    };
}
