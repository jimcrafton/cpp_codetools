#include "../extension/NativeEditControls/PropertiesGrid.h"

#include <newui/controls.h>
#include <newui/reflection.h>
#include <newui/rootview.h>

#include <gtest/gtest.h>

#include <typeindex>

using newui::reflection::classinfo;
using newui::reflection::Property;
using CodeToolsVsix::PropertiesGrid;
using CodeToolsVsix::PropertiesModel;
using CodeToolsVsix::PropertiesTreeController;

// registerReflectionData() is already run once globally for this whole
// binary by test_component_editor.cpp's own ::testing::Environment - real
// newui::Button properties/delegates are already registered by the time
// these tests run.

class PropertiesGridTest : public ::testing::Test {
protected:
    void SetUp() override {
        CodeToolsVsix::PropertyEditorRegistry::instance().registerBuiltinEditors();
        classinfo(typeid(newui::Button))->allProperties(properties_);
        ASSERT_FALSE(properties_.empty());

        grid_ = new PropertiesGrid();
        // Real bounds so ScrollView::updateLayout() actually sizes
        // treeView() to a non-degenerate viewport - TreeView::
        // rectForPath()/getClientBounds() are otherwise degenerate (see
        // TreeView::rectForPath()'s own doc comment, newui/controls.h).
        grid_->setBounds(newui::Rect(0.0f, 0.0f, 320.0f, 400.0f));
    }

    void TearDown() override {
        delete grid_;
    }

    // First root property classifying as kind, or properties_.size() if
    // none does.
    std::size_t firstIndexOfKind(PropertiesModel::Kind kind) const {
        for (std::size_t i = 0; i < properties_.size(); ++i) {
            if (grid_->model().nodeAt({i}).kind == kind) {
                return i;
            }
        }
        return properties_.size();
    }

    std::size_t firstLeafIndexOfType(const std::type_index& type) const {
        for (std::size_t i = 0; i < properties_.size(); ++i) {
            if (grid_->model().nodeAt({i}).kind == PropertiesModel::Kind::PropertyLeaf
                && properties_[i]->type() == type) {
                return i;
            }
        }
        return properties_.size();
    }

    // Selecting a row no longer activates its live editor by itself - only
    // a real click landing in the *value* column does (see
    // PropertiesGrid::activateLiveEditorIfClickedOnValueColumn()'s own
    // comment) - this drives exactly that click, for tests that need a
    // real live editor to exist. setSelectedPath() alone (still used
    // directly by tests specifically proving selection-without-a-click
    // does *not* create one) never does.
    void selectAndClickValueColumn(const std::vector<std::size_t>& path) {
        grid_->treeView()->setSelectedPath(path);
        std::optional<newui::Rect> rowRect = grid_->treeView()->rectForPath(path);
        ASSERT_TRUE(rowRect.has_value());

        auto* propsController = dynamic_cast<PropertiesTreeController*>(&grid_->treeView()->controller());
        float keyColumnFraction = propsController != nullptr
            ? propsController->keyColumnFraction() : PropertiesTreeController::kDefaultKeyColumnFraction;
        newui::Rect valueRect = CodeToolsVsix::PropertyItem::valueRectFor(*rowRect, path, keyColumnFraction);
        newui::Point clickPt(valueRect.left() + 2.0f, valueRect.top() + 2.0f);
        grid_->treeView()->onMouseDown(*grid_->treeView(), clickPt, 0, 0);
    }

    std::vector<const Property*> properties_;
    newui::Button button_;
    PropertiesGrid* grid_ = nullptr;
};

TEST_F(PropertiesGridTest, SetSelectionPopulatesTheTreeViewsModel)
{
    grid_->setSelection(&button_);
    EXPECT_EQ(grid_->selected(), &button_);
    EXPECT_GT(grid_->treeView()->controller().visibleCount(), 0u);
}

TEST_F(PropertiesGridTest, ClearingSelectionEmptiesTheTree)
{
    grid_->setSelection(&button_);
    grid_->setSelection(nullptr);
    EXPECT_EQ(grid_->selected(), nullptr);
    EXPECT_EQ(grid_->treeView()->controller().visibleCount(), 0u);
}

TEST_F(PropertiesGridTest, ReselectingClearsAnyPreviouslySelectedPath)
{
    grid_->setSelection(&button_);
    std::size_t leafIndex = firstLeafIndexOfType(std::type_index(typeid(std::string)));
    ASSERT_LT(leafIndex, properties_.size());
    grid_->treeView()->setSelectedPath(std::vector<std::size_t>{leafIndex});
    ASSERT_TRUE(grid_->treeView()->selectedPath().has_value());

    newui::Button other;
    grid_->setSelection(&other);

    EXPECT_FALSE(grid_->treeView()->selectedPath().has_value());
    EXPECT_TRUE(grid_->treeView()->childViews().empty()) << "no live editor should survive re-selection";
}

TEST_F(PropertiesGridTest, SelectingALeafRowAloneDoesNotCreateALiveEditor)
{
    // The real behavior this whole class exists to get right: clicking a
    // row (or programmatically selecting it, same as a keyboard-navigated
    // selection would) must not, by itself, spawn an editable widget -
    // only a click that actually lands on the value column does (see
    // ClickingTheValueColumnCreatesALiveTextField/ALiveToggle below).
    grid_->setSelection(&button_);
    std::size_t leafIndex = firstLeafIndexOfType(std::type_index(typeid(std::string)));
    ASSERT_LT(leafIndex, properties_.size()) << "expected newui::Button to have a real std::string leaf property";

    grid_->treeView()->setSelectedPath(std::vector<std::size_t>{leafIndex});

    EXPECT_TRUE(grid_->treeView()->childViews().empty());
}

TEST_F(PropertiesGridTest, ClickingTheValueColumnCreatesALiveTextField)
{
    grid_->setSelection(&button_);
    std::size_t leafIndex = firstLeafIndexOfType(std::type_index(typeid(std::string)));
    ASSERT_LT(leafIndex, properties_.size()) << "expected newui::Button to have a real std::string leaf property";

    selectAndClickValueColumn(std::vector<std::size_t>{leafIndex});

    ASSERT_EQ(grid_->treeView()->childViews().size(), 1u);
    EXPECT_NE(dynamic_cast<newui::TextField*>(grid_->treeView()->childViews()[0]), nullptr);
}

TEST_F(PropertiesGridTest, ClickingTheValueColumnCreatesALiveToggle)
{
    grid_->setSelection(&button_);
    std::size_t leafIndex = firstLeafIndexOfType(std::type_index(typeid(bool)));
    ASSERT_LT(leafIndex, properties_.size()) << "expected newui::Button to have a real bool leaf property";

    selectAndClickValueColumn(std::vector<std::size_t>{leafIndex});

    ASSERT_EQ(grid_->treeView()->childViews().size(), 1u);
    EXPECT_NE(dynamic_cast<newui::Toggle*>(grid_->treeView()->childViews()[0]), nullptr);
}

TEST_F(PropertiesGridTest, ClickingTheValueColumnGivesTheLiveEditorRealKeyboardFocus)
{
    // Real, reported bug: the live editor is created *during* the very
    // RootView::mouseDown() dispatch that activated it - which runs
    // *after* RootView::mouseDown() already hit-tested and called
    // setFocusedSubView() for this same click (rootview.cpp), so it never
    // received real focus on its own, and every subsequent keystroke
    // (Escape, Enter, ordinary typing) kept routing to whatever the
    // original hit-test found instead (treeView_ itself) - invisible to
    // every other test in this file, which fires onKeyDown/onReturnPressed
    // directly on the widget, bypassing real focus routing entirely (and
    // to grid_'s own, unattached-to-any-RootView construction, where
    // treeView_->rootView() is simply null and focusLiveEditorView() is a
    // no-op). Needs a real, attached RootView specifically to catch this.
    newui::RootView root(nullptr, newui::Rect(0.0f, 0.0f, 320.0f, 400.0f), "root");
    root.addChild(grid_);

    grid_->setSelection(&button_);
    std::size_t leafIndex = firstLeafIndexOfType(std::type_index(typeid(std::string)));
    ASSERT_LT(leafIndex, properties_.size());

    selectAndClickValueColumn(std::vector<std::size_t>{leafIndex});

    ASSERT_EQ(grid_->treeView()->childViews().size(), 1u);
    EXPECT_EQ(root.focusedSubView(), grid_->treeView()->childViews()[0]);

    root.removeChild(grid_);  // detach before TearDown()'s own delete grid_
}

TEST_F(PropertiesGridTest, SelectingAGroupOrDelegatesHeaderRowCreatesNoLiveEditor)
{
    grid_->setSelection(&button_);

    std::size_t groupIndex = firstIndexOfKind(PropertiesModel::Kind::PropertyGroup);
    ASSERT_LT(groupIndex, properties_.size()) << "expected newui::Button to have a SubProperties-style property";
    grid_->treeView()->setSelectedPath(std::vector<std::size_t>{groupIndex});
    EXPECT_TRUE(grid_->treeView()->childViews().empty());

    grid_->treeView()->setSelectedPath(std::vector<std::size_t>{properties_.size()});  // DelegatesHeader
    EXPECT_TRUE(grid_->treeView()->childViews().empty());
}

TEST_F(PropertiesGridTest, DeselectingDestroysTheLiveEditor)
{
    grid_->setSelection(&button_);
    std::size_t leafIndex = firstLeafIndexOfType(std::type_index(typeid(std::string)));
    ASSERT_LT(leafIndex, properties_.size());

    selectAndClickValueColumn(std::vector<std::size_t>{leafIndex});
    ASSERT_EQ(grid_->treeView()->childViews().size(), 1u);

    grid_->treeView()->clearSelection();
    EXPECT_TRUE(grid_->treeView()->childViews().empty());
}

// Commit-on-blur/Enter and discard-on-Escape (PropertyEditor::
// handleLiveTextCommit()/handleLiveTextReturnPressed()/
// handleLiveEditorKeyDown()) deliberately have no unit tests here -
// every attempt synthesized the event by calling textField->onLostFocus()/
// onReturnPressed()/onKeyDown() directly, bypassing the real Win32
// message pump and RootView's own focus routing entirely. Those tests
// stayed green through two real, live-debugged bugs this exact behavior
// had (RootView::lostFocus() clearing focus on a window-level
// WM_KILLFOCUS wholly unrelated to this app, and destroyLiveEditor()
// never calling markDirty() so the removal never actually repainted) -
// proving they verify nothing but the handler's own body, not that a
// real keystroke ever reaches it. A real test would need an actual
// RunLoop and synthesized window messages (SendMessage/SendInput against
// the real HWND) - out of scope for now; verify this by hand via
// testharness.exe instead.

// Resizable key/value divider (bluesky/property-grid-design.md).

TEST_F(PropertiesGridTest, ClickingAwayFromTheDividerDoesNotChangeTheKeyColumnFraction)
{
    grid_->setSelection(&button_);
    newui::Rect clientBounds = grid_->treeView()->getClientBounds();
    float startFraction = PropertiesTreeController::kDefaultKeyColumnFraction;

    // Well clear of the divider (kDividerHitSlop is only a few px either
    // side of clientBounds.left() + width*fraction).
    float awayX = clientBounds.left() + 5.0f;
    float y = clientBounds.top() + 5.0f;
    grid_->treeView()->onMouseDown(*grid_->treeView(), newui::Point(awayX, y), 0, 0);
    grid_->treeView()->onMouseMove(*grid_->treeView(), newui::Point(awayX + 20.0f, y), 0, 0);
    grid_->treeView()->onMouseUp(*grid_->treeView(), newui::Point(awayX + 20.0f, y), 0, 0);

    auto* propsController = dynamic_cast<PropertiesTreeController*>(&grid_->treeView()->controller());
    ASSERT_NE(propsController, nullptr);
    EXPECT_FLOAT_EQ(propsController->keyColumnFraction(), startFraction);
}

TEST_F(PropertiesGridTest, DraggingTheDividerChangesTheSharedKeyColumnFraction)
{
    grid_->setSelection(&button_);
    newui::Rect clientBounds = grid_->treeView()->getClientBounds();
    float y = clientBounds.top() + 5.0f;
    float dividerX = clientBounds.left() + clientBounds.size().width * PropertiesTreeController::kDefaultKeyColumnFraction;

    grid_->treeView()->onMouseDown(*grid_->treeView(), newui::Point(dividerX, y), 0, 0);
    float draggedToX = clientBounds.left() + clientBounds.size().width * 0.6f;
    grid_->treeView()->onMouseMove(*grid_->treeView(), newui::Point(draggedToX, y), 0, 0);
    grid_->treeView()->onMouseUp(*grid_->treeView(), newui::Point(draggedToX, y), 0, 0);

    auto* propsController = dynamic_cast<PropertiesTreeController*>(&grid_->treeView()->controller());
    ASSERT_NE(propsController, nullptr);
    EXPECT_NEAR(propsController->keyColumnFraction(), 0.6f, 0.01f);
}

TEST_F(PropertiesGridTest, DraggingTheDividerRepositionsTheLiveEditorWithoutLosingTypedText)
{
    grid_->setSelection(&button_);
    std::size_t leafIndex = firstLeafIndexOfType(std::type_index(typeid(std::string)));
    ASSERT_LT(leafIndex, properties_.size());
    selectAndClickValueColumn(std::vector<std::size_t>{leafIndex});

    ASSERT_EQ(grid_->treeView()->childViews().size(), 1u);
    auto* textField = dynamic_cast<newui::TextField*>(grid_->treeView()->childViews()[0]);
    ASSERT_NE(textField, nullptr);

    textField->setText(L"uncommitted typed text");
    float leftBefore = textField->bounds().left();

    newui::Rect clientBounds = grid_->treeView()->getClientBounds();
    float y = clientBounds.top() + 5.0f;
    float dividerX = clientBounds.left() + clientBounds.size().width * PropertiesTreeController::kDefaultKeyColumnFraction;
    grid_->treeView()->onMouseDown(*grid_->treeView(), newui::Point(dividerX, y), 0, 0);
    float draggedToX = clientBounds.left() + clientBounds.size().width * 0.65f;
    grid_->treeView()->onMouseMove(*grid_->treeView(), newui::Point(draggedToX, y), 0, 0);
    grid_->treeView()->onMouseUp(*grid_->treeView(), newui::Point(draggedToX, y), 0, 0);

    ASSERT_EQ(grid_->treeView()->childViews().size(), 1u) << "the drag should reposition, not destroy/recreate, the live editor";
    EXPECT_EQ(textField, dynamic_cast<newui::TextField*>(grid_->treeView()->childViews()[0]))
        << "same widget instance - a rebuild would have replaced it";
    EXPECT_EQ(textField->text(), L"uncommitted typed text") << "typed-but-uncommitted text must survive a divider drag";
    EXPECT_NE(textField->bounds().left(), leftBefore) << "the widget should have actually moved with the new column split";
}
