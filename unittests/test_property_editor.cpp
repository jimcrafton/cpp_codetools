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
                .property("tags", Scope::Public, &Widget::getTags, &Widget::setTags);
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

TEST_F(PropertyEditorTest, NarrowerClassAndNameSpecificRegistrationWinsOverTheWildcard)
{
    const Property* prop = findProperty(widgetClass_, "count");

    CodeToolsVsix::PropertyEditorRegistry::instance().registerEditor(
        std::type_index(typeid(int)),
        [](const Property* p, void* instance) { return std::make_unique<CodeToolsVsix::StringPropertyEditor>(p, instance); },
        widgetClass_, "count");

    auto editor = CodeToolsVsix::PropertyEditorRegistry::instance().createEditor(prop, widgetClass_, &widget_);
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
