#include "../extension/NativeEditControls/ViewDesignerModel.h"

#include <newui/subview.h>

#include <gtest/gtest.h>

using CodeToolsVsix::ViewDesignerModel;

namespace {
    int g_changedCount = 0;

    newui::SyncReturn CountChanged(newui::Model& /*sender*/) {
        ++g_changedCount;
        return newui::SyncReturn::Handled;
    }
}

TEST(ViewDesignerModel, StartsWithNoRoot)
{
    ViewDesignerModel model;
    EXPECT_EQ(model.root(), nullptr);
    EXPECT_EQ(model.childCount({}), 0u);
}

TEST(ViewDesignerModel, RootPathIsTheOnlyTopLevelItem)
{
    newui::SubView root;
    ViewDesignerModel model;
    model.setRoot(&root);

    EXPECT_EQ(model.childCount({}), 1u);
    EXPECT_EQ(model.viewAt(std::vector<std::size_t>{0}), &root);
}

TEST(ViewDesignerModel, ChildCountAtRootPathIsItsRealChildCount)
{
    newui::SubView root;
    auto* a = new newui::SubView();
    auto* b = new newui::SubView();
    root.addChild(a);
    root.addChild(b);

    ViewDesignerModel model;
    model.setRoot(&root);

    EXPECT_EQ(model.childCount(std::vector<std::size_t>{0}), 2u);
    EXPECT_EQ(model.viewAt(std::vector<std::size_t>{0, 0}), a);
    EXPECT_EQ(model.viewAt(std::vector<std::size_t>{0, 1}), b);
}

TEST(ViewDesignerModel, ViewAtWalksMultipleLevels)
{
    newui::SubView root;
    auto* container = new newui::SubView();
    auto* nested = new newui::SubView();
    container->addChild(nested);
    root.addChild(container);

    ViewDesignerModel model;
    model.setRoot(&root);

    EXPECT_EQ(model.childCount(std::vector<std::size_t>{0, 0}), 1u);
    EXPECT_EQ(model.viewAt(std::vector<std::size_t>{0, 0, 0}), nested);
}

TEST(ViewDesignerModel, ViewAtWithOutOfRangeIndexIsNull)
{
    newui::SubView root;
    ViewDesignerModel model;
    model.setRoot(&root);

    EXPECT_EQ(model.viewAt(std::vector<std::size_t>{0, 5}), nullptr);
}

TEST(ViewDesignerModel, ViewAtWithNoRootIsAlwaysNull)
{
    ViewDesignerModel model;
    EXPECT_EQ(model.viewAt(std::vector<std::size_t>{0}), nullptr);
}

TEST(ViewDesignerModel, PathForRootItselfIsPathZero)
{
    newui::SubView root;
    ViewDesignerModel model;
    model.setRoot(&root);

    auto path = model.pathFor(&root);
    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(*path, (std::vector<std::size_t>{0}));
}

TEST(ViewDesignerModel, PathForANestedChildWalksUpToRoot)
{
    newui::SubView root;
    auto* container = new newui::SubView();
    auto* nested = new newui::SubView();
    container->addChild(nested);
    root.addChild(container);

    ViewDesignerModel model;
    model.setRoot(&root);

    auto path = model.pathFor(nested);
    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(*path, (std::vector<std::size_t>{0, 0, 0}));
}

TEST(ViewDesignerModel, PathForAViewNotUnderRootIsNullopt)
{
    newui::SubView root;
    newui::SubView unrelated;
    ViewDesignerModel model;
    model.setRoot(&root);

    EXPECT_FALSE(model.pathFor(&unrelated).has_value());
}

TEST(ViewDesignerModel, PathForNullptrIsNullopt)
{
    newui::SubView root;
    ViewDesignerModel model;
    model.setRoot(&root);

    EXPECT_FALSE(model.pathFor(nullptr).has_value());
}

TEST(ViewDesignerModel, SetRootFiresOnChanged)
{
    g_changedCount = 0;
    newui::SubView root;
    ViewDesignerModel model;
    model.onChanged += CountChanged;

    model.setRoot(&root);
    EXPECT_EQ(g_changedCount, 1);
}

TEST(ViewDesignerModel, RefreshFiresOnChangedWithRootUnchanged)
{
    g_changedCount = 0;
    newui::SubView root;
    ViewDesignerModel model;
    model.setRoot(&root);
    model.onChanged += CountChanged;

    model.refresh();
    EXPECT_EQ(g_changedCount, 1);
    EXPECT_EQ(model.root(), &root);
}
