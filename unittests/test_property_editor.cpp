#include "../extension/NativeEditControls/PropertyEditor.h"

#include <newui/reflection.h>

#include <gtest/gtest.h>

using namespace newui::reflection;

namespace
{
    // A plain, hand-registered enum - see EnumEditorRoundTripsThroughTheRealProperty.
    enum class Direction { North, East, South, West };

    void registerDirectionEnumOnce()
    {
        static bool registered = [] {
            EnumBuilder<Direction> builder("Direction");
            builder.addValue("North", 0).addValue("East", 1).addValue("South", 2).addValue("West", 3);
            ReflectionRegistry::registerEnum(builder.build());
            return true;
        }();
        (void)registered;
    }

    // A hand-registered flags enum (real bitmask combos, not just sequential small values - see
    // Enum::isFlags()'s own comment, reflection.h, for why that distinction has to be explicit)
    // - see FlagsEnumPropertyEditor's own tests. Mirrors newui::Anchor's own real shape (a
    // dedicated None=0 that isn't itself a real bit to toggle, then independent power-of-two
    // flags) without depending on newui's own reflection data being registered for this binary.
    enum class Modifiers { None = 0, Ctrl = 1, Shift = 2, Alt = 4 };

    void registerModifiersEnumOnce()
    {
        static bool registered = [] {
            EnumBuilder<Modifiers> builder("Modifiers");
            builder.addValue("None", 0).addValue("Ctrl", 1).addValue("Shift", 2).addValue("Alt", 4).flags(true);
            ReflectionRegistry::registerEnum(builder.build());
            return true;
        }();
        (void)registered;
    }

    // Hand-registered per-test, not a real newui/cpptools class - same
    // "small example class, registered by hand" pattern as newui's own
    // examples/reflection2.cpp. Isolated so this test doesn't depend on
    // exactly which real classes reflectgen happens to expose.
    struct Widget
    {
        bool enabled = false;
        int count = 0;
        float opacity = 0.0f;
        std::string label;
        std::vector<int> tags;  // no editor type registered for this - see UnregisteredTypeReturnsNullptr
        newui::Color tint;
        std::string iconPath;  // tagged "filepath" - see TagRegistrationWinsOverTheTypeWildcard
        newui::Point origin;
        newui::Size extent;
        newui::Rect bounds;
        Direction facing = Direction::North;
        Modifiers modifiers = Modifiers::None;
        newui::Font font;

        bool isEnabled() const { return enabled; }
        void setEnabled(bool v) { enabled = v; }
        int getCount() const { return count; }
        void setCount(int v) { count = v; }
        float getOpacity() const { return opacity; }
        void setOpacity(float v) { opacity = v; }
        std::string getLabel() const { return label; }
        void setLabel(std::string v) { label = std::move(v); }
        std::vector<int> getTags() const { return tags; }
        void setTags(std::vector<int> v) { tags = std::move(v); }
        newui::Color getTint() const { return tint; }
        void setTint(newui::Color v) { tint = v; }
        std::string getIconPath() const { return iconPath; }
        void setIconPath(std::string v) { iconPath = std::move(v); }
        newui::Point getOrigin() const { return origin; }
        void setOrigin(newui::Point v) { origin = v; }
        newui::Size getExtent() const { return extent; }
        void setExtent(newui::Size v) { extent = v; }
        newui::Rect getBounds() const { return bounds; }
        void setBounds(newui::Rect v) { bounds = v; }
        Direction getFacing() const { return facing; }
        void setFacing(Direction v) { facing = v; }
        Modifiers getModifiers() const { return modifiers; }
        void setModifiers(Modifiers v) { modifiers = v; }
        newui::Font getFont() const { return font; }
        void setFont(newui::Font v) { font = v; }
    };

    const Class* registerWidgetOnce()
    {
        registerDirectionEnumOnce();
        registerModifiersEnumOnce();
        static const Class* registered = [] {
            ClassBuilder<Widget> builder;
            builder.clazz()
                .property("enabled", Scope::Public, &Widget::isEnabled, &Widget::setEnabled)
                .property("count", Scope::Public, &Widget::getCount, &Widget::setCount)
                .property("opacity", Scope::Public, &Widget::getOpacity, &Widget::setOpacity)
                .property("label", Scope::Public, &Widget::getLabel, &Widget::setLabel)
                .property("tags", Scope::Public, &Widget::getTags, &Widget::setTags)
                .property("tint", Scope::Public, &Widget::getTint, &Widget::setTint)
                .property("iconPath", Scope::Public, &Widget::getIconPath, &Widget::setIconPath, {"filepath"})
                .property("origin", Scope::Public, &Widget::getOrigin, &Widget::setOrigin)
                .property("extent", Scope::Public, &Widget::getExtent, &Widget::setExtent)
                .property("bounds", Scope::Public, &Widget::getBounds, &Widget::setBounds)
                .property("facing", Scope::Public, &Widget::getFacing, &Widget::setFacing)
                .property("modifiers", Scope::Public, &Widget::getModifiers, &Widget::setModifiers)
                .property("font", Scope::Public, &Widget::getFont, &Widget::setFont);
            ReflectionRegistry::registerClass(builder);
            return classinfo(typeid(Widget));
        }();
        return registered;
    }

    const Property* findProperty(const Class* clazz, const std::string& name)
    {
        std::vector<const Property*> props;
        clazz->allProperties(props);
        for (const Property* p : props) {
            if (p->name() == name) {
                return p;
            }
        }
        return nullptr;
    }
}

class PropertyEditorTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        widgetClass_ = registerWidgetOnce();
        CodeToolsVsix::PropertyEditorRegistry::instance().registerBuiltinEditors();
    }

    const Class* widgetClass_ = nullptr;
    Widget widget_;
};

TEST_F(PropertyEditorTest, BoolEditorRoundTripsThroughTheRealProperty)
{
    const Property* prop = findProperty(widgetClass_, "enabled");
    ASSERT_NE(prop, nullptr);

    auto editor = CodeToolsVsix::PropertyEditorRegistry::instance().createEditor(prop, widgetClass_, &widget_);
    ASSERT_NE(editor, nullptr);

    EXPECT_EQ(editor->valueAsString(), "false");
    editor->setValueFromString("true");
    EXPECT_TRUE(widget_.enabled);
    EXPECT_EQ(editor->valueAsString(), "true");
}

TEST_F(PropertyEditorTest, BoolEditorInvalidTextIsANoOp)
{
    const Property* prop = findProperty(widgetClass_, "enabled");
    auto editor = CodeToolsVsix::PropertyEditorRegistry::instance().createEditor(prop, widgetClass_, &widget_);

    widget_.enabled = true;
    editor->setValueFromString("not a bool");
    EXPECT_TRUE(widget_.enabled);
}

TEST_F(PropertyEditorTest, IntEditorRoundTripsThroughTheRealProperty)
{
    const Property* prop = findProperty(widgetClass_, "count");
    auto editor = CodeToolsVsix::PropertyEditorRegistry::instance().createEditor(prop, widgetClass_, &widget_);
    ASSERT_NE(editor, nullptr);

    editor->setValueFromString("42");
    EXPECT_EQ(widget_.count, 42);
    EXPECT_EQ(editor->valueAsString(), "42");
}

TEST_F(PropertyEditorTest, FloatEditorRoundTripsThroughTheRealProperty)
{
    const Property* prop = findProperty(widgetClass_, "opacity");
    auto editor = CodeToolsVsix::PropertyEditorRegistry::instance().createEditor(prop, widgetClass_, &widget_);
    ASSERT_NE(editor, nullptr);

    editor->setValueFromString("0.5");
    EXPECT_FLOAT_EQ(widget_.opacity, 0.5f);
}

TEST_F(PropertyEditorTest, StringEditorRoundTripsThroughTheRealProperty)
{
    const Property* prop = findProperty(widgetClass_, "label");
    auto editor = CodeToolsVsix::PropertyEditorRegistry::instance().createEditor(prop, widgetClass_, &widget_);
    ASSERT_NE(editor, nullptr);

    editor->setValueFromString("hello");
    EXPECT_EQ(widget_.label, "hello");
    EXPECT_EQ(editor->valueAsString(), "hello");
}

TEST_F(PropertyEditorTest, ColorEditorRoundTripsThroughTheRealProperty)
{
    const Property* prop = findProperty(widgetClass_, "tint");
    auto editor = CodeToolsVsix::PropertyEditorRegistry::instance().createEditor(prop, widgetClass_, &widget_);
    ASSERT_NE(editor, nullptr);

    editor->setValueFromString("#ff8800");
    EXPECT_EQ(editor->valueAsString(), "#ff8800ff");

    uint8_t rgb[3];
    widget_.tint.toRGB24(rgb);
    EXPECT_EQ(rgb[0], 0xff);
    EXPECT_EQ(rgb[1], 0x88);
    EXPECT_EQ(rgb[2], 0x00);
}

TEST_F(PropertyEditorTest, ColorEditorInvalidTextIsANoOp)
{
    const Property* prop = findProperty(widgetClass_, "tint");
    auto editor = CodeToolsVsix::PropertyEditorRegistry::instance().createEditor(prop, widgetClass_, &widget_);

    widget_.tint = newui::Color(1.0f, 0.0f, 0.0f);
    editor->setValueFromString("not a color");
    EXPECT_EQ(editor->valueAsString(), "#ff0000ff");
}

TEST_F(PropertyEditorTest, ColorEditorAcceptsANamedColorToo)
{
    const Property* prop = findProperty(widgetClass_, "tint");
    auto editor = CodeToolsVsix::PropertyEditorRegistry::instance().createEditor(prop, widgetClass_, &widget_);

    editor->setValueFromString("cornflowerblue");
    uint8_t rgb[3];
    widget_.tint.toRGB24(rgb);
    EXPECT_EQ(rgb[0], 0x64);
    EXPECT_EQ(rgb[1], 0x95);
    EXPECT_EQ(rgb[2], 0xed);
}

TEST_F(PropertyEditorTest, PointEditorRoundTripsThroughTheRealProperty)
{
    const Property* prop = findProperty(widgetClass_, "origin");
    auto editor = CodeToolsVsix::PropertyEditorRegistry::instance().createEditor(prop, widgetClass_, &widget_);
    ASSERT_NE(editor, nullptr);

    editor->setValueFromString("12, 34");
    EXPECT_FLOAT_EQ(widget_.origin.x, 12.0f);
    EXPECT_FLOAT_EQ(widget_.origin.y, 34.0f);
    EXPECT_EQ(editor->valueAsString(), "12, 34");
}

TEST_F(PropertyEditorTest, PointEditorInvalidTextIsANoOp)
{
    const Property* prop = findProperty(widgetClass_, "origin");
    auto editor = CodeToolsVsix::PropertyEditorRegistry::instance().createEditor(prop, widgetClass_, &widget_);

    widget_.origin = newui::Point(1.0f, 2.0f);
    editor->setValueFromString("12");  // only one component
    EXPECT_FLOAT_EQ(widget_.origin.x, 1.0f);
    EXPECT_FLOAT_EQ(widget_.origin.y, 2.0f);
}

TEST_F(PropertyEditorTest, SizeEditorRoundTripsThroughTheRealProperty)
{
    const Property* prop = findProperty(widgetClass_, "extent");
    auto editor = CodeToolsVsix::PropertyEditorRegistry::instance().createEditor(prop, widgetClass_, &widget_);
    ASSERT_NE(editor, nullptr);

    editor->setValueFromString("100, 50");
    EXPECT_FLOAT_EQ(widget_.extent.width, 100.0f);
    EXPECT_FLOAT_EQ(widget_.extent.height, 50.0f);
    EXPECT_EQ(editor->valueAsString(), "100, 50");
}

TEST_F(PropertyEditorTest, RectEditorRoundTripsThroughTheRealProperty)
{
    const Property* prop = findProperty(widgetClass_, "bounds");
    auto editor = CodeToolsVsix::PropertyEditorRegistry::instance().createEditor(prop, widgetClass_, &widget_);
    ASSERT_NE(editor, nullptr);

    editor->setValueFromString("0, 10, 640, 460");
    EXPECT_FLOAT_EQ(widget_.bounds.left(), 0.0f);
    EXPECT_FLOAT_EQ(widget_.bounds.top(), 10.0f);
    EXPECT_FLOAT_EQ(widget_.bounds.width(), 640.0f);
    EXPECT_FLOAT_EQ(widget_.bounds.height(), 460.0f);
    EXPECT_EQ(editor->valueAsString(), "0, 10, 640, 460");
}

TEST_F(PropertyEditorTest, RectEditorInvalidTextIsANoOp)
{
    const Property* prop = findProperty(widgetClass_, "bounds");
    auto editor = CodeToolsVsix::PropertyEditorRegistry::instance().createEditor(prop, widgetClass_, &widget_);

    widget_.bounds = newui::Rect(1.0f, 2.0f, 3.0f, 4.0f);
    editor->setValueFromString("not, a, rect");
    EXPECT_FLOAT_EQ(widget_.bounds.left(), 1.0f);
    EXPECT_FLOAT_EQ(widget_.bounds.width(), 3.0f);
}

TEST_F(PropertyEditorTest, RectEditorReportsSubPropertiesEditStyleAndNames)
{
    const Property* prop = findProperty(widgetClass_, "bounds");
    auto editor = CodeToolsVsix::PropertyEditorRegistry::instance().createEditor(prop, widgetClass_, &widget_);
    ASSERT_NE(editor, nullptr);

    EXPECT_EQ(editor->editStyle(), CodeToolsVsix::PropertyEditor::EditStyle::SubProperties);
    EXPECT_EQ(editor->subPropertyNames(), (std::vector<std::string>{"x", "y", "width", "height"}));
}

TEST_F(PropertyEditorTest, RectEditorSubPropertyRoundTripsOnlyThatOneComponent)
{
    const Property* prop = findProperty(widgetClass_, "bounds");
    auto editor = CodeToolsVsix::PropertyEditorRegistry::instance().createEditor(prop, widgetClass_, &widget_);

    widget_.bounds = newui::Rect(1.0f, 2.0f, 3.0f, 4.0f);
    EXPECT_EQ(editor->subPropertyValueAsString(0), "1");
    EXPECT_EQ(editor->subPropertyValueAsString(2), "3");

    editor->setSubPropertyValueFromString(2, "99");
    EXPECT_FLOAT_EQ(widget_.bounds.left(), 1.0f);   // untouched
    EXPECT_FLOAT_EQ(widget_.bounds.top(), 2.0f);    // untouched
    EXPECT_FLOAT_EQ(widget_.bounds.width(), 99.0f); // the one edited
    EXPECT_FLOAT_EQ(widget_.bounds.height(), 4.0f); // untouched
    EXPECT_EQ(editor->subPropertyValueAsString(2), "99");
}

TEST_F(PropertyEditorTest, RectEditorSubPropertyInvalidTextIsANoOp)
{
    const Property* prop = findProperty(widgetClass_, "bounds");
    auto editor = CodeToolsVsix::PropertyEditorRegistry::instance().createEditor(prop, widgetClass_, &widget_);

    widget_.bounds = newui::Rect(1.0f, 2.0f, 3.0f, 4.0f);
    editor->setSubPropertyValueFromString(0, "not a number");
    EXPECT_FLOAT_EQ(widget_.bounds.left(), 1.0f);
}

TEST_F(PropertyEditorTest, RectEditorSubPropertyEditIsUndoableAsTheWholeValue)
{
    const Property* prop = findProperty(widgetClass_, "bounds");
    auto editor = CodeToolsVsix::PropertyEditorRegistry::instance().createEditor(prop, widgetClass_, &widget_);

    newui::UndoStack undoStack;
    editor->setUndoStack(&undoStack);

    widget_.bounds = newui::Rect(1.0f, 2.0f, 3.0f, 4.0f);
    editor->setSubPropertyValueFromString(2, "99");
    EXPECT_FLOAT_EQ(widget_.bounds.width(), 99.0f);

    undoStack.undo();
    EXPECT_FLOAT_EQ(widget_.bounds.left(), 1.0f);
    EXPECT_FLOAT_EQ(widget_.bounds.width(), 3.0f);
}

TEST_F(PropertyEditorTest, FontEditorRoundTripsThroughTheRealProperty)
{
    const Property* prop = findProperty(widgetClass_, "font");
    auto editor = CodeToolsVsix::PropertyEditorRegistry::instance().createEditor(prop, widgetClass_, &widget_);
    ASSERT_NE(editor, nullptr);

    editor->setValueFromString("Arial, 14");
    EXPECT_EQ(widget_.font.name(), "Arial");
    EXPECT_FLOAT_EQ(widget_.font.size(), 14.0f);
    EXPECT_EQ(editor->valueAsString(), "Arial, 14");
}

TEST_F(PropertyEditorTest, FontEditorInvalidTextIsANoOp)
{
    const Property* prop = findProperty(widgetClass_, "font");
    auto editor = CodeToolsVsix::PropertyEditorRegistry::instance().createEditor(prop, widgetClass_, &widget_);

    widget_.font.setName("Segoe UI");
    widget_.font.setSize(12.0f);
    editor->setValueFromString("no size here");
    EXPECT_EQ(widget_.font.name(), "Segoe UI");
    EXPECT_FLOAT_EQ(widget_.font.size(), 12.0f);
}

TEST_F(PropertyEditorTest, FontEditorReportsSubPropertiesEditStyleAndNames)
{
    const Property* prop = findProperty(widgetClass_, "font");
    auto editor = CodeToolsVsix::PropertyEditorRegistry::instance().createEditor(prop, widgetClass_, &widget_);
    ASSERT_NE(editor, nullptr);

    EXPECT_EQ(editor->editStyle(), CodeToolsVsix::PropertyEditor::EditStyle::SubProperties);
    EXPECT_EQ(editor->subPropertyNames(),
        (std::vector<std::string>{"name", "size", "bold", "italic", "underlined", "strikeThrough"}));
    EXPECT_FALSE(editor->subPropertyIsBool(0));
    EXPECT_FALSE(editor->subPropertyIsBool(1));
    EXPECT_TRUE(editor->subPropertyIsBool(2));
    EXPECT_TRUE(editor->subPropertyIsBool(5));
}

TEST_F(PropertyEditorTest, FontEditorSubPropertyRoundTripsOnlyThatOneComponent)
{
    const Property* prop = findProperty(widgetClass_, "font");
    auto editor = CodeToolsVsix::PropertyEditorRegistry::instance().createEditor(prop, widgetClass_, &widget_);

    widget_.font.setName("Segoe UI");
    widget_.font.setSize(12.0f);
    EXPECT_EQ(editor->subPropertyValueAsString(0), "Segoe UI");
    EXPECT_EQ(editor->subPropertyValueAsString(1), "12");
    EXPECT_EQ(editor->subPropertyValueAsString(2), "false");

    editor->setSubPropertyValueFromString(2, "true");
    EXPECT_EQ(widget_.font.name(), "Segoe UI");   // untouched
    EXPECT_FLOAT_EQ(widget_.font.size(), 12.0f);  // untouched
    EXPECT_TRUE(widget_.font.bold());             // the one edited
    EXPECT_EQ(editor->subPropertyValueAsString(2), "true");
}

TEST_F(PropertyEditorTest, FontEditorSubPropertyInvalidTextIsANoOp)
{
    const Property* prop = findProperty(widgetClass_, "font");
    auto editor = CodeToolsVsix::PropertyEditorRegistry::instance().createEditor(prop, widgetClass_, &widget_);

    widget_.font.setSize(12.0f);
    editor->setSubPropertyValueFromString(1, "not a number");
    EXPECT_FLOAT_EQ(widget_.font.size(), 12.0f);
}

TEST_F(PropertyEditorTest, FontEditorSubPropertyEditIsUndoableAsTheWholeValue)
{
    const Property* prop = findProperty(widgetClass_, "font");
    auto editor = CodeToolsVsix::PropertyEditorRegistry::instance().createEditor(prop, widgetClass_, &widget_);

    newui::UndoStack undoStack;
    editor->setUndoStack(&undoStack);

    widget_.font.setName("Segoe UI");
    widget_.font.setSize(12.0f);
    editor->setSubPropertyValueFromString(1, "20");
    EXPECT_FLOAT_EQ(widget_.font.size(), 20.0f);

    undoStack.undo();
    EXPECT_EQ(widget_.font.name(), "Segoe UI");
    EXPECT_FLOAT_EQ(widget_.font.size(), 12.0f);
}

TEST_F(PropertyEditorTest, EnumEditorRoundTripsThroughTheRealProperty)
{
    // No registerEditor() call anywhere registers Direction specifically -
    // this exercises PropertyEditorRegistry::createEditor()'s generic
    // ReflectionRegistry::getEnum() fallback (see EnumPropertyEditor's own
    // comment), not a per-type wildcard.
    const Property* prop = findProperty(widgetClass_, "facing");
    ASSERT_NE(prop, nullptr);
    auto editor = CodeToolsVsix::PropertyEditorRegistry::instance().createEditor(prop, widgetClass_, &widget_);
    ASSERT_NE(editor, nullptr);

    EXPECT_EQ(editor->valueAsString(), "North");
    EXPECT_EQ(editor->dropdownValues(), (std::vector<std::string>{"North", "East", "South", "West"}));

    editor->setValueFromString("South");
    EXPECT_EQ(widget_.facing, Direction::South);
    EXPECT_EQ(editor->valueAsString(), "South");
}

TEST_F(PropertyEditorTest, EnumEditorInvalidTextIsANoOp)
{
    const Property* prop = findProperty(widgetClass_, "facing");
    auto editor = CodeToolsVsix::PropertyEditorRegistry::instance().createEditor(prop, widgetClass_, &widget_);

    widget_.facing = Direction::East;
    editor->setValueFromString("not a direction");
    EXPECT_EQ(widget_.facing, Direction::East);
}

// ---------------------------------------------------------------------------
// FlagsEnumPropertyEditor - PropertyEditorRegistry::createEditor()'s enum fallback routes here
// instead of EnumPropertyEditor once ReflectionRegistry::getEnum(property->type())->isFlags() is
// true (Modifiers, registered above with .flags(true) - mirrors newui::Anchor's own real shape).
// ---------------------------------------------------------------------------

TEST_F(PropertyEditorTest, FlagsEnumEditorIsSelectedInsteadOfThePlainEnumEditor)
{
    const Property* prop = findProperty(widgetClass_, "modifiers");
    ASSERT_NE(prop, nullptr);
    auto editor = CodeToolsVsix::PropertyEditorRegistry::instance().createEditor(prop, widgetClass_, &widget_);
    ASSERT_NE(editor, nullptr);

    EXPECT_EQ(editor->editStyle(), CodeToolsVsix::PropertyEditor::EditStyle::SubProperties);
    // None (value 0) is excluded - it isn't a real bit to toggle, same as Enum::decompose()'s own
    // zero-candidate skip (reflection.h).
    EXPECT_EQ(editor->subPropertyNames(), (std::vector<std::string>{"Ctrl", "Shift", "Alt"}));
    EXPECT_TRUE(editor->subPropertyIsBool(0));
}

TEST_F(PropertyEditorTest, FlagsEnumEditorReadsEachBitIndependently)
{
    const Property* prop = findProperty(widgetClass_, "modifiers");
    auto editor = CodeToolsVsix::PropertyEditorRegistry::instance().createEditor(prop, widgetClass_, &widget_);

    widget_.modifiers = static_cast<Modifiers>(static_cast<int>(Modifiers::Ctrl) | static_cast<int>(Modifiers::Alt));
    EXPECT_EQ(editor->subPropertyValueAsString(0), "true");   // Ctrl
    EXPECT_EQ(editor->subPropertyValueAsString(1), "false");  // Shift
    EXPECT_EQ(editor->subPropertyValueAsString(2), "true");   // Alt
    EXPECT_EQ(editor->valueAsString(), "Ctrl | Alt");
}

TEST_F(PropertyEditorTest, FlagsEnumEditorSetsOneBitWithoutDisturbingTheOthers)
{
    const Property* prop = findProperty(widgetClass_, "modifiers");
    auto editor = CodeToolsVsix::PropertyEditorRegistry::instance().createEditor(prop, widgetClass_, &widget_);

    widget_.modifiers = Modifiers::Ctrl;
    editor->setSubPropertyValueFromString(1, "true");  // check Shift too
    EXPECT_EQ(widget_.modifiers, static_cast<Modifiers>(static_cast<int>(Modifiers::Ctrl) | static_cast<int>(Modifiers::Shift)));

    editor->setSubPropertyValueFromString(0, "false");  // uncheck Ctrl
    EXPECT_EQ(widget_.modifiers, Modifiers::Shift);
}

TEST_F(PropertyEditorTest, FlagsEnumEditorSubPropertyEditIsUndoableAsTheWholeValue)
{
    const Property* prop = findProperty(widgetClass_, "modifiers");
    auto editor = CodeToolsVsix::PropertyEditorRegistry::instance().createEditor(prop, widgetClass_, &widget_);

    newui::UndoStack undoStack;
    editor->setUndoStack(&undoStack);

    widget_.modifiers = Modifiers::Ctrl;
    editor->setSubPropertyValueFromString(1, "true");  // check Shift
    EXPECT_EQ(widget_.modifiers, static_cast<Modifiers>(static_cast<int>(Modifiers::Ctrl) | static_cast<int>(Modifiers::Shift)));

    undoStack.undo();
    EXPECT_EQ(widget_.modifiers, Modifiers::Ctrl);
}

TEST_F(PropertyEditorTest, FlagsEnumEditorValueAsStringRoundTripsThroughParseValue)
{
    const Property* prop = findProperty(widgetClass_, "modifiers");
    auto editor = CodeToolsVsix::PropertyEditorRegistry::instance().createEditor(prop, widgetClass_, &widget_);

    editor->setValueFromString("Ctrl | Shift");
    EXPECT_EQ(widget_.modifiers, static_cast<Modifiers>(static_cast<int>(Modifiers::Ctrl) | static_cast<int>(Modifiers::Shift)));
    EXPECT_EQ(editor->valueAsString(), "Ctrl | Shift");
}

namespace
{
    // Minimal editor just to prove tag dispatch picked a *different* class
    // than the std::string wildcard (StringPropertyEditor) would.
    class MarkerPathEditor : public CodeToolsVsix::PropertyEditor
    {
    public:
        using CodeToolsVsix::PropertyEditor::PropertyEditor;
        std::string valueAsString() const override { return "marker:" + std::any_cast<std::string>(rawValue()); }
        std::optional<std::any> parseValue(const std::string& text) const override { return std::any(text); }
    };
}

TEST_F(PropertyEditorTest, TagRegistrationWinsOverTheTypeWildcard)
{
    const Property* prop = findProperty(widgetClass_, "iconPath");
    ASSERT_NE(prop, nullptr);
    EXPECT_EQ(prop->tags(), (std::vector<std::string>{"filepath"}));

    CodeToolsVsix::PropertyEditorRegistry registry;  // local, not instance() - same isolation lesson
    registry.registerBuiltinEditors();
    registry.registerEditor("filepath",
        [](const Property* p, void* instance) { return std::make_unique<MarkerPathEditor>(p, instance); });

    auto editor = registry.createEditor(prop, widgetClass_, &widget_);
    ASSERT_NE(editor, nullptr);

    widget_.iconPath = "icons/x.png";
    EXPECT_EQ(editor->valueAsString(), "marker:icons/x.png");
}

TEST_F(PropertyEditorTest, UntaggedPropertyStillFallsThroughToTheTypeWildcard)
{
    // "label" has no tags at all - a registry that only knows the
    // "filepath" tag must still resolve it via the ordinary std::string
    // wildcard, not fail or pick the tagged editor by mistake.
    const Property* prop = findProperty(widgetClass_, "label");
    ASSERT_TRUE(prop->tags().empty());

    CodeToolsVsix::PropertyEditorRegistry registry;
    registry.registerBuiltinEditors();
    registry.registerEditor("filepath",
        [](const Property* p, void* instance) { return std::make_unique<MarkerPathEditor>(p, instance); });

    auto editor = registry.createEditor(prop, widgetClass_, &widget_);
    ASSERT_NE(editor, nullptr);

    widget_.label = "hello";
    EXPECT_EQ(editor->valueAsString(), "hello");  // plain StringPropertyEditor, not "marker:hello"
}

TEST_F(PropertyEditorTest, NarrowerClassAndNameSpecificRegistrationWinsOverTheWildcard)
{
    const Property* prop = findProperty(widgetClass_, "count");

    // A local registry, not the shared instance() singleton - registering
    // a StringPropertyEditor override for "count" there would otherwise
    // leak into every later test in this binary that also touches "count"
    // (real bug, caught the hard way: it did, until this was a local
    // instance instead - see ComponentEditorRegistry's own tests, which
    // already avoid the singleton for exactly this reason).
    CodeToolsVsix::PropertyEditorRegistry registry;
    registry.registerBuiltinEditors();
    registry.registerEditor(
        std::type_index(typeid(int)),
        [](const Property* p, void* instance) { return std::make_unique<CodeToolsVsix::StringPropertyEditor>(p, instance); },
        widgetClass_, "count");

    auto editor = registry.createEditor(prop, widgetClass_, &widget_);
    ASSERT_NE(editor, nullptr);
    EXPECT_EQ(editor->editStyle(), CodeToolsVsix::PropertyEditor::EditStyle::None);
}

TEST_F(PropertyEditorTest, UnregisteredTypeReturnsNullptr)
{
    // "tags" (std::vector<int>) has no registered editor at all, unlike
    // bool/int/float/std::string - createEditor() must return nullptr, not
    // crash or silently pick something unrelated.
    const Property* prop = findProperty(widgetClass_, "tags");
    ASSERT_NE(prop, nullptr);

    auto editor = CodeToolsVsix::PropertyEditorRegistry::instance().createEditor(prop, widgetClass_, &widget_);
    EXPECT_EQ(editor, nullptr);
}

TEST_F(PropertyEditorTest, WithNoUndoStackAttachedCommitsDirectlyLikeBefore)
{
    const Property* prop = findProperty(widgetClass_, "count");
    auto editor = CodeToolsVsix::PropertyEditorRegistry::instance().createEditor(prop, widgetClass_, &widget_);

    EXPECT_EQ(editor->undoStack(), nullptr);
    editor->setValueFromString("7");
    EXPECT_EQ(widget_.count, 7);
}

TEST_F(PropertyEditorTest, WithAnUndoStackAttachedTheEditIsUndoable)
{
    const Property* prop = findProperty(widgetClass_, "count");
    auto editor = CodeToolsVsix::PropertyEditorRegistry::instance().createEditor(prop, widgetClass_, &widget_);

    newui::UndoStack undoStack;
    editor->setUndoStack(&undoStack);
    EXPECT_EQ(editor->undoStack(), &undoStack);

    widget_.count = 3;
    editor->setValueFromString("9");
    EXPECT_EQ(widget_.count, 9);
    EXPECT_TRUE(undoStack.canUndo());

    undoStack.undo();
    EXPECT_EQ(widget_.count, 3);

    undoStack.redo();
    EXPECT_EQ(widget_.count, 9);
}

TEST_F(PropertyEditorTest, InvalidTextWithAnUndoStackAttachedPushesNothing)
{
    const Property* prop = findProperty(widgetClass_, "count");
    auto editor = CodeToolsVsix::PropertyEditorRegistry::instance().createEditor(prop, widgetClass_, &widget_);

    newui::UndoStack undoStack;
    editor->setUndoStack(&undoStack);

    widget_.count = 3;
    editor->setValueFromString("not a number");
    EXPECT_EQ(widget_.count, 3);
    EXPECT_FALSE(undoStack.canUndo());
}

TEST_F(PropertyEditorTest, PushedActionDescriptionNamesTheProperty)
{
    const Property* prop = findProperty(widgetClass_, "label");
    auto editor = CodeToolsVsix::PropertyEditorRegistry::instance().createEditor(prop, widgetClass_, &widget_);

    newui::UndoStack undoStack;
    editor->setUndoStack(&undoStack);

    editor->setValueFromString("hello");
    EXPECT_EQ(undoStack.undoDescription(), "Change label");
}
