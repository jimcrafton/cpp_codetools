#include "ControllerBindings.h"

#include <newui/reflection.h>
#include <newui/subview.h>

#include <typeindex>

namespace CodeToolsVsix
{
    namespace
    {
        void collectFrom(newui::View& parent, std::vector<DesignerBinding>& out)
        {
            for (newui::SubView* child : parent.childViews())
            {
                if (child == nullptr)
                {
                    continue;
                }

                const std::string& name = child->name();
                const newui::reflection::Class* clazz =
                    name.empty() ? nullptr : newui::reflection::classinfo(std::type_index(typeid(*child)));
                if (clazz != nullptr)
                {
                    std::vector<const newui::reflection::Delegate*> delegates;
                    clazz->allDelegates(delegates);
                    for (const newui::reflection::Delegate* delegate : delegates)
                    {
                        for (const std::string& descriptor : delegate->describedListeners(child))
                        {
                            if (descriptor.find('@') == std::string::npos)
                            {
                                continue;   // a bare function name: nothing a controller could hold
                            }
                            DesignerBinding entry;
                            entry.viewName = name;
                            entry.binding.viewField = name + "_";   // the controller's member for this control
                            entry.binding.delegate = delegate->name();
                            entry.binding.descriptor = descriptor;
                            entry.binding.senderType = delegate->senderSpelling();
                            for (const newui::reflection::Argument& argument : delegate->arguments())
                            {
                                entry.binding.argumentTypes.push_back(argument.spelling);
                            }
                            out.push_back(std::move(entry));
                        }
                    }
                }
                collectFrom(*child, out);
            }
        }

        VerifiedBinding resultFor(const DesignerBinding& binding, cpptools::BindingCheck check)
        {
            return { binding.viewName, binding.binding.delegate, binding.binding.descriptor, std::move(check) };
        }
    }

    std::vector<DesignerBinding> collectRecordedBindings(newui::View& root)
    {
        std::vector<DesignerBinding> bindings;
        collectFrom(root, bindings);
        return bindings;
    }

    std::vector<VerifiedBinding> verifyBindings(const std::vector<DesignerBinding>& bindings,
                                                const std::string& controllerClass,
                                                const std::string& headerText,
                                                const std::vector<std::string>& compileArgs)
    {
        std::vector<cpptools::RecordedBinding> recorded;
        recorded.reserve(bindings.size());
        for (const DesignerBinding& binding : bindings)
        {
            recorded.push_back(binding.binding);
        }
        const std::vector<cpptools::BindingCheck> checks =
            cpptools::verifyDelegateBindings(headerText, controllerClass, recorded, compileArgs);

        std::vector<VerifiedBinding> results;
        results.reserve(bindings.size());
        for (std::size_t i = 0; i < bindings.size(); ++i)
        {
            results.push_back(resultFor(bindings[i], i < checks.size() ? checks[i] : cpptools::BindingCheck{}));
        }
        return results;
    }

    std::vector<VerifiedBinding> unverifiableBindings(const std::vector<DesignerBinding>& bindings,
                                                      const std::string& reason)
    {
        std::vector<VerifiedBinding> results;
        results.reserve(bindings.size());
        for (const DesignerBinding& binding : bindings)
        {
            results.push_back(resultFor(binding, { cpptools::BindingStatus::CannotVerify, reason }));
        }
        return results;
    }
}
