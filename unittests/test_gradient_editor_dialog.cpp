#include "../extension/NativeEditControls/GradientEditorDialog.h"
#include "../extension/NativeEditControls/TextEncoding.h"

#include <gtest/gtest.h>

// Exercises GradientEditorDialog's real public API - setGradient()/setKind()/selectStop()/
// setSelectedStopColor()/gradient() and the real child controls (kindControl()/stopTrack()/
// colorPicker()/hexField()) it builds - never calling the inherited showModal() (which blocks on
// a real modal message loop,
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

// Phase 3 - the shared "selected stop" editor (colorPicker()/hexField()) replaced the Phase 1
// per-stop hex-field rows this test originally covered - it now targets whichever stop is
// selected (stop 0 right after a fresh seed) instead of building one field per stop.
TEST(GradientEditorDialogTest, SetGradientSeedsTheSharedEditorWithTheFirstStopsColor)
{
    CodeToolsVsix::GradientEditorDialog dialog;
    newui::gfx::Gradient seed = makeTwoStopLinearGradient();

    dialog.setGradient(seed);

    ASSERT_NE(dialog.colorPicker(), nullptr);
    EXPECT_EQ(dialog.colorPicker()->color().toString(), seed.stops()[0].color().toString());
    ASSERT_NE(dialog.hexField(), nullptr);
    EXPECT_EQ(dialog.hexField()->text(), CodeToolsVsix::utf8ToWide(seed.stops()[0].color().toString()));
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
    // Point isn't built yet (Phase 4) - the shared stop editor is hidden while it's selected.
    EXPECT_FALSE(dialog.selectedStopEditor()->isVisible());
}

TEST(GradientEditorDialogTest, SwitchingBackFromPointRestoresTheSelectedStopEditor)
{
    CodeToolsVsix::GradientEditorDialog dialog;
    dialog.setGradient(makeTwoStopLinearGradient());

    dialog.kindControl()->setSelectedIndex(static_cast<std::size_t>(newui::gfx::GradientKind::Point));
    dialog.kindControl()->setSelectedIndex(static_cast<std::size_t>(newui::gfx::GradientKind::Linear));

    EXPECT_EQ(dialog.gradient().kind(), newui::gfx::GradientKind::Linear);
    EXPECT_TRUE(dialog.selectedStopEditor()->isVisible());
    EXPECT_EQ(dialog.colorPicker()->color().toString(), dialog.gradient().stops()[0].color().toString());
}

TEST(GradientEditorDialogTest, SelectStopClampsToTheLastValidIndex)
{
    CodeToolsVsix::GradientEditorDialog dialog;
    dialog.setGradient(makeTwoStopLinearGradient());

    dialog.selectStop(5);

    EXPECT_EQ(dialog.selectedStopIndex(), 1u);
}

// Phase 3 - the shared editor retargets to whichever stop is selected (colorPicker()/hexField()
// no longer have a fixed one-per-stop identity the way the Phase 1 rows did).
TEST(GradientEditorDialogTest, SelectingAStopRetargetsTheSharedEditor)
{
    CodeToolsVsix::GradientEditorDialog dialog;
    newui::gfx::Gradient seed = makeTwoStopLinearGradient();
    dialog.setGradient(seed);

    dialog.selectStop(1);

    EXPECT_EQ(dialog.colorPicker()->color().toString(), seed.stops()[1].color().toString());
    EXPECT_EQ(dialog.hexField()->text(), CodeToolsVsix::utf8ToWide(seed.stops()[1].color().toString()));
}

// Drives the real ColorPicker's own public setColor() (not a synthetic drag - ColorPicker's own
// drag mechanics are already covered by test_color_picker.cpp; this proves the *wiring* between
// the two classes, via colorPicker_->onColorChanged -> setSelectedStopColor()).
TEST(GradientEditorDialogTest, DrivingTheRealColorPickerCommitsTheSelectedStopsColor)
{
    CodeToolsVsix::GradientEditorDialog dialog;
    dialog.setGradient(makeTwoStopLinearGradient());
    dialog.selectStop(1);

    dialog.colorPicker()->setColor(newui::Color(0, 255, 0));

    EXPECT_EQ(dialog.gradient().stops()[1].color().toString(), newui::Color(0, 255, 0).toString());
    EXPECT_EQ(dialog.gradient().stops()[0].color().toString(), newui::Color(255, 0, 0).toString());
    // The hex field stays in sync with the picker's own commit too.
    EXPECT_EQ(dialog.hexField()->text(), CodeToolsVsix::utf8ToWide(newui::Color(0, 255, 0).toString()));
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

// Phase 2 - live preview + draggable stop track (StopTrack, GradientEditorDialog.cpp anonymous
// namespace). setSelectedStopOffset() is the real method a track drag calls - mirrors
// SetSelectedStopColorMutatesOnlyThatStop above for position instead of color.
TEST(GradientEditorDialogTest, SetSelectedStopOffsetClampsAndMutatesOnlyThatStop)
{
    CodeToolsVsix::GradientEditorDialog dialog;
    dialog.setGradient(makeTwoStopLinearGradient());

    dialog.selectStop(1);
    dialog.setSelectedStopOffset(1.5f);

    EXPECT_FLOAT_EQ(dialog.gradient().stops()[1].offset(), 1.0f);
    EXPECT_FLOAT_EQ(dialog.gradient().stops()[0].offset(), 0.0f);
}

TEST(GradientEditorDialogTest, AddStopAtInterpolatesColorBetweenBracketingStops)
{
    CodeToolsVsix::GradientEditorDialog dialog;
    dialog.setGradient(makeTwoStopLinearGradient());

    dialog.addStopAt(0.25f);

    ASSERT_EQ(dialog.gradient().stops().size(), 3u);
    EXPECT_EQ(dialog.selectedStopIndex(), 2u);
    EXPECT_NEAR(dialog.gradient().stops()[2].offset(), 0.25f, 0.001f);
    // 25% of the way from red (255,0,0) to blue (0,0,255).
    newui::Color quarter = dialog.gradient().stops()[2].color();
    EXPECT_NEAR(quarter.r, 191.25f, 0.1f);
    EXPECT_NEAR(quarter.b, 63.75f, 0.1f);
}

TEST(GradientEditorDialogTest, AddStopAtBeyondEveryStopUsesTheNearestEndpointsColorVerbatim)
{
    CodeToolsVsix::GradientEditorDialog dialog;
    dialog.setGradient(makeTwoStopLinearGradient());

    dialog.addStopAt(1.0f);

    ASSERT_EQ(dialog.gradient().stops().size(), 3u);
    EXPECT_EQ(dialog.gradient().stops()[2].color().toString(), dialog.gradient().stops()[1].color().toString());
}

TEST(GradientEditorDialogTest, DeleteSelectedStopRemovesItAndSelectsTheFirstStop)
{
    CodeToolsVsix::GradientEditorDialog dialog;
    dialog.setGradient(makeTwoStopLinearGradient());
    dialog.addStopAt(0.5f);
    ASSERT_EQ(dialog.gradient().stops().size(), 3u);
    dialog.selectStop(2);

    dialog.deleteSelectedStop();

    EXPECT_EQ(dialog.gradient().stops().size(), 2u);
    EXPECT_EQ(dialog.selectedStopIndex(), 0u);
}

// Matches the mockup's own `state.stops.length <= 2` guard - a gradient needs at least 2 stops
// to mean anything, so deletion refuses to go below that.
TEST(GradientEditorDialogTest, DeleteSelectedStopIsANoOpAtTheMinimumOfTwoStops)
{
    CodeToolsVsix::GradientEditorDialog dialog;
    dialog.setGradient(makeTwoStopLinearGradient());

    dialog.deleteSelectedStop();

    EXPECT_EQ(dialog.gradient().stops().size(), 2u);
}

TEST(GradientEditorDialogTest, DeleteStopButtonIsDisabledAtTheMinimumAndEnabledAboveIt)
{
    CodeToolsVsix::GradientEditorDialog dialog;
    dialog.setGradient(makeTwoStopLinearGradient());
    ASSERT_NE(dialog.deleteStopButton(), nullptr);
    EXPECT_FALSE(dialog.deleteStopButton()->isEnabled());

    dialog.addStopAt(0.5f);
    EXPECT_TRUE(dialog.deleteStopButton()->isEnabled());
}

// Drives the real button click (not deleteSelectedStop() directly) - proves the wiring, matching
// this file's own "drive the real method a click would" convention for every other control here.
TEST(GradientEditorDialogTest, ClickingTheRealDeleteButtonRemovesTheSelectedStop)
{
    CodeToolsVsix::GradientEditorDialog dialog;
    dialog.setGradient(makeTwoStopLinearGradient());
    dialog.addStopAt(0.5f);
    dialog.selectStop(2);

    dialog.deleteStopButton()->onClick(*dialog.deleteStopButton());

    EXPECT_EQ(dialog.gradient().stops().size(), 2u);
}

TEST(GradientEditorDialogTest, PreviewAndTrackAreHiddenForPointKindAndShownOtherwise)
{
    CodeToolsVsix::GradientEditorDialog dialog;
    dialog.setGradient(makeTwoStopLinearGradient());
    ASSERT_TRUE(dialog.previewBox()->isVisible());
    ASSERT_TRUE(dialog.stopTrack()->isVisible());
    ASSERT_TRUE(dialog.selectedStopEditor()->isVisible());

    dialog.setKind(newui::gfx::GradientKind::Point);
    EXPECT_FALSE(dialog.previewBox()->isVisible());
    EXPECT_FALSE(dialog.stopTrack()->isVisible());
    EXPECT_FALSE(dialog.selectedStopEditor()->isVisible());

    dialog.setKind(newui::gfx::GradientKind::Linear);
    EXPECT_TRUE(dialog.previewBox()->isVisible());
    EXPECT_TRUE(dialog.stopTrack()->isVisible());
    EXPECT_TRUE(dialog.selectedStopEditor()->isVisible());
}

// Drives the real track's onMouseDown/onMouseMove/onMouseUp path (same convention
// ClickingTheSegmentedControlActuallyChangesKind above and test_splitter.cpp's own drag tests
// already use: setBounds() directly to give a headless widget real dimensions, then fire real
// mouse events on it) rather than calling setSelectedStopOffset() directly - proves an actual
// drag gesture reaches it, not just the underlying state-update logic.
TEST(GradientEditorDialogTest, DraggingTheRealStopTrackHandleRepositionsTheSelectedStop)
{
    CodeToolsVsix::GradientEditorDialog dialog;
    dialog.setGradient(makeTwoStopLinearGradient());
    newui::SubView* track = dialog.stopTrack();
    ASSERT_NE(track, nullptr);
    track->setBounds(newui::Rect(0.0f, 0.0f, 300.0f, 28.0f));

    // Stop 0 sits at offset 0.0 - its handle is at track-local x = 0.
    newui::SyncReturn downResult = track->onMouseDown.syncCallFirst(*track, newui::Point(0.0f, 14.0f), 0, 0);
    EXPECT_EQ(downResult, newui::SyncReturn::Handled);
    EXPECT_EQ(dialog.selectedStopIndex(), 0u);

    newui::SyncReturn moveResult = track->onMouseMove.syncCallFirst(*track, newui::Point(150.0f, 14.0f), 0, 0);
    EXPECT_EQ(moveResult, newui::SyncReturn::Handled);
    EXPECT_NEAR(dialog.gradient().stops()[0].offset(), 0.5f, 0.01f);

    newui::SyncReturn upResult = track->onMouseUp.syncCallFirst(*track, newui::Point(150.0f, 14.0f), 0, 0);
    EXPECT_EQ(upResult, newui::SyncReturn::Handled);

    // No longer dragging once mouseUp fired - further movement is ignored and leaves the stop
    // exactly where mouseUp left it.
    newui::SyncReturn moveAfterUp = track->onMouseMove.syncCallFirst(*track, newui::Point(300.0f, 14.0f), 0, 0);
    EXPECT_EQ(moveAfterUp, newui::SyncReturn::Ignored);
    EXPECT_NEAR(dialog.gradient().stops()[0].offset(), 0.5f, 0.01f);
}

// A real click on the track away from every existing handle now inserts a new stop there
// (matching the mockup's own track click behavior) rather than being ignored.
TEST(GradientEditorDialogTest, ClickingTheTrackAwayFromEveryHandleInsertsANewStop)
{
    CodeToolsVsix::GradientEditorDialog dialog;
    dialog.setGradient(makeTwoStopLinearGradient());
    newui::SubView* track = dialog.stopTrack();
    track->setBounds(newui::Rect(0.0f, 0.0f, 300.0f, 28.0f));

    // Stops sit at track-local x = 0 (red) and x = 300 (blue) - the midpoint (offset 0.5) is far
    // from either handle's own hit radius.
    newui::SyncReturn result = track->onMouseDown.syncCallFirst(*track, newui::Point(150.0f, 14.0f), 0, 0);

    EXPECT_EQ(result, newui::SyncReturn::Handled);
    ASSERT_EQ(dialog.gradient().stops().size(), 3u);
    EXPECT_EQ(dialog.selectedStopIndex(), 2u);
    EXPECT_NEAR(dialog.gradient().stops()[2].offset(), 0.5f, 0.01f);
    // Interpolated halfway between red (255,0,0) and blue (0,0,255) - makeTwoStopLinearGradient()
    // constructs these via Color's raw (r,g,b,a) constructor, unclamped to [0,1], so the halfway
    // point is 127.5, not 0.5.
    newui::Color midpoint = dialog.gradient().stops()[2].color();
    EXPECT_NEAR(midpoint.r, 127.5f, 0.1f);
    EXPECT_NEAR(midpoint.b, 127.5f, 0.1f);
}
