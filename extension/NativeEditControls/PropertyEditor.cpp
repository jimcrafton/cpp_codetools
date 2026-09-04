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

    std::optional<std::any> BoolPropertyEditor::parseValue(const std::string& text) const
    {
        std::string lower = toLower(text);
        if (lower == "true" || lower == "1" || lower == "yes") {
            return std::any(true);
        }
        if (lower == "false" || lower == "0" || lower == "no") {
            return std::any(false);
        }
        return std::nullopt;
    }

    std::string IntPropertyEditor::valueAsString() const
    {
        return std::to_string(std::any_cast<int>(rawValue()));
    }

    std::optional<std::any> IntPropertyEditor::parseValue(const std::string& text) const
    {
        try {
            std::size_t consumed = 0;
            int value = std::stoi(text, &consumed);
            if (consumed == text.size()) {
                return std::any(value);
            }
        } catch (const std::exception&) {
            // invalid text: falls through to nullopt below
        }
        return std::nullopt;
    }

    std::string FloatPropertyEditor::valueAsString() const
    {
        return std::to_string(std::any_cast<float>(rawValue()));
    }

    std::optional<std::any> FloatPropertyEditor::parseValue(const std::string& text) const
    {
        try {
            std::size_t consumed = 0;
            float value = std::stof(text, &consumed);
            if (consumed == text.size()) {
                return std::any(value);
            }
        } catch (const std::exception&) {
            // invalid text: falls through to nullopt below
        }
        return std::nullopt;
    }

    std::string StringPropertyEditor::valueAsString() const
    {
        return std::any_cast<std::string>(rawValue());
    }

    std::optional<std::any> StringPropertyEditor::parseValue(const std::string& text) const
    {
        return std::any(text);
    }

    std::string ColorPropertyEditor::valueAsString() const
    {
        return std::any_cast<newui::Color>(rawValue()).toString();
    }

    std::optional<std::any> ColorPropertyEditor::parseValue(const std::string& text) const
    {
        newui::Color color;
        if (newui::Color::fromString(text, color)) {
            return std::any(color);
        }
        return std::nullopt;
    }

    void PropertyEditor::setValueFromString(const std::string& text)
    {
        std::optional<std::any> parsed = parseValue(text);
        if (!parsed.has_value()) {
            return;
        }

        if (undoStack_ == nullptr) {
            setRawValue(*parsed);
            return;
        }

        const newui::reflection::Property* property = property_;
        void* instance = instance_;
        std::any oldValue = rawValue();
        std::any newValue = *parsed;

        newui::UndoableAction action;
        action.description = "Change " + property_->name();
        action.doIt = [property, instance, newValue] { property->set(instance, newValue); };
        action.undoIt = [property, instance, oldValue] { property->set(instance, oldValue); };
        undoStack_->push(std::move(action));  // push() calls doIt() immediately
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

    void PropertyEditorRegistry::registerEditor(const std::string& tag, Factory factory)
    {
        tagEntries_[tag] = std::move(factory);
    }

    std::unique_ptr<PropertyEditor> PropertyEditorRegistry::createEditor(
        const newui::reflection::Property* property,
        const newui::reflection::Class* owningClass,
        void* instance) const
    {
        for (const std::string& tag : property->tags()) {
            auto it = tagEntries_.find(tag);
            if (it != tagEntries_.end()) {
                return it->second(property, instance);
            }
        }

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
        registerEditor(std::type_index(typeid(newui::Color)),
            [](const newui::reflection::Property* p, void* instance) { return std::make_unique<ColorPropertyEditor>(p, instance); });
    }
}
