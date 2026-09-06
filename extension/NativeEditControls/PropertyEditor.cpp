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

        std::string formatFloat(float value)
        {
            std::string text = std::to_string(value);
            // Trims the trailing zeros std::to_string always pads a float
            // with (e.g. "100.000000") down to a shorter, still-round-
            // trippable form ("100") - matches how a hand-typed value
            // looks, not a printf-style fixed width.
            while (!text.empty() && text.back() == '0') {
                text.pop_back();
            }
            if (!text.empty() && text.back() == '.') {
                text.pop_back();
            }
            return text;
        }

        // Parses exactly `count` comma-separated floats - std::nullopt if
        // the count doesn't match or any token fails to parse (including
        // trailing garbage after a valid number), same "no partial
        // commits" contract every other PropertyEditor::parseValue()
        // already follows.
        std::optional<std::vector<float>> parseFloatList(const std::string& text, std::size_t count)
        {
            std::vector<float> values;
            std::size_t start = 0;
            while (start <= text.size()) {
                std::size_t comma = text.find(',', start);
                std::string token = text.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
                std::size_t first = token.find_first_not_of(" \t");
                std::size_t last = token.find_last_not_of(" \t");
                if (first == std::string::npos) {
                    return std::nullopt;
                }
                token = token.substr(first, last - first + 1);
                try {
                    std::size_t consumed = 0;
                    float value = std::stof(token, &consumed);
                    if (consumed != token.size()) {
                        return std::nullopt;
                    }
                    values.push_back(value);
                } catch (const std::exception&) {
                    return std::nullopt;
                }
                if (comma == std::string::npos) {
                    break;
                }
                start = comma + 1;
            }
            if (values.size() != count) {
                return std::nullopt;
            }
            return values;
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

    std::string PointPropertyEditor::valueAsString() const
    {
        newui::Point p = std::any_cast<newui::Point>(rawValue());
        return formatFloat(p.x) + ", " + formatFloat(p.y);
    }

    std::optional<std::any> PointPropertyEditor::parseValue(const std::string& text) const
    {
        auto values = parseFloatList(text, 2);
        if (!values.has_value()) {
            return std::nullopt;
        }
        return std::any(newui::Point((*values)[0], (*values)[1]));
    }

    std::string PointPropertyEditor::subPropertyValueAsString(std::size_t index) const
    {
        newui::Point p = std::any_cast<newui::Point>(rawValue());
        return formatFloat(index == 0 ? p.x : p.y);
    }

    void PointPropertyEditor::setSubPropertyValueFromString(std::size_t index, const std::string& text)
    {
        auto values = parseFloatList(text, 1);
        if (!values.has_value()) {
            return;
        }
        newui::Point p = std::any_cast<newui::Point>(rawValue());
        (index == 0 ? p.x : p.y) = (*values)[0];
        commitValue(std::any(p));
    }

    std::string SizePropertyEditor::valueAsString() const
    {
        newui::Size s = std::any_cast<newui::Size>(rawValue());
        return formatFloat(s.width) + ", " + formatFloat(s.height);
    }

    std::optional<std::any> SizePropertyEditor::parseValue(const std::string& text) const
    {
        auto values = parseFloatList(text, 2);
        if (!values.has_value()) {
            return std::nullopt;
        }
        return std::any(newui::Size((*values)[0], (*values)[1]));
    }

    std::string SizePropertyEditor::subPropertyValueAsString(std::size_t index) const
    {
        newui::Size s = std::any_cast<newui::Size>(rawValue());
        return formatFloat(index == 0 ? s.width : s.height);
    }

    void SizePropertyEditor::setSubPropertyValueFromString(std::size_t index, const std::string& text)
    {
        auto values = parseFloatList(text, 1);
        if (!values.has_value()) {
            return;
        }
        newui::Size s = std::any_cast<newui::Size>(rawValue());
        (index == 0 ? s.width : s.height) = (*values)[0];
        commitValue(std::any(s));
    }

    std::string RectPropertyEditor::valueAsString() const
    {
        newui::Rect r = std::any_cast<newui::Rect>(rawValue());
        return formatFloat(r.left()) + ", " + formatFloat(r.top()) + ", "
            + formatFloat(r.width()) + ", " + formatFloat(r.height());
    }

    std::optional<std::any> RectPropertyEditor::parseValue(const std::string& text) const
    {
        auto values = parseFloatList(text, 4);
        if (!values.has_value()) {
            return std::nullopt;
        }
        return std::any(newui::Rect((*values)[0], (*values)[1], (*values)[2], (*values)[3]));
    }

    std::string RectPropertyEditor::subPropertyValueAsString(std::size_t index) const
    {
        newui::Rect r = std::any_cast<newui::Rect>(rawValue());
        switch (index) {
        case 0: return formatFloat(r.left());
        case 1: return formatFloat(r.top());
        case 2: return formatFloat(r.width());
        default: return formatFloat(r.height());
        }
    }

    void RectPropertyEditor::setSubPropertyValueFromString(std::size_t index, const std::string& text)
    {
        auto values = parseFloatList(text, 1);
        if (!values.has_value()) {
            return;
        }
        newui::Rect r = std::any_cast<newui::Rect>(rawValue());
        float value = (*values)[0];
        newui::Rect updated;
        switch (index) {
        case 0: updated = newui::Rect(value, r.top(), r.width(), r.height()); break;
        case 1: updated = newui::Rect(r.left(), value, r.width(), r.height()); break;
        case 2: updated = newui::Rect(r.left(), r.top(), value, r.height()); break;
        default: updated = newui::Rect(r.left(), r.top(), r.width(), value); break;
        }
        commitValue(std::any(updated));
    }

    std::string EnumPropertyEditor::valueAsString() const
    {
        std::uint64_t value = enum_->toUInt64(rawValue());
        std::string name;
        if (enum_->tryToString(value, name)) {
            return name;
        }
        return std::to_string(value);
    }

    std::optional<std::any> EnumPropertyEditor::parseValue(const std::string& text) const
    {
        std::uint64_t value = 0;
        if (!enum_->tryParse(text, value)) {
            return std::nullopt;
        }
        std::any result = enum_->fromUInt64(value);
        if (!result.has_value()) {
            return std::nullopt;
        }
        return result;
    }

    std::vector<std::string> EnumPropertyEditor::dropdownValues() const
    {
        std::vector<std::string> names;
        for (const auto& v : enum_->values()) {
            names.push_back(v.name);
        }
        return names;
    }

    void PropertyEditor::commitValue(const std::any& newValue) const
    {
        if (undoStack_ == nullptr) {
            setRawValue(newValue);
            return;
        }

        const newui::reflection::Property* property = property_;
        void* instance = instance_;
        std::any oldValue = rawValue();

        newui::UndoableAction action;
        action.description = "Change " + property_->name();
        action.doIt = [property, instance, newValue] { property->set(instance, newValue); };
        action.undoIt = [property, instance, oldValue] { property->set(instance, oldValue); };
        undoStack_->push(std::move(action));  // push() calls doIt() immediately
    }

    void PropertyEditor::setValueFromString(const std::string& text)
    {
        std::optional<std::any> parsed = parseValue(text);
        if (!parsed.has_value()) {
            return;
        }
        commitValue(*parsed);
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
            // No tag/type-specific registration - fall back to a generic
            // enum dropdown if this property's type happens to be a
            // registered Enum (see EnumPropertyEditor's own comment for
            // why this can't just be another registerEditor() entry).
            if (const newui::reflection::Enum* enumInfo =
                    newui::reflection::ReflectionRegistry::getEnum(property->type())) {
                return std::make_unique<EnumPropertyEditor>(property, instance, enumInfo);
            }
            return nullptr;
        }
        return best->factory(property, instance);
    }

    void PropertyEditorRegistry::registerBuiltinEditors()
    {
        if (builtinsRegistered_) {
            return;
        }
        builtinsRegistered_ = true;

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
        registerEditor(std::type_index(typeid(newui::Point)),
            [](const newui::reflection::Property* p, void* instance) { return std::make_unique<PointPropertyEditor>(p, instance); });
        registerEditor(std::type_index(typeid(newui::Size)),
            [](const newui::reflection::Property* p, void* instance) { return std::make_unique<SizePropertyEditor>(p, instance); });
        registerEditor(std::type_index(typeid(newui::Rect)),
            [](const newui::reflection::Property* p, void* instance) { return std::make_unique<RectPropertyEditor>(p, instance); });
    }
}
