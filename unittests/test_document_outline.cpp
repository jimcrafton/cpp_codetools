#include "../extension/NativeEditControls/DocumentOutline.h"
#include "../extension/NativeEditControls/ToolboxRegistry.h"

#include <newui/controls.h>
#include <newui/layout.h>
#include <newui/mouse_constants.h>
#include <newui/subview.h>

#include <gtest/gtest.h>

// registerReflectionData() is already run once globally for this whole
// binary by test_component_editor.cpp's own ::testing::Environment - no
// separate registration needed here (same convention test_toolbox.cpp's
// own comment documents) - needed for classinfo(typeid(*view))->name() to
// resolve a real "SubView" Class.

using CodeToolsVsix::DocumentOutline;
using CodeToolsVsix::DocumentOutlineController;
using CodeToolsVsix::DocumentOutlineDropDisposition;
using CodeToolsVsix::DocumentOutlineItem;
using CodeToolsVsix::DocumentOutlineModel;
using CodeToolsVsix::ToolboxRegistry;
using CodeToolsVsix::ViewDesignerModel;

namespace {
BLContext& SharedDocumentOutlinePaintContext() {
    static BLImage image(200, 400, BL_FORMAT_PRGB32);
    static BLContext ctx(image);
    return ctx;
}
}

TEST(DocumentOutlineModel, WithNoSourceEverythingIsEmpty)
{
    DocumentOutlineModel model;
    EXPECT_EQ(model.childCount({}), 0u);
    EXPECT_EQ(std::any_cast<std::string>(model.value(std::vector<std::size_t>{0})), std::string());
}

TEST(DocumentOutlineModel, ForwardsChildCountToItsSource)
{
    newui::SubView root;
    auto* child = new newui::SubView();
    root.addChild(child);

    ViewDesignerModel source;
    source.setRoot(&root);

    DocumentOutlineModel model;
    model.setSource(&source);

    EXPECT_EQ(model.childCount({}), 1u);
    EXPECT_EQ(model.childCount(std::vector<std::size_t>{0}), 1u);
}

TEST(DocumentOutlineModel, ValueCombinesNameAndRealTypeName)
{
    newui::SubView root;
    root.setName("myRoot");

    ViewDesignerModel source;
    source.setRoot(&root);

    DocumentOutlineModel model;
    model.setSource(&source);

    std::any value = model.value(std::vector<std::size_t>{0});
    EXPECT_EQ(std::any_cast<std::string>(value), "myRoot (SubView)");
}

TEST(DocumentOutlineModel, RelaysSourceOnChangedAsItsOwn)
{
    newui::SubView root;
    ViewDesignerModel source;
    source.setRoot(&root);

    DocumentOutlineModel model;
    model.setSource(&source);

    int changedCount = 0;
    model.onChanged.add([&changedCount](newui::Model&) {
        ++changedCount;
        return newui::SyncReturn::Handled;
    });

    source.refresh();
    EXPECT_EQ(changedCount, 1);
}

TEST(DocumentOutline, SetViewDesignerModelExpandsRootByDefault)
{
    auto* outline = new DocumentOutline();
    newui::SubView root;
    ViewDesignerModel source;
    source.setRoot(&root);

    outline->setViewDesignerModel(&source);
    EXPECT_TRUE(outline->treeView()->controller().isExpanded(std::vector<std::size_t>{0}));

    delete outline;
}

TEST(DocumentOutline, SetSelectionAppliesTheExactRequestedPaths)
{
    auto* outline = new DocumentOutline();
    newui::SubView root;
    auto* a = new newui::SubView();
    auto* b = new newui::SubView();
    root.addChild(a);
    root.addChild(b);

    ViewDesignerModel source;
    source.setRoot(&root);
    outline->setViewDesignerModel(&source);

    outline->setSelection(std::vector<newui::SubView*>{a, b});

    EXPECT_TRUE(outline->treeView()->isSelected(std::vector<std::size_t>{0, 0}));
    EXPECT_TRUE(outline->treeView()->isSelected(std::vector<std::size_t>{0, 1}));
    EXPECT_EQ(outline->treeView()->selectedPaths().size(), 2u);

    delete outline;
}

TEST(DocumentOutline, SetSelectionDoesNotFireOnSelectionActivatedBack)
{
    auto* outline = new DocumentOutline();
    newui::SubView root;
    auto* a = new newui::SubView();
    root.addChild(a);

    ViewDesignerModel source;
    source.setRoot(&root);
    outline->setViewDesignerModel(&source);

    bool activated = false;
    outline->onSelectionActivated.add([&activated](DocumentOutline&, const std::vector<newui::SubView*>&) {
        activated = true;
        return newui::SyncReturn::Handled;
    });

    outline->setSelection(std::vector<newui::SubView*>{a});
    EXPECT_FALSE(activated);

    delete outline;
}

// A real user click/Ctrl+click drives selection through TreeView's own
// public selection API (setSelectedPath()/addToSelection()), never through
// this class's own setSelection() - that's the real distinction
// applyingExternalSelection_ exists to preserve, so this test exercises
// TreeView's own real API rather than synthesizing a mouse event (see
// [[feedback_no_synthetic_input_unit_tests]] - not a mouse/keyboard
// handler test, TreeView's selection setters are real public API already
// covered by its own test suite).
TEST(DocumentOutline, RealTreeSelectionChangeFiresOnSelectionActivatedWithResolvedViews)
{
    auto* outline = new DocumentOutline();
    newui::SubView root;
    auto* a = new newui::SubView();
    root.addChild(a);

    ViewDesignerModel source;
    source.setRoot(&root);
    outline->setViewDesignerModel(&source);

    std::vector<newui::SubView*> activatedViews;
    int activatedCount = 0;
    outline->onSelectionActivated.add([&](DocumentOutline&, const std::vector<newui::SubView*>& views) {
        activatedViews = views;
        ++activatedCount;
        return newui::SyncReturn::Handled;
    });

    outline->treeView()->setSelectedPath(std::vector<std::size_t>{0, 0});

    EXPECT_EQ(activatedCount, 1);
    ASSERT_EQ(activatedViews.size(), 1u);
    EXPECT_EQ(activatedViews[0], a);

    delete outline;
}

TEST(DocumentOutline, SetSelectionExpandsCollapsedAncestorsOfTheTarget)
{
    auto* outline = new DocumentOutline();
    newui::SubView root;
    auto* group = new newui::SubView();
    auto* nested = new newui::SubView();
    root.addChild(group);
    group->addChild(nested);

    ViewDesignerModel source;
    source.setRoot(&root);
    outline->setViewDesignerModel(&source);

    // {0} (root) starts expanded by default; {0, 0} (group) does not -
    // nested (path {0, 0, 0}) is invisible until it does.
    ASSERT_FALSE(outline->treeView()->controller().isExpanded(std::vector<std::size_t>{0, 0}));

    outline->setSelection(std::vector<newui::SubView*>{nested});

    EXPECT_TRUE(outline->treeView()->controller().isExpanded(std::vector<std::size_t>{0}));
    EXPECT_TRUE(outline->treeView()->controller().isExpanded(std::vector<std::size_t>{0, 0}));
    EXPECT_TRUE(outline->treeView()->isSelected(std::vector<std::size_t>{0, 0, 0}));

    delete outline;
}

TEST(DocumentOutline, SetSelectionWithEmptyListClearsTreeSelection)
{
    auto* outline = new DocumentOutline();
    newui::SubView root;
    auto* a = new newui::SubView();
    root.addChild(a);

    ViewDesignerModel source;
    source.setRoot(&root);
    outline->setViewDesignerModel(&source);
    outline->treeView()->setSelectedPath(std::vector<std::size_t>{0, 0});

    outline->setSelection(std::vector<newui::SubView*>{});
    EXPECT_TRUE(outline->treeView()->selectedPaths().empty());

    delete outline;
}

TEST(DocumentOutlineController, IconForARealChildMatchesTheRegistrysOwnIconResourceName)
{
    newui::SubView root;
    auto* button = new newui::Button();
    root.addChild(button);

    ViewDesignerModel source;
    source.setRoot(&root);
    DocumentOutlineModel model;
    model.setSource(&source);

    DocumentOutlineController controller;
    controller.setModel(&model);

    auto icon = controller.iconFor(std::vector<std::size_t>{0, 0});
    ASSERT_TRUE(icon.has_value());
    EXPECT_EQ(*icon, ToolboxRegistry::iconResourceNameFor("Button"));
}

TEST(DocumentOutlineController, IconForAnOutOfRangePathIsNullopt)
{
    newui::SubView root;
    ViewDesignerModel source;
    source.setRoot(&root);
    DocumentOutlineModel model;
    model.setSource(&source);

    DocumentOutlineController controller;
    controller.setModel(&model);

    EXPECT_FALSE(controller.iconFor(std::vector<std::size_t>{99}).has_value());
}

TEST(DocumentOutlineController, IconForWithNoModelIsNullopt)
{
    DocumentOutlineController controller;
    EXPECT_FALSE(controller.iconFor(std::vector<std::size_t>{0}).has_value());
}

TEST(DocumentOutlineItem, PaintARealIconBearingRowDoesNotCrash)
{
    newui::SubView root;
    auto* button = new newui::Button();
    root.addChild(button);

    ViewDesignerModel source;
    source.setRoot(&root);
    DocumentOutlineModel model;
    model.setSource(&source);

    DocumentOutlineController controller;
    controller.setModel(&model);
    auto* item = static_cast<DocumentOutlineItem*>(controller.createItem({0, 0}));
    ASSERT_NE(item, nullptr);

    item->paint(SharedDocumentOutlinePaintContext(), newui::Rect(0.0f, 0.0f, 180.0f, 22.0f),
        std::vector<std::size_t>{0, 0}, controller);

    controller.releaseItem(item);
}

TEST(DocumentOutline, DraggingARowOntoARealContainerRowFiresOnReparentRequested)
{
    auto* outline = new DocumentOutline();
    newui::SubView root;
    auto* a = new newui::SubView();
    a->setName("a");
    root.addChild(a);
    auto* container = new newui::SubView();
    container->setName("container");
    container->setLayout(std::make_unique<newui::AnchorLayout>());
    root.addChild(container);

    ViewDesignerModel source;
    source.setRoot(&root);
    outline->setViewDesignerModel(&source);
    outline->treeView()->setBounds(newui::Rect(0, 0, 200, 400));

    std::optional<newui::Rect> aRect = outline->treeView()->rectForPath(std::vector<std::size_t>{0, 0});
    std::optional<newui::Rect> containerRect = outline->treeView()->rectForPath(std::vector<std::size_t>{0, 1});
    ASSERT_TRUE(aRect.has_value());
    ASSERT_TRUE(containerRect.has_value());

    newui::Point aPt(aRect->left() + 5.0f, aRect->top() + aRect->size().height / 2.0f);
    newui::Point containerPt(containerRect->left() + 5.0f, containerRect->top() + containerRect->size().height / 2.0f);

    newui::SubView* reparentedDragged = nullptr;
    newui::SubView* reparentedTarget = nullptr;
    DocumentOutlineDropDisposition reparentedDisposition = DocumentOutlineDropDisposition::Before;
    int firedCount = 0;
    outline->onDropRequested.add([&](DocumentOutline&, newui::SubView* dragged, newui::SubView* target,
        DocumentOutlineDropDisposition disposition) {
        reparentedDragged = dragged;
        reparentedTarget = target;
        reparentedDisposition = disposition;
        ++firedCount;
        return newui::SyncReturn::Handled;
    });

    outline->treeView()->onMouseDown(*outline->treeView(), aPt, newui::mbmLeftButton, newui::kmUndefined);
    outline->treeView()->onMouseMove(*outline->treeView(), containerPt, newui::mbmLeftButton, 0);

    auto& controller = static_cast<DocumentOutlineController&>(outline->treeView()->controller());
    EXPECT_EQ(controller.dispositionFor(std::vector<std::size_t>{0, 1}), DocumentOutlineDropDisposition::Into);

    outline->treeView()->onMouseUp(*outline->treeView(), containerPt, newui::mbmLeftButton, 0);

    EXPECT_EQ(firedCount, 1);
    EXPECT_EQ(reparentedDragged, a);
    EXPECT_EQ(reparentedTarget, container);
    EXPECT_EQ(reparentedDisposition, DocumentOutlineDropDisposition::Into);
    EXPECT_FALSE(controller.pendingDropTarget().has_value());

    delete outline;
}

TEST(DocumentOutline, DraggingWithinTheSameParentFiresOnDropRequestedWithABeforeOrAfterDisposition)
{
    // Dropping at a sibling's vertical center (row midpoint) with same-parent siblings that
    // aren't themselves containers is a reorder gesture now, not silently ignored - b and c
    // below share container as their real parent, and neither is a container itself, so the
    // resolved disposition is always Before/After, never Into.
    auto* outline = new DocumentOutline();
    newui::SubView root;
    auto* container = new newui::SubView();
    container->setName("container");
    container->setLayout(std::make_unique<newui::FlexLayout>(newui::Orientation::Vertical));
    root.addChild(container);
    auto* b = new newui::SubView();
    b->setName("b");
    container->addChild(b);
    auto* c = new newui::SubView();
    c->setName("c");
    container->addChild(c);

    ViewDesignerModel source;
    source.setRoot(&root);
    outline->setViewDesignerModel(&source);
    outline->treeView()->controller().setExpanded(std::vector<std::size_t>{0, 0}, true);
    outline->treeView()->setBounds(newui::Rect(0, 0, 200, 400));

    std::optional<newui::Rect> bRect = outline->treeView()->rectForPath(std::vector<std::size_t>{0, 0, 0});
    std::optional<newui::Rect> cRect = outline->treeView()->rectForPath(std::vector<std::size_t>{0, 0, 1});
    ASSERT_TRUE(bRect.has_value());
    ASSERT_TRUE(cRect.has_value());

    newui::Point bPt(bRect->left() + 5.0f, bRect->top() + bRect->size().height / 2.0f);
    newui::Point cPt(cRect->left() + 5.0f, cRect->top() + cRect->size().height / 2.0f);

    int firedCount = 0;
    newui::SubView* droppedDragged = nullptr;
    newui::SubView* droppedReferenceRow = nullptr;
    DocumentOutlineDropDisposition droppedDisposition = DocumentOutlineDropDisposition::Into;
    outline->onDropRequested.add([&](DocumentOutline&, newui::SubView* dragged, newui::SubView* referenceRow,
        DocumentOutlineDropDisposition disposition) {
        droppedDragged = dragged;
        droppedReferenceRow = referenceRow;
        droppedDisposition = disposition;
        ++firedCount;
        return newui::SyncReturn::Handled;
    });

    outline->treeView()->onMouseDown(*outline->treeView(), bPt, newui::mbmLeftButton, newui::kmUndefined);
    outline->treeView()->onMouseMove(*outline->treeView(), cPt, newui::mbmLeftButton, 0);
    outline->treeView()->onMouseUp(*outline->treeView(), cPt, newui::mbmLeftButton, 0);

    EXPECT_EQ(firedCount, 1);
    EXPECT_EQ(droppedDragged, b);
    EXPECT_EQ(droppedReferenceRow, c);
    EXPECT_NE(droppedDisposition, DocumentOutlineDropDisposition::Into);
    // The mutation itself is DesignerEditor::handleOutlineDropRequested()'s own job (see
    // test_designer_editor.cpp) - this class only ever detects/reports the gesture, so b's
    // parent is unaffected by this delegate firing alone.
    EXPECT_EQ(b->parent(), container);

    delete outline;
}

TEST(DocumentOutline, DraggingARowOntoItsOwnDescendantDoesNotFireOnReparentRequested)
{
    auto* outline = new DocumentOutline();
    newui::SubView root;
    auto* container = new newui::SubView();
    container->setName("container");
    container->setLayout(std::make_unique<newui::AnchorLayout>());
    root.addChild(container);
    auto* nested = new newui::SubView();
    nested->setName("nested");
    nested->setLayout(std::make_unique<newui::AnchorLayout>());
    container->addChild(nested);

    ViewDesignerModel source;
    source.setRoot(&root);
    outline->setViewDesignerModel(&source);
    outline->treeView()->controller().setExpanded(std::vector<std::size_t>{0, 0}, true);
    outline->treeView()->setBounds(newui::Rect(0, 0, 200, 400));

    std::optional<newui::Rect> containerRect = outline->treeView()->rectForPath(std::vector<std::size_t>{0, 0});
    std::optional<newui::Rect> nestedRect = outline->treeView()->rectForPath(std::vector<std::size_t>{0, 0, 0});
    ASSERT_TRUE(containerRect.has_value());
    ASSERT_TRUE(nestedRect.has_value());

    newui::Point containerPt(containerRect->left() + 5.0f, containerRect->top() + containerRect->size().height / 2.0f);
    newui::Point nestedPt(nestedRect->left() + 5.0f, nestedRect->top() + nestedRect->size().height / 2.0f);

    int firedCount = 0;
    outline->onDropRequested.add([&](DocumentOutline&, newui::SubView*, newui::SubView*,
        DocumentOutlineDropDisposition) {
        ++firedCount;
        return newui::SyncReturn::Handled;
    });

    outline->treeView()->onMouseDown(*outline->treeView(), containerPt, newui::mbmLeftButton, newui::kmUndefined);
    outline->treeView()->onMouseMove(*outline->treeView(), nestedPt, newui::mbmLeftButton, 0);
    outline->treeView()->onMouseUp(*outline->treeView(), nestedPt, newui::mbmLeftButton, 0);

    EXPECT_EQ(firedCount, 0);
    EXPECT_EQ(nested->parent(), container);

    delete outline;
}

TEST(DocumentOutline, DraggingOntoANonContainerRowInADifferentParentFiresOnDropRequestedBeforeOrAfter)
{
    // A row with no layout of its own can never be an Into target, but it can still be a real
    // Before/After reference point - even across a real parent boundary (containerA vs.
    // containerB below), a cross-container position-precise insert that only makes sense once
    // reordering exists at all (DesignerEditor::handleOutlineDropRequested() resolves the actual
    // new parent as referenceRow->parent(), see test_designer_editor.cpp's own coverage of that
    // mutation).
    auto* outline = new DocumentOutline();
    newui::SubView root;
    auto* containerA = new newui::SubView();
    containerA->setName("containerA");
    containerA->setLayout(std::make_unique<newui::AnchorLayout>());
    root.addChild(containerA);
    auto* a = new newui::SubView();
    a->setName("a");
    containerA->addChild(a);

    auto* containerB = new newui::SubView();
    containerB->setName("containerB");
    containerB->setLayout(std::make_unique<newui::AnchorLayout>());
    root.addChild(containerB);
    auto* bareView = new newui::SubView();
    bareView->setName("bareView");
    containerB->addChild(bareView);

    ViewDesignerModel source;
    source.setRoot(&root);
    outline->setViewDesignerModel(&source);
    outline->treeView()->controller().setExpanded(std::vector<std::size_t>{0, 0}, true);
    outline->treeView()->controller().setExpanded(std::vector<std::size_t>{0, 1}, true);
    outline->treeView()->setBounds(newui::Rect(0, 0, 200, 400));

    std::optional<newui::Rect> aRect = outline->treeView()->rectForPath(std::vector<std::size_t>{0, 0, 0});
    std::optional<newui::Rect> bareRect = outline->treeView()->rectForPath(std::vector<std::size_t>{0, 1, 0});
    ASSERT_TRUE(aRect.has_value());
    ASSERT_TRUE(bareRect.has_value());

    newui::Point aPt(aRect->left() + 5.0f, aRect->top() + aRect->size().height / 2.0f);
    newui::Point barePt(bareRect->left() + 5.0f, bareRect->top() + bareRect->size().height / 2.0f);

    int firedCount = 0;
    newui::SubView* droppedReferenceRow = nullptr;
    DocumentOutlineDropDisposition droppedDisposition = DocumentOutlineDropDisposition::Into;
    outline->onDropRequested.add([&](DocumentOutline&, newui::SubView*, newui::SubView* referenceRow,
        DocumentOutlineDropDisposition disposition) {
        droppedReferenceRow = referenceRow;
        droppedDisposition = disposition;
        ++firedCount;
        return newui::SyncReturn::Handled;
    });

    outline->treeView()->onMouseDown(*outline->treeView(), aPt, newui::mbmLeftButton, newui::kmUndefined);
    outline->treeView()->onMouseMove(*outline->treeView(), barePt, newui::mbmLeftButton, 0);
    outline->treeView()->onMouseUp(*outline->treeView(), barePt, newui::mbmLeftButton, 0);

    EXPECT_EQ(firedCount, 1);
    EXPECT_EQ(droppedReferenceRow, bareView);
    EXPECT_NE(droppedDisposition, DocumentOutlineDropDisposition::Into);
    EXPECT_EQ(a->parent(), containerA);

    delete outline;
}
