#pragma once

#include <newui/color.h>
#include <newui/controls.h>
#include <newui/dialogs.h>
#include <newui/geometry.h>
#include <newui/graphics.h>
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

        // Selects which stop the (single, Phase 1) hex field edits - clamped to
        // [0, gradient().stops().size()).
        void selectStop(std::size_t index);
        std::size_t selectedStopIndex() const { return selectedStopIndex_; }
        void setSelectedStopColor(const newui::Color& color);

        // Exposed for testability - same convention PropertiesGrid::treeView()/
        // Toolbox::treeView() already use for their own real child controls.
        newui::SegmentedControl* kindControl() const { return kindControl_; }
        const std::vector<newui::TextField*>& stopHexFields() const { return stopHexFields_; }

    private:
        // Builds the permanent chrome once (contentRoot_, kindControl_, the empty stopRows_
        // container, the Cancel/Apply footer) - called only from the constructor. Deliberately
        // never rebuilt: kindControl_'s own onSelectionChanged handler calls setKind(), which
        // rebuilds stop rows below - if that rebuild also tore down and recreated kindControl_
        // itself, it would delete the very SegmentedControl whose own click handler is still
        // executing higher up the call stack (a real use-after-free, caught before shipping this
        // phase). Keeping kindControl_/stopRows_/the footer alive for the dialog's whole lifetime
        // sidesteps that entirely - only rebuildStopRows() below ever tears anything down, and it
        // only ever touches stopRows_'s own children, never the control that might have called it.
        void buildChrome();

        // Rebuilds only stopRows_'s children against working_ - safe to call from setKind()
        // itself (see buildChrome()'s own comment for why) since it never touches kindControl_.
        void rebuildStopRows();

        newui::gfx::Gradient working_;
        newui::Rect shapeBounds_{0.0f, 0.0f, 200.0f, 140.0f};
        std::size_t selectedStopIndex_ = 0;

        newui::SubView* contentRoot_ = nullptr;
        newui::SegmentedControl* kindControl_ = nullptr;
        newui::SubView* stopRows_ = nullptr;
        std::vector<newui::TextField*> stopHexFields_;
    };
}
