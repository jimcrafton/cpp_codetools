#include "NativeEditor.h"
#include "Logging.h"
#include "TextEncoding.h"

#include <cpptools/version.h>
#include <newui/version.h>


#include "CppEditor.h"
#include "DesignerEditor.h"

// Defined in the reflectgen-generated .cpp (compiled into the `newui`
// target, global namespace) - self-guarding at the source (only actually
// registers once per process, however many places call it - see its own
// comment in reflectgen.py's generate()), so every call site can just
// call it directly.
extern void registerReflectionData();

namespace CodeToolsVsix
{
    // Every real newui example app calls registerReflectionData() once at
    // startup; nothing in this DLL (or testharness.cpp, which also goes
    // through NativeEditManager::instance() before ever constructing a
    // DesignerEditor/Workspace/Toolbox) ever did. Real gap found via
    // CodeToolsVsix::ToolboxRegistry's reflection-driven category scan
    // coming back empty against a never-populated ReflectionRegistry.
    NativeEditManager::NativeEditManager()
    {
        registerReflectionData();
    }

	void NativeEditManager::startRunLoop()
	{
		auto& instance = NativeEditManager::instance();
        if (nullptr == instance.runLoop_) {
            auto [runLoopPtr, loopThread] = newui::RunLoop::runThreaded();
            instance.runLoop_ = runLoopPtr;
            instance.runLoopThread_ = std::move(loopThread);
            instance.runLoop_->waitForStart();
        }
	}

    void NativeEditManager::shutdown()
    {
        auto& instance = NativeEditManager::instance();
        if (!instance.runLoopThread_.joinable())
        {
            return;
        }

        instance.runLoop_->quit();
        instance.runLoopThread_.join();
    }

    NativeEditor* NativeEditManager::createEditor(newui::RootView* rootView, DocumentType documentType)
    {
        auto& instance = NativeEditManager::instance();

        NativeEditor* result = nullptr;

        std::unique_ptr<NativeEditor> editor;
        switch (documentType)
        {
            case DocumentType::Designer:
                editor = std::make_unique<DesignerEditor>(rootView);
                break;
            case DocumentType::CppSource:
            default:
                editor = std::make_unique<CppEditor>(rootView);
                break;
        }
    

        auto hwnd = editor->windowHandle();

        if (!hwnd)
        {
            return nullptr;
        }

        result = editor.get();

        instance.controlMap_.emplace(hwnd, std::move(editor));
    
    

        return result;
    }

    NativeEditor* NativeEditManager::createEditor(HWND hwndParent, int x, int y, int width, int height, DocumentType documentType)
    {
        auto& instance = NativeEditManager::instance();

        instance.startRunLoop();

        logToDebugOut(L"NativeEditManager::createEditor");

        NativeEditor* result = nullptr;

        result = instance.runLoop()->postAndWait([&]() -> NativeEditor* {

            logToDebugOut(L"runloop runAndWait");

            std::unique_ptr<NativeEditor> editor;
            switch (documentType)
            {
            case DocumentType::Designer:
                editor = std::make_unique<DesignerEditor>(hwndParent, x, y, width, height);
                break;
            case DocumentType::CppSource:
            default:
                editor = std::make_unique<CppEditor>(hwndParent, x, y, width, height);
                break;
            }

			auto hwnd = editor->windowHandle();

            if (!hwnd)
            {
                return nullptr;
            }
			auto editorPtr = editor.get();

            instance.controlMap_.emplace(hwnd, std::move(editor));
            return editorPtr;
            });

        return result;
    }

}