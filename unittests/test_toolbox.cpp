#include "../extension/NativeEditControls/Toolbox.h"

#include <newui/layout.h>

#include <gtest/gtest.h>

namespace {
BLContext& SharedToolboxPaintContext() {
    static BLImage image(200, 400, BL_FORMAT_PRGB32);
    static BLContext ctx(image);
    return ctx;
}
}

// registerReflectionData() is already run once globally for this whole
// binary by test_component_editor.cpp's own ::testing::Environment - no
// separate registration needed here (same convention test_workspace.cpp's
// own comment documents).

TEST(ToolboxModel, RootChildCountIsTheCategoryCount) {
    CodeToolsVsix::ToolboxModel model;
    EXPECT_EQ(model.childCount({}), CodeToolsVsix::ToolboxRegistry::categories().size());
}

TEST(ToolboxModel, CategoryChildCountIsItsEntryCount) {
    CodeToolsVsix::ToolboxModel model;
    const auto& containers = CodeToolsVsix::ToolboxRegistry::categories()[0];
    EXPECT_EQ(model.childCount({0}), containers.entries.size());
}

TEST(ToolboxModel, EntryPathsHaveNoChildren) {
    CodeToolsVsix::ToolboxModel model;
    EXPECT_EQ(model.childCount({0, 0}), 0u);
}

TEST(ToolboxModel, ValueAtACategoryPathIsItsDisplayName) {
    CodeToolsVsix::ToolboxModel model;
    std::any value = model.value(std::vector<std::size_t>{0});
    EXPECT_EQ(std::any_cast<std::string>(value), "Containers");
}

TEST(ToolboxModel, ValueAtAnEntryPathIsItsDisplayName) {
    CodeToolsVsix::ToolboxModel model;
    std::any value = model.value(std::vector<std::size_t>{0, 0});
    EXPECT_EQ(std::any_cast<std::string>(value), "FlexLayout (Vertical)");
}

TEST(ToolboxController, IconForAnEntryPathMatchesTheRegistrysOwnIconResourceName) {
    CodeToolsVsix::ToolboxController controller;
    const auto& vertical = CodeToolsVsix::ToolboxRegistry::categories()[0].entries[0];  // "FlexLayout (Vertical)"

    auto icon = controller.iconFor(std::vector<std::size_t>{0, 0});
    ASSERT_TRUE(icon.has_value());
    EXPECT_EQ(*icon, vertical.iconResourceName);
}

TEST(ToolboxController, IconForACategoryHeaderPathIsNullopt) {
    CodeToolsVsix::ToolboxController controller;
    EXPECT_FALSE(controller.iconFor(std::vector<std::size_t>{0}).has_value());
}

TEST(ToolboxController, IconForAnOutOfRangePathIsNullopt) {
    CodeToolsVsix::ToolboxController controller;
    EXPECT_FALSE(controller.iconFor(std::vector<std::size_t>{999, 0}).has_value());
}

TEST(ToolboxItem, PaintACategoryHeaderRowDoesNotCrash) {
    CodeToolsVsix::ToolboxController controller;
    auto* item = static_cast<CodeToolsVsix::ToolboxItem*>(controller.createItem({0}));
    ASSERT_NE(item, nullptr);

    item->paint(SharedToolboxPaintContext(), newui::Rect(0.0f, 0.0f, 180.0f, 24.0f),
        std::vector<std::size_t>{0}, controller);

    controller.releaseItem(item);
}

TEST(ToolboxItem, PaintARealIconBearingEntryRowDoesNotCrash) {
    CodeToolsVsix::ToolboxController controller;
    auto modelOwner = std::make_unique<CodeToolsVsix::ToolboxModel>();
    CodeToolsVsix::ToolboxModel& model = *modelOwner;
    controller.setModel(std::move(modelOwner));
    auto* item = static_cast<CodeToolsVsix::ToolboxItem*>(controller.createItem({1, 0}));
    ASSERT_NE(item, nullptr);

    // Path {1, 0} - Basic's first entry, "Button" (ToolboxRegistry's own
    // fixed order) - a real icon-bearing entry, exercising the actual
    // controller.iconFor()/paintItemIcon() path this test file otherwise
    // never touched (Toolbox's other tests never call ToolboxItem::paint()
    // at all).
    item->paint(SharedToolboxPaintContext(), newui::Rect(0.0f, 0.0f, 180.0f, 22.0f),
        std::vector<std::size_t>{1, 0}, controller);

    controller.releaseItem(item);
}

TEST(Toolbox, EveryCategoryStartsExpanded) {
    auto* toolbox = new CodeToolsVsix::Toolbox();

    for (std::size_t i = 0; i < CodeToolsVsix::ToolboxRegistry::categories().size(); ++i) {
        EXPECT_TRUE(toolbox->treeView()->controller().isExpanded({i}));
    }

    delete toolbox;
}

TEST(Toolbox, DoubleClickingASelectedEntryActivatesItsRealFactory) {
    auto* toolbox = new CodeToolsVsix::Toolbox();

    newui::SubView* activated = nullptr;
    toolbox->onEntryActivated.add([&activated](CodeToolsVsix::Toolbox&, newui::SubView* created) {
        activated = created;
        return newui::SyncReturn::Handled;
    });

    // Path {0, 0} - Containers' first entry, "FlexLayout (Vertical)"
    // (ToolboxRegistry's own fixed order) - set directly via TreeView's
    // own public setSelectedPath() rather than guessing pixel coordinates
    // for a real mouse click.
    toolbox->treeView()->setSelectedPath(std::vector<std::size_t>{0, 0});
    toolbox->treeView()->onMouseDblClick(*toolbox->treeView(), newui::Point(0.0f, 0.0f), 1, 0);

    ASSERT_NE(activated, nullptr);
    auto* flexLayout = dynamic_cast<newui::FlexLayout*>(activated->layout());
    ASSERT_NE(flexLayout, nullptr);
    EXPECT_EQ(flexLayout->orientation(), newui::Orientation::Vertical);

    delete activated;  // never attached anywhere in this test - caller's responsibility, see class comment
    delete toolbox;
}

TEST(Toolbox, DoubleClickingWithNoSelectionDoesNothing) {
    auto* toolbox = new CodeToolsVsix::Toolbox();

    newui::SubView* activated = nullptr;
    toolbox->onEntryActivated.add([&activated](CodeToolsVsix::Toolbox&, newui::SubView* created) {
        activated = created;
        return newui::SyncReturn::Handled;
    });

    toolbox->treeView()->onMouseDblClick(*toolbox->treeView(), newui::Point(0.0f, 0.0f), 1, 0);

    EXPECT_EQ(activated, nullptr);
    delete toolbox;
}

TEST(Toolbox, DoubleClickingASelectedCategoryHeaderDoesNothing) {
    auto* toolbox = new CodeToolsVsix::Toolbox();

    newui::SubView* activated = nullptr;
    toolbox->onEntryActivated.add([&activated](CodeToolsVsix::Toolbox&, newui::SubView* created) {
        activated = created;
        return newui::SyncReturn::Handled;
    });

    toolbox->treeView()->setSelectedPath(std::vector<std::size_t>{0});  // "Containers" itself, not a leaf entry
    toolbox->treeView()->onMouseDblClick(*toolbox->treeView(), newui::Point(0.0f, 0.0f), 1, 0);

    EXPECT_EQ(activated, nullptr);
    delete toolbox;
}

// ---- Drag source / drop payload ----

namespace
{
    // The (category, entry) indices of the first registry entry named displayName.
    bool findEntry(const std::string& displayName, std::size_t& categoryIndex, std::size_t& entryIndex)
    {
        const auto& categories = CodeToolsVsix::ToolboxRegistry::categories();
        for (categoryIndex = 0; categoryIndex < categories.size(); ++categoryIndex) {
            for (entryIndex = 0; entryIndex < categories[categoryIndex].entries.size(); ++entryIndex) {
                if (categories[categoryIndex].entries[entryIndex].displayName == displayName) {
                    return true;
                }
            }
        }
        return false;
    }
}

TEST(ToolboxDragPayload, EveryRegistryEntryRoundTripsToWhateverItsOwnFactoryProduces)
{
    // Some entries' factories can legitimately yield nothing (double-click ignores those too), so
    // the payload path must agree with the factory entry by entry rather than always succeed.
    const auto& categories = CodeToolsVsix::ToolboxRegistry::categories();
    std::size_t roundTripped = 0;
    for (std::size_t c = 0; c < categories.size(); ++c) {
        for (std::size_t e = 0; e < categories[c].entries.size(); ++e) {
            newui::SubView* direct = categories[c].entries[e].factory();
            newui::SubView* viaPayload = CodeToolsVsix::Toolbox::createFromDragPayload(
                CodeToolsVsix::Toolbox::dragPayloadFor(c, e));
            EXPECT_EQ(direct != nullptr, viaPayload != nullptr) << categories[c].entries[e].displayName;
            if (viaPayload != nullptr) {
                ++roundTripped;
                EXPECT_EQ(typeid(*direct), typeid(*viaPayload)) << categories[c].entries[e].displayName;
            }
            for (newui::SubView* view : { direct, viaPayload }) {
                if (view != nullptr) {
                    view->destroy();
                    delete view;
                }
            }
        }
    }
    EXPECT_GT(roundTripped, 0u);
}

// Real bug: TabControl's only constructor is `explicit TabControl(TabAlignment = Top)`, and
// reflectgen registered just the one-argument form, so its factory (createInstance() with no
// arguments) produced nothing - a Toolbox double-click or drag of it silently did nothing. Every
// listed entry has to actually produce a control.
TEST(ToolboxRegistryFactories, EveryEntryProducesAControl)
{
    const auto& categories = CodeToolsVsix::ToolboxRegistry::categories();
    for (const auto& category : categories) {
        for (const auto& entry : category.entries) {
            newui::SubView* created = entry.factory();
            EXPECT_NE(created, nullptr) << category.displayName << " / " << entry.displayName;
            if (created != nullptr) {
                created->destroy();
                delete created;
            }
        }
    }
}

TEST(ToolboxDragPayload, RejectsForeignTextAndOutOfRangeOrMalformedPayloads)
{
    using CodeToolsVsix::Toolbox;
    EXPECT_EQ(Toolbox::createFromDragPayload(L"just some dragged text"), nullptr);
    EXPECT_EQ(Toolbox::createFromDragPayload(L""), nullptr);
    EXPECT_EQ(Toolbox::createFromDragPayload(L"codetools-toolbox-entry:"), nullptr);
    EXPECT_EQ(Toolbox::createFromDragPayload(L"codetools-toolbox-entry:0"), nullptr);
    EXPECT_EQ(Toolbox::createFromDragPayload(L"codetools-toolbox-entry:x:y"), nullptr);
    EXPECT_EQ(Toolbox::createFromDragPayload(L"codetools-toolbox-entry:999:0"), nullptr);
    EXPECT_EQ(Toolbox::createFromDragPayload(L"codetools-toolbox-entry:0:999"), nullptr);
}

TEST(ToolboxDragSource, ProvidesThePayloadOfTheSelectedEntryButNotForACategoryHeaderOrNothing)
{
    CodeToolsVsix::Toolbox toolbox;
    newui::DropSource* source = toolbox.treeView()->dragSource();
    ASSERT_NE(source, nullptr);
    ASSERT_FALSE(source->onProvideText.empty());

    std::wstring text;
    EXPECT_FALSE(source->onProvideText.syncCallFirst(*source, text).handled());  // nothing selected

    toolbox.treeView()->setSelectedPath(std::vector<std::size_t>{0});  // a category header
    EXPECT_FALSE(source->onProvideText.syncCallFirst(*source, text).handled());

    std::size_t c = 0, e = 0;
    ASSERT_TRUE(findEntry("Button", c, e));
    toolbox.treeView()->setSelectedPath(std::vector<std::size_t>{c, e});
    EXPECT_TRUE(source->onProvideText.syncCallFirst(*source, text).handled());
    EXPECT_EQ(text, CodeToolsVsix::Toolbox::dragPayloadFor(c, e));
}
