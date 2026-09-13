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

    // Positions fall inside GradientEditorDialog's own default shapeBounds() ({0,0,200,140}) -
    // real tests below either use that default directly or set it explicitly to something
    // matching previewBox()'s own test bounds for exact, checkable screen<->shape math.
    newui::gfx::Gradient makeTwoPointGradient()
    {
        newui::gfx::Gradient gradient;
        gradient.setKind(newui::gfx::GradientKind::Point);
        gradient.points().push_back(newui::gfx::GradientPoint(newui::Point(20.0f, 20.0f), newui::Color(255, 0, 0)));
        gradient.points().push_back(newui::gfx::GradientPoint(newui::Point(180.0f, 120.0f), newui::Color(0, 0, 255)));
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
    // Point has no 1D stop track at all (2D points are edited directly in the preview instead) -
    // the shared item editor itself stays visible (now retargeted at the selected point).
    EXPECT_FALSE(dialog.stopTrack()->isVisible());
    EXPECT_TRUE(dialog.selectedItemEditor()->isVisible());
}

TEST(GradientEditorDialogTest, SwitchingBackFromPointRestoresTheSelectedStopEditor)
{
    CodeToolsVsix::GradientEditorDialog dialog;
    dialog.setGradient(makeTwoStopLinearGradient());

    dialog.kindControl()->setSelectedIndex(static_cast<std::size_t>(newui::gfx::GradientKind::Point));
    dialog.kindControl()->setSelectedIndex(static_cast<std::size_t>(newui::gfx::GradientKind::Linear));

    EXPECT_EQ(dialog.gradient().kind(), newui::gfx::GradientKind::Linear);
    EXPECT_TRUE(dialog.stopTrack()->isVisible());
    EXPECT_TRUE(dialog.selectedItemEditor()->isVisible());
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
    ASSERT_NE(dialog.deleteItemButton(), nullptr);
    EXPECT_FALSE(dialog.deleteItemButton()->isEnabled());

    dialog.addStopAt(0.5f);
    EXPECT_TRUE(dialog.deleteItemButton()->isEnabled());
}

// Drives the real button click (not deleteSelectedStop() directly) - proves the wiring, matching
// this file's own "drive the real method a click would" convention for every other control here.
TEST(GradientEditorDialogTest, ClickingTheRealDeleteButtonRemovesTheSelectedStop)
{
    CodeToolsVsix::GradientEditorDialog dialog;
    dialog.setGradient(makeTwoStopLinearGradient());
    dialog.addStopAt(0.5f);
    dialog.selectStop(2);

    dialog.deleteItemButton()->onClick(*dialog.deleteItemButton());

    EXPECT_EQ(dialog.gradient().stops().size(), 2u);
}

// track_ alone toggles by kind now - Point has no 1D stop position at all (its points are edited
// directly in previewBox_ instead); previewBox_/selectedItemEditor_ stay visible for every kind.
TEST(GradientEditorDialogTest, TrackIsHiddenForPointKindAndShownOtherwise)
{
    CodeToolsVsix::GradientEditorDialog dialog;
    dialog.setGradient(makeTwoStopLinearGradient());
    ASSERT_TRUE(dialog.previewBox()->isVisible());
    ASSERT_TRUE(dialog.stopTrack()->isVisible());
    ASSERT_TRUE(dialog.selectedItemEditor()->isVisible());

    dialog.setKind(newui::gfx::GradientKind::Point);
    EXPECT_TRUE(dialog.previewBox()->isVisible());
    EXPECT_FALSE(dialog.stopTrack()->isVisible());
    EXPECT_TRUE(dialog.selectedItemEditor()->isVisible());

    dialog.setKind(newui::gfx::GradientKind::Linear);
    EXPECT_TRUE(dialog.previewBox()->isVisible());
    EXPECT_TRUE(dialog.stopTrack()->isVisible());
    EXPECT_TRUE(dialog.selectedItemEditor()->isVisible());
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

// Phase 4 - GradientKind::Point. Unlike stops, a point's own position() *is* the real committed
// data (see GradientEditorDialog.h's own header comment) - these tests exercise the real public
// API directly; the PreviewBox drag tests further below drive the actual mouse path.

TEST(GradientEditorDialogTest, SwitchingToPointSeedsTwoDefaultPoints)
{
    CodeToolsVsix::GradientEditorDialog dialog;
    dialog.setGradient(makeTwoStopLinearGradient());

    dialog.setKind(newui::gfx::GradientKind::Point);

    EXPECT_EQ(dialog.gradient().points().size(), 2u);
}

// Real, live-debugged bug (found via a real breakpoint in PreviewBox::paint(), not guessed): a
// real View's own Point-kind Gradient arrived at this dialog with pointBlendPower()/
// pointRasterMax() already at 0 (not their real C++ class defaults, 2.0f/64) and 2 real points
// already present - so the old "seed points if empty" guard alone never touched these two scalar
// fields at all. A pointRasterMax() of 0 collapses rasterizePoints()'s own baked raster to a
// degenerate ~1x1 image with an undefined pattern transform - exactly why the live preview showed
// nothing but checkerboard, no visible blend at all. Reproduces that exact degenerate state
// directly (not relying on whatever upstream process produced it live) and proves setGradient()
// normalizes it.
TEST(GradientEditorDialogTest, SetGradientNormalizesDegeneratePointBlendSettings)
{
    newui::gfx::Gradient degenerate = makeTwoPointGradient();
    degenerate.setPointBlendPower(0.0f);
    degenerate.setPointRasterMax(0);

    CodeToolsVsix::GradientEditorDialog dialog;
    dialog.setGradient(degenerate);

    EXPECT_GT(dialog.gradient().pointBlendPower(), 0.0f);
    EXPECT_GT(dialog.gradient().pointRasterMax(), 0);
    // The real points themselves are untouched - only the degenerate scalars are normalized.
    ASSERT_EQ(dialog.gradient().points().size(), 2u);
    EXPECT_EQ(dialog.gradient().points()[0].color().toString(), degenerate.points()[0].color().toString());
}

TEST(GradientEditorDialogTest, SelectPointClampsToTheLastValidIndex)
{
    CodeToolsVsix::GradientEditorDialog dialog;
    dialog.setGradient(makeTwoPointGradient());

    dialog.selectPoint(5);

    EXPECT_EQ(dialog.selectedPointIndex(), 1u);
}

TEST(GradientEditorDialogTest, SetSelectedPointColorMutatesOnlyThatPoint)
{
    CodeToolsVsix::GradientEditorDialog dialog;
    dialog.setGradient(makeTwoPointGradient());

    dialog.selectPoint(1);
    dialog.setSelectedPointColor(newui::Color(0, 255, 0));

    EXPECT_EQ(dialog.gradient().points()[1].color().toString(), newui::Color(0, 255, 0).toString());
    EXPECT_EQ(dialog.gradient().points()[0].color().toString(), newui::Color(255, 0, 0).toString());
}

TEST(GradientEditorDialogTest, SetSelectedPointPositionMovesOnlyThatPoint)
{
    CodeToolsVsix::GradientEditorDialog dialog;
    dialog.setGradient(makeTwoPointGradient());

    dialog.selectPoint(0);
    dialog.setSelectedPointPosition(newui::Point(55.0f, 66.0f));

    EXPECT_FLOAT_EQ(dialog.gradient().points()[0].position().x, 55.0f);
    EXPECT_FLOAT_EQ(dialog.gradient().points()[0].position().y, 66.0f);
    EXPECT_FLOAT_EQ(dialog.gradient().points()[1].position().x, 180.0f);
}

TEST(GradientEditorDialogTest, AddPointAtSeedsColorFromTheNearestExistingPoint)
{
    CodeToolsVsix::GradientEditorDialog dialog;
    dialog.setGradient(makeTwoPointGradient());

    // Closer to point 0 (20,20) than point 1 (180,120).
    dialog.addPointAt(newui::Point(30.0f, 30.0f));

    ASSERT_EQ(dialog.gradient().points().size(), 3u);
    EXPECT_EQ(dialog.selectedPointIndex(), 2u);
    EXPECT_EQ(dialog.gradient().points()[2].color().toString(), newui::Color(255, 0, 0).toString());
}

TEST(GradientEditorDialogTest, DeleteSelectedPointRemovesItAndSelectsTheFirstPoint)
{
    CodeToolsVsix::GradientEditorDialog dialog;
    dialog.setGradient(makeTwoPointGradient());
    dialog.selectPoint(1);

    dialog.deleteSelectedPoint();

    EXPECT_EQ(dialog.gradient().points().size(), 1u);
    EXPECT_EQ(dialog.selectedPointIndex(), 0u);
}

// A single point still renders something real (Gradient::rasterizePoints() blends however many
// there are) - unlike stops, which need at least 2, so 1 is the real floor here.
TEST(GradientEditorDialogTest, DeleteSelectedPointIsANoOpAtTheMinimumOfOnePoint)
{
    CodeToolsVsix::GradientEditorDialog dialog;
    dialog.setGradient(makeTwoPointGradient());
    dialog.deleteSelectedPoint();
    ASSERT_EQ(dialog.gradient().points().size(), 1u);

    dialog.deleteSelectedPoint();

    EXPECT_EQ(dialog.gradient().points().size(), 1u);
}

TEST(GradientEditorDialogTest, SelectingAPointRetargetsTheSharedEditor)
{
    CodeToolsVsix::GradientEditorDialog dialog;
    newui::gfx::Gradient seed = makeTwoPointGradient();
    dialog.setGradient(seed);

    dialog.selectPoint(1);

    EXPECT_EQ(dialog.colorPicker()->color().toString(), seed.points()[1].color().toString());
    EXPECT_EQ(dialog.hexField()->text(), CodeToolsVsix::utf8ToWide(seed.points()[1].color().toString()));
}

// Proves the wiring (colorPicker_->onColorChanged branches to setSelectedPointColor() when
// working_.kind() == Point) - ColorPicker's own drag mechanics are already covered by
// test_color_picker.cpp.
TEST(GradientEditorDialogTest, DrivingTheRealColorPickerCommitsTheSelectedPointsColor)
{
    CodeToolsVsix::GradientEditorDialog dialog;
    dialog.setGradient(makeTwoPointGradient());
    dialog.selectPoint(1);

    dialog.colorPicker()->setColor(newui::Color(0, 255, 0));

    EXPECT_EQ(dialog.gradient().points()[1].color().toString(), newui::Color(0, 255, 0).toString());
    EXPECT_EQ(dialog.gradient().points()[0].color().toString(), newui::Color(255, 0, 0).toString());
}

TEST(GradientEditorDialogTest, DeleteItemButtonThresholdIsOneForPointsNotTwo)
{
    CodeToolsVsix::GradientEditorDialog dialog;
    dialog.setGradient(makeTwoPointGradient());
    dialog.selectPoint(1);
    dialog.deleteSelectedPoint();
    ASSERT_EQ(dialog.gradient().points().size(), 1u);

    // Only 1 point left - disabled, unlike stops' own floor of 2.
    EXPECT_FALSE(dialog.deleteItemButton()->isEnabled());
}

TEST(GradientEditorDialogTest, PreviewBoxIgnoresMouseForNonPointKinds)
{
    CodeToolsVsix::GradientEditorDialog dialog;
    dialog.setGradient(makeTwoStopLinearGradient());
    newui::SubView* preview = dialog.previewBox();
    ASSERT_NE(preview, nullptr);
    preview->setBounds(newui::Rect(0.0f, 0.0f, 300.0f, 96.0f));

    newui::SyncReturn result = preview->onMouseDown.syncCallFirst(*preview, newui::Point(50.0f, 50.0f), 0, 0);

    EXPECT_EQ(result, newui::SyncReturn::Ignored);
}

// Drives the real previewBox()'s own onMouseDown/onMouseMove/onMouseUp path (same setBounds()-
// first convention every other drag test in this file uses) - previewBox()'s own bounds are set
// equal to shapeBounds() here so screen<->shape mapping is 1:1, keeping the expected numbers exact.
TEST(GradientEditorDialogTest, DraggingTheRealPreviewBoxHandleRepositionsTheSelectedPoint)
{
    CodeToolsVsix::GradientEditorDialog dialog;
    dialog.setShapeBounds(newui::Rect(0.0f, 0.0f, 200.0f, 140.0f));
    dialog.setGradient(makeTwoPointGradient());
    newui::SubView* preview = dialog.previewBox();
    ASSERT_NE(preview, nullptr);
    preview->setBounds(newui::Rect(0.0f, 0.0f, 200.0f, 140.0f));

    // Point 0 sits at shape-space (20,20) - identical on screen since previewBox()'s bounds match
    // shapeBounds() exactly here.
    newui::SyncReturn downResult = preview->onMouseDown.syncCallFirst(*preview, newui::Point(20.0f, 20.0f), 0, 0);
    EXPECT_EQ(downResult, newui::SyncReturn::Handled);
    EXPECT_EQ(dialog.selectedPointIndex(), 0u);

    newui::SyncReturn moveResult = preview->onMouseMove.syncCallFirst(*preview, newui::Point(100.0f, 70.0f), 0, 0);
    EXPECT_EQ(moveResult, newui::SyncReturn::Handled);
    EXPECT_NEAR(dialog.gradient().points()[0].position().x, 100.0f, 0.01f);
    EXPECT_NEAR(dialog.gradient().points()[0].position().y, 70.0f, 0.01f);

    newui::SyncReturn upResult = preview->onMouseUp.syncCallFirst(*preview, newui::Point(100.0f, 70.0f), 0, 0);
    EXPECT_EQ(upResult, newui::SyncReturn::Handled);

    // No longer dragging once mouseUp fired.
    newui::SyncReturn moveAfterUp = preview->onMouseMove.syncCallFirst(*preview, newui::Point(0.0f, 0.0f), 0, 0);
    EXPECT_EQ(moveAfterUp, newui::SyncReturn::Ignored);
    EXPECT_NEAR(dialog.gradient().points()[0].position().x, 100.0f, 0.01f);
}

TEST(GradientEditorDialogTest, ClickingThePreviewBoxAwayFromEveryHandleAddsANewPoint)
{
    CodeToolsVsix::GradientEditorDialog dialog;
    dialog.setShapeBounds(newui::Rect(0.0f, 0.0f, 200.0f, 140.0f));
    dialog.setGradient(makeTwoPointGradient());
    newui::SubView* preview = dialog.previewBox();
    preview->setBounds(newui::Rect(0.0f, 0.0f, 200.0f, 140.0f));

    // Far from both existing points (20,20)/(180,120) and their hit radius.
    newui::SyncReturn result = preview->onMouseDown.syncCallFirst(*preview, newui::Point(100.0f, 70.0f), 0, 0);

    EXPECT_EQ(result, newui::SyncReturn::Handled);
    ASSERT_EQ(dialog.gradient().points().size(), 3u);
    EXPECT_EQ(dialog.selectedPointIndex(), 2u);
    EXPECT_NEAR(dialog.gradient().points()[2].position().x, 100.0f, 0.01f);
    EXPECT_NEAR(dialog.gradient().points()[2].position().y, 70.0f, 0.01f);
}

// Real, live-reported bug: the preview showed bare checkerboard for Point kind, no visible color
// blend at all. Root cause - newui::Color's own (r,g,b,a) constructor stores raw, unclamped
// floats: Color(255, 255, 255) is *not* white, it's (255.0f, 255.0f, 255.0f, 1.0f), wildly out of
// [0,1] range. That never visibly broke Linear/Radial/Conic stops (blend2d's own per-channel
// stop-color packing clamps independently), but rasterizePoints()'s weighted-average blend
// operates on the raw values directly, with no clamping until the very end - so even a tiny,
// far-away point's weight times 255 could swamp a much larger nearby weight times a
// correctly-scaled color, producing exactly the wrong-colored (or here, all-white) result seen
// live. Fixed in seedDefaultPointsIfEmpty() (real [0,1]-scale Color(1.0f, 1.0f, 1.0f)) - this
// test proves the raster itself now blends correctly near each real point, not just that
// toBLVar()/rasterizePoints() produces *some* non-empty image (which it already did even with the
// bug - the image was real, just filled with the wrong color).
TEST(GradientEditorDialogTest, PointGradientRasterBlendsTowardTheNearestPointsRealColor)
{
    newui::gfx::Gradient g;
    g.setKind(newui::gfx::GradientKind::Point);
    g.points().push_back(newui::gfx::GradientPoint(newui::Point(20.0f, 20.0f), newui::Color(0.0f, 0.0f, 0.0f)));
    g.points().push_back(newui::gfx::GradientPoint(newui::Point(180.0f, 120.0f), newui::Color(1.0f, 1.0f, 1.0f)));

    newui::Rect bounds(0.0f, 0.0f, 336.0f, 96.0f);
    BLVar fill = g.toBLVar(bounds);
    ASSERT_TRUE(fill.is_pattern());
    BLImage img = fill.as<BLPattern>().get_image();
    ASSERT_FALSE(img.is_empty());

    BLImageData data;
    img.get_data(&data);
    const uint8_t* pixels = static_cast<const uint8_t*>(data.pixel_data);
    auto pixelAt = [&](int x, int y) { return pixels + intptr_t(y) * data.stride + intptr_t(x) * 4; };

    // Top-left corner raster pixel is nearest the black point (20,20) - premultiplied BGRA should
    // be near-black, not near-white.
    const uint8_t* nearBlack = pixelAt(0, 0);
    EXPECT_LT(int(nearBlack[0]), 40) << "blue channel too high near the black point";
    EXPECT_LT(int(nearBlack[2]), 40) << "red channel too high near the black point";

    // Bottom-right corner raster pixel is nearest the white point (180,120) - should be near-white.
    // Inverse-distance blending never reaches pure 255 here (the black point is still a finite,
    // if much larger, distance away too) - 190 is comfortably "clearly white-ish", not a demand
    // for perfect purity.
    const uint8_t* nearWhite = pixelAt(int(img.size().w) - 1, int(img.size().h) - 1);
    EXPECT_GT(int(nearWhite[0]), 190) << "blue channel too low near the white point";
    EXPECT_GT(int(nearWhite[2]), 190) << "red channel too low near the white point";
}

// The previous test only inspected rasterizePoints()'s own intermediate BLImage - it never proved
// that ctx.set_fill_style(fill)/ctx.fill_rect() (the exact call PreviewBox::paint() makes) actually
// composites that image onto a real canvas the way it visibly needs to. This one renders through a
// real BLContext bound to a real BLImage, pre-filled with a known sentinel color, and checks that
// color was actually overwritten - the only way to tell whether the fill call itself is a no-op.
TEST(GradientEditorDialogTest, PointGradientFillActuallyPaintsOntoARealCanvas)
{
    newui::gfx::Gradient g;
    g.setKind(newui::gfx::GradientKind::Point);
    g.points().push_back(newui::gfx::GradientPoint(newui::Point(20.0f, 20.0f), newui::Color(0.0f, 0.0f, 0.0f)));
    g.points().push_back(newui::gfx::GradientPoint(newui::Point(180.0f, 120.0f), newui::Color(1.0f, 1.0f, 1.0f)));

    newui::Rect bounds(0.0f, 0.0f, 200.0f, 140.0f);

    BLImage canvas;
    ASSERT_EQ(canvas.create(int(bounds.width()), int(bounds.height()), BL_FORMAT_PRGB32), BL_SUCCESS);

    BLContext ctx(canvas);
    // Sentinel: pure opaque red, nothing else in this test ever produces red.
    ctx.set_fill_style(BLRgba32(255, 0, 0, 255));
    ctx.fill_all();

    BLVar fill = g.toBLVar(bounds);
    ctx.set_fill_style(fill);
    ctx.fill_rect(BLRect(bounds));
    ctx.end();

    BLImageData data;
    canvas.get_data(&data);
    const uint8_t* px = static_cast<const uint8_t*>(data.pixel_data) + intptr_t(70) * data.stride + intptr_t(100) * 4;
    // Center of the canvas - should be some blend of black/white (all channels roughly equal,
    // none of them the sentinel's pure red), if the fill actually painted at all.
    bool stillSentinelRed = (int(px[2]) > 200 && int(px[1]) < 40 && int(px[0]) < 40);
    EXPECT_FALSE(stillSentinelRed) << "fill_rect() left the sentinel red untouched - the Point "
        << "gradient fill never actually painted anything (BGR=" << int(px[0]) << "," << int(px[1]) << "," << int(px[2]) << ")";
}

// Phase 5 - built-in presets (design/reference/gradient_editor_dialog.html's own `presets` array,
// minus each entry's own "angle" - see GradientEditorDialog.h's own header comment on why that's
// dropped).
TEST(GradientEditorDialogTest, ApplyPresetSetsKindToLinearAndReplacesStops)
{
    CodeToolsVsix::GradientEditorDialog dialog;
    dialog.setGradient(makeTwoPointGradient());
    ASSERT_EQ(dialog.gradient().kind(), newui::gfx::GradientKind::Point);

    dialog.applyPreset(0);

    EXPECT_EQ(dialog.gradient().kind(), newui::gfx::GradientKind::Linear);
    ASSERT_EQ(dialog.gradient().stops().size(), 2u);
    newui::Color expected;
    newui::Color::fromString("#5A7CE9", expected);
    EXPECT_EQ(dialog.gradient().stops()[0].color().toString(), expected.toString());
}

TEST(GradientEditorDialogTest, ApplyPresetSelectsTheFirstStopAndRefreshesTheSharedEditor)
{
    CodeToolsVsix::GradientEditorDialog dialog;
    dialog.setGradient(makeTwoStopLinearGradient());

    dialog.applyPreset(1);

    EXPECT_EQ(dialog.selectedStopIndex(), 0u);
    EXPECT_EQ(dialog.colorPicker()->color().toString(), dialog.gradient().stops()[0].color().toString());
}

TEST(GradientEditorDialogTest, ApplyPresetOutOfRangeIsANoOp)
{
    CodeToolsVsix::GradientEditorDialog dialog;
    newui::gfx::Gradient seed = makeTwoStopLinearGradient();
    dialog.setGradient(seed);

    dialog.applyPreset(CodeToolsVsix::GradientEditorDialog::presetCount() + 1);

    ASSERT_EQ(dialog.gradient().stops().size(), 2u);
    EXPECT_EQ(dialog.gradient().stops()[0].color().toString(), seed.stops()[0].color().toString());
    EXPECT_EQ(dialog.gradient().stops()[1].color().toString(), seed.stops()[1].color().toString());
}

// The last built-in preset is opaque-to-transparent same blue - guards the real alpha value
// actually survives presetColor()'s own hex-parse-then-set-alpha construction (the exact class of
// mistake this file already found and fixed twice this session for other Color literals).
TEST(GradientEditorDialogTest, LastPresetPreservesItsRealAlphaValues)
{
    CodeToolsVsix::GradientEditorDialog dialog;
    dialog.setGradient(makeTwoStopLinearGradient());

    dialog.applyPreset(CodeToolsVsix::GradientEditorDialog::presetCount() - 1);

    ASSERT_EQ(dialog.gradient().stops().size(), 2u);
    EXPECT_FLOAT_EQ(dialog.gradient().stops()[0].color().a, 0.0f);
    EXPECT_FLOAT_EQ(dialog.gradient().stops()[1].color().a, 1.0f);
}

// +1 for the trailing AddPresetButton ("+") - always present alongside one PresetButton per
// current preset.
TEST(GradientEditorDialogTest, PresetsRowHasOneRealButtonPerPresetPlusTheAddButton)
{
    CodeToolsVsix::GradientEditorDialog dialog;
    dialog.setGradient(makeTwoStopLinearGradient());
    ASSERT_NE(dialog.presetsRow(), nullptr);

    EXPECT_EQ(dialog.presetsRow()->childViews().size(), CodeToolsVsix::GradientEditorDialog::presetCount() + 1);
}

TEST(GradientEditorDialogTest, PresetsAreHiddenForPointKindAndShownOtherwise)
{
    CodeToolsVsix::GradientEditorDialog dialog;
    dialog.setGradient(makeTwoStopLinearGradient());
    ASSERT_TRUE(dialog.presetsRow()->isVisible());

    dialog.setKind(newui::gfx::GradientKind::Point);
    EXPECT_FALSE(dialog.presetsRow()->isVisible());

    dialog.setKind(newui::gfx::GradientKind::Linear);
    EXPECT_TRUE(dialog.presetsRow()->isVisible());
}

// Drives the real click on a real preset button (not applyPreset() directly) - matches this
// file's own "drive the real method a click would" convention for every other control here.
TEST(GradientEditorDialogTest, ClickingARealPresetButtonAppliesIt)
{
    CodeToolsVsix::GradientEditorDialog dialog;
    dialog.setGradient(makeTwoPointGradient());
    ASSERT_NE(dialog.presetsRow(), nullptr);
    newui::SubView* secondPreset = dialog.presetsRow()->childViews()[2];

    newui::SyncReturn result = secondPreset->onMouseDown.syncCallFirst(*secondPreset, newui::Point(1.0f, 1.0f), 0, 0);

    EXPECT_EQ(result, newui::SyncReturn::Handled);
    EXPECT_EQ(dialog.gradient().kind(), newui::gfx::GradientKind::Linear);
    ASSERT_EQ(dialog.gradient().stops().size(), 2u);
    newui::Color expected;
    newui::Color::fromString("#2FBF71", expected);
    EXPECT_EQ(dialog.gradient().stops()[0].color().toString(), expected.toString());
}

TEST(GradientEditorDialogTest, ApplyPresetSelectsIt)
{
    CodeToolsVsix::GradientEditorDialog dialog;
    dialog.setGradient(makeTwoStopLinearGradient());

    dialog.applyPreset(2);

    ASSERT_TRUE(dialog.selectedPresetIndex().has_value());
    EXPECT_EQ(*dialog.selectedPresetIndex(), 2u);
}

// presetRegistry() (GradientEditorDialog.cpp) is a real, process-wide shared registry - every
// GradientEditorDialog instance in this same test binary sees the same one. Tests below that
// mutate it round-trip (add then remove, or vice versa) so they leave it exactly as they found it
// rather than leaking state into whichever test happens to run next.

TEST(GradientEditorDialogTest, AddPresetFromCurrentAppendsAndSelectsANewPreset)
{
    CodeToolsVsix::GradientEditorDialog dialog;
    dialog.setGradient(makeTwoStopLinearGradient());
    std::size_t before = CodeToolsVsix::GradientEditorDialog::presetCount();

    dialog.addPresetFromCurrent();

    EXPECT_EQ(CodeToolsVsix::GradientEditorDialog::presetCount(), before + 1);
    ASSERT_TRUE(dialog.selectedPresetIndex().has_value());
    EXPECT_EQ(*dialog.selectedPresetIndex(), before);

    dialog.removePreset(before);
    ASSERT_EQ(CodeToolsVsix::GradientEditorDialog::presetCount(), before);
}

TEST(GradientEditorDialogTest, RemovePresetRemovesItAndClearsSelection)
{
    CodeToolsVsix::GradientEditorDialog dialog;
    dialog.setGradient(makeTwoStopLinearGradient());
    std::size_t before = CodeToolsVsix::GradientEditorDialog::presetCount();
    dialog.addPresetFromCurrent();
    ASSERT_EQ(CodeToolsVsix::GradientEditorDialog::presetCount(), before + 1);

    dialog.removePreset(before);

    EXPECT_EQ(CodeToolsVsix::GradientEditorDialog::presetCount(), before);
    EXPECT_FALSE(dialog.selectedPresetIndex().has_value());
}

TEST(GradientEditorDialogTest, RemovePresetOutOfRangeIsANoOp)
{
    CodeToolsVsix::GradientEditorDialog dialog;
    dialog.setGradient(makeTwoStopLinearGradient());
    std::size_t before = CodeToolsVsix::GradientEditorDialog::presetCount();

    dialog.removePreset(before + 100);

    EXPECT_EQ(CodeToolsVsix::GradientEditorDialog::presetCount(), before);
}

// Drives the real click on the real trailing "+" button (not addPresetFromCurrent() directly).
TEST(GradientEditorDialogTest, ClickingTheRealAddButtonAppendsAPreset)
{
    CodeToolsVsix::GradientEditorDialog dialog;
    dialog.setGradient(makeTwoStopLinearGradient());
    std::size_t before = CodeToolsVsix::GradientEditorDialog::presetCount();
    newui::SubView* addButton = dialog.presetsRow()->childViews().back();

    newui::SyncReturn result = addButton->onMouseDown.syncCallFirst(*addButton, newui::Point(1.0f, 1.0f), 0, 0);

    EXPECT_EQ(result, newui::SyncReturn::Handled);
    EXPECT_EQ(CodeToolsVsix::GradientEditorDialog::presetCount(), before + 1);

    dialog.removePreset(before);
    ASSERT_EQ(CodeToolsVsix::GradientEditorDialog::presetCount(), before);
}

// A click within the delete-corner region on a preset that ISN'T currently selected just applies
// it instead - only the selected swatch's own delete-corner is ever real, so there's no ambiguity
// about which preset a delete click would remove (the user's own concern with an earlier draft
// that showed a delete mark on every swatch at once).
TEST(GradientEditorDialogTest, ClickingNearWhereADeleteCornerWouldBeOnAnUnselectedPresetJustAppliesIt)
{
    CodeToolsVsix::GradientEditorDialog dialog;
    dialog.setGradient(makeTwoStopLinearGradient());
    newui::SubView* preset = dialog.presetsRow()->childViews()[0];
    preset->setBounds(newui::Rect(0.0f, 0.0f, 32.0f, 32.0f));

    // Top-right corner - where the delete mark would render if this preset were selected.
    newui::SyncReturn result = preset->onMouseDown.syncCallFirst(*preset, newui::Point(28.0f, 4.0f), 0, 0);

    EXPECT_EQ(result, newui::SyncReturn::Handled);
    EXPECT_EQ(dialog.gradient().kind(), newui::gfx::GradientKind::Linear);
    ASSERT_TRUE(dialog.selectedPresetIndex().has_value());
    EXPECT_EQ(*dialog.selectedPresetIndex(), 0u);
}

// Once a preset IS selected, a click in that same top-right corner removes it instead of
// re-applying it.
TEST(GradientEditorDialogTest, ClickingTheSelectedPresetsDeleteCornerRemovesIt)
{
    CodeToolsVsix::GradientEditorDialog dialog;
    dialog.setGradient(makeTwoStopLinearGradient());
    std::size_t before = CodeToolsVsix::GradientEditorDialog::presetCount();
    dialog.addPresetFromCurrent();
    ASSERT_TRUE(dialog.selectedPresetIndex().has_value());
    ASSERT_EQ(*dialog.selectedPresetIndex(), before);
    newui::SubView* newPreset = dialog.presetsRow()->childViews()[before];
    newPreset->setBounds(newui::Rect(0.0f, 0.0f, 32.0f, 32.0f));

    newui::SyncReturn result = newPreset->onMouseDown.syncCallFirst(*newPreset, newui::Point(28.0f, 4.0f), 0, 0);

    EXPECT_EQ(result, newui::SyncReturn::Handled);
    EXPECT_EQ(CodeToolsVsix::GradientEditorDialog::presetCount(), before);
    EXPECT_FALSE(dialog.selectedPresetIndex().has_value());
}
