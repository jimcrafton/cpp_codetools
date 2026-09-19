#include "../extension/NativeEditControls/DesignerArrange.h"
#include "../extension/NativeEditControls/DesignerEditor.h"

#include <newui/controls.h>
#include <newui/layout.h>
#include <newui/rootview.h>
#include <newui/rootviewproxy.h>
#include <newui/subview.h>

#include <gtest/gtest.h>

using namespace CodeToolsVsix;

namespace
{
    // A designer with one free-position (AnchorLayout) container and one Flex column on its surface.
    struct ArrangeFixture
    {
        newui::RootView root{nullptr, newui::Rect(0, 0, 10, 10), "designerRoot"};
        DesignerEditor editor{&root};
        newui::RootViewProxy* surface = nullptr;
        newui::SubView* free = nullptr;
        newui::SubView* flex = nullptr;

        ArrangeFixture()
        {
            root.setBounds(newui::Rect(0, 0, 1400, 700));
            surface = editor.workspace()->rootViewProxy();

            free = new newui::SubView();
            free->setVisible(true);
            free->setBounds(newui::Rect(10, 10, 400, 300));
            free->setLayout(std::make_unique<newui::AnchorLayout>());
            surface->addChild(free);

            flex = new newui::SubView();
            flex->setVisible(true);
            flex->setBounds(newui::Rect(500, 10, 100, 300));
            flex->setLayout(std::make_unique<newui::FlexLayout>(newui::Orientation::Vertical));
            surface->addChild(flex);
        }

        // A button at parent-local bounds, added to `parent` (Anchor parents get matching params).
        newui::Button* button(newui::SubView* parent, float x, float y, float w = 50, float h = 20)
        {
            auto* b = new newui::Button();
            b->setVisible(true);
            parent->addChild(b);
            b->setBounds(newui::Rect(x, y, w, h));
            if (dynamic_cast<newui::AnchorLayout*>(parent->layout()) != nullptr) {
                applyFreePositionAnchorParams(b, newui::Rect(x, y, w, h));
            }
            editor.viewDesignerModel().refresh();
            return b;
        }
    };

    std::vector<newui::SubView*> orderOf(newui::View* parent)
    {
        return parent->childViews();
    }
}

// ---------------------------------------------------------------------------
// z-order
// ---------------------------------------------------------------------------

TEST(DesignerArrangeZOrder, BringToFrontAndSendToBackKeepTheSelectedViewsRelativeOrder)
{
    newui::SubView parent;
    auto* a = new newui::SubView(); auto* b = new newui::SubView();
    auto* c = new newui::SubView(); auto* d = new newui::SubView();
    for (auto* v : { a, b, c, d }) { parent.addChild(v); }

    auto front = computeZOrder({ b, c }, ZOrderOp::BringToFront);
    ASSERT_EQ(front.size(), 1u);
    EXPECT_EQ(front[0].parent, &parent);
    EXPECT_EQ(front[0].before, (std::vector<newui::SubView*>{ a, b, c, d }));
    EXPECT_EQ(front[0].after, (std::vector<newui::SubView*>{ a, d, b, c }));

    auto back = computeZOrder({ c, d }, ZOrderOp::SendToBack);
    ASSERT_EQ(back.size(), 1u);
    EXPECT_EQ(back[0].after, (std::vector<newui::SubView*>{ c, d, a, b }));
}

TEST(DesignerArrangeZOrder, ForwardAndBackwardStepPastTheNearestUnselectedSibling)
{
    newui::SubView parent;
    auto* a = new newui::SubView(); auto* b = new newui::SubView();
    auto* c = new newui::SubView(); auto* d = new newui::SubView();
    for (auto* v : { a, b, c, d }) { parent.addChild(v); }

    EXPECT_EQ(computeZOrder({ b }, ZOrderOp::BringForward)[0].after, (std::vector<newui::SubView*>{ a, c, b, d }));
    EXPECT_EQ(computeZOrder({ c }, ZOrderOp::SendBackward)[0].after, (std::vector<newui::SubView*>{ a, c, b, d }));
    // Two adjacent selected views both step up past the same sibling, staying together.
    EXPECT_EQ(computeZOrder({ b, c }, ZOrderOp::BringForward)[0].after, (std::vector<newui::SubView*>{ a, d, b, c }));
    EXPECT_EQ(computeZOrder({ b, c }, ZOrderOp::SendBackward)[0].after, (std::vector<newui::SubView*>{ b, c, a, d }));
}

TEST(DesignerArrangeZOrder, AViewAlreadyAtTheLimitProducesNoChange)
{
    newui::SubView parent;
    auto* a = new newui::SubView(); auto* b = new newui::SubView();
    parent.addChild(a);
    parent.addChild(b);

    EXPECT_TRUE(computeZOrder({ b }, ZOrderOp::BringToFront).empty());
    EXPECT_TRUE(computeZOrder({ b }, ZOrderOp::BringForward).empty());
    EXPECT_TRUE(computeZOrder({ a }, ZOrderOp::SendToBack).empty());
    EXPECT_TRUE(computeZOrder({ a }, ZOrderOp::SendBackward).empty());
}

TEST(DesignerArrangeZOrder, EachParentIsReorderedOnItsOwn)
{
    newui::SubView p1, p2;
    auto* a1 = new newui::SubView(); auto* b1 = new newui::SubView();
    auto* a2 = new newui::SubView(); auto* b2 = new newui::SubView();
    p1.addChild(a1); p1.addChild(b1);
    p2.addChild(a2); p2.addChild(b2);

    auto changes = computeZOrder({ a1, a2 }, ZOrderOp::BringToFront);
    ASSERT_EQ(changes.size(), 2u);
    EXPECT_EQ(changes[0].after, (std::vector<newui::SubView*>{ b1, a1 }));
    EXPECT_EQ(changes[1].after, (std::vector<newui::SubView*>{ b2, a2 }));
}

TEST(DesignerEditorArrange, ZOrderIsOneUndoableStepAndSelectionSurvivesIt)
{
    ArrangeFixture f;
    auto* a = f.button(f.free, 10, 10);
    auto* b = f.button(f.free, 20, 20);
    auto* c = f.button(f.free, 30, 30);
    f.editor.viewDesignerController().selectExclusive(a);

    ASSERT_TRUE(f.editor.reorderSelection(ZOrderOp::BringToFront));
    EXPECT_EQ(orderOf(f.free), (std::vector<newui::SubView*>{ b, c, a }));
    EXPECT_EQ(f.editor.undoStack().undoDescription(), "Bring to Front");
    EXPECT_TRUE(f.editor.isDirty());
    EXPECT_EQ(f.editor.viewDesignerController().primary(), a);

    f.editor.undoStack().undo();
    EXPECT_EQ(orderOf(f.free), (std::vector<newui::SubView*>{ a, b, c }));
    f.editor.undoStack().redo();
    EXPECT_EQ(orderOf(f.free), (std::vector<newui::SubView*>{ b, c, a }));

    EXPECT_FALSE(f.editor.reorderSelection(ZOrderOp::BringToFront));   // already in front: nothing to do
}

// ---------------------------------------------------------------------------
// alignment, distribution, matching size
// ---------------------------------------------------------------------------

TEST(DesignerEditorArrange, AlignLeftMovesTheOthersToThePrimaryAndKeepsAnchorParamsInStep)
{
    ArrangeFixture f;
    auto* a = f.button(f.free, 10, 10);
    auto* b = f.button(f.free, 80, 50);
    auto* primary = f.button(f.free, 40, 90);
    f.editor.viewDesignerController().setSelection({ a, b, primary });   // primary = the last selected

    ASSERT_TRUE(f.editor.alignSelection(AlignKind::Left));

    EXPECT_FLOAT_EQ(a->bounds().left(), 40.0f);
    EXPECT_FLOAT_EQ(b->bounds().left(), 40.0f);
    EXPECT_FLOAT_EQ(b->bounds().top(), 50.0f);         // only the aligned axis moves
    EXPECT_FLOAT_EQ(primary->bounds().left(), 40.0f);  // the primary never moves
    auto* params = dynamic_cast<newui::AnchorLayoutParams*>(b->layoutParams());
    ASSERT_NE(params, nullptr);
    EXPECT_FLOAT_EQ(params->leftMargin(), 40.0f);      // the params drive the layout, so they must follow
    EXPECT_EQ(f.editor.undoStack().undoDescription(), "Align Left");

    f.editor.undoStack().undo();
    EXPECT_FLOAT_EQ(a->bounds().left(), 10.0f);
    EXPECT_FLOAT_EQ(b->bounds().left(), 80.0f);
    f.editor.undoStack().redo();
    EXPECT_FLOAT_EQ(a->bounds().left(), 40.0f);
}

TEST(DesignerEditorArrange, AlignCentersRightsAndBottomsUseTheRightEdgesOfEachControl)
{
    ArrangeFixture f;
    auto* narrow = f.button(f.free, 0, 0, 20, 10);
    auto* primary = f.button(f.free, 100, 100, 60, 40);
    f.editor.viewDesignerController().setSelection({ narrow, primary });

    f.editor.alignSelection(AlignKind::HorizontalCenter);
    EXPECT_FLOAT_EQ(narrow->bounds().left(), 100.0f + 30.0f - 10.0f);   // centres line up
    f.editor.alignSelection(AlignKind::Right);
    EXPECT_FLOAT_EQ(narrow->bounds().right(), 160.0f);
    f.editor.alignSelection(AlignKind::Bottom);
    EXPECT_FLOAT_EQ(narrow->bounds().bottom(), 140.0f);
    f.editor.alignSelection(AlignKind::VerticalMiddle);
    EXPECT_FLOAT_EQ(narrow->bounds().top() + 5.0f, 120.0f);
}

TEST(DesignerEditorArrange, AlignmentWorksAcrossContainersByComparingRootPositions)
{
    ArrangeFixture f;
    auto* inner = new newui::SubView();
    inner->setVisible(true);
    inner->setBounds(newui::Rect(100, 100, 200, 150));
    inner->setLayout(std::make_unique<newui::AnchorLayout>());
    f.free->addChild(inner);
    auto* outerButton = f.button(f.free, 10, 10);
    auto* innerButton = f.button(inner, 5, 5);
    f.editor.viewDesignerController().setSelection({ outerButton, innerButton });

    // innerButton sits at root x = box x + inner x + 5; aligning outerButton to it moves outerButton
    // to that same on-screen x, whatever its own parent-local number becomes.
    ASSERT_TRUE(f.editor.alignSelection(AlignKind::Left));
    newui::Rect a = SelectionOverlay::boundsInRootView(outerButton);
    newui::Rect b = SelectionOverlay::boundsInRootView(innerButton);
    EXPECT_FLOAT_EQ(a.left(), b.left());
}

TEST(DesignerEditorArrange, ControlsInAFlexParentAreLeftAloneAndNeedingTwoFreeOnesIsEnforced)
{
    ArrangeFixture f;
    auto* freeButton = f.button(f.free, 10, 10);
    auto* flexButton = f.button(f.flex, 0, 0);
    const newui::Rect flexBefore = flexButton->bounds();
    f.editor.viewDesignerController().setSelection({ flexButton, freeButton });

    // Only one of the two has geometry of its own: nothing to align.
    EXPECT_FALSE(f.editor.alignSelection(AlignKind::Left));
    EXPECT_FALSE(f.editor.matchSizeSelection(MatchSizeKind::Both));
    EXPECT_EQ(flexButton->bounds().left(), flexBefore.left());
    EXPECT_FALSE(hasFreeGeometry(flexButton));
    EXPECT_TRUE(hasFreeGeometry(freeButton));
}

TEST(DesignerArrange, DistributeSpacesTheMiddleViewsEvenlyBetweenTheOutermost)
{
    ArrangeFixture f;
    auto* first = f.button(f.free, 0, 0, 20, 10);
    auto* middle = f.button(f.free, 30, 0, 20, 10);   // gaps 10 and 60: unequal
    auto* last = f.button(f.free, 100, 0, 20, 10);

    auto changes = computeDistribute({ last, first, middle }, DistributeKind::Horizontal);   // order given doesn't matter

    ASSERT_EQ(changes.size(), 1u);
    EXPECT_EQ(changes[0].view, middle);
    // span 0..120, widths total 60, so two gaps of 30: the middle view starts at 20 + 30 = 50.
    EXPECT_FLOAT_EQ(changes[0].after.left(), 50.0f);
    EXPECT_TRUE(computeDistribute({ first, last }, DistributeKind::Horizontal).empty());   // needs three
}

TEST(DesignerEditorArrange, MatchSizeCopiesThePrimarysWidthAndOrHeight)
{
    ArrangeFixture f;
    auto* a = f.button(f.free, 10, 10, 30, 15);
    auto* primary = f.button(f.free, 100, 100, 80, 40);
    f.editor.viewDesignerController().setSelection({ a, primary });

    ASSERT_TRUE(f.editor.matchSizeSelection(MatchSizeKind::Width));
    EXPECT_FLOAT_EQ(a->bounds().width(), 80.0f);
    EXPECT_FLOAT_EQ(a->bounds().height(), 15.0f);   // height untouched
    EXPECT_FLOAT_EQ(a->bounds().left(), 10.0f);     // top-left stays
    ASSERT_TRUE(f.editor.matchSizeSelection(MatchSizeKind::Height));
    EXPECT_FLOAT_EQ(a->bounds().height(), 40.0f);
    EXPECT_FALSE(f.editor.matchSizeSelection(MatchSizeKind::Both));   // already the same size

    f.editor.undoStack().undo();
    EXPECT_FLOAT_EQ(a->bounds().height(), 15.0f);
}
