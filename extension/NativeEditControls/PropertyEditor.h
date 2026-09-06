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
#include <newui/geometry.h>
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

        // EditStyle::SubProperties only (e.g. RectPropertyEditor) - the
        // whole value decomposed into named synthetic child rows (e.g.
        // "x"/"y"/"width"/"height") for a compound type that isn't a real
        // addressable nested Class (PropertiesModel::classifyProperty()
        // can't turn it into a real Kind::PropertyGroup the normal way -
        // see PropertiesModel.h's own comment on why). Each sub-property
        // is read/written by decomposing/recomposing the *whole* value
        // through this same editor's rawValue()/commitValue() - there's no
        // real Property/address() backing an individual component, only a
        // synthetic index PropertiesModel hands back here. Base defaults
        // are empty/no-op; only a SubProperties-style editor overrides
        // these.
        virtual std::vector<std::string> subPropertyNames() const { return {}; }
        virtual std::string subPropertyValueAsString(std::size_t index) const { return {}; }
        virtual void setSubPropertyValueFromString(std::size_t index, const std::string& text) {}

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

        // Commits an already-built value - through undoStack() if one is
        // attached (undoable), or directly otherwise. Factored out of
        // setValueFromString() (below) so a SubProperties editor's
        // setSubPropertyValueFromString() override can commit a freshly
        // recomposed whole value the same undo-aware way, without
        // re-parsing it back through parseValue()/a string round-trip.
        void commitValue(const std::any& newValue) const;

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

    // Compact comma-separated text ("x, y" / "width, height" / "x, y,
    // width, height") *and* EditStyle::SubProperties (individually
    // editable "x"/"y"/etc rows) - neither newui::Point/Size/Rect property
    // this is used for (View::origin()/desiredSize()/bounds(), all
    // returning by value or const-ref) is ever addressable, so none can
    // become a real Kind::PropertyGroup (PropertiesModel::
    // classifyProperty() needs a live pointer to recurse into, which none
    // of these have) - SubProperties decomposes/recomposes the whole
    // value through this same editor's rawValue()/commitValue() instead,
    // no real Property/address() needed per component. valueAsString()
    // stays the single-line summary (used if a caller/test wants the
    // whole value without expanding); the grid itself always expands a
    // SubProperties editor rather than showing its single-line form.
    class PointPropertyEditor : public PropertyEditor
    {
    public:
        using PropertyEditor::PropertyEditor;
        EditStyle editStyle() const override { return EditStyle::SubProperties; }
        std::string valueAsString() const override;
        std::optional<std::any> parseValue(const std::string& text) const override;
        std::vector<std::string> subPropertyNames() const override { return { "x", "y" }; }
        std::string subPropertyValueAsString(std::size_t index) const override;
        void setSubPropertyValueFromString(std::size_t index, const std::string& text) override;
    };

    class SizePropertyEditor : public PropertyEditor
    {
    public:
        using PropertyEditor::PropertyEditor;
        EditStyle editStyle() const override { return EditStyle::SubProperties; }
        std::string valueAsString() const override;
        std::optional<std::any> parseValue(const std::string& text) const override;
        std::vector<std::string> subPropertyNames() const override { return { "width", "height" }; }
        std::string subPropertyValueAsString(std::size_t index) const override;
        void setSubPropertyValueFromString(std::size_t index, const std::string& text) override;
    };

    class RectPropertyEditor : public PropertyEditor
    {
    public:
        using PropertyEditor::PropertyEditor;
        EditStyle editStyle() const override { return EditStyle::SubProperties; }
        std::string valueAsString() const override;
        std::optional<std::any> parseValue(const std::string& text) const override;
        std::vector<std::string> subPropertyNames() const override { return { "x", "y", "width", "height" }; }
        std::string subPropertyValueAsString(std::size_t index) const override;
        void setSubPropertyValueFromString(std::size_t index, const std::string& text) override;
    };

    // A generic dropdown for *any* registered newui::reflection::Enum -
    // unlike every editor above (each keyed to one fixed C++ type),
    // PropertyEditorRegistry can't key this one by std::type_index the
    // same way (every distinct enum is its own type_index) - instead
    // PropertyEditorRegistry::createEditor() falls back to this, by
    // looking up ReflectionRegistry::getEnum(property->type()) directly,
    // once no tag/type-specific entry matches. Reads/writes purely
    // through Enum's own type-erased toUInt64()/fromUInt64()/tryParse()/
    // tryToString() (reflection.h) - never needs to know the enum's real
    // C++ type here, so one editor covers every enum newui ever
    // registers. Flags-style decompose() (multi-value) editing is out of
    // scope for v1 - tryToString()/tryParse() already degrade gracefully
    // for a flags enum (falls back to a raw numeric string), just without
    // the "Ctrl+Shift"-style combined display decompose() could give.
    class EnumPropertyEditor : public PropertyEditor
    {
    public:
        EnumPropertyEditor(const newui::reflection::Property* property, void* instance,
            const newui::reflection::Enum* enumInfo)
            : PropertyEditor(property, instance), enum_(enumInfo) {}

        EditStyle editStyle() const override { return EditStyle::Dropdown; }
        std::string valueAsString() const override;
        std::optional<std::any> parseValue(const std::string& text) const override;
        std::vector<std::string> dropdownValues() const override;

    private:
        const newui::reflection::Enum* enum_;
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
