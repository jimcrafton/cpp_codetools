#include "../extension/NativeEditControls/ComponentEditor.h"

#include <newui/subview.h>
#include <newui/controls.h>
#include <newui/undostack.h>

#include <gtest/gtest.h>

using namespace newui::reflection;

// Defined in the reflectgen-generated .cpp linked into the `newui` target -
// self-guarding at the source (only actually registers once per process,
// however many places call it - see its own comment in reflectgen.py's
// generate()), so this and NativeEditManager's own constructor
// (NativeEditor.cpp) can both just call it directly.
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

// ---------------------------------------------------------------------------
// TabControlEditor
// ---------------------------------------------------------------------------

namespace
{
    std::unique_ptr<CodeToolsVsix::ComponentEditor> tabEditorFor(newui::TabControl* tabs)
    {
        CodeToolsVsix::ComponentEditorRegistry registry;
        registry.registerBuiltinEditors();
        return registry.createEditor(classinfo(typeid(newui::TabControl)), tabs);
    }

    struct TabsFixture
    {
        TabsFixture() : tabs(new newui::TabControl()) {}
        ~TabsFixture()
        {
            tabs->destroy();
            delete tabs;
        }
        newui::TabControl* tabs;
    };
}

TEST(TabControlEditorTest, BuiltinRegistrationCoversTabControlOnly)
{
    TabsFixture f;
    auto editor = tabEditorFor(f.tabs);
    ASSERT_NE(editor, nullptr);
    EXPECT_NE(dynamic_cast<CodeToolsVsix::TabControlEditor*>(editor.get()), nullptr);

    CodeToolsVsix::ComponentEditorRegistry registry;
    registry.registerBuiltinEditors();
    registry.registerBuiltinEditors();  // guarded - harmless twice
    newui::SubView plain;
    EXPECT_EQ(registry.createEditor(classinfo(typeid(newui::SubView)), &plain), nullptr);
}

TEST(TabControlEditorTest, VerbsAreAddTabAlwaysAndRemoveOnlyOnceThereIsATab)
{
    TabsFixture f;
    auto editor = tabEditorFor(f.tabs);

    EXPECT_EQ(editor->verbCount(), 1u);
    EXPECT_EQ(editor->verb(0), "Add Tab");

    editor->executeVerb(0);
    EXPECT_EQ(editor->verbCount(), 2u);
    EXPECT_EQ(editor->verb(1), "Remove Last Tab");
}

TEST(TabControlEditorTest, AddTabAppendsNumberedTabsWithNoUndoStackAttached)
{
    TabsFixture f;
    auto editor = tabEditorFor(f.tabs);

    editor->executeVerb(0);
    editor->executeVerb(0);

    ASSERT_EQ(f.tabs->tabCount(), 2u);
    EXPECT_EQ(f.tabs->tabButton(0)->name(), "Tab 1");
    EXPECT_EQ(f.tabs->tabButton(1)->name(), "Tab 2");
    EXPECT_EQ(f.tabs->selectedIndex(), 0u);  // first tab auto-selects; CardLayout hides the others
    EXPECT_TRUE(f.tabs->page(0)->isVisible());
}

TEST(TabControlEditorTest, AddTabIsUndoableAndRedoRestoresTheSamePage)
{
    TabsFixture f;
    newui::UndoStack undo;
    auto editor = tabEditorFor(f.tabs);
    editor->setUndoStack(&undo);
    int syncs = 0;
    editor->setPostExecuteSync([&syncs] { ++syncs; });

    editor->executeVerb(0);
    ASSERT_EQ(f.tabs->tabCount(), 1u);
    newui::SubView* page = f.tabs->page(0);
    EXPECT_EQ(undo.undoDescription(), "Add Tab");
    EXPECT_EQ(syncs, 1);

    undo.undo();
    EXPECT_EQ(f.tabs->tabCount(), 0u);
    EXPECT_EQ(syncs, 2);

    undo.redo();
    ASSERT_EQ(f.tabs->tabCount(), 1u);
    EXPECT_EQ(f.tabs->page(0), page);
    EXPECT_EQ(syncs, 3);
}

TEST(TabControlEditorTest, RemoveLastTabIsUndoableAndRestoresTheTabAndSelection)
{
    TabsFixture f;
    newui::UndoStack undo;
    auto editor = tabEditorFor(f.tabs);
    editor->executeVerb(0);
    editor->executeVerb(0);
    editor->executeVerb(0);
    f.tabs->selectTab(1);
    newui::SubView* lastPage = f.tabs->page(2);

    editor->setUndoStack(&undo);
    editor->executeVerb(1);
    EXPECT_EQ(undo.undoDescription(), "Remove Tab");
    ASSERT_EQ(f.tabs->tabCount(), 2u);
    EXPECT_EQ(f.tabs->selectedIndex(), 1u);

    undo.undo();
    ASSERT_EQ(f.tabs->tabCount(), 3u);
    EXPECT_EQ(f.tabs->tabButton(2)->name(), "Tab 3");
    EXPECT_EQ(f.tabs->page(2), lastPage);
    EXPECT_EQ(f.tabs->selectedIndex(), 1u);

    undo.redo();
    EXPECT_EQ(f.tabs->tabCount(), 2u);
    lastPage->destroy();  // detached again by redo - nothing owns it now
    delete lastPage;
}

TEST(TabControlEditorTest, RemoveLastTabOnAnEmptyControlDoesNothing)
{
    TabsFixture f;
    newui::UndoStack undo;
    auto editor = tabEditorFor(f.tabs);
    editor->setUndoStack(&undo);

    editor->executeVerb(1);
    EXPECT_FALSE(undo.canUndo());
}
