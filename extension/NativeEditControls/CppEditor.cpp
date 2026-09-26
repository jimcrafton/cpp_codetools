#include "CppEditor.h"
#include "CppDiagnostics.h"
#include "CppHighlight.h"
#include "TextEncoding.h"
#include "Logging.h"

#include <cpptools/parser.h>
#include <cpptools/symbol.h>
#include <cpptools/log.h>

#include <newui/layout.h>
#include <newui/texthistory.h>
#include <newui/uicolormanager.h>

#include <vector>

namespace CodeToolsVsix
{
    namespace
    {
        // filePath is never assumed to be null-terminated (see NativeEditor.h) - reads
        // exactly filePathLength wchar_t characters and builds an internally-owned, properly
        // null-terminated std::wstring from them, so every downstream Win32 call (CreateFileW,
        // etc.) gets a safe buffer regardless of what the caller actually passed.
        std::wstring copyPath(const wchar_t* filePath, std::size_t filePathLength)
        {
            if (!filePath || filePathLength == 0)
            {
                return std::wstring();
            }

            return std::wstring(filePath, filePathLength);
        }

        bool readFileUtf8(const std::wstring& path, std::string& outUtf8)
        {
            HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE)
            {
                return false;
            }

            LARGE_INTEGER size{};
            if (!GetFileSizeEx(file, &size) || size.QuadPart < 0)
            {
                CloseHandle(file);
                return false;
            }

            outUtf8.assign(static_cast<std::size_t>(size.QuadPart), '\0');

            bool ok = true;
            if (!outUtf8.empty())
            {
                DWORD bytesRead = 0;
                ok = ReadFile(file, outUtf8.data(), static_cast<DWORD>(outUtf8.size()), &bytesRead, nullptr) != FALSE
                     && bytesRead == outUtf8.size();
            }

            CloseHandle(file);
            return ok;
        }

        bool writeFileUtf8(const std::wstring& path, const std::string& utf8)
        {
            HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE)
            {
                return false;
            }

            bool ok = true;
            if (!utf8.empty())
            {
                DWORD bytesWritten = 0;
                ok = WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &bytesWritten, nullptr) != FALSE
                     && bytesWritten == utf8.size();
            }

            CloseHandle(file);
            return ok;
        }

        const wchar_t* commandName(EditorCommand command)
        {
            switch (command)
            {
            case EditorCommand::Undo: return L"Undo";
            case EditorCommand::Redo: return L"Redo";
            case EditorCommand::Cut: return L"Cut";
            case EditorCommand::Copy: return L"Copy";
            case EditorCommand::Paste: return L"Paste";
            case EditorCommand::Find: return L"Find";
            case EditorCommand::Replace: return L"Replace";
            case EditorCommand::GotoLine: return L"GotoLine";
            default: return L"Unknown";
            }
        }

        // Registers this DLL's log() as cpptools's log sink (see cpptools/log.h) - once, on first
        // use. A C++11 function-local static's initialization is itself thread-safe/exactly-once,
        // so no separate guard/mutex is needed.
        void registerCppToolsLogSink()
        {
            static bool registered = []() {
                cpptools::setLogSink([](cpptools::Severity severity, const std::string& message) {
                    CodeToolsVsix::log(severity, message);
                });
                return true;
            }();
            (void)registered;
        }

        // Parses contentUtf8 (the buffer just loaded) via cpptools::Parser and formats an
        // indented outline. Never throws - a parse failure just means no outline, not a failure
        // to load the file.
        std::wstring buildOutline(const std::wstring& filePath, const std::string& contentUtf8, const cpptools::CompileFlags& flags)
        {
            try
            {
                registerCppToolsLogSink();

                cpptools::Parser parser;
                cpptools::ParseResult result = parser.parseBuffer(wideToUtf8(filePath.c_str(), filePath.size()), contentUtf8, flags.args);

                return formatOutline(result, flags.origin);
            }
            catch (...)
            {
                return L"--- Outline (cpptools): parse failed ---";
            }
        }
    }

    CppEditor::CppEditor(newui::RootView* rootView, newui::SubView* contentHost)
    {
        rootViewOwned_ = true;

        if (!setupUI(rootView, contentHost))
        {
            return;
        }

        setRootView(std::unique_ptr<newui::RootView>(rootView));
    }

    CppEditor::CppEditor(HWND hwndParent, int x, int y, int width, int height)
    {
        logToDebugOut(L"CppEditorControl");

        auto root = std::make_unique<newui::RootView>(
            hwndParent, NativeEditManager::moduleHandle(),
            newui::Rect(static_cast<float>(x), static_cast<float>(y), static_cast<float>(width), static_cast<float>(height)),
            "cppEditorRoot");


        if ( !setupUI(root.get()) )
        {
            return ;
        }

        setRootView(std::move(root));

        

        logToDebugOut(L"CppEditorControl completed");
    }

    bool CppEditor::setupUI(newui::RootView* root, newui::SubView* contentHost)
    {
        root->style().setBackgroundColor(newui::UIColorManager::colorFor(newui::UIColorRole::WindowBackground));

        // contentHost: nullptr (the default - every pre-existing caller) means root itself gets
        // both the layout and the two TextControls below, unchanged from before this parameter
        // existed. A caller that already built other chrome directly onto root (e.g.
        // testharness's own directory tree pane, added as a sibling of contentHost under root's
        // own top-level layout) passes that pane instead, so this editor's own layout/content
        // only ever touches its own subtree, never displacing that other chrome.
        newui::View* host = contentHost != nullptr ? static_cast<newui::View*>(contentHost) : static_cast<newui::View*>(root);

        auto rootLayout = std::make_unique<newui::FlexLayout>(newui::Orientation::Vertical);
        rootLayout->setSpacing(0.0f);
        rootLayout->setPadding(0.0f);
        host->setLayout(std::move(rootLayout));

        // Each pane is a text control hosted by a ScrollView, which scrolls it (a TextControl has no
        // scrollbar of its own). The scroll views share the vertical space: most of it to the source.
        auto* scrollView = new newui::ScrollView();
        scrollView->setName("cppEditorScroll");
        scrollView->setVisible(true);
        scrollView->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(3.0f));
        host->addChild(scrollView);

        // A TextFoldingControl: line numbers, folding, colors. Its model remembers edits for undo -
        // installed before anything subscribes to the model.
        auto* textControl = new newui::TextFoldingControl();
        textControl->setName("cppEditorText");
        textControl->setVisible(true);
        textControl->setModel(std::make_unique<newui::text::HistoryTextModel>());
        scrollView->addChild(textControl);

        auto* outlineScroll = new newui::ScrollView();
        outlineScroll->setName("cppEditorOutlineScroll");
        outlineScroll->setVisible(true);
        outlineScroll->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
        host->addChild(outlineScroll);

        auto* outlineControl = new newui::TextControl();
        outlineControl->setName("cppEditorOutline");
        outlineControl->setVisible(true);
        outlineControl->inputTraits().setReadOnly(true);
        // Visually distinct from the editable pane above it, so it doesn't read as "more of the
        // same editable buffer" - same UIColorManager pattern the root's own background uses.
        outlineControl->style().setBackgroundColor(newui::UIColorManager::colorFor(newui::UIColorRole::ControlBackground));
        outlineScroll->addChild(outlineControl);
        if (!this->rootViewOwned_) {
            if (!root->initialize())
            {
                // Leave the base's RootView/textControl_/outlineControl_ null - windowHandle()
                // reports failure the same way StandInEditControl::Create() used to (a null HWND),
                // and load/save/execCommand all already guard on textControl_ being null before
                // touching it.
                logToDebugOut(L"!root->initialize()");
                return false;
            }
        }
        

        // Only the editable pane's changes count as "dirty" - outlineControl_'s own setText()
        // calls (load(), below) fire this same delegate too, but nothing should ever mark the
        // document dirty just because the outline was refreshed.
        textControl->model().onChanged.add([this](newui::Model&) {
            markDirty();
            return newui::SyncReturn::Handled;
            });

        
        scrollView_ = scrollView;
        textControl_ = textControl;
        outlineScroll_ = outlineScroll;
        outlineControl_ = outlineControl;

        // Colors and folds follow the text, off the UI thread; a slower pass parses it with libclang
        // for syntax-error squiggles, and keeps the outline current.
        highlight_ = std::make_unique<HighlightController>(*textControl, &analyzeCpp);
        document_ = std::make_shared<CppDocument>();
        highlight_->setOverlayAnalyzer([document = document_](const std::wstring& text) {
            return analyzeCppDiagnostics(text, document);
        });
        highlight_->setOnOverlayApplied([this](HighlightOverlay& overlay) {
            if (const auto* outline = std::any_cast<std::wstring>(&overlay.extra)) {
                setOutlineText(*outline);
            }
        });

        return true;
    }

    bool CppEditor::load(const wchar_t* filePath, std::size_t filePathLength)
    {
        if (!textControl_)
        {
            CodeToolsVsix::log(cpptools::Severity::Error, "CppEditorControl::load: textControl_ is null (construction must have failed)");
            return false;
        }

        std::wstring path = copyPath(filePath, filePathLength);
        if (path.empty())
        {
            CodeToolsVsix::log(cpptools::Severity::Error, "CppEditorControl::load: empty path");
            return false;
        }

        std::string contentUtf8;
        if (!readFileUtf8(path, contentUtf8))
        {
            CodeToolsVsix::log(cpptools::Severity::Error, "CppEditorControl::load: readFileUtf8 failed for " + wideToUtf8(path.c_str(), path.size()));
            return false;
        }

        // The parse pretends the text is this file (so its own directory is searched for includes).
        document_->setPath(wideToUtf8(path.c_str(), path.size()));
        textControl_->setText(utf8ToWide(contentUtf8));
        clearDirty();

        // Right away, rather than after the first background parse.
        setOutlineText(buildOutline(path, contentUtf8, document_->flags()));

        return true;
    }

    void CppEditor::setOutlineText(const std::wstring& outline)
    {
        if (!outlineControl_ || outlineControl_->text() == outline)
        {
            return;
        }
        // TextController::handleModelBeforeRangeChanged() (controls.cpp) vetoes *any* model
        // change - including a programmatic setText(), not just user keystrokes - while
        // isReadOnly() is true. Lift it only for this call, then restore it immediately, so
        // the pane can still be refreshed while staying non-editable to the user the rest of the
        // time.
        outlineControl_->inputTraits().setReadOnly(false);
        outlineControl_->setText(outline);
        outlineControl_->inputTraits().setReadOnly(true);
    }

    bool CppEditor::save(const wchar_t* filePath, std::size_t filePathLength)
    {
        if (!textControl_)
        {
            return false;
        }

        std::wstring path = copyPath(filePath, filePathLength);
        if (path.empty())
        {
            return false;
        }

        if (!writeFileUtf8(path, wideToUtf8(textControl_->text().c_str(), textControl_->text().size())))
        {
            return false;
        }

        clearDirty();
        return true;
    }

    bool CppEditor::execCommand(EditorCommand command, std::uint32_t flags, const EditorCommandArgs* args)
    {
        // The editing commands act on the source pane (what "handled" means to the caller: false
        // when there's nothing to undo / redo / copy / paste).
        if (textControl_ != nullptr)
        {
            switch (command)
            {
            case EditorCommand::Undo: return textControl_->undo();
            case EditorCommand::Redo: return textControl_->redo();
            case EditorCommand::Cut: return textControl_->cut();
            case EditorCommand::Copy: return textControl_->copy();
            case EditorCommand::Paste: return textControl_->paste();
            default: break;
            }
        }

        // STUB: Find / Replace / GotoLine are logged no-ops - real behavior is still to come.
        wchar_t buffer[256];
        if (args)
        {
            swprintf_s(buffer,
                       L"CppEditorControl::execCommand: %s stub (flags=%u, text1Length=%zu, text2Length=%zu, number=%lld)\n",
                       commandName(command), flags, args->text1Length, args->text2Length, args->number);
        }
        else
        {
            swprintf_s(buffer, L"CppEditorControl::execCommand: %s stub (flags=%u, no args)\n", commandName(command), flags);
        }

        OutputDebugStringW(buffer);
        return true;
    }
}
