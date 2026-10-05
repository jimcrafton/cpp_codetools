#include "Settings.h"

#include <newui/runloop.h>

#include <algorithm>
#include <cwchar>
#include <cwctype>

namespace CodeToolsVsix
{
    Settings& Settings::instance()
    {
        static Settings settings;
        return settings;
    }

    void Settings::setRunLoop(newui::RunLoop* loop)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        loop_ = loop;
    }

    void Settings::set(const std::string& key, const std::wstring& value)
    {
        newui::RunLoop* loop = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = values_.find(key);
            if (it != values_.end() && it->second == value) {
                return;
            }
            values_[key] = value;
            loop = loop_;
        }
        // The listeners are read when the task runs, on the loop's thread (where they are also added and
        // removed), so one disconnected since this call is not called.
        if (loop != nullptr) {
            loop->post([this, key]() { onChanged.syncCall(*this, key); });
        } else {
            onChanged.syncCall(*this, key);
        }
    }

    std::wstring Settings::valueOrDefault(const Def& def) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = values_.find(def.key);
        if (it != values_.end()) {
            return it->second;
        }
        const std::string narrow = def.defaultValue;
        return std::wstring(narrow.begin(), narrow.end());   // defaults are ASCII
    }

    std::wstring Settings::getString(const Def& def) const
    {
        return valueOrDefault(def);
    }

    int Settings::getInt(const Def& def, int minValue, int maxValue) const
    {
        const std::wstring value = valueOrDefault(def);
        wchar_t* end = nullptr;
        long parsed = std::wcstol(value.c_str(), &end, 10);
        if (end == value.c_str() || *end != L'\0') {
            const std::string narrow = def.defaultValue;
            parsed = std::wcstol(std::wstring(narrow.begin(), narrow.end()).c_str(), nullptr, 10);
        }
        return static_cast<int>(std::clamp<long>(parsed, minValue, maxValue));
    }

    bool Settings::getBool(const Def& def) const
    {
        std::wstring value = valueOrDefault(def);
        std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
        return value == L"true" || value == L"1" || value == L"on";
    }
}
