#include "../extension/NativeEditControls/DesignerClipboard.h"

#include <newui/controls.h>
#include <newui/layout.h>
#include <newui/rootview.h>
#include <newui/subview.h>

#include <gtest/gtest.h>

// registerReflectionData() is run once for this whole binary by test_component_editor.cpp's own
// ::testing::Environment - the serializer needs the real classes registered.

using CodeToolsVsix::DesignerClipboard;

namespace
{
    // A root the clones can be attached under (their names live in its NameManager).
    struct Doc
    {
        Doc() : root(nullptr, newui::Rect(0, 0, 400, 300), "docRoot") {}
        newui::RootView root;
    };
}

TEST(DesignerClipboard, TopLevelOfDropsAnySelectedViewWhoseAncestorIsAlsoSelected)
{
    newui::SubView parent;
    auto* child = new newui::SubView();
    auto* grandchild = new newui::SubView();
    auto* sibling = new newui::SubView();
    parent.addChild(child);
    child->addChild(grandchild);
    parent.addChild(sibling);

    auto top = DesignerClipboard::topLevelOf({ grandchild, child, sibling, child });
    ASSERT_EQ(top.size(), 2u);
    EXPECT_EQ(top[0], child);     // the container stands for its own subtree; a duplicate entry is dropped
    EXPECT_EQ(top[1], sibling);
}

TEST(DesignerClipboard, PackAndUnpackRoundTripSeveralViewsAndRejectGarbage)
{
    std::vector<std::string> views = { "{a:1}", std::string(), "{ multi\nline: \"text\" }" };
    auto payload = DesignerClipboard::pack(views);
    EXPECT_EQ(DesignerClipboard::unpack(payload), views);

    EXPECT_TRUE(DesignerClipboard::unpack({}).empty());
    EXPECT_TRUE(DesignerClipboard::unpack(std::vector<std::uint8_t>{ 'n', 'o', 'p', 'e' }).empty());

    auto truncated = DesignerClipboard::pack({ "{abcdef}" });
    truncated.pop_back();
    EXPECT_TRUE(DesignerClipboard::unpack(truncated).empty());
}

TEST(DesignerClipboard, ACloneKeepsItsClassPropertiesLayoutParamsAndChildren)
{
    newui::SubView container;
    container.setLayout(std::make_unique<newui::AnchorLayout>());
    auto* button = new newui::Button();
    button->setText("Hello");
    button->setBounds(newui::Rect(12, 34, 100, 30));
    auto params = std::make_unique<newui::AnchorLayoutParams>();
    params->setLeftMargin(12);
    params->setTopMargin(34);
    button->setLayoutParams(std::move(params));
    container.addChild(button);

    std::string text = DesignerClipboard::serialize(container);
    ASSERT_FALSE(text.empty());
    newui::SubView* clone = DesignerClipboard::create(text);
    ASSERT_NE(clone, nullptr);

    EXPECT_NE(dynamic_cast<newui::AnchorLayout*>(clone->layout()), nullptr);
    ASSERT_EQ(clone->childViews().size(), 1u);
    auto* clonedButton = dynamic_cast<newui::Button*>(clone->childViews()[0]);
    ASSERT_NE(clonedButton, nullptr);
    EXPECT_EQ(clonedButton->text(), "Hello");
    EXPECT_FLOAT_EQ(clonedButton->bounds().left(), 12.0f);
    auto* clonedParams = dynamic_cast<newui::AnchorLayoutParams*>(clonedButton->layoutParams());
    ASSERT_NE(clonedParams, nullptr);
    EXPECT_FLOAT_EQ(clonedParams->topMargin(), 34.0f);
    EXPECT_TRUE(clone->isDesignTime());   // comes back flagged like a loaded document's views

    clone->destroy();
    delete clone;
}

TEST(DesignerClipboard, ATabControlCloneKeepsItsTabsAndTheirTitles)
{
    newui::TabControl tabs;
    auto* first = new newui::TabPage();
    tabs.addTab("Alpha", first);
    auto* second = new newui::TabPage();
    tabs.addTab("Beta", second);

    newui::SubView* clone = DesignerClipboard::create(DesignerClipboard::serialize(tabs));
    auto* clonedTabs = dynamic_cast<newui::TabControl*>(clone);
    ASSERT_NE(clonedTabs, nullptr);
    ASSERT_EQ(clonedTabs->tabCount(), 2u);
    EXPECT_EQ(clonedTabs->tabButton(0)->name(), "Alpha");
    EXPECT_EQ(clonedTabs->tabButton(1)->name(), "Beta");
    EXPECT_EQ(clonedTabs->childViews().size(), 2u);   // strip + pages area, nothing duplicated

    clone->destroy();
    delete clone;
}

TEST(DesignerClipboard, CreateReturnsNullForTextThatIsNotAView)
{
    EXPECT_EQ(DesignerClipboard::create(std::string()), nullptr);
    EXPECT_EQ(DesignerClipboard::create("this is not json5 {{{"), nullptr);
    EXPECT_EQ(DesignerClipboard::create("{ type: \"NoSuchClass\" }"), nullptr);
}

TEST(DesignerClipboard, UniquifyNamesClearsATakenNameButLeavesInternalPartsAndFreeNames)
{
    Doc doc;
    auto* original = new newui::Button();
    original->setName("button1");
    doc.root.addChild(original);
    ASSERT_TRUE(doc.root.nameManager().isTaken("button1"));

    auto* tabs = new newui::TabControl();
    tabs->addTab("One", new newui::TabPage());
    auto* taken = new newui::Button();
    taken->setName("button1");
    auto* free = new newui::Button();
    free->setName("brandNew");
    newui::SubView holder;
    holder.addChild(taken);
    holder.addChild(free);
    holder.addChild(tabs);

    DesignerClipboard::uniquifyNames(holder, doc.root);

    EXPECT_EQ(taken->name(), "");            // regenerated on attach
    EXPECT_EQ(free->name(), "brandNew");
    EXPECT_EQ(tabs->childViews()[0]->name(), "TabControlStrip");   // internal: kept
}

// A GridLayout's row/column tracks come back from a copy (they used to be written but never read).
TEST(DesignerClipboard, AGridLayoutCloneKeepsItsTracksAndSpacing)
{
    newui::SubView container;
    auto grid = std::make_unique<newui::GridLayout>();
    grid->addFixedRow(40);
    grid->addStarRow(2);
    grid->addAutoColumn();
    grid->addStarColumn(1);
    grid->setRowSpacing(3);
    container.setLayout(std::move(grid));

    newui::SubView* clone = DesignerClipboard::create(DesignerClipboard::serialize(container));
    ASSERT_NE(clone, nullptr);
    auto* cloned = dynamic_cast<newui::GridLayout*>(clone->layout());
    ASSERT_NE(cloned, nullptr);
    ASSERT_EQ(cloned->rows().size(), 2u);
    EXPECT_EQ(cloned->rows()[0].kind, newui::GridTrackKind::Fixed);
    EXPECT_FLOAT_EQ(cloned->rows()[0].value, 40.0f);
    EXPECT_FLOAT_EQ(cloned->rows()[1].value, 2.0f);
    ASSERT_EQ(cloned->columns().size(), 2u);
    EXPECT_EQ(cloned->columns()[0].kind, newui::GridTrackKind::Auto);
    EXPECT_FLOAT_EQ(cloned->rowSpacing(), 3.0f);

    clone->destroy();
    delete clone;
}
