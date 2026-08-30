#include "CppEditorControl.h"
#include "EditThreadHost.h"
#include "TextEncoding.h"
#include "Logging.h"

#include <cpptools/parser.h>
#include <cpptools/symbol.h>
#include <cpptools/log.h>

#include <newui/layout.h>
#include <newui/uicolormanager.h>

#include <vector>

namespace CodeToolsVsix
{
    namespace
    {
        // filePath is never assumed to be null-terminated (see NativeEditControl.h) - reads
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

        // Fixed strings for cpptools::SymbolKind - deliberately not libclang's own kind spelling
        // (cpptools::Symbol doesn't retain that), just a readable label for the outline display.
        const wchar_t* kindSpelling(cpptools::SymbolKind kind)
        {
            switch (kind)
            {
            case cpptools::SymbolKind::Namespace: return L"Namespace";
            case cpptools::SymbolKind::Class: return L"Class";
            case cpptools::SymbolKind::Struct: return L"Struct";
            case cpptools::SymbolKind::Union: return L"Union";
            case cpptools::SymbolKind::Enum: return L"Enum";
            case cpptools::SymbolKind::ClassTemplate: return L"ClassTemplate";
            case cpptools::SymbolKind::Function: return L"Function";
            case cpptools::SymbolKind::Method: return L"Method";
            case cpptools::SymbolKind::Constructor: return L"Constructor";
            case cpptools::SymbolKind::Destructor: return L"Destructor";
            case cpptools::SymbolKind::Field: return L"Field";
            case cpptools::SymbolKind::Variable: return L"Variable";
            case cpptools::SymbolKind::Typedef: return L"Typedef";
            default: return L"Other";
            }
        }

        void appendSymbols(const std::vector<cpptools::Symbol>& symbols, unsigned depth, std::wstring& out)
        {
            for (const cpptools::Symbol& symbol : symbols)
            {
                out += L"\r\n";
                out.append(static_cast<std::size_t>(depth) * 2, L' ');
                out += kindSpelling(symbol.kind);
                out += L' ';
                out += utf8ToWide(symbol.name);
                out += L" @ " + std::to_wstring(symbol.location.line) + L':' + std::to_wstring(symbol.location.column);

                appendSymbols(symbol.children, depth + 1, out);
            }
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
        void ensureCpptoolsLogSinkRegistered()
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
        std::wstring buildOutline(const std::wstring& filePath, const std::string& contentUtf8)
        {
            try
            {
                ensureCpptoolsLogSinkRegistered();

                cpptools::Parser parser;
                cpptools::ParseResult result = parser.parseBuffer(wideToUtf8(filePath.c_str(), filePath.size()), contentUtf8);

                if (result.symbols.empty())
                {
                    return L"--- Outline (cpptools): no symbols found ---";
                }

                std::wstring outline = L"--- Outline (cpptools) ---";
                appendSymbols(result.symbols, 0, outline);
                return outline;
            }
            catch (...)
            {
                return L"--- Outline (cpptools): parse failed ---";
            }
        }
    }

    CppEditorControl::CppEditorControl(HWND hwndParent, int x, int y, int width, int height)
    {
        auto root = std::make_unique<newui::RootView>(
            hwndParent, EditThreadHost::moduleHandle(),
            newui::Rect(static_cast<float>(x), static_cast<float>(y), static_cast<float>(width), static_cast<float>(height)),
            "cppEditorRoot");

        root->style().setBackgroundColor(newui::UIColorManager::colorFor(newui::UIColorRole::WindowBackground));

        auto rootLayout = std::make_unique<newui::FlexLayout>(newui::Orientation::Vertical);
        rootLayout->setSpacing(0.0f);
        rootLayout->setPadding(0.0f);
        root->setLayout(std::move(rootLayout));

        auto* textControl = new newui::TextControl();
        textControl->setVisible(true);
        // Most of the space - the outline pane below gets the rest.
        textControl->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(3.0f));
        root->addChild(textControl);

        auto* outlineControl = new newui::TextControl();
        outlineControl->setVisible(true);
        outlineControl->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
        outlineControl->inputTraits().setReadOnly(true);
        // Visually distinct from the editable pane above it, so it doesn't read as "more of the
        // same editable buffer" - same UIColorManager pattern the root's own background uses.
        outlineControl->style().setBackgroundColor(newui::UIColorManager::colorFor(newui::UIColorRole::ControlBackground));
        root->addChild(outlineControl);

        if (!root->initialize())
        {
            // Leave the base's RootView/textControl_/outlineControl_ null - windowHandle()
            // reports failure the same way StandInEditControl::Create() used to (a null HWND),
            // and load/save/execCommand all already guard on textControl_ being null before
            // touching it.
            return;
        }

        // Only the editable pane's changes count as "dirty" - outlineControl_'s own setText()
        // calls (load(), below) fire this same delegate too, but nothing should ever mark the
        // document dirty just because the outline was refreshed.
        textControl->model().onChanged.add([this](newui::Model&) {
            markDirty();
            return newui::SyncReturn::Handled;
            });

        setRootView(std::move(root));
        textControl_ = textControl;
        outlineControl_ = outlineControl;
    }

    bool CppEditorControl::load(const wchar_t* filePath, std::size_t filePathLength)
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

        textControl_->setText(utf8ToWide(contentUtf8));
        clearDirty();

        if (outlineControl_)
        {
            // TextController::handleModelBeforeRangeChanged() (controls.cpp) vetoes *any* model
            // change - including a programmatic setText(), not just user keystrokes - while
            // isReadOnly() is true. Lift it only for this call, then restore it immediately, so
            // the pane can still be refreshed on every load() while staying non-editable to the
            // user the rest of the time.
            outlineControl_->inputTraits().setReadOnly(false);
            outlineControl_->setText(buildOutline(path, contentUtf8));
            outlineControl_->inputTraits().setReadOnly(true);
        }

        return true;
    }

    bool CppEditorControl::save(const wchar_t* filePath, std::size_t filePathLength)
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

    bool CppEditorControl::execCommand(EditorCommand command, std::uint32_t flags, const EditorCommandArgs* args)
    {
        // STUB: every command is a logged no-op today, same as StandInEditControl's own
        // execCommand - real per-command behavior is out of scope for this phase.
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
