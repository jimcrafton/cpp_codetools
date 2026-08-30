#pragma once
#include <Windows.h>
#include <cstddef>
#include <string>

namespace CodeToolsVsix
{
    // Shared UTF-8 <-> UTF-16 conversion, used anywhere this DLL crosses between std::string
    // (UTF-8, e.g. cpptools::Symbol::name) and std::wstring/wchar_t* (UTF-16, this DLL's native
    // text and every Win32/wide-string call). Factored out of StandInEditControl.cpp so Logging.cpp
    // (and anything else that needs it later) doesn't duplicate it.
    std::wstring utf8ToWide(const std::string& utf8);
    std::string wideToUtf8(const wchar_t* wide, std::size_t length);
}
