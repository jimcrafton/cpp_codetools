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
#include <newui/font.h>
#include <newui/geometry.h>
#include <newui/graphics.h>
#include <newui/layout.h>
#include <newui/popuptool.h>
#include <newui/reflection.h>
#include <newui/undostack.h>
#include <newui/view.h>
#include <newui/viewstyle.h>

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

        // Paints this leaf row's own *value* representation into rect (a PropertiesGrid row's own
        // value column, already shrunk to make room for a Dialog editor's "..." button if this is
        // one) - the rendering counterpart to valueAsString(), for a caller (PropertyItem::
        // paint()) that wants to actually draw the value, not just read it as text. Each subclass
        // now owns how its own value looks (BoolPropertyEditor draws a checkbox, ColorPropertyEditor
        // a swatch+text, ...) instead of PropertyItem::paint() hardcoding a growing
        // property()->type()-based if/else chain - see this project's own git history for the
        // tangle that used to be. Default: plain text via valueAsString().
        virtual void paintValue(BLContext& ctx, const newui::Rect& rect, const newui::Color& textColor) const;

        // Same idea as paintValue() above, for one EditStyle::SubProperties row (index into
        // subPropertyNames()/subPropertyValueAsString()) - default: plain text via
        // subPropertyValueAsString(index). FlagsEnumPropertyEditor (below) overrides to draw a
        // checkbox instead (one named flag bit) - replaces PropertyItem::paint()'s own
        // subPropertyIsBool()-then-checkbox-else-text dispatch.
        virtual void paintSubPropertyValue(BLContext& ctx, const newui::Rect& rect, std::size_t index,
            const newui::Color& textColor) const;

        // EditStyle::Dialog only, and only for an editor whose "dialog" is actually a non-modal
        // newui::PopupTool (LayoutPropertyEditor/ViewStylePropertyEditor below) rather than a real
        // blocking newui::Dialog::showModal() - edit() has no return value to hand such a popup
        // back through, so PropertiesGrid calls this instead of edit() for every Dialog-style
        // editor, and takes ownership of tracking (and dismissing) whatever non-null PopupTool*
        // comes back. The default just forwards to edit() and returns nullptr - every existing
        // Dialog editor (Color/Gradient/FilePath, all real blocking showModal()/showOpenFile()
        // calls with nothing left open afterward) needs no change at all.
        virtual newui::PopupTool* editAsync(newui::View* owner, const newui::Rect& /*anchorScreenRect*/) { edit(owner); return nullptr; }

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

        // Whether the sub-property at index is boolean-shaped (a single named flag bit -
        // FlagsEnumPropertyEditor, below) rather than free text (Rect/Point/Size's own float
        // components) - lets PropertiesGrid/PropertyItem paint a real checkbox glyph instead of
        // literal "true"/"false" text, without either of them needing to know FlagsEnumPropertyEditor
        // exists as a concrete type. False for every existing SubProperties editor (Rect/Point/Size).
        virtual bool subPropertyIsBool(std::size_t /*index*/) const { return false; }

        // Non-empty for a sub-property row that should edit through a dropdown of fixed choices
        // instead of free text (FontPropertyEditor's "name" row, below) - mirrors dropdownValues()
        // above, just per-sub-index rather than for the whole value. Empty (the default) for every
        // other SubProperties editor/index.
        virtual std::vector<std::string> subPropertyDropdownValues(std::size_t /*index*/) const { return {}; }

        const newui::reflection::Property* property() const { return property_; }

        // Attaches the UndoStack setValueFromString() pushes through -
        // nullptr (the default) means "commit directly, no undo", so a
        // PropertyEditor stays usable with no undo infrastructure at all
        // (testharness.exe, tests).
        void setUndoStack(newui::UndoStack* undoStack) { undoStack_ = undoStack; }
        newui::UndoStack* undoStack() const { return undoStack_; }

        // An extra side effect folded into commitValue()'s own pushed UndoableAction (run right
        // after property_->set(), in *both* doIt and undoIt, so it stays in lockstep with
        // undo/redo instead of drifting out of sync the way a bare post-commit call would) - or
        // run inline, once, when there's no undoStack() at all. nullptr (the default) adds
        // nothing; PropertyEditor/instance_ stay fully type-erased either way (this editor never
        // interprets the callback, just invokes it). The one real caller today is PropertiesGrid,
        // for "bounds" specifically - it already knows node.ownerInstance is a real
        // newui::SubView* (PropertiesModel's own gating), which this class itself has no safe way
        // to assume (PropertyEditorTest's own isolated fixtures construct a RectPropertyEditor
        // against a plain, non-View struct on purpose - a View-specific assumption in here would
        // be undefined behavior for them).
        using PostCommitSync = std::function<void()>;
        void setPostCommitSync(PostCommitSync sync) { postCommitSync_ = std::move(sync); }

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
        PostCommitSync postCommitSync_;
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
        // A hand-drawn checkbox glyph, not literal "true"/"false" text - matches the real live
        // newui::Toggle PropertiesGrid substitutes in once this row is actually being edited.
        void paintValue(BLContext& ctx, const newui::Rect& rect, const newui::Color& textColor) const override;
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

    // Registered by tag ("filepath" - PropertyEditorRegistry::registerEditor(const std::string&,
    // Factory)), not by C++ type - createEditor() checks property()->tags() before the plain
    // std::string wildcard (StringPropertyEditor above), so a property tagged this way (e.g. a
    // future gfx::Fill::imagePath() once it carries "@reflect tags=filepath" on the newui side -
    // not done as of this class existing; that's a vendored-newui-side edit left for the user's
    // own D:\code\newui checkout, not this repo's 3rdparty copy) gets a native file picker
    // instead of a plain text field. valueAsString()/parseValue() are unchanged from
    // StringPropertyEditor's own shape (parseValue() is never actually reached - EditStyle::
    // Dialog only ever calls edit(), never setValueFromString() - kept real rather than
    // returning std::nullopt unconditionally, in case a future caller ever types a path in by
    // hand through some other path).
    class FilePathPropertyEditor : public PropertyEditor
    {
    public:
        using PropertyEditor::PropertyEditor;
        EditStyle editStyle() const override { return EditStyle::Dialog; }
        std::string valueAsString() const override;
        std::optional<std::any> parseValue(const std::string& text) const override;
        // owner's own rootView()->windowHandle() is the real native owner HWND for the picker -
        // same owner-resolution GradientPropertyEditor::edit() already relies on (see its own
        // comment). Commits through the ordinary commitValue() path (undo-aware) on a real
        // selection; a no-op on Cancel/dialog failure, same contract every other EditStyle::
        // Dialog editor here already has.
        void edit(newui::View* owner) override;
    };

    // valueAsString()/parseValue() still reuse newui::Color::toString()/fromString() (CSS-style
    // hex) directly - unchanged from this class's original text-only shape. edit() (EditStyle::
    // Dialog, ColorEditorDialog.h) is the real picker, same shape as GradientPropertyEditor's own
    // edit() below.
    class ColorPropertyEditor : public PropertyEditor
    {
    public:
        using PropertyEditor::PropertyEditor;
        EditStyle editStyle() const override { return EditStyle::Dialog; }
        std::string valueAsString() const override;
        std::optional<std::any> parseValue(const std::string& text) const override;
        void edit(newui::View* owner) override;
        // A swatch + hex text, not just plain text - matches PropertyRow::build()'s own original
        // swatch preview.
        void paintValue(BLContext& ctx, const newui::Rect& rect, const newui::Color& textColor) const override;
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

    // Same "decompose/recompose the whole value" shape as RectPropertyEditor above -
    // newui::Font isn't a reflected Class (no @reflect on it, see font.h), and even if it were,
    // ViewStyle::font()/LabelStyle's own font access has both a getter and a setter, which
    // ClassBuilder::property()'s addressability rule (reflection.h) always excludes from becoming
    // a real, live Kind::PropertyGroup - so this is the only way a Font-typed property becomes
    // editable instead of "(unsupported)". bold/italic/underlined/strikeThrough are exposed as
    // checkbox rows (subPropertyIsBool()) alongside name/size.
    class FontPropertyEditor : public PropertyEditor
    {
    public:
        using PropertyEditor::PropertyEditor;
        EditStyle editStyle() const override { return EditStyle::SubProperties; }
        std::string valueAsString() const override;
        std::optional<std::any> parseValue(const std::string& text) const override;
        std::vector<std::string> subPropertyNames() const override {
            return { "name", "size", "bold", "italic", "underlined", "strikeThrough" };
        }
        std::string subPropertyValueAsString(std::size_t index) const override;
        void setSubPropertyValueFromString(std::size_t index, const std::string& text) override;
        bool subPropertyIsBool(std::size_t index) const override { return index >= 2; }
        std::vector<std::string> subPropertyDropdownValues(std::size_t index) const override;
    };

    // gfx::Fill::gradient() has the identical addressability problem font had (a non-const
    // getter, but also a real setGradient() - reflectgen.py's addressability rule explicitly
    // excludes that shape too, confirmed by reading collect_property_accessors()/
    // is_addressable_getter directly), so this is registered the same type-only-wildcard way
    // FontPropertyEditor is. Unlike Font/Rect, Gradient has variable-length collections
    // (stops()/points()) that don't fit EditStyle::SubProperties' fixed named-row shape at all -
    // this is the first real consumer of EditStyle::Dialog/edit(), previously declared but never
    // wired anywhere (see PropertiesGrid.cpp's own rebuildLiveEditor() dispatch for the other
    // half of this). parseValue() always rejects text - this property is dialog-only editable,
    // there's no sensible single-line text form to type a whole gradient into.
    class GradientPropertyEditor : public PropertyEditor
    {
    public:
        using PropertyEditor::PropertyEditor;
        EditStyle editStyle() const override { return EditStyle::Dialog; }
        std::string valueAsString() const override;
        std::optional<std::any> parseValue(const std::string& text) const override;
        // owner is the View currently being edited - always real (PropertiesGrid, this method's
        // only caller, never reaches EditStyle::Dialog dispatch without a live model_.selected()
        // backing the property tree a PropertyLeaf row came from). Its bounds() anchors the
        // dialog's shape-relative math (radial/conic center chip, preview aspect - see
        // GradientEditorDialog::setShapeBounds()); it's also passed straight to
        // newui::Dialog::showModal(View*) as the modal's real owner.
        void edit(newui::View* owner) override;
    };

    // Real, reported bug this comment exists to prevent recurring: the base PropertyEditor::
    // editAsync(owner) signature originally had no way to say *where on screen* the popup should
    // appear - LayoutPropertyEditor/ViewStylePropertyEditor (below) used to derive their own
    // anchor from owner's own screen rect (ownerScreenRect(), PropertyEditor.cpp), which is the
    // *selected canvas View being edited*, not the Properties grid row the user actually clicked -
    // the popup ended up anchored next to the edited element on the design surface instead of
    // near the "..." button that opened it. anchorScreenRect is that missing piece: real screen
    // coordinates (not owner-local) of whatever the caller wants the popup to appear next to -
    // PropertiesGrid::openDialogEditorFor() computes it from the actual clicked row/button (its
    // own screenRectFor() helper - newui has no general View-to-screen mapper of its own, only
    // RootView::localToScreen(), confirmed by grep). Every editAsync() override should anchor to
    // this, never to owner's own bounds.

    // View::layout()/style() "type swap" pickers - selecting which concrete Layout/ViewStyle
    // subclass is installed on the selected View, via a non-modal newui::CalloutTool popup
    // (editAsync() above) showing one preview cell per candidate, click-to-select. Both are
    // registered at typeid(newui::Layout)/typeid(newui::ViewStyle) - classifyProperty()
    // (PropertiesModel.cpp) checks PropertyEditorRegistry first, so this pre-empts the generic
    // "addressable nested Class becomes an expandable PropertyGroup" path these two properties
    // would otherwise take, same as every other EditStyle-registered type here.
    //
    // View::layout()/style() are real PtrGetter/PtrSetter-backed addressable properties
    // (reflection.h's TypedProperty) - but NOT usable through the ordinary commitValue()/undo
    // path every other editor here uses: get() returns either an empty std::any (a null pointer)
    // or a *sliced copy* of whatever concrete subclass is currently installed (std::any(*p), the
    // ValueT base-class slice - reflection.h's TypedProperty::get(), PtrGetter branch), while
    // set() expects a raw ValueT* (ownership-transfer, not a copy-into-what's-there) - two
    // incompatible std::any payload shapes for the same property, so valueAsString() below reads
    // through property_->getClass(instance_) instead (the true *runtime* class, never sliced -
    // see Property::getClass()'s own comment), and edit()/editAsync() commit via property_->set()
    // directly, deliberately bypassing commitValue()/undoStack() entirely. A "type swap" is a
    // rare, deliberate structural action - undo for the swap itself is out of scope for v1; every
    // edit to the resulting object's own fields (e.g. a FlexLayout's spacing) still goes through
    // the ordinary, fully undoable Property/commitValue() path once it becomes its own
    // PropertyGroup subtree.
    class LayoutPropertyEditor : public PropertyEditor
    {
    public:
        using PropertyEditor::PropertyEditor;
        EditStyle editStyle() const override { return EditStyle::Dialog; }
        std::string valueAsString() const override;
        std::optional<std::any> parseValue(const std::string& text) const override { return std::nullopt; }
        newui::PopupTool* editAsync(newui::View* owner, const newui::Rect& anchorScreenRect) override;
    };

    // Same shape as LayoutPropertyEditor above, just against ViewStyleRegistry::optionsFor(owner)
    // instead of a fixed 4-entry Layout list - see that class's own header comment for why the
    // option set is curated per owner class rather than every registered ViewStyle subclass.
    class ViewStylePropertyEditor : public PropertyEditor
    {
    public:
        using PropertyEditor::PropertyEditor;
        EditStyle editStyle() const override { return EditStyle::Dialog; }
        std::string valueAsString() const override;
        std::optional<std::any> parseValue(const std::string& text) const override { return std::nullopt; }
        newui::PopupTool* editAsync(newui::View* owner, const newui::Rect& anchorScreenRect) override;
    };

    // A generic dropdown for *any* non-flags registered newui::reflection::Enum -
    // unlike every editor above (each keyed to one fixed C++ type),
    // PropertyEditorRegistry can't key this one by std::type_index the
    // same way (every distinct enum is its own type_index) - instead
    // PropertyEditorRegistry::createEditor() falls back to this, by
    // looking up ReflectionRegistry::getEnum(property->type()) directly,
    // once no tag/type-specific entry matches. Reads/writes purely
    // through Enum's own type-erased toUInt64()/fromUInt64()/tryParse()/
    // tryToString() (reflection.h) - never needs to know the enum's real
    // C++ type here, so one editor covers every enum newui ever
    // registers. A flags-shaped enum (Enum::isFlags()) never reaches this
    // class at all - createEditor() routes it to FlagsEnumPropertyEditor
    // (below) instead, a real checkbox-per-bit editor.
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

    // The flags counterpart to EnumPropertyEditor above - selected instead of it by
    // PropertyEditorRegistry::createEditor()'s own enum fallback once ReflectionRegistry::
    // getEnum(property->type())->isFlags() is true (e.g. newui::Anchor, tagged "@reflect flags" -
    // see EnumBuilder<T>::flags()'s own comment, reflection.h, which names Anchor directly as a
    // real example needing this explicit opt-in). One synthetic SubProperties row per named,
    // nonzero flag value (a zero-value entry like "None" isn't a real bit to toggle, so it's
    // excluded the same way Enum::decompose() itself skips zero candidates) - each one a
    // checkbox (subPropertyIsBool() override below) read/written by testing/setting that one bit
    // against the enum's whole current combined value, same "decompose/recompose the *whole*
    // value through this editor's own rawValue()/commitValue()" shape RectPropertyEditor's own
    // x/y/width/height rows already use for a different reason (no real per-component Property to
    // address either way).
    class FlagsEnumPropertyEditor : public PropertyEditor
    {
    public:
        FlagsEnumPropertyEditor(const newui::reflection::Property* property, void* instance,
            const newui::reflection::Enum* enumInfo)
            : PropertyEditor(property, instance), enum_(enumInfo) {}

        EditStyle editStyle() const override { return EditStyle::SubProperties; }
        std::string valueAsString() const override;
        std::optional<std::any> parseValue(const std::string& text) const override;
        std::vector<std::string> subPropertyNames() const override;
        std::string subPropertyValueAsString(std::size_t index) const override;
        void setSubPropertyValueFromString(std::size_t index, const std::string& text) override;
        bool subPropertyIsBool(std::size_t /*index*/) const override { return true; }

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
