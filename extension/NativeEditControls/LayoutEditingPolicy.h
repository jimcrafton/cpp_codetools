#pragma once

#include <newui/geometry.h>
#include <newui/undostack.h>
#include <newui/view.h>

#include <blend2d/blend2d.h>

#include <cstddef>

namespace newui
{
    class SubView;
}

namespace CodeToolsVsix
{
    // What kind of edit gesture dragging a control affords, given its real
    // parent's newui::Layout - resolved via policyFor() below. Bluesky
    // "Move" design session, 2026-09-08/09: dragging must respect whatever
    // the governing Layout actually affords (AnchorLayout: free pixel
    // position; FlexLayout: reorder among siblings; GridLayout: move to a
    // different cell; CardLayout/anything else: no per-child geometry to
    // edit at all - dragging is refused outright, matching how the real
    // running app behaves).
    enum class GeometryEditKind
    {
        None,
        FreePosition,
        LinearReorder,
        GridCell,
    };

    // One drag's worth of context a LayoutEditingPolicy needs - same shape
    // regardless of which kind of Layout parent is actually involved.
    // startBounds/startPt/currentPt are all in the same coordinate space
    // (root-local, matching DesignerEditor's existing Move convention) -
    // resolve() only ever needs their *difference*, which is the same delta
    // in parent-local space too since nothing in this tree scales/rotates.
    struct GeometryDragContext
    {
        newui::SubView* view = nullptr;
        newui::View* parent = nullptr;
        newui::Rect startBounds;
        newui::Point startPt;
        newui::Point currentPt;
    };

    // What resolve() decided for the current drag position - tagged by kind
    // so a non-drag caller (e.g. PropertiesGrid deciding whether BOUNDS is
    // freely editable) can read .kind alone without touching a concrete
    // Layout type at all.
    struct GeometryEditResult
    {
        GeometryEditKind kind = GeometryEditKind::None;
        newui::Rect proposedBounds;             // FreePosition
        std::size_t targetSiblingIndex = 0;     // LinearReorder
        std::size_t targetRow = 0;              // GridCell
        std::size_t targetColumn = 0;           // GridCell
    };

    class LayoutEditingPolicy;

    // One active drag's worth of state for SelectionOverlay to paint a cue for - bundles a
    // policy with the context/result it already resolved, so SelectionOverlay (which knows
    // nothing about DesignerEditor's own drag bookkeeping) only ever needs to call
    // policy->drawCue(bl, ctx, result). See SelectionOverlay::setActiveDragCuesProvider().
    struct ActiveGeometryDrag
    {
        const LayoutEditingPolicy* policy = nullptr;
        GeometryDragContext ctx;
        GeometryEditResult result;
    };

    // Resolves/previews/draws/commits one newui::Layout kind's real editing
    // gesture during a Move drag - one subclass per real Layout subtype
    // (see policyFor()). Design-time-only, single real consumer
    // (DesignerEditor/SelectionOverlay, and eventually PropertiesGrid), same
    // placement reasoning as PropertyEditor/ComponentEditor - lives in
    // cpp_codetools, not newui.
    class LayoutEditingPolicy
    {
    public:
        virtual ~LayoutEditingPolicy() = default;

        virtual GeometryEditKind kind() const = 0;

        // Called every mouse-move during a drag - interprets ctx.currentPt
        // for this gesture. Mutates nothing itself.
        virtual GeometryEditResult resolve(const GeometryDragContext& ctx) const = 0;

        // Live per-frame feedback while dragging. FreePosition really moves
        // the view (setBounds()); LinearReorder/GridCell really reorder/
        // re-cell it too (via View::reorderChild()/GridLayoutParams - both
        // already trigger a real updateLayout(), which is what makes
        // siblings visibly shift out of the way during the drag) - none of
        // this is undo-tracked; only commit() (mouse-up) is.
        virtual void applyPreview(const GeometryDragContext& ctx, const GeometryEditResult& result) const = 0;

        // Paints this gesture's adorner cue for the current drag - called
        // from SelectionOverlay::paint() while a drag is active. bl is
        // already in root-local coordinates, same space Overlay::paint()
        // itself draws in (see SelectionOverlay::boundsInRootView()).
        virtual void drawCue(BLContext& bl, const GeometryDragContext& ctx, const GeometryEditResult& result) const = 0;

        // Builds the UndoableAction to push at mouse-up (empty description
        // = nothing to push). startResult is what resolve() would have
        // returned at mouse-down (zero delta) - the undo target; endResult
        // is the last real resolve() result before mouse-up - the redo/doIt
        // target. DesignerEditor still owns the actual undoStack_.push()
        // call, matching every other gesture.
        virtual newui::UndoableAction commit(const GeometryDragContext& ctx,
            const GeometryEditResult& startResult, const GeometryEditResult& endResult) const = 0;
    };

    // Picks the right policy for layout's real concrete type - nullptr or a
    // real AnchorLayout -> FreePosition (matches the original, pre-policy
    // Move code's own gating check), FlexLayout -> LinearReorder,
    // GridLayout -> GridCell, anything else (CardLayout, or any future/
    // unrecognized Layout subtype) -> None. Never returns a null reference;
    // always resolves to one of the four singletons below.
    const LayoutEditingPolicy& policyFor(newui::Layout* layout);
}
