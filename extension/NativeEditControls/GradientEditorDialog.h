#pragma once

#include "ColorPicker.h"

#include <newui/color.h>
#include <newui/controls.h>
#include <newui/dialogs.h>
#include <newui/geometry.h>
#include <newui/graphics.h>
#include <newui/layout.h>
#include <newui/segmentedcontrol.h>
#include <newui/subview.h>

#include <cstddef>
#include <optional>
#include <vector>

namespace CodeToolsVsix
{
    // Real, first-ever custom-content newui::Dialog in this codebase - see
    // GradientPropertyEditor's own comment (PropertyEditor.h) for why this exists at all
    // (Gradient's variable-length stops()/points() don't fit the fixed-row EditStyle::SubProperties
    // shape Font/Rect use). Design source: design/reference/gradient_editor_dialog.html, trimmed to
    // a resolved v1 scope - see bluesky/ plan/memory for the full decision log.
    //
    // Derives from newui::Dialog directly (rootView() below is the inherited one) rather than
    // composing a separate instance - Dialog itself has nothing virtual to override, but this
    // still reads as "is-a real Dialog with gradient-specific content and a typed result", not a
    // wrapper around an unrelated one. showModal(View*)/showModal(Frame*) are both inherited
    // as-is (no wrapper method here) - GradientPropertyEditor::edit(), this class's only real
    // caller, always has a real owner View to pass, so there's nothing for a wrapper to add.
    //
    // Phase 1: kind tabs (Linear/Radial/Conic/Point) + Cancel/Apply, proving
    // GradientPropertyEditor::edit()'s whole seed/edit/commit pipeline end to end.
    // Phase 2: a live gradient preview (previewBox_) and a real draggable stop track (track_,
    // PreviewBox/StopTrack - both private, GradientEditorDialog.cpp anonymous namespace).
    // Phase 3: one shared "selected item" editor (selectedItemEditor_ - a real ColorPicker.h/.cpp
    // SV-square + hue/alpha rails, plus one hex TextField and a delete button), matching the
    // mockup's own `.stop-editor` - retargeted to whichever stop *or point* is selected, rather
    // than a fixed row per stop.
    //
    // Phase 4 (this pass): real GradientKind::Point editing. Point has no separate "stops" concept
    // at all - each newui::gfx::GradientPoint's own position() *is* the real committed data (unlike
    // Linear/Radial/Conic, where the dialog's start/end/center fields are purely synthetic preview
    // geometry, never committed - see previewGradientFor()'s own comment), so editing happens by
    // clicking/dragging directly in previewBox_ itself (addPointAt()/setSelectedPointPosition()) -
    // track_ doesn't apply to Point at all (no 1D position, no offset) and is hidden for it
    // (showPageForKind()). A real point position is in shapeBounds()'s own coordinate space (same
    // as Linear/Radial/Conic's own geometry fields) - PreviewBox maps between that and its own
    // screen bounds for both rendering (previewGradientFor()'s Point case) and hit-testing/dragging
    // (its own mapToScreen()/mapToShapeSpace() helpers), so what's actually stored in working_ is
    // always in the real target View's own coordinate space, never the preview box's arbitrary
    // pixel size.
    //
    // The shared "selected item" editor (colorPicker()/hexField()/deleteItemButton()) now targets
    // whichever *stop* is selected for Linear/Radial/Conic, or whichever *point* is selected for
    // Point - refreshSelectedItemEditor() is the one place that branches on working_.kind() to
    // decide which; every other real interaction path (colorPicker_'s onColorChanged, hexField_'s
    // commit, deleteItemButton_'s click) also branches the same way, so there's exactly one rule
    // ("Point uses points(), everything else uses stops()") applied consistently rather than
    // duplicated per call site.
    //
    // Phase 5: a row of presets (presetsRow_, matching the mockup's own `.presets` grid, seeded
    // from its own 6 built-in entries) - each swatch is a real, independently-clickable
    // PresetButton showing its own actual resolved gradient. Clicking one selects *and* applies it
    // in one gesture - always resets kind to Linear and replaces stops() (applyPreset()) - the
    // mockup's own presets also carry an "angle" per preset, deliberately dropped here: this dialog
    // has no real control surface for editing Linear's own start/end geometry yet (see
    // previewGradientFor()'s own comment on that separate, still-open gap), so there's no real
    // "angle" concept to apply a preset's own value onto. Hidden for Point alongside track_ - a
    // preset is a Linear stop list, meaningless for Point's own anchors.
    //
    // Phase 5b (this pass): presets became a real, mutable, process-lifetime registry
    // (presetRegistry(), GradientEditorDialog.cpp) rather than a fixed list - a trailing
    // AddPresetButton ("+") appends the current working_.stops() as a new preset
    // (addPresetFromCurrent()), and whichever preset is currently selected (selectedPresetIndex())
    // shows both a highlight ring and a small delete-corner mark, so it's always visually
    // unambiguous which one a delete click would remove (removePreset()) - a real, user-raised
    // concern with an earlier draft that showed a delete mark on every swatch at once. Not
    // persisted across process restarts - a real, later feature if ever wanted.
    //
    // The kind tabs are 4 real newui::CardLayout pages (linearPage_/radialPage_/conicPage_/
    // pointPage_ below, built once in buildChrome() in that exact order so a GradientKind's own
    // numeric value already IS its CardLayout index - no separate translation table anywhere),
    // not one shared container whose content gets destroyed and rebuilt on every kind change. The
    // mockup (gradient_editor_dialog.html) itself treats the 4 type tabs as 4 real, separate
    // things - each will eventually need its own kind-specific controls (an angle dial for
    // Linear/Conic, a Circle/Ellipse shape toggle for Radial) - giving each kind a real, distinct
    // SubView means those land later without another structural rework. Linear/Radial/Conic's 3
    // pages are still empty today (none of those controls exist yet); pointPage_ now holds a real
    // usage hint (not a placeholder anymore, now that Point editing is real).
    //
    // Deliberately separates "build/mutate the working Gradient value" (every method below) from
    // "actually show a native modal window" (the inherited showModal(), never called by anything
    // in this class itself) - the former is exercised directly by tests (this project's own
    // convention: call the same public method a real UI event would, never simulate raw input),
    // the latter never is (it would block on real user interaction).
    class GradientEditorDialog : public newui::Dialog
    {
    public:
        GradientEditorDialog();

        // Seeds the working copy this dialog edits - resets kind tabs/stops/points to match. Call
        // before showModal()/any of the mutators below.
        void setGradient(const newui::gfx::Gradient& gradient);
        const newui::gfx::Gradient& gradient() const { return working_; }

        // The real target View's own local bounds (e.g. a Button's own bounds() - see
        // GradientPropertyEditor::edit()) - the coordinate space every one of Linear/Radial/Conic's
        // own geometry fields and every real GradientPoint position already live in, and the one
        // PreviewBox maps its own screen bounds to/from for both rendering and Point-kind editing.
        void setShapeBounds(const newui::Rect& bounds) { shapeBounds_ = bounds; }
        const newui::Rect& shapeBounds() const { return shapeBounds_; }

        // Below: the real public API the dialog's own UI calls on every interaction - exposed so
        // tests can drive the exact same path a click would, per this project's "call the real
        // method, don't simulate raw input" convention.
        void setKind(newui::gfx::GradientKind kind);

        // --- Stops (Linear/Radial/Conic) ---

        // Selects which stop the shared editor (colorPicker()/hexField()) targets - clamped to
        // [0, gradient().stops().size()); refreshes that editor to match (see
        // refreshSelectedItemEditor()'s own comment).
        void selectStop(std::size_t index);
        std::size_t selectedStopIndex() const { return selectedStopIndex_; }
        void setSelectedStopColor(const newui::Color& color);
        // Repositions the selected stop along [0,1] - clamped. The real path a StopTrack drag
        // calls; also directly testable per this project's "drive the real method" convention.
        void setSelectedStopOffset(float offset);

        // Inserts a new stop at offset (clamped [0,1]), color linearly interpolated (per RGBA
        // channel) between whichever two existing stops bracket it by offset - or the nearest
        // endpoint's color verbatim if offset falls outside every existing stop's range. Selects
        // the new stop. The real path StopTrack's own click-away-from-any-handle calls.
        void addStopAt(float offset);

        // Removes the selected stop, then selects stop 0 - a no-op if that would leave fewer than
        // 2 stops (a gradient needs at least 2 to mean anything), matching the mockup's own
        // `state.stops.length <= 2` guard. The real path deleteItemButton()'s click calls (for
        // Linear/Radial/Conic - see deleteSelectedItem()).
        void deleteSelectedStop();

        // --- Points (Point kind only) ---

        // Selects which point the shared editor targets - clamped to [0, gradient().points().
        // size()); refreshes the editor to match. The real path PreviewBox's own hit-on-an-
        // existing-handle click calls.
        void selectPoint(std::size_t index);
        std::size_t selectedPointIndex() const { return selectedPointIndex_; }
        void setSelectedPointColor(const newui::Color& color);
        // position is in shapeBounds()'s own coordinate space (see this class's own header
        // comment) - the real path PreviewBox's own drag calls, already converted out of its
        // screen-space mouse coordinates via mapToShapeSpace().
        void setSelectedPointPosition(const newui::Point& position);

        // Inserts a new point at position (shapeBounds()-relative), color copied verbatim from
        // whichever existing point is nearest to it - or opaque black if there are no existing
        // points yet - matching the mockup's own "seed new point's color from the nearest existing
        // point" behavior. Selects the new point. The real path PreviewBox's own
        // click-away-from-every-existing-handle calls.
        void addPointAt(const newui::Point& position);

        // Removes the selected point, then selects point 0 - a no-op if that would leave zero
        // points (unlike stops, a single point still renders something real - Gradient::
        // rasterizePoints() blends however many points there are - so 1 is the real floor, matching
        // the mockup's own `state.points.length <= 1` guard exactly, not stops' own 2).
        void deleteSelectedPoint();

        // --- Presets ---

        // How many presets currently exist - built-in plus any added this process's run (see
        // GradientEditorDialog.cpp's own presetRegistry() for why this is process-lifetime state,
        // not persisted to disk). Real, current count, exposed so tests/UI never hardcode it.
        static std::size_t presetCount();

        // Replaces the working gradient's kind (always Linear - see this method's own .cpp comment
        // for why) and stops with preset index's own, selects stop 0, and marks index as the
        // selected preset (selectedPresetIndex()) - the highlighted swatch presetsRow_'s own delete
        // affordance targets. Out-of-range index is a silent no-op, same contract every other
        // bounds-guarded mutator here has. The real path a PresetButton's own (non-delete-corner)
        // click calls.
        void applyPreset(std::size_t index);

        // Which preset (if any) is currently selected - real UI state, not a formatting/derived
        // value: presetsRow_'s own PresetButton draws a highlight ring around this one and only
        // shows its own small delete-corner mark there, so it's always visually unambiguous which
        // preset a delete click would remove (a real, user-raised concern - a delete affordance on
        // every swatch at once left that ambiguous). No preset is selected until the first
        // applyPreset()/addPresetFromCurrent() call - unlike selectedStopIndex()/
        // selectedPointIndex(), which always have a real target (a gradient always has stops or
        // points once seeded), nothing here requires a preset to ever be "the current one".
        std::optional<std::size_t> selectedPresetIndex() const { return selectedPresetIndex_; }

        // Appends a copy of the current working_.stops() as a new preset (available to every
        // GradientEditorDialog instance for the rest of this process's run) and selects it. The
        // real path presetsRow_'s own "+" button calls.
        void addPresetFromCurrent();

        // Removes preset index and rebuilds presetsRow_ to match - out-of-range is a silent no-op.
        // Works on any preset, built-in or user-added; unlike stops/points there's no real floor -
        // presetsRow_ can legitimately end up holding only its "+" button, since "+" alone can
        // always rebuild the list back up. Clears selectedPresetIndex() (the deleted one was
        // necessarily the selected one - see PresetButton's own comment on why delete is only ever
        // reachable through the selected swatch). The real path the selected PresetButton's own
        // small delete-corner click calls.
        void removePreset(std::size_t index);

        // Exposed for testability - same convention PropertiesGrid::treeView()/
        // Toolbox::treeView() already use for their own real child controls.
        newui::SegmentedControl* kindControl() const { return kindControl_; }
        newui::SubView* previewBox() const { return previewBox_; }
        newui::SubView* stopTrack() const { return track_; }
        newui::SubView* selectedItemEditor() const { return selectedItemEditor_; }
        ColorPicker* colorPicker() const { return colorPicker_; }
        newui::TextField* hexField() const { return hexField_; }
        newui::Button* deleteItemButton() const { return deleteItemButton_; }
        newui::SubView* presetsRow() const { return presetsRow_; }

    private:
        // Builds the permanent chrome once (contentRoot_, kindControl_, pagesContainer_ and its 4
        // real child pages - linearPage_/radialPage_/conicPage_/pointPage_ - the Cancel/Apply
        // footer) - called only from the constructor. Deliberately never rebuilt: kindControl_'s
        // own onSelectionChanged handler calls setKind(), which - in the single-container design
        // this replaced - tore down and rebuilt all of that container's content on every kind
        // change; if that rebuild had also torn down kindControl_ itself, it would delete the
        // very SegmentedControl whose own click handler is still executing higher up the call
        // stack (a real use-after-free, caught before shipping Phase 1). CardLayout removes the
        // whole class of bug: switching pages is just show(index), never a destroy/rebuild, so
        // there's nothing left here that a page switch could delete out from under its own caller.
        void buildChrome();

        // A no-op unless working_.kind() == Point. Two real, independent jobs, both real,
        // live-found bugs:
        // (1) normalizes pointBlendPower()/pointRasterMax() to sane defaults (2.0f/64) whenever
        //     they're <= 0 - a real Point-kind Gradient can arrive here with both at 0 (not their
        //     real C++ class defaults) however it was constructed before reaching this dialog
        //     (confirmed live via the debugger: a real View's own gradient already had 2 real
        //     points but both these fields at 0 - likely a reflection object-creation gap, not
        //     fixed here). A pointRasterMax() of 0 collapses rasterizePoints()'s own baked raster
        //     to a degenerate ~1x1 image, which is exactly why the live preview showed nothing but
        //     checkerboard - see this method's own .cpp comment for the full trace.
        // (2) seeds working_.points() with 2 reasonable default points (shapeBounds()-relative, one
        //     black one white - same spirit as setGradient()'s own default stop seeding) if it's
        //     currently empty.
        // Called from both setGradient() (a fresh Point-kind seed) and setKind() (switching *into*
        // Point, whether for the first time or not - (1) above needs to run every time, not just
        // once).
        void normalizePointStateForEditing();

        // The one real place kind selection actually takes effect: switches pagesLayout_ to kind's
        // page, shows/hides track_ (Point has no 1D stop track at all - see this class's own header
        // comment; previewBox_/selectedItemEditor_ stay visible for every kind now), refreshes the
        // selected-item editor and preview, and requests a real repaint. CardLayout::show()
        // re-arranges (setVisible()+setBounds()) immediately, but neither that nor addChild()/
        // removeChild()/setVisible() ever calls markDirty() - only a real window resize does that
        // implicitly (see View::addChild()'s own definition, subview.cpp/view.cpp). Without this
        // explicit call, clicking a segment updated every bit of program state correctly but the
        // dialog kept painting whatever page was on screen before the click - the actual bug
        // reported live against the earlier design, and CardLayout alone doesn't fix that half of
        // it. Also re-runs contentRoot_'s own layout, since toggling track_'s visibility changes
        // how much vertical space FlexLayout gives every row below it - setVisible() alone doesn't
        // trigger that (see its own definition).
        void showPageForKind(newui::gfx::GradientKind kind);

        // Redraws previewBox_/track_ against the current working_ - called after anything that
        // changes what they'd paint (a stop or point's color/position, or a kind switch). Both
        // classes read owner_.gradient() fresh on every paint() call, so there's no separate
        // content to rebuild here, just a repaint request (View::redraw(), same call Splitter's
        // own mutating setters already use).
        void refreshPreview();

        // Syncs colorPicker_/hexField_/deleteItemButton_ to whichever stop or point is selected -
        // working_.stops()[selectedStopIndex_] normally, or working_.points()[selectedPointIndex_]
        // when working_.kind() == Point (the one place that distinction is made - see this class's
        // own header comment). Called from selectStop()/selectPoint() (a click/drag) and
        // showPageForKind() (a kind switch or a fresh setGradient() seed), so the editor always
        // reflects whichever item is actually selected regardless of which of those changed it. A
        // no-op if there's no such stop/point (an empty list) - the editor just keeps showing
        // whatever it last did.
        void refreshSelectedItemEditor();

        // hexField_'s own onLostFocus/onReturnPressed handler - parses hex, commits via
        // setSelectedStopColor()/setSelectedPointColor() (whichever working_.kind() calls for) on
        // success, silently no-ops on invalid text (same contract every other hex-entry point in
        // this codebase already has). Deliberately has no unit test that fires onLostFocus()/
        // onReturnPressed() directly on hexField_ - see test_properties_grid.cpp's own documented
        // reasoning (its "Commit-on-blur/Enter..." comment) for why that would verify nothing but
        // this method's own body, not that a real keystroke reaches it; verify this one by hand via
        // testharness.exe instead.
        void commitHexField();

        // Removes whichever of the selected stop/point working_.kind() calls for -
        // deleteItemButton_'s own click handler. deleteSelectedStop()/deleteSelectedPoint() (both
        // still real, independently testable public methods) are what it actually calls.
        void deleteSelectedItem();

        // Clears presetsRow_'s own children and rebuilds them fresh from the current
        // presetRegistry() (GradientEditorDialog.cpp) plus one trailing AddPresetButton ("+") -
        // called from buildChrome() (the initial build) and after every addPresetFromCurrent()/
        // removePreset() (the registry's own size changed). Rebuilding fresh each time, rather than
        // patching in/out one child, keeps every PresetButton's own captured index_ correct without
        // a separate renumbering step - removeChild() only detaches (never deletes), same
        // "copy the list first, delete each" shape Workspace's own "New" button already uses.
        void rebuildPresetsRow();

        newui::gfx::Gradient working_;
        newui::Rect shapeBounds_{0.0f, 0.0f, 200.0f, 140.0f};
        std::size_t selectedStopIndex_ = 0;
        std::size_t selectedPointIndex_ = 0;
        std::optional<std::size_t> selectedPresetIndex_;

        newui::SubView* contentRoot_ = nullptr;
        newui::SegmentedControl* kindControl_ = nullptr;
        newui::SubView* pagesContainer_ = nullptr;
        newui::CardLayout* pagesLayout_ = nullptr;  // non-owning - owned by pagesContainer_'s Layout
        // Built in GradientKind order (Linear=0, Radial=1, Conic=2, Point=3) - see buildChrome()'s
        // own comment for why that ordering matters (it's what lets showPageForKind() index
        // pagesLayout_ directly off the enum value, no translation table).
        // Currently empty for Linear/Radial/Conic (none of their own real kind-specific controls -
        // an angle dial, a shape toggle - are built yet) - real, distinct pages ready for those
        // later, not dead weight; pointPage_ holds a real usage hint, not a placeholder.
        newui::SubView* linearPage_ = nullptr;
        newui::SubView* radialPage_ = nullptr;
        newui::SubView* conicPage_ = nullptr;
        newui::SubView* pointPage_ = nullptr;

        // PreviewBox/StopTrack (GradientEditorDialog.cpp, anonymous namespace) - own no state of
        // their own beyond drag tracking, always painting straight off gradient()/
        // selectedStopIndex()/selectedPointIndex(). track_ alone is hidden for Point (see
        // showPageForKind()); shown as plain newui::SubView* here since nothing outside this
        // file's own .cpp needs their concrete types.
        newui::SubView* previewBox_ = nullptr;
        newui::SubView* track_ = nullptr;

        // The shared "selected item" editor (Phase 3, generalized to points in Phase 4) - one
        // ColorPicker + one hex TextField + one delete button, retargeted to whichever stop *or*
        // point is selected via refreshSelectedItemEditor(), rather than a fixed row per stop
        // (Phase 1's own design, replaced - see this class's own header comment). Visible for
        // every kind now (unlike track_).
        newui::SubView* selectedItemEditor_ = nullptr;
        ColorPicker* colorPicker_ = nullptr;
        newui::TextField* hexField_ = nullptr;
        newui::Button* deleteItemButton_ = nullptr;

        // Presets (Phase 5) - a row of PresetButton (GradientEditorDialog.cpp anonymous namespace)
        // swatches plus one trailing AddPresetButton ("+"), rebuilt fresh by rebuildPresetsRow()
        // whenever presetRegistry()'s own size changes. Hidden for Point alongside track_ (a preset
        // is always a Linear stop list - meaningless for Point's own scattered anchors).
        newui::SubView* presetsLabel_ = nullptr;
        newui::SubView* presetsRow_ = nullptr;
    };
}
