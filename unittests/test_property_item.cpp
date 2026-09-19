#include "../extension/NativeEditControls/PropertyItem.h"

#include <newui/controllers.h>
#include <newui/controls.h>
#include <newui/reflection.h>

#include <blend2d/blend2d.h>

#include <gtest/gtest.h>

using newui::reflection::classinfo;
using CodeToolsVsix::PropertiesModel;
using CodeToolsVsix::PropertiesTreeController;
using CodeToolsVsix::PropertyItem;

// registerReflectionData() is already run once globally for this whole
// binary by test_component_editor.cpp's own ::testing::Environment - real
// newui::Button properties/delegates are already registered by the time
// these tests run. Same "render into a real BLImage/BLContext, check
// pixels" pattern test_selection_overlay.cpp already uses.

namespace {
    bool anyPixelPainted(const BLImage& surface, int width, int height) {
        BLImageData data;
        surface.get_data(&data);
        const uint8_t* bytes = static_cast<const uint8_t*>(data.pixel_data);
        for (int row = 0; row < height; ++row) {
            const uint8_t* rowBytes = bytes + row * data.stride;
            for (int i = 0; i < width * 4; ++i) {
                if (rowBytes[i] != 0) {
                    return true;
                }
            }
        }
        return false;
    }
}

namespace {
    // The root properties PropertiesModel lists for a Button, in row order - allProperties() minus
    // collections (childViews), which the grid hides.
    std::vector<const newui::reflection::Property*> listedProperties() {
        std::vector<const newui::reflection::Property*> all;
        classinfo(typeid(newui::Button))->allProperties(all);
        std::vector<const newui::reflection::Property*> listed;
        for (const auto* property : all) {
            if (!property->isCollection()) {
                listed.push_back(property);
            }
        }
        return listed;
    }
}

class PropertyItemTest : public ::testing::Test {
protected:
    void SetUp() override {
        CodeToolsVsix::PropertyEditorRegistry::instance().registerBuiltinEditors();
        model_.setSelection(&button_);
        controller_.setModel(&model_);

        ASSERT_EQ(surface_.create(200, 24, BL_FORMAT_PRGB32), BL_SUCCESS);
    }

    // Paints path via a fresh PropertyItem into surface_, returning true if
    // any pixel came out non-zero.
    bool paintPath(const std::vector<std::size_t>& path, bool selectedBefore = false) {
        PropertyItem item;
        item.setSelected(selectedBefore);
        BLContext ctx(surface_);
        ctx.clear_all();
        item.paint(ctx, newui::Rect(0.0f, 0.0f, 200.0f, 24.0f), path, controller_);
        ctx.end();
        lastPaintedSelected_ = item.isSelected();
        return anyPixelPainted(surface_, 200, 24);
    }

    newui::Button button_;
    PropertiesModel model_;
    newui::TreeController controller_;
    BLImage surface_;
    bool lastPaintedSelected_ = false;
};

TEST_F(PropertyItemTest, LeafPropertyRowPaintsSomething)
{
    std::vector<const newui::reflection::Property*> properties = listedProperties();
    ASSERT_FALSE(properties.empty());

    // Some root property is bound to end up PropertyLeaf (the model's own
    // classification, verified independently in test_properties_model.cpp)
    // - find one so this test doesn't depend on which index it happens to
    // be.
    std::size_t leafIndex = properties.size();
    for (std::size_t i = 0; i < properties.size(); ++i) {
        if (model_.nodeAt({i}).kind == PropertiesModel::Kind::PropertyLeaf) {
            leafIndex = i;
            break;
        }
    }
    ASSERT_LT(leafIndex, properties.size());

    EXPECT_TRUE(paintPath({leafIndex}));
}

TEST_F(PropertyItemTest, PropertyGroupRowForcesSelectedFalseAndPaintsALabel)
{
    std::vector<const newui::reflection::Property*> properties = listedProperties();

    std::size_t groupIndex = properties.size();
    for (std::size_t i = 0; i < properties.size(); ++i) {
        if (model_.nodeAt({i}).kind == PropertiesModel::Kind::PropertyGroup) {
            groupIndex = i;
            break;
        }
    }
    ASSERT_LT(groupIndex, properties.size())
        << "expected newui::Button to have at least one SubProperties-style property";

    EXPECT_TRUE(paintPath({groupIndex}, /*selectedBefore=*/true));
    EXPECT_FALSE(lastPaintedSelected_);
}

TEST_F(PropertyItemTest, PropertyGroupRowWithNoLiveInstanceAttachedStillPaints)
{
    // newui::Button (a leaf control) has no layout() attached by default -
    // "layout" still classifies as Kind::PropertyGroup (Layout is a
    // registered, addressable, polymorphic property - see
    // PropertiesModel::classifyProperty()'s own comment), just with a
    // null address(). This exercises the "(none)" type-suffix branch
    // (PropertyItem::paint()'s own comment) rather than crashing on the
    // null pointer.
    std::vector<const newui::reflection::Property*> properties = listedProperties();

    std::size_t layoutIndex = properties.size();
    for (std::size_t i = 0; i < properties.size(); ++i) {
        if (properties[i]->name() == "layout") {
            layoutIndex = i;
            break;
        }
    }
    ASSERT_LT(layoutIndex, properties.size());
    ASSERT_EQ(button_.layout(), nullptr);
    ASSERT_EQ(model_.nodeAt({layoutIndex}).kind, PropertiesModel::Kind::PropertyGroup);

    EXPECT_TRUE(paintPath({layoutIndex}));
}

// "style" is the same Layout-shaped case as "layout" above, just with a real, never-null live
// instance (View::style() always returns a live reference - see ViewStylePropertyEditor::
// valueAsString()'s own comment, PropertyEditor.cpp) - exercises PropertyItem::paint()'s new
// group-header "..." button branch (this row has a registered ViewStylePropertyEditor,
// EditStyle::Dialog) rather than crashing on the ellipsis geometry/paintEllipsisButton() call.
TEST_F(PropertyItemTest, StyleGroupHeaderWithATypeSwapEditorStillPaintsItsEllipsisButton)
{
    std::vector<const newui::reflection::Property*> properties = listedProperties();

    std::size_t styleIndex = properties.size();
    for (std::size_t i = 0; i < properties.size(); ++i) {
        if (properties[i]->name() == "style") {
            styleIndex = i;
            break;
        }
    }
    ASSERT_LT(styleIndex, properties.size());
    ASSERT_EQ(model_.nodeAt({styleIndex}).kind, PropertiesModel::Kind::PropertyGroup);

    EXPECT_TRUE(paintPath({styleIndex}));
}

TEST(PropertyItemEllipsisButtonRectFor, RightAlignedAndVerticallyCenteredWithinContentRect)
{
    newui::Rect contentRect(10.0f, 20.0f, 200.0f, 24.0f);
    newui::Rect ellipsisRect = PropertyItem::ellipsisButtonRectFor(contentRect);

    EXPECT_FLOAT_EQ(ellipsisRect.size().width, PropertyItem::kEllipsisButtonSize);
    EXPECT_FLOAT_EQ(ellipsisRect.size().height, PropertyItem::kEllipsisButtonSize);
    EXPECT_LT(ellipsisRect.right(), contentRect.right());
    EXPECT_GT(ellipsisRect.left(), contentRect.left());
    EXPECT_GE(ellipsisRect.top(), contentRect.top());
    EXPECT_LE(ellipsisRect.bottom(), contentRect.bottom());

    // Vertically centered.
    float contentCenterY = contentRect.top() + contentRect.size().height * 0.5f;
    float ellipsisCenterY = ellipsisRect.top() + ellipsisRect.size().height * 0.5f;
    EXPECT_NEAR(contentCenterY, ellipsisCenterY, 0.01f);
}

TEST_F(PropertyItemTest, DelegatesHeaderRowForcesSelectedFalseAndPaints)
{
    std::vector<const newui::reflection::Property*> properties = listedProperties();

    EXPECT_TRUE(paintPath({properties.size()}, /*selectedBefore=*/true));
    EXPECT_FALSE(lastPaintedSelected_);
}

TEST_F(PropertyItemTest, DelegateEntryRowPaints)
{
    std::vector<const newui::reflection::Property*> properties = listedProperties();
    std::vector<const newui::reflection::Delegate*> delegates;
    classinfo(typeid(newui::Button))->allDelegates(delegates);
    ASSERT_FALSE(delegates.empty());

    EXPECT_TRUE(paintPath({properties.size(), 0}));
}

TEST_F(PropertyItemTest, UnsupportedPropertyRowPaintsKeyAndPlaceholder)
{
    std::vector<const newui::reflection::Property*> properties = listedProperties();

    std::size_t unsupportedIndex = properties.size();
    for (std::size_t i = 0; i < properties.size(); ++i) {
        if (model_.nodeAt({i}).kind == PropertiesModel::Kind::PropertyUnsupported) {
            unsupportedIndex = i;
            break;
        }
    }
    if (unsupportedIndex == properties.size()) {
        GTEST_SKIP() << "newui::Button has no PropertyUnsupported property to exercise right now";
    }

    EXPECT_TRUE(paintPath({unsupportedIndex}));
}

TEST_F(PropertyItemTest, FallsBackToBaseTreeItemPaintWhenModelIsNotAPropertiesModel)
{
    newui::TreeController plainController;
    PropertyItem item;

    BLContext ctx(surface_);
    ctx.clear_all();
    EXPECT_NO_THROW(item.paint(ctx, newui::Rect(0.0f, 0.0f, 200.0f, 24.0f), {0}, plainController));
    ctx.end();
}

// PropertiesTreeController - the one piece of real, shared, mutable state
// the resizable key/value divider needs (bluesky/property-grid-design.md).

TEST(PropertiesTreeControllerTest, DefaultsToTheDocumentedFraction)
{
    PropertiesTreeController controller;
    EXPECT_FLOAT_EQ(controller.keyColumnFraction(), PropertiesTreeController::kDefaultKeyColumnFraction);
}

TEST(PropertiesTreeControllerTest, SetKeyColumnFractionClampsToTheDocumentedRange)
{
    PropertiesTreeController controller;

    controller.setKeyColumnFraction(0.01f);
    EXPECT_FLOAT_EQ(controller.keyColumnFraction(), PropertiesTreeController::kMinKeyColumnFraction);

    controller.setKeyColumnFraction(0.99f);
    EXPECT_FLOAT_EQ(controller.keyColumnFraction(), PropertiesTreeController::kMaxKeyColumnFraction);

    controller.setKeyColumnFraction(0.5f);
    EXPECT_FLOAT_EQ(controller.keyColumnFraction(), 0.5f);
}

TEST(PropertiesTreeControllerTest, SetKeyColumnFractionFiresOnDataChangedOnlyWhenItActuallyChanges)
{
    PropertiesTreeController controller;
    int changedCount = 0;
    controller.onDataChanged.add([&changedCount](newui::TreeController&) {
        ++changedCount;
        return newui::SyncReturn::Handled;
    });

    controller.setKeyColumnFraction(PropertiesTreeController::kDefaultKeyColumnFraction);
    EXPECT_EQ(changedCount, 0) << "setting it to what it already is should not fire anything";

    controller.setKeyColumnFraction(0.6f);
    EXPECT_EQ(changedCount, 1);
}

TEST_F(PropertyItemTest, ParentPickerRowPaintsTheKeyAndTheCurrentParentsName)
{
    newui::SubView container;
    container.setName("container");
    container.addChild(&button_);
    model_.setSelection(&button_);

    ASSERT_EQ(model_.nodeAt({0}).kind, PropertiesModel::Kind::ParentPicker);
    EXPECT_TRUE(paintPath({0}));

    container.removeChild(&button_);
}

TEST_F(PropertyItemTest, ParentPickerRowStillPaintsWithNoParentButtonIsNeverGivenOne)
{
    // button_ (the fixture's own member) never gets addChild()'d anywhere in
    // this test, so showsParentPicker() correctly keeps the row out of the
    // tree entirely (see test_properties_model.cpp's own
    // ParentPickerIsAbsentWhenSelectedHasNoParent) - nothing here paints a
    // ParentPicker row at all, which is itself the behavior worth asserting:
    // path {0} must resolve to a real property, never Invalid/ParentPicker.
    ASSERT_EQ(button_.parent(), nullptr);
    EXPECT_NE(model_.nodeAt({0}).kind, PropertiesModel::Kind::ParentPicker);
    EXPECT_NE(model_.nodeAt({0}).kind, PropertiesModel::Kind::Invalid);
}

TEST(PropertyItemGroupTypeName, NamesTheClassOrElseTheEnumNeverAQuestionMark)
{
    CodeToolsVsix::PropertyEditorRegistry::instance().registerBuiltinEditors();

    newui::Button button;
    button.setLayoutParams(std::make_unique<newui::AnchorLayoutParams>());
    CodeToolsVsix::PropertiesModel model;
    model.setSelection(&button);

    bool sawLayoutParams = false;
    for (std::size_t i = 0; i < model.childCount({}); ++i) {
        CodeToolsVsix::PropertiesModel::Node node = model.nodeAt({i});
        if (node.property == nullptr || node.property->name() != "layoutParams") {
            continue;
        }
        sawLayoutParams = true;
        EXPECT_EQ(CodeToolsVsix::PropertyItem::groupTypeNameFor(node), "AnchorLayoutParams");
        for (std::size_t c = 0; c < model.childCount({i}); ++c) {
            CodeToolsVsix::PropertiesModel::Node child = model.nodeAt({i, c});
            if (child.property != nullptr && child.property->name() == "anchors") {
                EXPECT_EQ(child.kind, CodeToolsVsix::PropertiesModel::Kind::PropertySubGroup);
                EXPECT_EQ(CodeToolsVsix::PropertyItem::groupTypeNameFor(child), "Anchor");
            }
        }
    }
    EXPECT_TRUE(sawLayoutParams);
}
