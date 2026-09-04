#include "../extension/NativeEditControls/ComponentEditor.h"

#include <newui/subview.h>

#include <gtest/gtest.h>

using namespace newui::reflection;

// Defined in the reflectgen-generated .cpp linked into the `newui` target -
// only runs once something calls it (same pattern as newui's own
// unittests/test_reflection.cpp). Needed here since these tests use real
// newui::View/SubView classes, unlike test_property_editor.cpp's own
// hand-registered Widget.
extern void registerReflectionData();

namespace
{
    class ReflectionDataEnvironment : public ::testing::Environment
    {
    public:
        void SetUp() override { registerReflectionData(); }
    };

    ::testing::Environment* const g_reflectionDataEnv =
        ::testing::AddGlobalTestEnvironment(new ReflectionDataEnvironment());

    class RecordingComponentEditor : public CodeToolsVsix::ComponentEditor
    {
    public:
        using CodeToolsVsix::ComponentEditor::ComponentEditor;

        std::size_t verbCount() const override { return 1; }
        std::string verb(std::size_t index) const override { return "Rename..."; }
        void executeVerb(std::size_t index) override { executedVerb = static_cast<int>(index); }
        void edit() override { editCalled = true; }

        int executedVerb = -1;
        bool editCalled = false;
    };
}

class ComponentEditorTest : public ::testing::Test
{
protected:
    void TearDown() override
    {
        delete view_;
    }

    newui::SubView* view_ = new newui::SubView();
};

TEST_F(ComponentEditorTest, ExactClassMatchWins)
{
    const Class* subViewClass = classinfo(typeid(newui::SubView));
    ASSERT_NE(subViewClass, nullptr);

    CodeToolsVsix::ComponentEditorRegistry registry;
    registry.registerEditor(subViewClass,
        [](newui::View* v) { return std::make_unique<RecordingComponentEditor>(v); });

    auto editor = registry.createEditor(subViewClass, view_);
    ASSERT_NE(editor, nullptr);
    EXPECT_EQ(editor->view(), view_);
    EXPECT_EQ(editor->verbCount(), 1u);
}

TEST_F(ComponentEditorTest, FallsBackToParentClassWhenNoExactMatchExists)
{
    const Class* viewClass = classinfo(typeid(newui::View));
    const Class* subViewClass = classinfo(typeid(newui::SubView));
    ASSERT_NE(viewClass, nullptr);
    ASSERT_NE(subViewClass, nullptr);

    CodeToolsVsix::ComponentEditorRegistry registry;
    registry.registerEditor(viewClass,
        [](newui::View* v) { return std::make_unique<RecordingComponentEditor>(v); });

    // Nothing registered for SubView itself - should walk up parentClass()
    // to View's registration.
    auto editor = registry.createEditor(subViewClass, view_);
    ASSERT_NE(editor, nullptr);
    EXPECT_EQ(editor->view(), view_);
}

TEST_F(ComponentEditorTest, NullptrWhenNothingInTheChainIsRegistered)
{
    const Class* subViewClass = classinfo(typeid(newui::SubView));

    CodeToolsVsix::ComponentEditorRegistry registry;  // nothing registered at all

    auto editor = registry.createEditor(subViewClass, view_);
    EXPECT_EQ(editor, nullptr);
}

TEST_F(ComponentEditorTest, ExecuteVerbAndEditReachTheConcreteOverride)
{
    const Class* subViewClass = classinfo(typeid(newui::SubView));

    CodeToolsVsix::ComponentEditorRegistry registry;
    registry.registerEditor(subViewClass,
        [](newui::View* v) { return std::make_unique<RecordingComponentEditor>(v); });

    auto editor = registry.createEditor(subViewClass, view_);
    auto* recording = static_cast<RecordingComponentEditor*>(editor.get());

    editor->executeVerb(0);
    EXPECT_EQ(recording->executedVerb, 0);

    editor->edit();
    EXPECT_TRUE(recording->editCalled);
}

TEST(ComponentEditorDefaults, BaseClassHasNoVerbsAndANoOpEdit)
{
    newui::SubView view;
    CodeToolsVsix::ComponentEditor editor(&view);

    EXPECT_EQ(editor.verbCount(), 0u);
    EXPECT_EQ(editor.view(), &view);
    editor.edit();  // must not crash - default is a no-op
}
