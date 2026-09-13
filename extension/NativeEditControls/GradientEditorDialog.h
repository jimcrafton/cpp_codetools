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
    // Phase 1 of the resolved scope: kind tabs (Linear/Radial/Conic/Point) + one plain hex
    // TextField per existing stop (Linear/Radial/Conic only - Point isn't built yet, shown as an
    // honest placeholder) + Cancel/Apply. Proves GradientPropertyEditor::edit()'s whole
    // seed/edit/commit pipeline end to end before the real ColorPicker/draggable-handle-track
    // widgets (later phases) replace the plain-text stand-ins here.
    //
    // Phase 2: a live gradient preview (previewBox_) and a real draggable stop track (track_,
    // PreviewBox/StopTrack - both private, GradientEditorDialog.cpp anonymous namespace) sit above
    // the per-kind pages, matching the mockup's own `.preview`/`.track`. Dragging a handle
    // repositions that stop (setSelectedStopOffset()); both are hidden for GradientKind::Point
    // (see showPageForKind()) since neither applies to it.
    //
    // Phase 3 (this pass): the Phase 1 per-stop-row hex fields are gone, replaced by one shared
    // "selected stop" editor (selectedStopEditor_ - a real ColorPicker.h/.cpp, SV-square + hue/
    // alpha rails, plus one hex TextField), matching the mockup's own `.stop-editor` - which
    // targets whichever stop is selected on track_, not a fixed row per stop. This only made sense
    // once track_ (Phase 2) existed as the real way to select a stop without a hex-row of its own;
    // colorPicker()/hexField() are kept in sync with each other and with whichever stop is
    // selected via refreshSelectedStopEditor() (called from selectStop()/showPageForKind()).
    //
    // The kind tabs are 4 real newui::CardLayout pages (linearPage_/radialPage_/conicPage_/
    // pointPage_ below, built once in buildChrome() in that exact order so a GradientKind's own
    // numeric value already IS its CardLayout index - no separate translation table anywhere),
    // not one shared container whose content gets destroyed and rebuilt on every kind change. The
    // mockup (gradient_editor_dialog.html) itself treats the 4 type tabs as 4 real, separate
    // things - each will eventually need its own kind-specific controls this Phase 1 doesn't
    // build yet (an angle dial for Linear/Conic, a Circle/Ellipse shape toggle for Radial, a
    // scatter canvas + spread field for Point) - giving each kind a real, distinct SubView now
    // means those land later without another structural rework, rather than trying to retrofit
    // per-kind fields into one shared container. Linear/Radial/Conic's 3 pages happen to hold
    // identical content today (none of those kind-specific controls exist yet - see
    // showPageForKind()'s own comment for exactly how a page's content gets kept in sync),
    // deliberately not collapsed into one shared page anyway, per the reasoning above. CardLayout
    // is this toolkit's own real "wizard steps/tabbed content panes" mechanism (see its class
    // comment, newui/layout.h).
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

        // Seeds the working copy this dialog edits - resets kind tabs/stop rows to match. Call
        // before showModal()/any of the mutators below.
        void setGradient(const newui::gfx::Gradient& gradient);
        const newui::gfx::Gradient& gradient() const { return working_; }

        // Anchors future (Phase 3+) center-chip/preview-aspect math to the live shape actually
        // being edited - see GradientPropertyEditor::edit(). Unused by Phase 1's UI, but plumbed
        // through now so later phases don't need to touch this class's construction again.
        void setShapeBounds(const newui::Rect& bounds) { shapeBounds_ = bounds; }
        const newui::Rect& shapeBounds() const { return shapeBounds_; }

        // Below: the real public API the dialog's own UI calls on every interaction - exposed so
        // tests can drive the exact same path a click would, per this project's "call the real
        // method, don't simulate raw input" convention.
        void setKind(newui::gfx::GradientKind kind);

        // Selects which stop the shared editor (colorPicker()/hexField()) targets - clamped to
        // [0, gradient().stops().size()); refreshes that editor to match (see
        // refreshSelectedStopEditor()'s own comment).
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
        // `state.stops.length <= 2` guard. The real path deleteStopButton()'s click calls.
        void deleteSelectedStop();

        // Exposed for testability - same convention PropertiesGrid::treeView()/
        // Toolbox::treeView() already use for their own real child controls.
        newui::SegmentedControl* kindControl() const { return kindControl_; }
        newui::SubView* previewBox() const { return previewBox_; }
        newui::SubView* stopTrack() const { return track_; }
        // isVisible() is a plain local flag, not ancestor-aware (see View::isVisible()'s own
        // definition) - colorPicker()/hexField() themselves are never individually hidden, only
        // this container is (showPageForKind()), so check *this* to see whether the shared editor
        // is actually showing for the current kind.
        newui::SubView* selectedStopEditor() const { return selectedStopEditor_; }
        ColorPicker* colorPicker() const { return colorPicker_; }
        newui::TextField* hexField() const { return hexField_; }
        newui::Button* deleteStopButton() const { return deleteStopButton_; }

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

        // The one real place kind selection actually takes effect: switches pagesLayout_ to kind's
        // page, shows/hides previewBox_/track_/selectedStopEditor_ (none apply to Point),
        // refreshes the selected-stop editor and preview, and requests a real repaint.
        // CardLayout::show() re-arranges (setVisible()+setBounds()) immediately, but neither that
        // nor addChild()/removeChild()/setVisible() ever calls markDirty() - only a real window
        // resize does that implicitly (see View::addChild()'s own definition, subview.cpp/
        // view.cpp). Without this explicit call, clicking a segment updated every bit of program
        // state correctly but the dialog kept painting whatever page was on screen before the
        // click - the actual bug reported live against the earlier design, and CardLayout alone
        // doesn't fix that half of it. Also re-runs contentRoot_'s own layout, since toggling
        // previewBox_/track_/selectedStopEditor_'s visibility changes how much vertical space
        // FlexLayout gives every row below them - setVisible() alone doesn't trigger that (see its
        // own definition).
        void showPageForKind(newui::gfx::GradientKind kind);

        // Redraws previewBox_/track_ against the current working_ - called after anything that
        // changes what they'd paint (a stop's color or offset, or a kind switch). Both classes
        // read owner_.gradient() fresh on every paint() call, so there's no separate content to
        // rebuild here, just a repaint request (View::redraw(), same call Splitter's own mutating
        // setters already use).
        void refreshPreview();

        // Syncs colorPicker_/hexField_ to working_.stops()[selectedStopIndex_] - called from
        // selectStop() (a track drag/click) and showPageForKind() (a kind switch or a fresh
        // setGradient() seed), so both always reflect whichever stop is actually selected
        // regardless of which of those changed it. A no-op if there's no such stop (Point, or a
        // still-empty stops list) - colorPicker_/hexField_ just keep showing whatever they last
        // did.
        void refreshSelectedStopEditor();

        // hexField_'s own onLostFocus/onReturnPressed handler - parses hex, commits via
        // setSelectedStopColor() on success, silently no-ops on invalid text (same contract every
        // other hex-entry point in this codebase already has). Deliberately has no unit test that
        // fires onLostFocus()/onReturnPressed() directly on hexField_ - see test_properties_grid.
        // cpp's own documented reasoning (its "Commit-on-blur/Enter..." comment) for why that
        // would verify nothing but this method's own body, not that a real keystroke reaches it;
        // verify this one by hand via testharness.exe instead.
        void commitHexField();

        newui::gfx::Gradient working_;
        newui::Rect shapeBounds_{0.0f, 0.0f, 200.0f, 140.0f};
        std::size_t selectedStopIndex_ = 0;

        newui::SubView* contentRoot_ = nullptr;
        newui::SegmentedControl* kindControl_ = nullptr;
        newui::SubView* pagesContainer_ = nullptr;
        newui::CardLayout* pagesLayout_ = nullptr;  // non-owning - owned by pagesContainer_'s Layout
        // Built in GradientKind order (Linear=0, Radial=1, Conic=2, Point=3) - see buildChrome()'s
        // own comment for why that ordering matters (it's what lets showPageForKind() index
        // pagesLayout_ directly off the enum value, no translation table).
        // Currently empty for Linear/Radial/Conic (none of their own real kind-specific controls -
        // an angle dial, a shape toggle - are built yet) - real, distinct pages ready for those
        // later, not dead weight; pointPage_ alone already holds its own honest placeholder.
        newui::SubView* linearPage_ = nullptr;
        newui::SubView* radialPage_ = nullptr;
        newui::SubView* conicPage_ = nullptr;
        newui::SubView* pointPage_ = nullptr;

        // PreviewBox/StopTrack (GradientEditorDialog.cpp, anonymous namespace) - own no state of
        // their own beyond drag tracking, always painting straight off gradient()/
        // selectedStopIndex(). Hidden for Point (see showPageForKind()); shown as plain
        // newui::SubView* here since nothing outside this file's own .cpp needs their concrete
        // types.
        newui::SubView* previewBox_ = nullptr;
        newui::SubView* track_ = nullptr;

        // The shared "selected stop" editor (Phase 3) - one ColorPicker + one hex TextField,
        // retargeted to whichever stop is selected via refreshSelectedStopEditor(), rather than a
        // fixed row per stop (Phase 1's own design, replaced - see this class's own header
        // comment). Hidden for Point alongside previewBox_/track_.
        newui::SubView* selectedStopEditor_ = nullptr;
        ColorPicker* colorPicker_ = nullptr;
        newui::TextField* hexField_ = nullptr;
        newui::Button* deleteStopButton_ = nullptr;
    };
}
