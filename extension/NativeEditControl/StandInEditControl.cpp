#include "StandInEditControl.h"
#include "TextEncoding.h"
#include "Logging.h"

#include <cpptools/parser.h>
#include <cpptools/symbol.h>
#include <cpptools/log.h>

#include <cstdio>
#include <string>
#include <vector>

namespace CodeToolsVsix
{
    namespace
    {
        // filePath is never assumed to be null-terminated (see NativeEditControl.h) - reads
        // exactly filePathLength wchar_t characters and builds an internally-owned, properly
        // null-terminated std::wstring from them, so every downstream Win32 call (CreateFileW,
        // etc.) gets a safe buffer regardless of what the caller actually passed.
        std::wstring CopyPath(const wchar_t* filePath, std::size_t filePathLength)
        {
            if (!filePath || filePathLength == 0)
            {
                return std::wstring();
            }

            return std::wstring(filePath, filePathLength);
        }

        bool ReadFileUtf8(const std::wstring& path, std::string& outUtf8)
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

        bool WriteFileUtf8(const std::wstring& path, const std::string& utf8)
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
        const wchar_t* KindSpelling(cpptools::SymbolKind kind)
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

        void AppendSymbols(const std::vector<cpptools::Symbol>& symbols, unsigned depth, std::wstring& out)
        {
            for (const cpptools::Symbol& symbol : symbols)
            {
                out += L"\r\n";
                out.append(static_cast<std::size_t>(depth) * 2, L' ');
                out += KindSpelling(symbol.kind);
                out += L' ';
                out += Utf8ToWide(symbol.name);
                out += L" @ " + std::to_wstring(symbol.location.line) + L':' + std::to_wstring(symbol.location.column);

                AppendSymbols(symbol.children, depth + 1, out);
            }
        }

        const wchar_t* CommandName(EditorCommand command)
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

        // Registers this DLL's Log() as cpptools's log sink (see cpptools/log.h) - once, on first
        // use. A C++11 function-local static's initialization is itself thread-safe/exactly-once,
        // so no separate guard/mutex is needed. Cheaper and simpler than doing this in DllMain
        // (which the CRT/loader-lock discourages for anything beyond trivial work).
        void EnsureCpptoolsLogSinkRegistered()
        {
            static bool registered = []() {
                cpptools::setLogSink([](cpptools::Severity severity, const std::string& message) {
                    Log(severity, message);
                });
                return true;
            }();
            (void)registered;
        }

        // Parses contentUtf8 (the buffer just loaded, not necessarily what's on disk - it never
        // will be, in fact, since this control has no editing UI yet) via cpptools::Parser and
        // formats an indented outline. Never throws - a parse failure just means no outline, not
        // a failure to load the file.
        std::wstring BuildOutline(const std::wstring& filePath, const std::string& contentUtf8)
        {
            try
            {
                EnsureCpptoolsLogSinkRegistered();

                cpptools::Parser parser;
                cpptools::ParseResult result = parser.parseBuffer(WideToUtf8(filePath.c_str(), filePath.size()), contentUtf8);

                if (result.symbols.empty())
                {
                    return L"--- Outline (cpptools): no symbols found ---";
                }

                std::wstring outline = L"--- Outline (cpptools) ---";
                AppendSymbols(result.symbols, 0, outline);
                return outline;
            }
            catch (...)
            {
                return L"--- Outline (cpptools): parse failed ---";
            }
        }
    }

    const wchar_t StandInEditControl::kClassName[] = L"CodeToolsVsix.StandInEditControl";

    bool StandInEditControl::RegisterWindowClass(HINSTANCE hInstance)
    {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = &StandInEditControl::WndProc;
        wc.hInstance = hInstance;
        wc.hCursor = LoadCursorW(nullptr, IDC_IBEAM);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = kClassName;

        if (RegisterClassExW(&wc) != 0)
        {
            return true;
        }

        // Already registered (e.g. a second control instance) is fine; anything else isn't.
        return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }

    HWND StandInEditControl::Create(HWND hwndParent, int x, int y, int width, int height, HINSTANCE hInstance)
    {
        if (!RegisterWindowClass(hInstance))
        {
            return nullptr;
        }

        auto* control = new StandInEditControl();

        HWND hwnd = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            kClassName,
            L"",
            WS_CHILD | WS_VISIBLE,
            x, y, width, height,
            hwndParent,
            nullptr,
            hInstance,
            control);

        if (!hwnd)
        {
            delete control;
            return nullptr;
        }

        return hwnd;
    }

    StandInEditControl* StandInEditControl::FromHandle(HWND hwnd)
    {
        if (!hwnd)
        {
            return nullptr;
        }

        return reinterpret_cast<StandInEditControl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    bool StandInEditControl::Load(const wchar_t* filePath, std::size_t filePathLength)
    {
        std::wstring path = CopyPath(filePath, filePathLength);
        if (path.empty())
        {
            return false;
        }

        std::string contentUtf8;
        if (!ReadFileUtf8(path, contentUtf8))
        {
            return false;
        }

        SetWindowTextW(m_hwnd, Utf8ToWide(contentUtf8).c_str());
        m_annotation = BuildOutline(path, contentUtf8);
        m_dirty = false;

        InvalidateRect(m_hwnd, nullptr, TRUE);
        return true;
    }

    bool StandInEditControl::Save(const wchar_t* filePath, std::size_t filePathLength)
    {
        std::wstring path = CopyPath(filePath, filePathLength);
        if (path.empty())
        {
            return false;
        }

        int length = GetWindowTextLengthW(m_hwnd);
        std::wstring text(static_cast<std::size_t>(length > 0 ? length : 0), L'\0');
        if (length > 0)
        {
            GetWindowTextW(m_hwnd, text.data(), length + 1);
        }

        if (!WriteFileUtf8(path, WideToUtf8(text.c_str(), text.size())))
        {
            return false;
        }

        m_dirty = false;
        return true;
    }

    bool StandInEditControl::ExecCommand(EditorCommand command, std::uint32_t flags, const EditorCommandArgs* args)
    {
        // STUB: every command is a logged no-op today. Real per-command behavior (actually
        // undoing, actually cutting a selection, ...) lands here once this control has a real
        // text buffer/selection to act on - see the class comment and NativeEditControl.h.
        wchar_t buffer[256];
        if (args)
        {
            swprintf_s(buffer,
                       L"StandInEditControl::ExecCommand: %s stub (flags=%u, text1Length=%zu, text2Length=%zu, number=%lld)\n",
                       CommandName(command), flags, args->text1Length, args->text2Length, args->number);
        }
        else
        {
            swprintf_s(buffer, L"StandInEditControl::ExecCommand: %s stub (flags=%u, no args)\n", CommandName(command), flags);
        }

        OutputDebugStringW(buffer);
        return true;
    }

    LRESULT CALLBACK StandInEditControl::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        StandInEditControl* self;

        if (msg == WM_NCCREATE)
        {
            auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<StandInEditControl*>(createStruct->lpCreateParams);
            self->m_hwnd = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        else
        {
            self = FromHandle(hwnd);
        }

        if (self)
        {
            return self->HandleMessage(hwnd, msg, wParam, lParam);
        }

        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    LRESULT StandInEditControl::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        {
        case WM_SETTEXT:
        {
            // Let DefWindowProc store the text in the window's normal internal buffer, then
            // repaint to show it - no separate storage of our own to keep in sync.
            LRESULT result = DefWindowProcW(hwnd, msg, wParam, lParam);
            InvalidateRect(hwnd, nullptr, TRUE);
            return result;
        }

        case WM_SIZE:
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;

        case WM_PAINT:
            HandlePaint(hwnd);
            return 0;

        case WM_ERASEBKGND:
            // WM_PAINT below repaints the full client area every time, so skip the separate erase
            // to avoid an extra flicker-prone fill.
            return 1;

        case WM_NCDESTROY:
        {
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            LRESULT result = DefWindowProcW(hwnd, msg, wParam, lParam);
            delete this;
            return result;
        }

        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }
    }

    void StandInEditControl::HandlePaint(HWND hwnd)
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT client;
        GetClientRect(hwnd, &client);
        FillRect(hdc, &client, reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));

        int length = GetWindowTextLengthW(hwnd);
        std::wstring text(static_cast<std::size_t>(length > 0 ? length : 0), L'\0');
        if (length > 0)
        {
            GetWindowTextW(hwnd, text.data(), length + 1);
        }

        std::wstring display =
            L"[codetools++ stand-in edit control - replace HandlePaint()/HandleMessage() with a real editor]\r\n\r\n" + text;

        if (!m_annotation.empty())
        {
            display += L"\r\n\r\n" + m_annotation;
        }

        RECT textRect = client;
        InflateRect(&textRect, -6, -6);
        DrawTextW(hdc, display.c_str(), -1, &textRect, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);

        EndPaint(hwnd, &ps);
    }
}
