#pragma once

#include <newui/controllers.h>
#include <newui/delegate.h>
#include <newui/subview.h>

#include <vector>

namespace CodeToolsVsix
{
    // Owns the View Designer's own selection state and the real logic that
    // mutates it in response to input - DesignerEditor's mouse handling
    // calls selectExclusive()/toggleSelection()/clearSelection() below
    // instead of poking selection state directly, and any UI piece that
    // cares when selection changes (PropertiesGrid today, Document
    // Outline later) subscribes to onSelectionChanged independently,
    // without DesignerEditor needing to know they exist at all - the
    // coupling problem hard-wiring each consumer into the mouse handler
    // would otherwise create.
    //
    // A real newui::Controller (Controller's own class comment says a
    // data-driven Control is expected to *own* one via composition, not
    // inherit from it - but a screen/surface-level controller genuinely IS
    // one, the same exception ViewController's own comment carves out for
    // itself) - deliberately not pointed at a real newui::Model yet
    // (setModel() never called): selection is an operation performed on
    // data, not data in its own right, so it doesn't belong wrapped in a
    // Model just to get onChanged-style notification - a plain
    // onSelectionChanged Delegate does that job directly instead. The
    // loaded .newui document (the real "model data" here) isn't itself
    // wrapped in a formal newui::Model today - it's just a SubView/
    // RootViewProxy tree - formalizing that (and pointing Controller::
    // model() at it) is a separate, bigger piece, deliberately not taken
    // on just to unblock selection notification.
    class ViewDesignerController : public newui::Controller
    {
    public:
        typedef newui::Delegate<ViewDesignerController> SelectionChangedDelegate;
        SelectionChangedDelegate onSelectionChanged;

        // Kept in click order, so primary() (selected().back()) is always
        // well-defined - every selected view gets an outline (see
        // SelectionOverlay), only primary() also gets the corner handles
        // and drives the Properties panel's own single "current" display.
        const std::vector<newui::SubView*>& selected() const { return selected_; }
        newui::SubView* primary() const { return selected_.empty() ? nullptr : selected_.back(); }
        bool isSelected(const newui::SubView* view) const;

        // Plain click - replaces the whole selection with just view, or
        // clears it if view is nullptr (an empty-canvas click). Always
        // fires onSelectionChanged, even when the result is unchanged from
        // before - every real caller only ever calls this in direct
        // response to an actual click, never speculatively, so there's no
        // real case where that would be wasteful.
        void selectExclusive(newui::SubView* view);

        // Ctrl+click - adds view if not already selected (becoming the
        // new primary()), removes it otherwise. A no-op (no
        // onSelectionChanged fired either) for a null view - Ctrl+click on
        // empty canvas leaves the existing selection alone, the same
        // convention ListView/TreeView's own kmCtrl handling already
        // follows elsewhere in newui.
        void toggleSelection(newui::SubView* view);

        void clearSelection();

    private:
        std::vector<newui::SubView*> selected_;
    };
}
