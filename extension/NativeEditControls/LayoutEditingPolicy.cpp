#include "LayoutEditingPolicy.h"

#include "SelectionOverlay.h"

#include <newui/subview.h>
#include <newui/uicolormanager.h>

#include <algorithm>
#include <memory>

namespace CodeToolsVsix
{
    namespace
    {
        // Shared by FreePositionPolicy's doIt()/undoIt() below - keeps a dragged view's
        // AnchorLayoutParams in sync with its real bounds so a *future* relayout (e.g. a window
        // resize) doesn't silently snap it back to stale margins from an earlier drag (the same
        // trap named in PropertiesGrid's own still-open BOUNDS-editing gap). Applies to any real
        // AnchorLayout parent, not just rootViewProxy() specifically - the original, narrower
        // "only if parent is rootViewProxy()" restriction only ever mattered because rootViewProxy()
        // was the sole AnchorLayout parent that could reach this code at all; the underlying
        // reasoning (must refresh AnchorLayoutParams, or arrange() reverts the position later)
        // applies identically to any AnchorLayout-governed container.
        void applyFreePositionAnchorParams(newui::SubView* view, const newui::Rect& bounds)
        {
            auto* params = dynamic_cast<newui::AnchorLayoutParams*>(view->layoutParams());
            if (params == nullptr) {
                auto owned = std::make_unique<newui::AnchorLayoutParams>(newui::Anchor::Left | newui::Anchor::Top);
                params = owned.get();
                view->setLayoutParams(std::move(owned));
            }
            params->anchors = newui::Anchor::Left | newui::Anchor::Top;
            params->leftMargin = bounds.left();
            params->topMargin = bounds.top();
            params->width = bounds.size().width;
            params->height = bounds.size().height;
        }

        // Shared by GridCellPolicy's applyPreview()/commit() below.
        void applyGridCell(newui::View* parent, newui::SubView* view, std::size_t row, std::size_t column)
        {
            auto* params = dynamic_cast<newui::GridLayoutParams*>(view->layoutParams());
            if (params == nullptr) {
                view->setLayoutParams(std::make_unique<newui::GridLayoutParams>(row, column));
            } else {
                params->row = row;
                params->column = column;
            }
            parent->updateLayout();
        }

        // Where ctx.view would land (parent-local, same space as every sibling's own bounds())
        // if the drag committed right now - shared by FreePositionPolicy and, for its own
        // center-point math, LinearReorderPolicy/GridCellPolicy. Pure translation only (nothing
        // in this tree scales/rotates), so ctx.currentPt - ctx.startPt is the same delta in
        // root-local or parent-local space.
        newui::Rect draggedBounds(const GeometryDragContext& ctx)
        {
            newui::Point delta = ctx.currentPt - ctx.startPt;
            return newui::Rect(
                ctx.startBounds.left() + delta.x, ctx.startBounds.top() + delta.y,
                ctx.startBounds.size().width, ctx.startBounds.size().height);
        }

        // Maps pos into one of tracks' indices by which [offsets[i], offsets[i]+sizes[i]) range
        // it falls in - clamped to the first/last track when pos falls before/after every real
        // track (e.g. dragging past the grid's own edge still resolves to a real cell, never an
        // out-of-range index). Empty tracks (a GridLayout with no rows/columns configured at all)
        // can't happen here - GridCellPolicy is only ever resolved for a real, attached
        // GridLayout parent that already has children placed in it.
        std::size_t trackIndexForPosition(const std::vector<float>& offsets, const std::vector<float>& sizes, float pos)
        {
            for (std::size_t i = 0; i < offsets.size(); ++i) {
                if (pos < offsets[i] + sizes[i]) {
                    return i;
                }
            }
            return offsets.empty() ? 0 : offsets.size() - 1;
        }

        // -------------------------------------------------------------------
        // FreePosition - AnchorLayout, or no Layout at all.
        // -------------------------------------------------------------------
        class FreePositionPolicy : public LayoutEditingPolicy
        {
        public:
            GeometryEditKind kind() const override { return GeometryEditKind::FreePosition; }

            GeometryEditResult resolve(const GeometryDragContext& ctx) const override
            {
                GeometryEditResult result;
                result.kind = GeometryEditKind::FreePosition;
                result.proposedBounds = draggedBounds(ctx);
                return result;
            }

            void applyPreview(const GeometryDragContext& ctx, const GeometryEditResult& result) const override
            {
                ctx.view->setBounds(result.proposedBounds);
            }

            void drawCue(BLContext&, const GeometryDragContext&, const GeometryEditResult&) const override
            {
                // Nothing beyond the ordinary selection outline/handles - free position needs no
                // extra cue, the dragged view's own live bounds already show where it's going.
            }

            newui::UndoableAction commit(const GeometryDragContext& ctx,
                const GeometryEditResult& startResult, const GeometryEditResult& endResult) const override
            {
                newui::SubView* view = ctx.view;
                newui::View* parent = ctx.parent;
                newui::Rect startBoundsCopy = startResult.proposedBounds;
                newui::Rect endBoundsCopy = endResult.proposedBounds;
                bool hasAnchorLayout = parent != nullptr
                    && dynamic_cast<newui::AnchorLayout*>(parent->layout()) != nullptr;

                newui::UndoableAction action;
                action.description = "Move Control";
                // The trailing updateLayout() re-runs AnchorLayout::arrange() against the params
                // just written - a no-op in practice (they already match view's own bounds), kept
                // as a cheap consistency check rather than trusting the two stay in sync silently.
                action.doIt = [view, parent, endBoundsCopy, hasAnchorLayout]() {
                    view->setBounds(endBoundsCopy);
                    if (hasAnchorLayout) {
                        applyFreePositionAnchorParams(view, endBoundsCopy);
                        parent->updateLayout();
                    }
                };
                action.undoIt = [view, parent, startBoundsCopy, hasAnchorLayout]() {
                    view->setBounds(startBoundsCopy);
                    if (hasAnchorLayout) {
                        applyFreePositionAnchorParams(view, startBoundsCopy);
                        parent->updateLayout();
                    }
                };
                return action;
            }
        };

        // -------------------------------------------------------------------
        // LinearReorder - FlexLayout. "Ghost gap opens live" is achieved for
        // free by reordering the real child during the drag itself (via the
        // new View::reorderChild() primitive, which already triggers a real
        // arrange() pass) - no separate placeholder view needed.
        // -------------------------------------------------------------------
        class LinearReorderPolicy : public LayoutEditingPolicy
        {
        public:
            GeometryEditKind kind() const override { return GeometryEditKind::LinearReorder; }

            GeometryEditResult resolve(const GeometryDragContext& ctx) const override
            {
                GeometryEditResult result;
                result.kind = GeometryEditKind::LinearReorder;

                auto* flex = dynamic_cast<newui::FlexLayout*>(ctx.parent->layout());
                bool horizontal = flex == nullptr || flex->orientation() == newui::Orientation::Horizontal;

                newui::Rect dragged = draggedBounds(ctx);
                float draggedCenter = horizontal
                    ? dragged.left() + dragged.size().width * 0.5f
                    : dragged.top() + dragged.size().height * 0.5f;

                // Counts how many *other* siblings' own centers fall before the dragged view's
                // hypothetical position - exactly the insertion index View::reorderChild() expects
                // (an index within the list *after* the dragged view is taken out of it).
                std::size_t targetIndex = 0;
                for (newui::SubView* sibling : ctx.parent->childViews()) {
                    if (sibling == ctx.view) {
                        continue;
                    }
                    const newui::Rect& siblingBounds = sibling->bounds();
                    float siblingCenter = horizontal
                        ? siblingBounds.left() + siblingBounds.size().width * 0.5f
                        : siblingBounds.top() + siblingBounds.size().height * 0.5f;
                    if (siblingCenter < draggedCenter) {
                        ++targetIndex;
                    }
                }
                result.targetSiblingIndex = targetIndex;
                return result;
            }

            void applyPreview(const GeometryDragContext& ctx, const GeometryEditResult& result) const override
            {
                ctx.parent->reorderChild(ctx.view, result.targetSiblingIndex);
            }

            void drawCue(BLContext& bl, const GeometryDragContext& ctx, const GeometryEditResult&) const override
            {
                // Highlights the dragged view's own current (already-reordered-live) position,
                // so it still reads as "being positioned" rather than an ordinary settled child.
                newui::Rect bounds = SelectionOverlay::boundsInRootView(ctx.view);
                BLRgba32 accent = newui::UIColorManager::colorFor(newui::UIColorRole::HighlightBackground).toBLRgba32();
                bl.set_stroke_style(accent);
                bl.set_stroke_width(2.0);
                bl.stroke_rect(BLRect(bounds.left(), bounds.top(), bounds.size().width, bounds.size().height));
            }

            newui::UndoableAction commit(const GeometryDragContext& ctx,
                const GeometryEditResult& startResult, const GeometryEditResult& endResult) const override
            {
                newui::SubView* view = ctx.view;
                newui::View* parent = ctx.parent;
                std::size_t startIndex = startResult.targetSiblingIndex;
                std::size_t endIndex = endResult.targetSiblingIndex;

                newui::UndoableAction action;
                action.description = "Reorder Control";
                // reorderChild() is idempotent (moves to an absolute index regardless of current
                // position), so doIt() re-affirming the already-live-previewed order (and undoIt()
                // running it "cold" after a later redo) both work correctly.
                action.doIt = [parent, view, endIndex]() { parent->reorderChild(view, endIndex); };
                action.undoIt = [parent, view, startIndex]() { parent->reorderChild(view, startIndex); };
                return action;
            }
        };

        // -------------------------------------------------------------------
        // GridCell - GridLayout.
        // -------------------------------------------------------------------
        class GridCellPolicy : public LayoutEditingPolicy
        {
        public:
            GeometryEditKind kind() const override { return GeometryEditKind::GridCell; }

            GeometryEditResult resolve(const GeometryDragContext& ctx) const override
            {
                GeometryEditResult result;
                result.kind = GeometryEditKind::GridCell;

                auto* grid = dynamic_cast<newui::GridLayout*>(ctx.parent->layout());
                newui::GridLayout::GridGeometry geometry = grid->trackGeometry(*ctx.parent);
                if (geometry.columns.offsets.empty() || geometry.rows.offsets.empty()) {
                    return result;  // no real tracks configured - nothing to resolve
                }

                newui::Rect dragged = draggedBounds(ctx);
                float centerX = dragged.left() + dragged.size().width * 0.5f;
                float centerY = dragged.top() + dragged.size().height * 0.5f;

                result.targetColumn = trackIndexForPosition(geometry.columns.offsets, geometry.columns.sizes, centerX);
                result.targetRow = trackIndexForPosition(geometry.rows.offsets, geometry.rows.sizes, centerY);
                return result;
            }

            void applyPreview(const GeometryDragContext& ctx, const GeometryEditResult& result) const override
            {
                applyGridCell(ctx.parent, ctx.view, result.targetRow, result.targetColumn);
            }

            void drawCue(BLContext& bl, const GeometryDragContext& ctx, const GeometryEditResult& result) const override
            {
                auto* grid = dynamic_cast<newui::GridLayout*>(ctx.parent->layout());
                newui::GridLayout::GridGeometry geometry = grid->trackGeometry(*ctx.parent);
                newui::Rect parentBounds = SelectionOverlay::boundsInRootView(ctx.parent);

                // Faint tracker lines across every real row/column boundary - "this is grid-bound",
                // same idea as an IDE's own alignment guides.
                bl.set_stroke_style(BLRgba32(0xC0, 0xC0, 0xC0, 0x44));
                bl.set_stroke_width(1.0);
                for (float columnOffset : geometry.columns.offsets) {
                    float x = parentBounds.left() + columnOffset;
                    bl.stroke_line(BLPoint(x, parentBounds.top()), BLPoint(x, parentBounds.bottom()));
                }
                for (float rowOffset : geometry.rows.offsets) {
                    float y = parentBounds.top() + rowOffset;
                    bl.stroke_line(BLPoint(parentBounds.left(), y), BLPoint(parentBounds.right(), y));
                }

                // Highlights the target cell itself.
                if (result.targetColumn < geometry.columns.offsets.size() && result.targetRow < geometry.rows.offsets.size()) {
                    BLRgba32 accent = newui::UIColorManager::colorFor(newui::UIColorRole::HighlightBackground).toBLRgba32();
                    BLRect cell(
                        parentBounds.left() + geometry.columns.offsets[result.targetColumn],
                        parentBounds.top() + geometry.rows.offsets[result.targetRow],
                        geometry.columns.sizes[result.targetColumn],
                        geometry.rows.sizes[result.targetRow]);
                    bl.set_fill_style(BLRgba32(accent.r(), accent.g(), accent.b(), 0x33));
                    bl.fill_rect(cell);
                    bl.set_stroke_style(accent);
                    bl.set_stroke_width(2.0);
                    bl.stroke_rect(cell);
                }
            }

            newui::UndoableAction commit(const GeometryDragContext& ctx,
                const GeometryEditResult& startResult, const GeometryEditResult& endResult) const override
            {
                newui::SubView* view = ctx.view;
                newui::View* parent = ctx.parent;
                std::size_t startRow = startResult.targetRow;
                std::size_t startColumn = startResult.targetColumn;
                std::size_t endRow = endResult.targetRow;
                std::size_t endColumn = endResult.targetColumn;

                newui::UndoableAction action;
                action.description = "Move Control";
                action.doIt = [parent, view, endRow, endColumn]() { applyGridCell(parent, view, endRow, endColumn); };
                action.undoIt = [parent, view, startRow, startColumn]() { applyGridCell(parent, view, startRow, startColumn); };
                return action;
            }
        };

        // -------------------------------------------------------------------
        // None - CardLayout, or any future/unrecognized Layout subtype.
        // Never actually called in practice: DesignerEditor refuses to arm a
        // drag at all once policyFor(...).kind() == None, matching how the
        // real app behaves (that Layout always computes this view's
        // position, there's no free "wherever you left it" input to drag).
        // -------------------------------------------------------------------
        class NoGeometryPolicy : public LayoutEditingPolicy
        {
        public:
            GeometryEditKind kind() const override { return GeometryEditKind::None; }
            GeometryEditResult resolve(const GeometryDragContext&) const override { return GeometryEditResult(); }
            void applyPreview(const GeometryDragContext&, const GeometryEditResult&) const override {}
            void drawCue(BLContext&, const GeometryDragContext&, const GeometryEditResult&) const override {}
            newui::UndoableAction commit(const GeometryDragContext&,
                const GeometryEditResult&, const GeometryEditResult&) const override
            {
                return newui::UndoableAction();
            }
        };
    }

    const LayoutEditingPolicy& policyFor(newui::Layout* layout)
    {
        static const FreePositionPolicy freePosition;
        static const LinearReorderPolicy linearReorder;
        static const GridCellPolicy gridCell;
        static const NoGeometryPolicy noGeometry;

        if (layout == nullptr || dynamic_cast<newui::AnchorLayout*>(layout) != nullptr) {
            return freePosition;
        }
        if (dynamic_cast<newui::FlexLayout*>(layout) != nullptr) {
            return linearReorder;
        }
        if (dynamic_cast<newui::GridLayout*>(layout) != nullptr) {
            return gridCell;
        }
        return noGeometry;
    }
}
