#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <memory>

// runloop.h must come before rootview.h/controls.h - see CppEditorControl.h's own comment on why
// (Delegate<...>::postCall(RunLoop&, ...) needs RunLoop's full definition).
#include <newui/runloop.h>
#include <newui/rootview.h>

#include <vsshell.h>
#include <wil/com.h>


#include "NativeEditControlApi.h"
#include "Logging.h"


typedef wil::com_ptr<IServiceProvider> IServiceProviderPtr;
typedef wil::com_ptr<IVsOutputWindow> IVsOutputWindowPtr;
typedef wil::com_ptr<IVsOutputWindowPane> IVsOutputWindowPanePtr;




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
    class NativeEditor
    {
    public:
        virtual ~NativeEditor()
        {
            if (rootView_)
            {
                logToDebugOut(L"~NativeEditor about to delete NativeEditor::rootView_");
                rootView_->destroy();
            }
        }

        HWND windowHandle() const { return rootView_ ? rootView_->windowHandle() : nullptr; }

        virtual bool load(const wchar_t* filePath, std::size_t filePathLength) = 0;
        virtual bool save(const wchar_t* filePath, std::size_t filePathLength) = 0;
        bool isDirty() const { return dirty_; }
        virtual bool execCommand(EditorCommand command, std::uint32_t flags, const EditorCommandArgs* args) = 0;

    protected:
        NativeEditor() = default;

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


    class NativeEditManager {
    public:
		static void registerEditor(HWND hwnd, std::unique_ptr<NativeEditor> editor) {
			instance().controlMap_[hwnd] = std::move(editor);
		}
		static void unregisterEditor(HWND hwnd) {
			instance().controlMap_.erase(hwnd);
		}
		static NativeEditor* getEditor(HWND hwnd) {
			auto it = instance().controlMap_.find(hwnd);
			if (it != instance().controlMap_.end()) {
				return it->second.get();
			}
			return nullptr;
		}

		static void setModuleHandle(HINSTANCE hInstance) {
			instance().hInstance_ = hInstance;
		}
        static void setServiceProvider(IServiceProviderPtr svcProvPtr) {
            instance().svcProvPtr_ = svcProvPtr;
        }

        static IServiceProviderPtr serviceProvider() {
            return instance().svcProvPtr_;
        }

        static HINSTANCE moduleHandle() {
            return instance().hInstance_;
        }

		static void startRunLoop();

        static void shutdown();

        static newui::RunLoop* runLoop() { return instance().runLoop_; }

        static NativeEditor* createEditor(HWND hwndParent, int x, int y, int width, int height);

		static bool closeEditor(HWND hwnd) {
			auto& instance = NativeEditManager::instance();

            bool result = instance.runLoop()->postAndWait([&]() -> bool {
                auto it = instance.controlMap_.find(hwnd);
                if (it != instance.controlMap_.end()) {
                    instance.controlMap_.erase(it);
                }
                else {
                    logToDebugOut(L"closeEditor: editor not found for hwnd");
                    return false;
                }
                return true;
				});

			return result;
		}

        static bool loadFileForEditor(HWND hwnd, const wchar_t* filePath, size_t filePathLength) {
            auto& instance = NativeEditManager::instance();

            bool result = instance.runLoop()->postAndWait([&]() -> bool {
                auto it = instance.controlMap_.find(hwnd);
                if (it != instance.controlMap_.end()) {
                    return it->second->load(filePath, filePathLength);
                }
                else {
                    logToDebugOut(L"loadFileForEditor: editor not found for hwnd");
                    return false;
                }
                return true;
                });

            return result;
        }

        static bool saveFileForEditor(HWND hwnd, const wchar_t* filePath, size_t filePathLength) {
            auto& instance = NativeEditManager::instance();

            bool result = instance.runLoop()->postAndWait([&]() -> bool {
                auto it = instance.controlMap_.find(hwnd);
                if (it != instance.controlMap_.end()) {
                    return it->second->save(filePath, filePathLength);
                }
                else {
                    logToDebugOut(L"saveFileForEditor: editor not found for hwnd");
                    return false;
                }
                return true;
                });

            return result;
        }

        static bool isEditorDirty(HWND hwnd) {
            auto& instance = NativeEditManager::instance();

            bool result = instance.runLoop()->postAndWait([&]() -> bool {
                auto it = instance.controlMap_.find(hwnd);
                if (it != instance.controlMap_.end()) {
                    return it->second->isDirty();
                }
                else {
                    logToDebugOut(L"isEditorDirty: editor not found for hwnd");
                    return false;
                }
                return true;
                });

            return result;
        }

        static bool execCmdForEditor(HWND hwnd, EditorCommand command, uint32_t flags, const EditorCommandArgs* args) {
            auto& instance = NativeEditManager::instance();

            bool result = instance.runLoop()->postAndWait([&]() -> bool {
                auto it = instance.controlMap_.find(hwnd);
                if (it != instance.controlMap_.end()) {
                    return it->second->execCommand(command, flags, args);
                }
                else {
                    logToDebugOut(L"execCmdForEditor: editor not found for hwnd");
                    return false;
                }
                return true;
                });

            return result;
        }
    private:
        static NativeEditManager& instance() {
            static NativeEditManager instance;
            return instance;
        }

        HINSTANCE hInstance_ = nullptr;
        IServiceProviderPtr svcProvPtr_;
        newui::RunLoop* runLoop_ = nullptr;
        std::thread runLoopThread_;

        std::unordered_map<HWND, std::unique_ptr<NativeEditor>> controlMap_;

        NativeEditManager();
    };
}
