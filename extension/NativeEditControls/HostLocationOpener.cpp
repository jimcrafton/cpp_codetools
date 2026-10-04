#include "HostLocationOpener.h"

namespace CodeToolsVsix
{
    HostLocationOpener& HostLocationOpener::instance()
    {
        static HostLocationOpener opener;
        return opener;
    }

    bool HostLocationOpener::open(const std::wstring& path, std::size_t line, std::size_t column) const
    {
        const HostOpenLocationCallback callback = callback_.load();
        if (callback == nullptr || path.empty()) {
            return false;
        }
        callback(path.c_str(), path.size(), static_cast<std::uint64_t>(line), static_cast<std::uint64_t>(column));
        return true;
    }
}
