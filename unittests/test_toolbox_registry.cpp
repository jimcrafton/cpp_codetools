#include "../extension/NativeEditControls/ToolboxRegistry.h"

#include <newui/bundle.h>
#include <newui/controls.h>
#include <newui/layout.h>
#include <newui/subview.h>

#include <gtest/gtest.h>

#include <memory>

// registerReflectionData() is already run once globally for this whole
// binary by test_component_editor.cpp's own ::testing::Environment - no
// separate registration needed here (same convention test_workspace.cpp's
// own comment documents).

TEST(ToolboxRegistry, CategoriesAreInMainDcHtmlsOwnDisplayOrder) {
    const auto& categories = CodeToolsVsix::ToolboxRegistry::categories();
    ASSERT_EQ(categories.size(), 5u);
    EXPECT_EQ(categories[0].displayName, "Containers");
    EXPECT_EQ(categories[1].displayName, "Basic");
    EXPECT_EQ(categories[2].displayName, "Text & Input");
    EXPECT_EQ(categories[3].displayName, "Data");
    EXPECT_EQ(categories[4].displayName, "Menu & Toolbar");
}

namespace {
    bool hasEntry(const CodeToolsVsix::ToolboxCategory& category, const std::string& name) {
        for (const auto& entry : category.entries) {
            if (entry.displayName == name) {
                return true;
            }
        }
        return false;
    }

    const CodeToolsVsix::ToolboxEntry* findEntry(const CodeToolsVsix::ToolboxCategory& category, const std::string& name) {
        for (const auto& entry : category.entries) {
            if (entry.displayName == name) {
                return &entry;
            }
        }
        return nullptr;
    }
}

TEST(ToolboxRegistry, ContainersIncludesTheTwoFlexLayoutEntriesPlusRealClasses) {
    const auto& categories = CodeToolsVsix::ToolboxRegistry::categories();
    const auto& containers = categories[0];

    EXPECT_TRUE(hasEntry(containers, "FlexLayout (Vertical)"));
    EXPECT_TRUE(hasEntry(containers, "FlexLayout (Horizontal)"));
    EXPECT_TRUE(hasEntry(containers, "SubView"));
    EXPECT_TRUE(hasEntry(containers, "ScrollView"));
    EXPECT_TRUE(hasEntry(containers, "TabControl"));
}

TEST(ToolboxRegistry, BasicIncludesTheRealTaggedClasses) {
    const auto& categories = CodeToolsVsix::ToolboxRegistry::categories();
    const auto& basic = categories[1];

    EXPECT_TRUE(hasEntry(basic, "Button"));
    EXPECT_TRUE(hasEntry(basic, "Label"));
    EXPECT_TRUE(hasEntry(basic, "Toggle"));
    EXPECT_TRUE(hasEntry(basic, "Image"));
}

TEST(ToolboxRegistry, FlexLayoutVerticalFactoryBuildsARealAttachableSubViewWithThatOrientation) {
    const auto& categories = CodeToolsVsix::ToolboxRegistry::categories();
    const auto& containers = categories[0];

    const CodeToolsVsix::ToolboxEntry* flexVertical = nullptr;
    for (const auto& entry : containers.entries) {
        if (entry.displayName == "FlexLayout (Vertical)") {
            flexVertical = &entry;
        }
    }
    ASSERT_NE(flexVertical, nullptr);

    newui::SubView* created = flexVertical->factory();
    ASSERT_NE(created, nullptr);
    auto* flexLayout = dynamic_cast<newui::FlexLayout*>(created->layout());
    ASSERT_NE(flexLayout, nullptr);
    EXPECT_EQ(flexLayout->orientation(), newui::Orientation::Vertical);
    delete created;
}

TEST(ToolboxRegistry, ButtonFactoryBuildsARealButtonInstance) {
    const auto& categories = CodeToolsVsix::ToolboxRegistry::categories();
    const auto& basic = categories[1];

    const CodeToolsVsix::ToolboxEntry* buttonEntry = nullptr;
    for (const auto& entry : basic.entries) {
        if (entry.displayName == "Button") {
            buttonEntry = &entry;
        }
    }
    ASSERT_NE(buttonEntry, nullptr);

    newui::SubView* created = buttonEntry->factory();
    ASSERT_NE(created, nullptr);
    EXPECT_NE(dynamic_cast<newui::Button*>(created), nullptr);
    delete created;
}

// Real files under extension/NativeEditControls/Resources/Images/, not
// just a non-empty string - a typo'd/renamed filename in
// ToolboxRegistry.cpp's own toolboxIconFor() table would otherwise pass
// silently (Bundle::resourcePath() just returns "" for a missing file,
// no throw) and only be noticed by actually looking at the running app.
TEST(ToolboxRegistry, RealTaggedClassesHaveAResolvableIcon) {
    const auto& categories = CodeToolsVsix::ToolboxRegistry::categories();
    const newui::Bundle& bundle = newui::Bundle::instance();

    const CodeToolsVsix::ToolboxEntry* button = findEntry(categories[1], "Button");
    ASSERT_NE(button, nullptr);
    EXPECT_FALSE(button->iconResourceName.empty());
    EXPECT_FALSE(bundle.resourcePath(button->iconResourceName).empty())
        << "iconResourceName '" << button->iconResourceName << "' doesn't resolve to a real file";

    const CodeToolsVsix::ToolboxEntry* subView = findEntry(categories[0], "SubView");
    ASSERT_NE(subView, nullptr);
    EXPECT_FALSE(subView->iconResourceName.empty());
    EXPECT_FALSE(bundle.resourcePath(subView->iconResourceName).empty())
        << "iconResourceName '" << subView->iconResourceName << "' doesn't resolve to a real file";
}

TEST(ToolboxRegistry, FlexLayoutEntriesEachHaveTheirOwnDistinctResolvableIcon) {
    const auto& categories = CodeToolsVsix::ToolboxRegistry::categories();
    const newui::Bundle& bundle = newui::Bundle::instance();

    const CodeToolsVsix::ToolboxEntry* vertical = findEntry(categories[0], "FlexLayout (Vertical)");
    const CodeToolsVsix::ToolboxEntry* horizontal = findEntry(categories[0], "FlexLayout (Horizontal)");
    ASSERT_NE(vertical, nullptr);
    ASSERT_NE(horizontal, nullptr);

    EXPECT_FALSE(bundle.resourcePath(vertical->iconResourceName).empty());
    EXPECT_FALSE(bundle.resourcePath(horizontal->iconResourceName).empty());
    EXPECT_NE(vertical->iconResourceName, horizontal->iconResourceName);
}

TEST(ToolboxRegistry, MenuItemIsDeliberatelyNotIncludedInMenuAndToolbar) {
    // Real gap, not an oversight - see designer-plan.md's/ToolboxRegistry's
    // own class comment: MenuItem needs to be dropped onto an existing
    // MenuBar/submenu specifically, which doesn't fit "double-click
    // appends to rootViewProxy() directly" at all.
    const auto& categories = CodeToolsVsix::ToolboxRegistry::categories();
    const auto& menuAndToolbar = categories[4];
    EXPECT_FALSE(hasEntry(menuAndToolbar, "MenuItem"));
}

TEST(ToolboxRegistryIsContainer, TrueForAViewWithARealLayoutAttached) {
    auto* view = new newui::SubView();
    view->setLayout(std::make_unique<newui::AnchorLayout>());
    EXPECT_TRUE(CodeToolsVsix::ToolboxRegistry::isContainer(view));
    delete view;
}

TEST(ToolboxRegistryIsContainer, FalseForABareViewWithNoLayout) {
    // Deliberate, temporary - see ToolboxRegistry.h's own comment: this is expected to change
    // once a Properties-panel feature lets a Layout be added/changed/removed on a view.
    auto* view = new newui::SubView();
    EXPECT_FALSE(CodeToolsVsix::ToolboxRegistry::isContainer(view));
    delete view;
}

TEST(ToolboxRegistryIsContainer, FalseForNullptr) {
    EXPECT_FALSE(CodeToolsVsix::ToolboxRegistry::isContainer(nullptr));
}
