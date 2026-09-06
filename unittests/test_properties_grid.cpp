#include "../extension/NativeEditControls/PropertiesGrid.h"

#include <newui/controls.h>
#include <newui/reflection.h>

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

TEST_F(PropertiesGridTest, SelectingAPlainLeafRowCreatesALiveTextField)
{
    grid_->setSelection(&button_);
    std::size_t leafIndex = firstLeafIndexOfType(std::type_index(typeid(std::string)));
    ASSERT_LT(leafIndex, properties_.size()) << "expected newui::Button to have a real std::string leaf property";

    grid_->treeView()->setSelectedPath(std::vector<std::size_t>{leafIndex});

    ASSERT_EQ(grid_->treeView()->childViews().size(), 1u);
    EXPECT_NE(dynamic_cast<newui::TextField*>(grid_->treeView()->childViews()[0]), nullptr);
}

TEST_F(PropertiesGridTest, SelectingABoolLeafRowCreatesALiveToggle)
{
    grid_->setSelection(&button_);
    std::size_t leafIndex = firstLeafIndexOfType(std::type_index(typeid(bool)));
    ASSERT_LT(leafIndex, properties_.size()) << "expected newui::Button to have a real bool leaf property";

    grid_->treeView()->setSelectedPath(std::vector<std::size_t>{leafIndex});

    ASSERT_EQ(grid_->treeView()->childViews().size(), 1u);
    EXPECT_NE(dynamic_cast<newui::Toggle*>(grid_->treeView()->childViews()[0]), nullptr);
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

    grid_->treeView()->setSelectedPath(std::vector<std::size_t>{leafIndex});
    ASSERT_EQ(grid_->treeView()->childViews().size(), 1u);

    grid_->treeView()->clearSelection();
    EXPECT_TRUE(grid_->treeView()->childViews().empty());
}

TEST_F(PropertiesGridTest, CommittingTheLiveTextFieldWritesThroughTheRealProperty)
{
    grid_->setSelection(&button_);
    std::size_t leafIndex = firstLeafIndexOfType(std::type_index(typeid(std::string)));
    ASSERT_LT(leafIndex, properties_.size());

    grid_->treeView()->setSelectedPath(std::vector<std::size_t>{leafIndex});

    ASSERT_EQ(grid_->treeView()->childViews().size(), 1u);
    auto* textField = dynamic_cast<newui::TextField*>(grid_->treeView()->childViews()[0]);
    ASSERT_NE(textField, nullptr);

    textField->setText(L"a genuinely new value");
    textField->onLostFocus(*textField);

    std::any newValue = properties_[leafIndex]->get(&button_);
    EXPECT_EQ(std::any_cast<std::string>(newValue), "a genuinely new value");
}

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
    grid_->treeView()->setSelectedPath(std::vector<std::size_t>{leafIndex});

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
