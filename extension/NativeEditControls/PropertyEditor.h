#pragma once

#include <any>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <vector>

#include <newui/color.h>
#include <newui/reflection.h>
#include <newui/undostack.h>
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

        // Parses text into a value of this property's type - std::nullopt
        // for invalid input (a no-op). Each subclass implements only this;
        // committing the value (and pushing undo/redo, if an UndoStack is
        // attached) is handled once, below, not per subclass - Property::
        // set()'s type erasure means that part needs no per-type code.
        virtual std::optional<std::any> parseValue(const std::string& text) const = 0;

        // Parses text via parseValue(); if valid, commits it - through
        // undoStack() if one is attached (undoable), or directly
        // otherwise. Invalid text is a no-op either way.
        void setValueFromString(const std::string& text);

        virtual std::vector<std::string> dropdownValues() const { return {}; }
        virtual void edit(newui::View* owner) {}  // EditStyle::Dialog

        const newui::reflection::Property* property() const { return property_; }

        // Attaches the UndoStack setValueFromString() pushes through -
        // nullptr (the default) means "commit directly, no undo", so a
        // PropertyEditor stays usable with no undo infrastructure at all
        // (testharness.exe, tests).
        void setUndoStack(newui::UndoStack* undoStack) { undoStack_ = undoStack; }
        newui::UndoStack* undoStack() const { return undoStack_; }

    protected:
        std::any rawValue() const { return property_->get(instance_); }
        void setRawValue(const std::any& value) const { property_->set(instance_, value); }

        const newui::reflection::Property* property_;
        void* instance_;
        newui::UndoStack* undoStack_ = nullptr;
    };

    // Generic editors - registered as the type-only wildcard fallback for
    // bool/int/float/std::string properties (see PropertyEditorRegistry).
    class BoolPropertyEditor : public PropertyEditor
    {
    public:
        using PropertyEditor::PropertyEditor;
        EditStyle editStyle() const override { return EditStyle::Dropdown; }
        std::string valueAsString() const override;
        std::optional<std::any> parseValue(const std::string& text) const override;
        std::vector<std::string> dropdownValues() const override { return { "false", "true" }; }
    };

    class IntPropertyEditor : public PropertyEditor
    {
    public:
        using PropertyEditor::PropertyEditor;
        std::string valueAsString() const override;
        std::optional<std::any> parseValue(const std::string& text) const override;
    };

    class FloatPropertyEditor : public PropertyEditor
    {
    public:
        using PropertyEditor::PropertyEditor;
        std::string valueAsString() const override;
        std::optional<std::any> parseValue(const std::string& text) const override;
    };

    class StringPropertyEditor : public PropertyEditor
    {
    public:
        using PropertyEditor::PropertyEditor;
        std::string valueAsString() const override;
        std::optional<std::any> parseValue(const std::string& text) const override;
    };

    // Text edit only for now (EditStyle::None, the base default) - reuses
    // newui::Color::toString()/fromString() (CSS-style hex) directly
    // rather than inventing another format. A real picker (EditStyle::
    // Dialog) is a future increment, not built here.
    class ColorPropertyEditor : public PropertyEditor
    {
    public:
        using PropertyEditor::PropertyEditor;
        std::string valueAsString() const override;
        std::optional<std::any> parseValue(const std::string& text) const override;
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

        // Keyed on one of a property's tags() (see reflection.h's
        // "@reflect tags=..." support) - checked before propertyType, so
        // e.g. a plain std::string property tagged "filepath" resolves to
        // a real file-picker editor instead of the generic
        // StringPropertyEditor wildcard, without the two ever colliding
        // (the tag is what distinguishes them, not the C++ type, which is
        // identical for both). A property with multiple tags checks them
        // in tags() order, first registered match wins.
        void registerEditor(const std::string& tag, Factory factory);

        // Tag match (if property has any tags and one is registered)
        // wins over the propertyType-based lookup; nullptr if neither
        // finds anything, even the type-only wildcard - caller falls back
        // to read-only display.
        std::unique_ptr<PropertyEditor> createEditor(const newui::reflection::Property* property,
                                                       const newui::reflection::Class* owningClass,
                                                       void* instance) const;

        // bool/int/float/std::string/Color wildcard editors - called once
        // by whoever owns instance() at startup. Self-guarding per
        // instance (builtinsRegistered_ below), not just "the one real
        // caller happens to call it once" - a real, caught bug: unlike
        // registerReflectionData() (a self-guarding global magic static),
        // this used to duplicate every entries_ registration on a second
        // call. A per-instance bool, not a magic static, since this is
        // also called on local, non-singleton instances in tests
        // (test_property_editor.cpp) - a global guard would have made
        // every such instance after the first come up empty.
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
        std::unordered_map<std::string, Factory> tagEntries_;
        bool builtinsRegistered_ = false;
    };
}
