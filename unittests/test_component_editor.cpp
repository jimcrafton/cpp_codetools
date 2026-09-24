#include "../extension/NativeEditControls/ComponentEditor.h"

#include <newui/subview.h>
#include <newui/controls.h>
#include <newui/splitter.h>
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

// ---------------------------------------------------------------------------
// ToolbarEditor
// ---------------------------------------------------------------------------

namespace
{
    std::unique_ptr<CodeToolsVsix::ComponentEditor> toolbarEditorFor(newui::Toolbar* toolbar)
    {
        CodeToolsVsix::ComponentEditorRegistry registry;
        registry.registerBuiltinEditors();
        return registry.createEditor(classinfo(typeid(newui::Toolbar)), toolbar);
    }

    struct ToolbarFixture
    {
        ToolbarFixture() : toolbar(new newui::Toolbar()) {}
        ~ToolbarFixture()
        {
            toolbar->destroy();
            delete toolbar;
        }
        newui::Toolbar* toolbar;
    };
}

TEST(ToolbarEditorTest, RegisteredForToolbarWithRemoveOnlyOnceThereIsAnItem)
{
    ToolbarFixture f;
    auto editor = toolbarEditorFor(f.toolbar);
    ASSERT_NE(editor, nullptr);
    EXPECT_NE(dynamic_cast<CodeToolsVsix::ToolbarEditor*>(editor.get()), nullptr);

    EXPECT_EQ(editor->verbCount(), 2u);
    EXPECT_EQ(editor->verb(0), "Add Button");
    EXPECT_EQ(editor->verb(1), "Add Separator");

    editor->executeVerb(0);
    EXPECT_EQ(editor->verbCount(), 3u);
    EXPECT_EQ(editor->verb(2), "Remove Last Item");
}

TEST(ToolbarEditorTest, AddsNumberedSizedButtonsAndSeparators)
{
    ToolbarFixture f;
    auto editor = toolbarEditorFor(f.toolbar);

    editor->executeVerb(0);
    editor->executeVerb(1);
    editor->executeVerb(0);

    ASSERT_EQ(f.toolbar->childViews().size(), 3u);
    auto* first = dynamic_cast<newui::ToolbarButton*>(f.toolbar->childViews()[0]);
    auto* sep = dynamic_cast<newui::ToolbarSeparator*>(f.toolbar->childViews()[1]);
    auto* second = dynamic_cast<newui::ToolbarButton*>(f.toolbar->childViews()[2]);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(sep, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(first->name(), "Button 1");
    EXPECT_EQ(second->name(), "Button 2");
    EXPECT_EQ(sep->name(), "Separator 1");
    EXPECT_GT(first->desiredSize().width, 0.0f);
    EXPECT_GT(first->desiredSize().height, 0.0f);
    EXPECT_TRUE(sep->isHorizontal());
}

TEST(ToolbarEditorTest, VerticalToolbarGetsVerticalSeparator)
{
    ToolbarFixture f;
    f.toolbar->setOrientation(newui::Orientation::Vertical);
    auto editor = toolbarEditorFor(f.toolbar);

    editor->executeVerb(1);

    auto* sep = dynamic_cast<newui::ToolbarSeparator*>(f.toolbar->childViews()[0]);
    ASSERT_NE(sep, nullptr);
    EXPECT_FALSE(sep->isHorizontal());
}

TEST(ToolbarEditorTest, AddButtonIsUndoableAndRedoRestoresTheSameButton)
{
    ToolbarFixture f;
    newui::UndoStack undo;
    auto editor = toolbarEditorFor(f.toolbar);
    editor->setUndoStack(&undo);
    int syncs = 0;
    editor->setPostExecuteSync([&syncs] { ++syncs; });

    editor->executeVerb(0);
    ASSERT_EQ(f.toolbar->childViews().size(), 1u);
    newui::SubView* button = f.toolbar->childViews()[0];
    EXPECT_EQ(undo.undoDescription(), "Add Button");
    EXPECT_EQ(syncs, 1);

    undo.undo();
    EXPECT_TRUE(f.toolbar->childViews().empty());
    EXPECT_EQ(syncs, 2);

    undo.redo();
    ASSERT_EQ(f.toolbar->childViews().size(), 1u);
    EXPECT_EQ(f.toolbar->childViews()[0], button);
}

TEST(ToolbarEditorTest, RemoveLastItemIsUndoableAndKeepsOrder)
{
    ToolbarFixture f;
    newui::UndoStack undo;
    auto editor = toolbarEditorFor(f.toolbar);
    editor->executeVerb(0);
    editor->executeVerb(1);
    newui::SubView* last = f.toolbar->childViews()[1];

    editor->setUndoStack(&undo);
    editor->executeVerb(2);
    EXPECT_EQ(undo.undoDescription(), "Remove Toolbar Item");
    ASSERT_EQ(f.toolbar->childViews().size(), 1u);

    undo.undo();
    ASSERT_EQ(f.toolbar->childViews().size(), 2u);
    EXPECT_EQ(f.toolbar->childViews()[1], last);

    undo.redo();
    EXPECT_EQ(f.toolbar->childViews().size(), 1u);
    last->destroy();  // detached again by redo - nothing owns it now
    delete last;
}

// ---------------------------------------------------------------------------
// SplitterEditor
// ---------------------------------------------------------------------------

namespace
{
    std::unique_ptr<CodeToolsVsix::ComponentEditor> splitterEditorFor(newui::Splitter* splitter)
    {
        CodeToolsVsix::ComponentEditorRegistry registry;
        registry.registerBuiltinEditors();
        return registry.createEditor(classinfo(typeid(newui::Splitter)), splitter);
    }

    struct SplitterFixture
    {
        SplitterFixture() : splitter(new newui::Splitter())
        {
            splitter->setBounds(newui::Rect(0, 0, 400, 200));
        }
        ~SplitterFixture()
        {
            splitter->destroy();
            delete splitter;
        }
        newui::Splitter* splitter;
    };
}

TEST(SplitterEditorTest, VerbsFollowThePaneCount)
{
    SplitterFixture f;
    auto editor = splitterEditorFor(f.splitter);
    ASSERT_NE(editor, nullptr);
    EXPECT_NE(dynamic_cast<CodeToolsVsix::SplitterEditor*>(editor.get()), nullptr);

    ASSERT_EQ(editor->verbCount(), 1u);
    EXPECT_EQ(editor->verb(0), "Add Pane");

    editor->executeVerb(0);
    ASSERT_EQ(editor->verbCount(), 2u);
    EXPECT_EQ(editor->verb(0), "Add Pane");
    EXPECT_EQ(editor->verb(1), "Remove Last Pane");

    editor->executeVerb(0);
    ASSERT_EQ(editor->verbCount(), 2u);
    EXPECT_EQ(editor->verb(0), "Remove Last Pane");
    EXPECT_EQ(editor->verb(1), "Swap Panes");
}

TEST(SplitterEditorTest, AddPaneCreatesTwoDroppableArrangedPanes)
{
    SplitterFixture f;
    auto editor = splitterEditorFor(f.splitter);

    editor->executeVerb(0);
    editor->executeVerb(0);

    ASSERT_EQ(f.splitter->childViews().size(), 2u);
    newui::SubView* a = f.splitter->childViews()[0];
    newui::SubView* b = f.splitter->childViews()[1];
    EXPECT_EQ(a->name(), "Pane 1");
    EXPECT_EQ(b->name(), "Pane 2");
    EXPECT_NE(a->layout(), nullptr);
    EXPECT_NE(b->layout(), nullptr);
    EXPECT_GT(a->bounds().size().width, 0.0f);
    EXPECT_GT(b->bounds().size().width, 0.0f);
    EXPECT_LT(a->bounds().left(), b->bounds().left());
}

TEST(SplitterEditorTest, AddPaneIsUndoable)
{
    SplitterFixture f;
    newui::UndoStack undo;
    auto editor = splitterEditorFor(f.splitter);
    editor->setUndoStack(&undo);

    editor->executeVerb(0);
    ASSERT_EQ(f.splitter->childViews().size(), 1u);
    newui::SubView* pane = f.splitter->childViews()[0];
    EXPECT_EQ(undo.undoDescription(), "Add Pane");

    undo.undo();
    EXPECT_TRUE(f.splitter->childViews().empty());

    undo.redo();
    ASSERT_EQ(f.splitter->childViews().size(), 1u);
    EXPECT_EQ(f.splitter->childViews()[0], pane);
}

TEST(SplitterEditorTest, RemoveLastPaneIsUndoable)
{
    SplitterFixture f;
    newui::UndoStack undo;
    auto editor = splitterEditorFor(f.splitter);
    editor->executeVerb(0);
    editor->executeVerb(0);
    newui::SubView* second = f.splitter->childViews()[1];

    editor->setUndoStack(&undo);
    editor->executeVerb(0);   // with two panes the verbs are Remove Last Pane, Swap Panes
    EXPECT_EQ(undo.undoDescription(), "Remove Pane");
    ASSERT_EQ(f.splitter->childViews().size(), 1u);

    undo.undo();
    ASSERT_EQ(f.splitter->childViews().size(), 2u);
    EXPECT_EQ(f.splitter->childViews()[1], second);

    undo.redo();
    EXPECT_EQ(f.splitter->childViews().size(), 1u);
    second->destroy();  // detached again by redo - nothing owns it now
    delete second;
}

TEST(SplitterEditorTest, SwapPanesReordersRearrangesAndUndoesToTheOriginal)
{
    SplitterFixture f;
    newui::UndoStack undo;
    auto editor = splitterEditorFor(f.splitter);
    editor->executeVerb(0);
    editor->executeVerb(0);
    newui::SubView* a = f.splitter->childViews()[0];
    newui::SubView* b = f.splitter->childViews()[1];
    const float aLeftBefore = a->bounds().left();
    const float bLeftBefore = b->bounds().left();

    editor->setUndoStack(&undo);
    ASSERT_EQ(editor->verb(1), "Swap Panes");
    editor->executeVerb(1);
    EXPECT_EQ(undo.undoDescription(), "Swap Panes");
    EXPECT_EQ(f.splitter->childViews()[0], b);
    EXPECT_EQ(f.splitter->childViews()[1], a);
    EXPECT_EQ(b->bounds().left(), aLeftBefore);   // panes actually re-laid-out, not just reordered
    EXPECT_EQ(a->bounds().left(), bLeftBefore);

    undo.undo();
    EXPECT_EQ(f.splitter->childViews()[0], a);
    EXPECT_EQ(f.splitter->childViews()[1], b);
    EXPECT_EQ(a->bounds().left(), aLeftBefore);
}

// ---------------------------------------------------------------------------
// ListModelEditor
// ---------------------------------------------------------------------------

namespace
{
    std::unique_ptr<CodeToolsVsix::ComponentEditor> listEditorFor(newui::View* view, const std::type_info& type)
    {
        CodeToolsVsix::ComponentEditorRegistry registry;
        registry.registerBuiltinEditors();
        return registry.createEditor(classinfo(type), view);
    }

    // A model the editor must leave alone - not a StringListModel.
    class OtherListModel : public newui::ListModel
    {
    public:
        std::size_t size() const override { return 2; }
    };

    struct ListFixture
    {
        ListFixture() : list(new newui::ListView()) {}
        ~ListFixture()
        {
            list->destroy();
            delete list;
        }
        newui::ListView* list;
    };
}

TEST(ListModelEditorTest, RegisteredForListViewAndDropDownList)
{
    ListFixture f;
    auto listEditor = listEditorFor(f.list, typeid(newui::ListView));
    EXPECT_NE(dynamic_cast<CodeToolsVsix::ListModelEditor*>(listEditor.get()), nullptr);

    auto* dropDown = new newui::DropDownList();
    auto dropDownEditor = listEditorFor(dropDown, typeid(newui::DropDownList));
    EXPECT_NE(dynamic_cast<CodeToolsVsix::ListModelEditor*>(dropDownEditor.get()), nullptr);
    dropDown->destroy();
    delete dropDown;
}

TEST(ListModelEditorTest, AddItemOnAViewWithNoModelAttachesAStringListModelAndUndoRemovesIt)
{
    ListFixture f;
    newui::UndoStack undo;
    auto editor = listEditorFor(f.list, typeid(newui::ListView));
    editor->setUndoStack(&undo);
    int syncs = 0;
    editor->setPostExecuteSync([&syncs] { ++syncs; });

    ASSERT_EQ(editor->verbCount(), 1u);
    EXPECT_EQ(editor->verb(0), "Add Item");
    ASSERT_EQ(f.list->model(), nullptr);

    editor->executeVerb(0);
    auto* model = dynamic_cast<newui::StringListModel*>(f.list->model());
    ASSERT_NE(model, nullptr);
    EXPECT_EQ(model->items(), (std::vector<std::string>{ "Item 1" }));
    EXPECT_EQ(f.list->controller().itemCount(), 1u);
    EXPECT_EQ(undo.undoDescription(), "Add Item");
    EXPECT_EQ(syncs, 1);

    undo.undo();
    EXPECT_EQ(f.list->model(), nullptr);   // the model it created goes away with the item
    EXPECT_EQ(syncs, 2);

    undo.redo();
    model = dynamic_cast<newui::StringListModel*>(f.list->model());
    ASSERT_NE(model, nullptr);
    EXPECT_EQ(model->items(), (std::vector<std::string>{ "Item 1" }));
}

TEST(ListModelEditorTest, ItemsAreNumberedRemovedFromTheEndAndBothAreUndoable)
{
    ListFixture f;
    newui::UndoStack undo;
    auto model = std::make_unique<newui::StringListModel>();
    model->items() = { "a", "b" };
    newui::StringListModel* raw = model.get();
    f.list->setModel(std::move(model));

    auto editor = listEditorFor(f.list, typeid(newui::ListView));
    editor->setUndoStack(&undo);
    ASSERT_EQ(editor->verbCount(), 2u);
    EXPECT_EQ(editor->verb(1), "Remove Last Item");

    editor->executeVerb(0);
    EXPECT_EQ(raw->items(), (std::vector<std::string>{ "a", "b", "Item 3" }));

    editor->executeVerb(1);
    EXPECT_EQ(raw->items(), (std::vector<std::string>{ "a", "b" }));
    EXPECT_EQ(undo.undoDescription(), "Remove Item");

    undo.undo();
    EXPECT_EQ(raw->items(), (std::vector<std::string>{ "a", "b", "Item 3" }));
    undo.undo();
    EXPECT_EQ(raw->items(), (std::vector<std::string>{ "a", "b" }));
    EXPECT_EQ(f.list->model(), raw);   // a model that was already there is never replaced
}

TEST(ListModelEditorTest, AViewShowingSomeOtherKindOfModelGetsNoVerbs)
{
    ListFixture f;
    f.list->setModel(std::make_unique<OtherListModel>());

    auto editor = listEditorFor(f.list, typeid(newui::ListView));
    EXPECT_EQ(editor->verbCount(), 0u);
    editor->executeVerb(0);   // must not touch the model
    EXPECT_EQ(f.list->controller().itemCount(), 2u);
}

TEST(ListModelEditorTest, WorksOnADropDownListToo)
{
    auto* dropDown = new newui::DropDownList();
    auto editor = listEditorFor(dropDown, typeid(newui::DropDownList));

    editor->executeVerb(0);
    editor->executeVerb(0);

    auto* model = dynamic_cast<newui::StringListModel*>(dropDown->model());
    ASSERT_NE(model, nullptr);
    EXPECT_EQ(model->items(), (std::vector<std::string>{ "Item 1", "Item 2" }));
    dropDown->destroy();
    delete dropDown;
}

// ---------------------------------------------------------------------------
// TreeModelEditor
// ---------------------------------------------------------------------------

namespace
{
    // A model the editor must leave alone - not a StringTreeModel.
    class OtherTreeModel : public newui::TreeModel
    {
    public:
        std::size_t childCount(const std::vector<std::size_t>& path) const override {
            return path.empty() ? 2 : 0;
        }
    };

    struct TreeFixture
    {
        TreeFixture() : tree(new newui::TreeView()) {}
        ~TreeFixture()
        {
            tree->destroy();
            delete tree;
        }
        newui::TreeView* tree;
    };
}

TEST(TreeModelEditorTest, RegisteredForTreeView)
{
    TreeFixture f;
    auto editor = listEditorFor(f.tree, typeid(newui::TreeView));
    EXPECT_NE(dynamic_cast<CodeToolsVsix::TreeModelEditor*>(editor.get()), nullptr);
}

TEST(TreeModelEditorTest, AddItemOnAViewWithNoModelAttachesAStringTreeModelAndUndoRemovesIt)
{
    TreeFixture f;
    newui::UndoStack undo;
    auto editor = listEditorFor(f.tree, typeid(newui::TreeView));
    editor->setUndoStack(&undo);
    int syncs = 0;
    editor->setPostExecuteSync([&syncs] { ++syncs; });

    ASSERT_EQ(editor->verbCount(), 1u);
    EXPECT_EQ(editor->verb(0), "Add Item");
    ASSERT_EQ(f.tree->model(), nullptr);

    editor->executeVerb(0);
    auto* model = dynamic_cast<newui::StringTreeModel*>(f.tree->model());
    ASSERT_NE(model, nullptr);
    ASSERT_EQ(model->rows().size(), 1u);
    EXPECT_EQ(model->rows()[0].depth, 0u);
    EXPECT_EQ(model->rows()[0].text, "Item 1");
    EXPECT_EQ(f.tree->controller().visibleCount(), 1u);
    EXPECT_EQ(undo.undoDescription(), "Add Item");
    EXPECT_EQ(syncs, 1);

    undo.undo();
    EXPECT_EQ(f.tree->model(), nullptr);   // the model it created goes away with the item
    EXPECT_EQ(syncs, 2);

    undo.redo();
    model = dynamic_cast<newui::StringTreeModel*>(f.tree->model());
    ASSERT_NE(model, nullptr);
    EXPECT_EQ(model->rows().size(), 1u);
}

TEST(TreeModelEditorTest, ItemsAreNumberedRemovedFromTheEndPreservingDepthAndBothAreUndoable)
{
    TreeFixture f;
    newui::UndoStack undo;
    auto model = std::make_unique<newui::StringTreeModel>();
    model->rows() = { { 0, "a" }, { 1, "b" } };   // "b" nests under "a"
    newui::StringTreeModel* raw = model.get();
    f.tree->setModel(std::move(model));

    auto editor = listEditorFor(f.tree, typeid(newui::TreeView));
    editor->setUndoStack(&undo);
    ASSERT_EQ(editor->verbCount(), 2u);
    EXPECT_EQ(editor->verb(1), "Remove Last Item");

    editor->executeVerb(0);   // Add Item always appends a root row, never a child
    ASSERT_EQ(raw->rows().size(), 3u);
    EXPECT_EQ(raw->rows()[2].depth, 0u);
    EXPECT_EQ(raw->rows()[2].text, "Item 3");

    editor->executeVerb(1);
    ASSERT_EQ(raw->rows().size(), 2u);
    EXPECT_EQ(undo.undoDescription(), "Remove Item");

    // Removing "b" (depth 1) and undoing must restore it exactly, nesting included.
    editor->executeVerb(1);
    ASSERT_EQ(raw->rows().size(), 1u);
    undo.undo();
    ASSERT_EQ(raw->rows().size(), 2u);
    EXPECT_EQ(raw->rows()[1].depth, 1u);
    EXPECT_EQ(raw->rows()[1].text, "b");
    EXPECT_EQ(raw->childCount({0}), 1u);

    undo.undo();
    ASSERT_EQ(raw->rows().size(), 3u);
    EXPECT_EQ(f.tree->model(), raw);   // a model that was already there is never replaced
}

TEST(TreeModelEditorTest, AViewShowingSomeOtherKindOfModelGetsNoVerbs)
{
    TreeFixture f;
    f.tree->setModel(std::make_unique<OtherTreeModel>());

    auto editor = listEditorFor(f.tree, typeid(newui::TreeView));
    EXPECT_EQ(editor->verbCount(), 0u);
    editor->executeVerb(0);   // must not touch the model
    EXPECT_EQ(f.tree->controller().visibleCount(), 2u);
}

// ---------------------------------------------------------------------------
// Double-click default property (edit())
// ---------------------------------------------------------------------------

namespace
{
    std::vector<std::string> defaultPathFor(const std::type_info& type, newui::View* view)
    {
        CodeToolsVsix::ComponentEditorRegistry registry;
        registry.registerBuiltinEditors();
        auto editor = registry.createEditor(classinfo(type), view);
        return editor != nullptr ? editor->defaultPropertyPath() : std::vector<std::string>();
    }
}

TEST(ComponentEditorEditTest, BuiltinsNameTheirDefaultProperty)
{
    newui::Button button;
    newui::Label label;
    newui::Image image;
    EXPECT_EQ(defaultPathFor(typeid(newui::Button), &button), std::vector<std::string>({ "text" }));
    EXPECT_EQ(defaultPathFor(typeid(newui::Label), &label), std::vector<std::string>({ "text" }));
    EXPECT_EQ(defaultPathFor(typeid(newui::Image), &image), std::vector<std::string>({ "imagePath" }));

    ListFixture list;
    EXPECT_EQ(defaultPathFor(typeid(newui::ListView), list.list), std::vector<std::string>({ "model", "items" }));
}

TEST(ComponentEditorEditTest, VerblessDefaultPropertyEditorsStayOutOfTheContextMenu)
{
    newui::Button button;
    CodeToolsVsix::ComponentEditorRegistry registry;
    registry.registerBuiltinEditors();
    auto editor = registry.createEditor(classinfo(typeid(newui::Button)), &button);
    ASSERT_NE(editor, nullptr);
    EXPECT_EQ(editor->verbCount(), 0u);
}

TEST(ComponentEditorEditTest, EditHandsTheViewAndPathToTheHandler)
{
    newui::Button button;
    CodeToolsVsix::DefaultPropertyEditor editor(&button, { "text" });
    newui::View* seenView = nullptr;
    std::vector<std::string> seenPath;
    editor.setEditPropertyHandler([&](newui::View* view, const std::vector<std::string>& path) {
        seenView = view;
        seenPath = path;
    });

    editor.edit();

    EXPECT_EQ(seenView, &button);
    EXPECT_EQ(seenPath, std::vector<std::string>({ "text" }));
}

TEST(ComponentEditorEditTest, EditWithoutAPathNeverCallsTheHandler)
{
    newui::SubView view;
    CodeToolsVsix::ComponentEditor editor(&view);
    bool called = false;
    editor.setEditPropertyHandler([&](newui::View*, const std::vector<std::string>&) { called = true; });

    editor.edit();

    EXPECT_FALSE(called);
}
