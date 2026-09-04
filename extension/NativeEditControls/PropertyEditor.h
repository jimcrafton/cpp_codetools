#pragma once

#include <any>
#include <functional>
#include <memory>
#include <string>
#include <typeindex>
#include <vector>

#include <newui/reflection.h>
#include <newui/view.h>

namespace CodeToolsVsix
{
    // Design-time editor for one newui::reflection::Property on one live
    // instance - UI-facing, not part of Property itself. See
    // bluesky/designer-plan.md §4.1 (single-consumer, design-time-only -
    // deliberately not in newui).
    class PropertyEditor
    {
    public:
        enum class EditStyle { None, Dropdown, Dialog, SubProperties };

        PropertyEditor(const newui::reflection::Property* property, void* instance)
            : property_(property), instance_(instance) {}
        virtual ~PropertyEditor() = default;

        virtual EditStyle editStyle() const { return EditStyle::None; }
        virtual std::string valueAsString() const = 0;
        virtual void setValueFromString(const std::string& text) = 0;  // invalid text: no-op
        virtual std::vector<std::string> dropdownValues() const { return {}; }
        virtual void edit(newui::View* owner) {}  // EditStyle::Dialog

        const newui::reflection::Property* property() const { return property_; }

    protected:
        std::any rawValue() const { return property_->get(instance_); }
        void setRawValue(const std::any& value) const { property_->set(instance_, value); }

        const newui::reflection::Property* property_;
        void* instance_;
    };

    // Generic editors - registered as the type-only wildcard fallback for
    // bool/int/float/std::string properties (see PropertyEditorRegistry).
    class BoolPropertyEditor : public PropertyEditor
    {
    public:
        using PropertyEditor::PropertyEditor;
        EditStyle editStyle() const override { return EditStyle::Dropdown; }
        std::string valueAsString() const override;
        void setValueFromString(const std::string& text) override;
        std::vector<std::string> dropdownValues() const override { return { "false", "true" }; }
    };

    class IntPropertyEditor : public PropertyEditor
    {
    public:
        using PropertyEditor::PropertyEditor;
        std::string valueAsString() const override;
        void setValueFromString(const std::string& text) override;
    };

    class FloatPropertyEditor : public PropertyEditor
    {
    public:
        using PropertyEditor::PropertyEditor;
        std::string valueAsString() const override;
        void setValueFromString(const std::string& text) override;
    };

    class StringPropertyEditor : public PropertyEditor
    {
    public:
        using PropertyEditor::PropertyEditor;
        std::string valueAsString() const override;
        void setValueFromString(const std::string& text) override;
    };

    // Keyed (propertyType, owningClass, propertyName) with wildcards
    // (nullptr class / empty name) - narrowest match wins, same 3-key shape
    // as Delphi's RegisterPropertyEditor. Generic editors register at the
    // type-only wildcard; a codetools++-specific override registers a
    // narrower key to take precedence for one property or class.
    class PropertyEditorRegistry
    {
    public:
        using Factory = std::function<std::unique_ptr<PropertyEditor>(const newui::reflection::Property*, void*)>;

        static PropertyEditorRegistry& instance();

        void registerEditor(std::type_index propertyType, Factory factory,
                             const newui::reflection::Class* owningClass = nullptr,
                             const std::string& propertyName = std::string());

        // nullptr if nothing registered matches propertyType at all, even
        // the wildcard form - caller falls back to read-only display.
        std::unique_ptr<PropertyEditor> createEditor(const newui::reflection::Property* property,
                                                       const newui::reflection::Class* owningClass,
                                                       void* instance) const;

        // bool/int/float/std::string wildcard editors - called once by
        // whoever owns instance() at startup.
        void registerBuiltinEditors();

    private:
        struct Entry
        {
            std::type_index propertyType;
            const newui::reflection::Class* owningClass;
            std::string propertyName;
            Factory factory;
        };

        std::vector<Entry> entries_;
    };
}
