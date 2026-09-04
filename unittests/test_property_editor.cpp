#include "../extension/NativeEditControls/PropertyEditor.h"

#include <newui/reflection.h>

#include <gtest/gtest.h>

using namespace newui::reflection;

namespace
{
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
    };

    const Class* registerWidgetOnce()
    {
        static const Class* registered = [] {
            ClassBuilder<Widget> builder;
            builder.clazz()
                .property("enabled", Scope::Public, &Widget::isEnabled, &Widget::setEnabled)
                .property("count", Scope::Public, &Widget::getCount, &Widget::setCount)
                .property("opacity", Scope::Public, &Widget::getOpacity, &Widget::setOpacity)
                .property("label", Scope::Public, &Widget::getLabel, &Widget::setLabel)
                .property("tags", Scope::Public, &Widget::getTags, &Widget::setTags)
                .property("tint", Scope::Public, &Widget::getTint, &Widget::setTint)
                .property("iconPath", Scope::Public, &Widget::getIconPath, &Widget::setIconPath, {"filepath"});
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
