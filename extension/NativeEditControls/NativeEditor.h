#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

// runloop.h must come before rootview.h/controls.h - see CppEditorControl.h's own comment on why
// (Delegate<...>::postCall(RunLoop&, ...) needs RunLoop's full definition).
#include <newui/runloop.h>
#include <newui/rootview.h>
#include <newui/bundle.h>

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
    // Every public method here is only ever called through NativeEditManager::runOnEditThread() - see
    // its wrappers below for where that marshaling actually happens - so,
    // like CppEditorControl, this has no thread-safety of its own; callers are responsible for
    // only ever reaching an instance from the edit thread.
    class NativeEditor
    {
    public:
        virtual ~NativeEditor()
        {
            if (rootView_ )
            {
                if (rootViewOwned_) {
                    rootView_.release();
                }
                else {
                    logToDebugOut(L"~NativeEditor about to delete NativeEditor::rootView_");
                    rootView_->destroy();
                }
            }
        }

        HWND windowHandle() const { return rootView_ ? rootView_->windowHandle() : nullptr; }

        // Moves and sizes this editor's window within its parent window (client coordinates). Goes
        // through the RootView, never a raw ::SetWindowPos(): the RootView keeps its own idea of where
        // it is and re-applies that position on every WM_SIZE, so a window moved behind its back snaps
        // straight back to the position it still remembers.
        void setWindowBounds(const newui::Rect& bounds)
        {
            if (rootView_) {
                rootView_->setBounds(bounds);
            }
        }

        virtual bool load(const wchar_t* filePath, std::size_t filePathLength) = 0;
        virtual bool save(const wchar_t* filePath, std::size_t filePathLength) = 0;
        // Virtual so an editor backed by a newui::Document (DesignerEditor) can report that
        // document's own isModified() instead of this class's plain flag.
        virtual bool isDirty() const { return dirty_; }
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

        virtual void markDirty() { dirty_ = true; }
        void clearDirty() { dirty_ = false; }

    protected:
        bool rootViewOwned_ = false;
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

			// Bundle::instance() otherwise resolves resourcesDir() off the
			// hosting process's own exe (devenv.exe in the VSIX) - point it
			// at this DLL's own directory instead, where Resources/ is
			// actually deployed (CodeToolsVsix.csproj).
			char pathBuf[MAX_PATH]{};
			DWORD len = ::GetModuleFileNameA(hInstance, pathBuf, MAX_PATH);
			if (len > 0 && len < MAX_PATH) {
				std::string path(pathBuf, len);
				std::size_t pos = path.find_last_of("\\/");
				if (pos != std::string::npos) {
					newui::Bundle::instance().setExecutableDirOverride(path.substr(0, pos));
				}
			}
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

        // Runs func on the edit thread and waits for its result (RunLoop::postAndWait). While it waits,
        // the calling thread - VS's UI thread - wakes every 100 ms to pump its own messages
        // (RunLoop::runTillNotified), so it is not hard-blocked but it is re-entrant. Afterwards, back on
        // the calling thread, delivers the log lines the edit thread queued (Logging.h's log()): the
        // managed log sink may only be called from a thread the CLR knows, and nothing else flushes them.
        template <typename Func>
        static auto runOnEditThread(Func&& func) {
            using Result = decltype(func());
            if constexpr (std::is_void_v<Result>) {
                instance().runLoop()->postAndWait(std::forward<Func>(func));
                flushQueuedLogs();
            } else {
                Result result = instance().runLoop()->postAndWait(std::forward<Func>(func));
                flushQueuedLogs();
                return result;
            }
        }

        static NativeEditor* createEditor(HWND hwndParent, int x, int y, int width, int height, DocumentType documentType);

        // contentHost: where the editor's own content children get added -
        // nullptr (default) means rootView itself, matching every existing
        // caller (the VSIX extension always wants its content filling the
        // whole hosting RootView). A caller that's already built other
        // chrome onto rootView (e.g. testharness's own directory tree pane)
        // passes a child SubView of rootView instead, so the editor's
        // content lands there rather than displacing that chrome - see
        // CppEditor::setupUI()/DesignerEditor::setupUI()'s own comments.
        static NativeEditor* createEditor(newui::RootView* rootView, DocumentType documentType,
            newui::SubView* contentHost = nullptr);

		static bool closeEditor(HWND hwnd) {
			auto& instance = NativeEditManager::instance();

            bool result = runOnEditThread([&]() -> bool {
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

            bool result = runOnEditThread([&]() -> bool {
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

            bool result = runOnEditThread([&]() -> bool {
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

            bool result = runOnEditThread([&]() -> bool {
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

            bool result = runOnEditThread([&]() -> bool {
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
