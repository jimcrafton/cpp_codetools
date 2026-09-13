#include "../extension/NativeEditControls/GradientEditorDialog.h"

#include <gtest/gtest.h>

// Exercises GradientEditorDialog's real public API - setGradient()/setKind()/selectStop()/
// setSelectedStopColor()/gradient() and the real child controls (kindControl()/stopHexFields())
// it builds - never calling the inherited showModal() (which blocks on a real modal message loop,
// per this project's own convention of never simulating raw input or driving a real message pump
// in a unit test). Constructing a GradientEditorDialog/adding content to its rootView() needs no
// real native window at all - newui::Dialog only lazily creates one on show()/showModal() (see
// dialogs.h's own class comment), so this is safe and fast, same as any other newui tree-building
// test in this codebase (e.g. test_workspace.cpp).
namespace
{
    newui::gfx::Gradient makeTwoStopLinearGradient()
    {
        newui::gfx::Gradient gradient;
        gradient.setKind(newui::gfx::GradientKind::Linear);
        gradient.stops().push_back(newui::gfx::GradientStop(0.0f, newui::Color(255, 0, 0)));
        gradient.stops().push_back(newui::gfx::GradientStop(1.0f, newui::Color(0, 0, 255)));
        return gradient;
    }
}

TEST(GradientEditorDialogTest, SetGradientSeedsGradientBackVerbatim)
{
    CodeToolsVsix::GradientEditorDialog dialog;
    newui::gfx::Gradient seed = makeTwoStopLinearGradient();

    dialog.setGradient(seed);

    EXPECT_EQ(dialog.gradient().kind(), newui::gfx::GradientKind::Linear);
    ASSERT_EQ(dialog.gradient().stops().size(), 2u);
    EXPECT_EQ(dialog.gradient().stops()[0].color().toString(), seed.stops()[0].color().toString());
    EXPECT_EQ(dialog.gradient().stops()[1].color().toString(), seed.stops()[1].color().toString());
}

TEST(GradientEditorDialogTest, SetGradientBuildsOneHexFieldPerStop)
{
    CodeToolsVsix::GradientEditorDialog dialog;
    dialog.setGradient(makeTwoStopLinearGradient());

    EXPECT_EQ(dialog.stopHexFields().size(), 2u);
}

TEST(GradientEditorDialogTest, SetKindUpdatesTheWorkingGradient)
{
    CodeToolsVsix::GradientEditorDialog dialog;
    dialog.setGradient(makeTwoStopLinearGradient());

    dialog.setKind(newui::gfx::GradientKind::Radial);

    EXPECT_EQ(dialog.gradient().kind(), newui::gfx::GradientKind::Radial);
}

// Drives the actual click path (SegmentedControl::onMouseDown -> segmentAt() hit-testing ->
// setSelectedIndex()) rather than calling setSelectedIndex() directly - the earlier tests in this
// file only ever proved the state-update logic works, never that a real click on the rendered
// control reaches it at all. That gap is exactly why a real, reported "the tab buttons don't work
// at all" bug shipped past this suite - this test exists to close it.
TEST(GradientEditorDialogTest, ClickingTheSegmentedControlActuallyChangesKind)
{
    CodeToolsVsix::GradientEditorDialog dialog;
    dialog.setGradient(makeTwoStopLinearGradient());
    ASSERT_NE(dialog.kindControl(), nullptr);
    ASSERT_EQ(dialog.gradient().kind(), newui::gfx::GradientKind::Linear);

    newui::Size natural = dialog.kindControl()->naturalSize();
    ASSERT_GT(natural.width, 0.0f) << "test assumption: the control has a real, nonzero size to click within";

    // Near the right edge - lands in the last segment ("Point"), unambiguously not the
    // already-selected "Linear" (index 0).
    newui::SyncReturn result = dialog.kindControl()->onMouseDown.syncCallFirst(*dialog.kindControl(),
        newui::Point(natural.width - 2.0f, natural.height * 0.5f), 0, 0);

    EXPECT_EQ(result, newui::SyncReturn::Handled);
    EXPECT_EQ(dialog.gradient().kind(), newui::gfx::GradientKind::Point);
}

// Real, caught-before-shipping bug: the first draft rebuilt kindControl_ itself (along with the
// stop rows) every time the kind changed - since that rebuild runs *from inside* kindControl_'s
// own onSelectionChanged handler (a real click), it deleted the very SegmentedControl whose click
// handling was still executing higher up the call stack. This drives that exact real path (a real
// call on the real child control, not synthetic mouse/keyboard input - same convention
// PropertiesGrid's own live-editor tests already use) rather than just calling setKind()
// directly, so a regression here would actually be caught.
TEST(GradientEditorDialogTest, ChangingKindViaTheRealSegmentedControlDoesNotCrash)
{
    CodeToolsVsix::GradientEditorDialog dialog;
    dialog.setGradient(makeTwoStopLinearGradient());
    ASSERT_NE(dialog.kindControl(), nullptr);

    dialog.kindControl()->setSelectedIndex(static_cast<std::size_t>(newui::gfx::GradientKind::Point));

    EXPECT_EQ(dialog.gradient().kind(), newui::gfx::GradientKind::Point);
    // Point isn't built yet (Phase 4) - no stop hex fields while it's selected.
    EXPECT_TRUE(dialog.stopHexFields().empty());
}

TEST(GradientEditorDialogTest, SwitchingBackFromPointRebuildsTheStopFields)
{
    CodeToolsVsix::GradientEditorDialog dialog;
    dialog.setGradient(makeTwoStopLinearGradient());

    dialog.kindControl()->setSelectedIndex(static_cast<std::size_t>(newui::gfx::GradientKind::Point));
    dialog.kindControl()->setSelectedIndex(static_cast<std::size_t>(newui::gfx::GradientKind::Linear));

    EXPECT_EQ(dialog.gradient().kind(), newui::gfx::GradientKind::Linear);
    EXPECT_EQ(dialog.stopHexFields().size(), 2u);
}

TEST(GradientEditorDialogTest, SelectStopClampsToTheLastValidIndex)
{
    CodeToolsVsix::GradientEditorDialog dialog;
    dialog.setGradient(makeTwoStopLinearGradient());

    dialog.selectStop(5);

    EXPECT_EQ(dialog.selectedStopIndex(), 1u);
}

TEST(GradientEditorDialogTest, SetSelectedStopColorMutatesOnlyThatStop)
{
    CodeToolsVsix::GradientEditorDialog dialog;
    dialog.setGradient(makeTwoStopLinearGradient());

    dialog.selectStop(1);
    dialog.setSelectedStopColor(newui::Color(0, 255, 0));

    EXPECT_EQ(dialog.gradient().stops()[1].color().toString(), newui::Color(0, 255, 0).toString());
    EXPECT_EQ(dialog.gradient().stops()[0].color().toString(), newui::Color(255, 0, 0).toString());
}
