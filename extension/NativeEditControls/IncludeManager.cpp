#include "IncludeManager.h"

#include <algorithm>
#include <map>
#include <mutex>

namespace CodeToolsVsix
{
    namespace
    {
        std::mutex& cacheMutex()
        {
            static std::mutex mutex;
            return mutex;
        }

        std::map<const newui::reflection::Class*, std::vector<std::string>>& cache()
        {
            static std::map<const newui::reflection::Class*, std::vector<std::string>> map;
            return map;
        }

        void appendUnique(std::vector<std::string>& out, const std::string& header)
        {
            if (!header.empty() && std::find(out.begin(), out.end(), header) == out.end())
            {
                out.push_back(header);
            }
        }

        std::vector<std::string> compute(const newui::reflection::Class* clazz)
        {
            std::vector<const newui::reflection::Class*> chain;
            for (const newui::reflection::Class* c = clazz; c != nullptr; c = c->parentClass())
            {
                chain.push_back(c);
            }
            std::vector<std::string> headers;
            for (auto it = chain.rbegin(); it != chain.rend(); ++it)   // base first
            {
                appendUnique(headers, (*it)->header());
            }
            return headers;
        }
    }

    std::vector<std::string> IncludeManager::getHeaders(const newui::reflection::Class* clazz)
    {
        if (clazz == nullptr)
        {
            return {};
        }
        std::lock_guard<std::mutex> lock(cacheMutex());
        auto found = cache().find(clazz);
        if (found == cache().end())
        {
            found = cache().emplace(clazz, compute(clazz)).first;
        }
        return found->second;
    }

    std::vector<std::string> IncludeManager::getHeaders(const std::vector<const newui::reflection::Class*>& classes)
    {
        std::vector<std::string> headers;
        for (const newui::reflection::Class* clazz : classes)
        {
            for (const std::string& header : getHeaders(clazz))
            {
                appendUnique(headers, header);
            }
        }
        return headers;
    }

    std::string IncludeManager::headerOf(const newui::reflection::Class* clazz)
    {
        return clazz != nullptr ? clazz->header() : std::string();
    }

    std::vector<std::string> IncludeManager::headersOf(const std::vector<const newui::reflection::Class*>& classes)
    {
        std::vector<std::string> headers;
        for (const newui::reflection::Class* clazz : classes)
        {
            appendUnique(headers, headerOf(clazz));
        }
        return headers;
    }

    void IncludeManager::clearCache()
    {
        std::lock_guard<std::mutex> lock(cacheMutex());
        cache().clear();
    }
}
