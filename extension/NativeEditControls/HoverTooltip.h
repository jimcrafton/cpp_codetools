#pragma once

#include <windows.h>
#include <commctrl.h>

#include <string>

#pragma comment(lib, "comctl32.lib")

namespace CodeToolsVsix
{
    // One native tracking tooltip, shown at a screen point and hidden again - for a control that
    // draws its own sub-items (the Delegates row's chips) and so has nothing for Win32 to hook.
    // Create/use/destroy on the thread that owns `owner`.
    class HoverTooltip
    {
    public:
        HoverTooltip() = default;
        HoverTooltip(const HoverTooltip&) = delete;
        HoverTooltip& operator=(const HoverTooltip&) = delete;
        ~HoverTooltip() { destroy(); }

        void show(HWND owner, long screenX, long screenY, const std::wstring& text)
        {
            if (owner == nullptr || !::IsWindow(owner)) {
                return;
            }
            if (tip_ != nullptr && owner_ != owner) {
                destroy();
            }
            if (tip_ == nullptr && !create(owner)) {
                return;
            }
            if (text != text_) {
                text_ = text;
                TOOLINFOW info = toolInfo();
                info.lpszText = const_cast<wchar_t*>(text_.c_str());
                ::SendMessageW(tip_, TTM_SETTOOLINFOW, 0, reinterpret_cast<LPARAM>(&info));
            }
            ::SendMessageW(tip_, TTM_TRACKPOSITION, 0, MAKELPARAM(screenX, screenY));
            if (!visible_) {
                TOOLINFOW info = toolInfo();
                ::SendMessageW(tip_, TTM_TRACKACTIVATE, TRUE, reinterpret_cast<LPARAM>(&info));
                visible_ = true;
            }
        }

        void hide()
        {
            if (tip_ != nullptr && visible_) {
                TOOLINFOW info = toolInfo();
                ::SendMessageW(tip_, TTM_TRACKACTIVATE, FALSE, reinterpret_cast<LPARAM>(&info));
            }
            visible_ = false;
        }

        bool visible() const { return visible_; }

    private:
        static constexpr UINT_PTR kToolId = 1;

        bool create(HWND owner)
        {
            tip_ = ::CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
                                     CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, owner, nullptr,
                                     ::GetModuleHandleW(nullptr), nullptr);
            if (tip_ == nullptr) {
                return false;
            }
            owner_ = owner;
            ::SendMessageW(tip_, TTM_SETMAXTIPWIDTH, 0, 400);   // also enables multi-line text
            text_ = L" ";
            TOOLINFOW info = toolInfo();
            info.uFlags = TTF_TRACK | TTF_ABSOLUTE;
            info.lpszText = const_cast<wchar_t*>(text_.c_str());
            ::SendMessageW(tip_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&info));
            return true;
        }

        TOOLINFOW toolInfo() const
        {
            TOOLINFOW info{};
            info.cbSize = sizeof(info);
            info.uFlags = TTF_TRACK | TTF_ABSOLUTE;
            info.hwnd = owner_;
            info.uId = kToolId;
            return info;
        }

        void destroy()
        {
            if (tip_ != nullptr) {
                if (::IsWindow(tip_)) {
                    ::DestroyWindow(tip_);
                }
                tip_ = nullptr;
            }
            owner_ = nullptr;
            visible_ = false;
            text_.clear();
        }

        HWND tip_ = nullptr;
        HWND owner_ = nullptr;
        bool visible_ = false;
        std::wstring text_;
    };
}
