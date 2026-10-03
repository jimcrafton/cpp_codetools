#include "WiringRequestBuilder.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <vector>

namespace CodeToolsVsix
{
    namespace
    {
        // The type's own name inside its spelling: "const newui::Size&" -> "Size"; "" if there is none.
        std::string bareTypeName(std::string spelling)
        {
            for (const char* qualifier : { "const ", "volatile " })
            {
                std::size_t at;
                while ((at = spelling.find(qualifier)) != std::string::npos)
                {
                    spelling.erase(at, std::char_traits<char>::length(qualifier));
                }
            }
            while (!spelling.empty() && (spelling.back() == '&' || spelling.back() == '*' || spelling.back() == ' '))
            {
                spelling.pop_back();
            }
            const std::size_t scope = spelling.rfind("::");
            return scope == std::string::npos ? spelling : spelling.substr(scope + 2);
        }

        bool looksLikeAClassName(const std::string& name)
        {
            return !name.empty() && std::isupper(static_cast<unsigned char>(name.front())) &&
                   name.find_first_of("<> ,") == std::string::npos;
        }

        std::string parameterNameFor(const std::string& spelling, std::size_t position, std::set<std::string>& used)
        {
            const std::string bare = bareTypeName(spelling);
            std::string name;
            if (looksLikeAClassName(bare))
            {
                name = bare;
                name[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(name[0])));
            }
            else
            {
                name = "arg" + std::to_string(position + 1);
            }
            std::string unique = name;
            for (int suffix = 2; used.count(unique) != 0; ++suffix)
            {
                unique = name + std::to_string(suffix);
            }
            used.insert(unique);
            return unique;
        }
    }

    bool buildControllerWiringRequest(const newui::reflection::Class& viewClass,
                                      const std::string& delegateName,
                                      const std::string& viewName,
                                      const std::string& controllerClass,
                                      cpptools_codegen::ControllerWiringRequest& out)
    {
        std::vector<const newui::reflection::Delegate*> delegates;
        viewClass.allDelegates(delegates);

        const newui::reflection::Delegate* delegate = nullptr;
        for (const newui::reflection::Delegate* candidate : delegates)
        {
            if (candidate->name() == delegateName)
            {
                delegate = candidate;
                break;
            }
        }
        if (delegate == nullptr || delegate->senderSpelling().empty())
        {
            return false;
        }

        std::set<std::string> used = { "sender" };
        std::vector<std::pair<std::string, std::string>> arguments;
        std::size_t position = 0;
        for (const newui::reflection::Argument& argument : delegate->arguments())
        {
            if (argument.spelling.empty())
            {
                return false;
            }
            arguments.emplace_back(argument.spelling, parameterNameFor(argument.spelling, position, used));
            ++position;
        }

        cpptools_codegen::ControllerWiringRequest request;
        request.className = controllerClass;
        request.viewName = viewName;
        request.viewType = viewClass.qualifiedName();
        request.delegateName = delegateName;
        request.senderType = delegate->senderSpelling();
        request.arguments = std::move(arguments);
        out = std::move(request);
        return true;
    }

    std::vector<const newui::reflection::Class*> classesNamedBy(const newui::reflection::Class& viewClass,
                                                                const std::string& delegateName)
    {
        std::vector<const newui::reflection::Class*> named;
        auto add = [&named](const newui::reflection::Class* clazz) {
            if (clazz != nullptr && std::find(named.begin(), named.end(), clazz) == named.end())
            {
                named.push_back(clazz);
            }
        };
        add(&viewClass);

        std::vector<const newui::reflection::Delegate*> delegates;
        viewClass.allDelegates(delegates);
        for (const newui::reflection::Delegate* delegate : delegates)
        {
            if (delegate->name() != delegateName)
            {
                continue;
            }
            add(newui::reflection::classinfo(delegate->senderType()));
            for (const newui::reflection::Argument& argument : delegate->arguments())
            {
                add(newui::reflection::classinfo(argument.type));
            }
            break;
        }
        return named;
    }
}
