#include "ComponentEditor.h"

namespace CodeToolsVsix
{
    ComponentEditorRegistry& ComponentEditorRegistry::instance()
    {
        static ComponentEditorRegistry registry;
        return registry;
    }

    void ComponentEditorRegistry::registerEditor(const newui::reflection::Class* owningClass, Factory factory)
    {
        entries_.emplace_back(owningClass, std::move(factory));
    }

    std::unique_ptr<ComponentEditor> ComponentEditorRegistry::createEditor(
        const newui::reflection::Class* owningClass,
        newui::View* view) const
    {
        for (const newui::reflection::Class* c = owningClass; c != nullptr; c = c->parentClass()) {
            for (const auto& entry : entries_) {
                if (entry.first == c) {
                    return entry.second(view);
                }
            }
        }
        return nullptr;
    }
}
