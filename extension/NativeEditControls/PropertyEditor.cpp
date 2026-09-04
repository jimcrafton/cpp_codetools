#include "PropertyEditor.h"

#include <algorithm>
#include <cctype>

namespace CodeToolsVsix
{
    namespace
    {
        std::string toLower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        }
    }

    std::string BoolPropertyEditor::valueAsString() const
    {
        return std::any_cast<bool>(rawValue()) ? "true" : "false";
    }

    void BoolPropertyEditor::setValueFromString(const std::string& text)
    {
        std::string lower = toLower(text);
        if (lower == "true" || lower == "1" || lower == "yes") {
            setRawValue(true);
        } else if (lower == "false" || lower == "0" || lower == "no") {
            setRawValue(false);
        }
        // anything else: invalid, no-op - see PropertyEditor::setValueFromString's doc comment.
    }

    std::string IntPropertyEditor::valueAsString() const
    {
        return std::to_string(std::any_cast<int>(rawValue()));
    }

    void IntPropertyEditor::setValueFromString(const std::string& text)
    {
        try {
            std::size_t consumed = 0;
            int value = std::stoi(text, &consumed);
            if (consumed == text.size()) {
                setRawValue(value);
            }
        } catch (const std::exception&) {
            // invalid text: no-op
        }
    }

    std::string FloatPropertyEditor::valueAsString() const
    {
        return std::to_string(std::any_cast<float>(rawValue()));
    }

    void FloatPropertyEditor::setValueFromString(const std::string& text)
    {
        try {
            std::size_t consumed = 0;
            float value = std::stof(text, &consumed);
            if (consumed == text.size()) {
                setRawValue(value);
            }
        } catch (const std::exception&) {
            // invalid text: no-op
        }
    }

    std::string StringPropertyEditor::valueAsString() const
    {
        return std::any_cast<std::string>(rawValue());
    }

    void StringPropertyEditor::setValueFromString(const std::string& text)
    {
        setRawValue(text);
    }

    PropertyEditorRegistry& PropertyEditorRegistry::instance()
    {
        static PropertyEditorRegistry registry;
        return registry;
    }

    void PropertyEditorRegistry::registerEditor(std::type_index propertyType, Factory factory,
                                                 const newui::reflection::Class* owningClass,
                                                 const std::string& propertyName)
    {
        entries_.push_back(Entry{ propertyType, owningClass, propertyName, std::move(factory) });
    }

    std::unique_ptr<PropertyEditor> PropertyEditorRegistry::createEditor(
        const newui::reflection::Property* property,
        const newui::reflection::Class* owningClass,
        void* instance) const
    {
        const Entry* best = nullptr;
        int bestScore = -1;

        for (const Entry& entry : entries_) {
            if (entry.propertyType != property->type()) {
                continue;
            }
            if (entry.owningClass != nullptr && entry.owningClass != owningClass) {
                continue;
            }
            if (!entry.propertyName.empty() && entry.propertyName != property->name()) {
                continue;
            }

            int score = (entry.owningClass != nullptr ? 2 : 0) + (!entry.propertyName.empty() ? 1 : 0);
            if (score >= bestScore) {
                bestScore = score;
                best = &entry;
            }
        }

        if (best == nullptr) {
            return nullptr;
        }
        return best->factory(property, instance);
    }

    void PropertyEditorRegistry::registerBuiltinEditors()
    {
        registerEditor(std::type_index(typeid(bool)),
            [](const newui::reflection::Property* p, void* instance) { return std::make_unique<BoolPropertyEditor>(p, instance); });
        registerEditor(std::type_index(typeid(int)),
            [](const newui::reflection::Property* p, void* instance) { return std::make_unique<IntPropertyEditor>(p, instance); });
        registerEditor(std::type_index(typeid(float)),
            [](const newui::reflection::Property* p, void* instance) { return std::make_unique<FloatPropertyEditor>(p, instance); });
        registerEditor(std::type_index(typeid(std::string)),
            [](const newui::reflection::Property* p, void* instance) { return std::make_unique<StringPropertyEditor>(p, instance); });
    }
}
