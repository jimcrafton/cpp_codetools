#include "TextEncoding.h"

namespace CodeToolsVsix
{
    std::wstring utf8ToWide(const std::string& utf8)
    {
        if (utf8.empty())
        {
            return std::wstring();
        }

        int chars = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
        if (chars <= 0)
        {
            return std::wstring();
        }

        std::wstring result(static_cast<std::size_t>(chars), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), result.data(), chars);
        return result;
    }

    std::string wideToUtf8(const std::wstring& wstr)
    {
        return wideToUtf8(wstr.c_str(), wstr.length());
    }

    std::string wideToUtf8(const wchar_t* wide, std::size_t length)
    {
        if (!wide || length == 0)
        {
            return std::string();
        }

        int bytes = WideCharToMultiByte(CP_UTF8, 0, wide, static_cast<int>(length), nullptr, 0, nullptr, nullptr);
        if (bytes <= 0)
        {
            return std::string();
        }

        std::string result(static_cast<std::size_t>(bytes), '\0');
        WideCharToMultiByte(CP_UTF8, 0, wide, static_cast<int>(length), result.data(), bytes, nullptr, nullptr);
        return result;
    }
}
