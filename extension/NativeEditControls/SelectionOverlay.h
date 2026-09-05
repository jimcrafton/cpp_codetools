#pragma once

#include <newui/geometry.h>
#include <newui/overlay.h>
#include <newui/subview.h>

#include <vector>

namespace CodeToolsVsix
{
    // Selection + handles (bluesky/designer-plan.md 6.1 item 3) - a design-
    // mode-only overlay painted on top of the whole editor pane via newui's
    // existing RootView::setOverlay() mechanism (overlay.h's own class
    // comment names exactly this use: "whatever needs to sit above the
    // entire view hierarchy regardless of z-order within it"). DesignerEditor
    // owns one (setupUI() sets it as root's overlay) and feeds it selection
    // changes from its own root->onMouseDown hook (DesignerEditor.cpp) -
    // this class itself only tracks state and paints; it does no hit-testing/
    // mouse handling of its own (Overlay isn't a SubView - see overlay.h).
    //
    // Multi-select: selected() is kept in click order, so primary()
    // (selected().back()) is always well-defined - every selected view gets
    // the outline, only primary() also gets the four corner handles. Visual
    // only for v1 - dragging a handle doesn't resize anything yet (a
    // deliberately deferred follow-up).
    class SelectionOverlay : public newui::Overlay
    {
    public:
        const std::vector<newui::SubView*>& selected() const { return selected_; }

        newui::SubView* primary() const { return selected_.empty() ? nullptr : selected_.back(); }

        bool isSelected(const newui::SubView* view) const;

        // Plain click - replaces the whole selection with just view, or
        // clears it if view is nullptr (an empty-canvas click).
        void selectExclusive(newui::SubView* view);

        // Ctrl+click - adds view if not already selected (becoming the new
        // primary()), removes it otherwise. A no-op for a null view
        // (Ctrl+click on empty canvas leaves the existing selection alone -
        // the same convention ListView/TreeView's own kmCtrl handling
        // already follows elsewhere in newui).
        void toggleSelection(newui::SubView* view);

        void clearSelection();

        // view's bounds in the coordinate space Overlay::paint() itself
        // already draws in (overlay.h - (0,0) at the hosting RootView's own
        // top-left). Walks view->parent() up to the root, undoing each
        // ancestor's own origin() (scroll offset) one level at a time - the
        // same math View::paintChildren()/hitTestChildren() already apply
        // per level (view.cpp), just accumulated across the whole chain.
        // Deliberately kept local here rather than added to newui::View
        // itself - no second real consumer for a generic version of this
        // exists yet (an "ask before assuming placement" call).
        static newui::Rect boundsInRootView(const newui::View* view);

        void paint(BLContext& ctx, const newui::Rect& rect) override;

    private:
        std::vector<newui::SubView*> selected_;
    };
}
