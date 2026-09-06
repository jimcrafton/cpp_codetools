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
    std::vector<const newui::reflection::Property*> properties;
    classinfo(typeid(newui::Button))->allProperties(properties);
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
    std::vector<const newui::reflection::Property*> properties;
    classinfo(typeid(newui::Button))->allProperties(properties);

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

TEST_F(PropertyItemTest, DelegatesHeaderRowForcesSelectedFalseAndPaints)
{
    std::vector<const newui::reflection::Property*> properties;
    classinfo(typeid(newui::Button))->allProperties(properties);

    EXPECT_TRUE(paintPath({properties.size()}, /*selectedBefore=*/true));
    EXPECT_FALSE(lastPaintedSelected_);
}

TEST_F(PropertyItemTest, DelegateEntryRowPaints)
{
    std::vector<const newui::reflection::Property*> properties;
    classinfo(typeid(newui::Button))->allProperties(properties);
    std::vector<const newui::reflection::Delegate*> delegates;
    classinfo(typeid(newui::Button))->allDelegates(delegates);
    ASSERT_FALSE(delegates.empty());

    EXPECT_TRUE(paintPath({properties.size(), 0}));
}

TEST_F(PropertyItemTest, UnsupportedPropertyRowPaintsKeyAndPlaceholder)
{
    std::vector<const newui::reflection::Property*> properties;
    classinfo(typeid(newui::Button))->allProperties(properties);

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
