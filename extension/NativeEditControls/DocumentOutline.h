#pragma once

#include "ViewDesignerModel.h"

#include <newui/controllers.h>
#include <newui/controls.h>
#include <newui/delegate.h>
#include <newui/items.h>
#include <newui/models.h>

#include <any>
#include <optional>
#include <string>
#include <vector>

namespace CodeToolsVsix
{
    // What a drag-and-drop drop onto a given row actually means, resolved from which third of
    // the row's own rect the cursor is over (DocumentOutline::dropTargetAt()) - the same
    // "insertion line vs. drop-into box" split most real tree UIs use. Into is only ever offered
    // for a row that's a real container (ToolboxRegistry::isContainer()); a non-container row can
    // only ever be Before/After (there's nothing to nest into).
    enum class DocumentOutlineDropDisposition
    {
        Before,
        Into,
        After,
    };

    // One resolved drop target - the row path plus what dropping there right now would mean.
    struct DocumentOutlineDropTarget
    {
        std::vector<std::size_t> path;
        DocumentOutlineDropDisposition disposition = DocumentOutlineDropDisposition::Into;

        bool operator==(const DocumentOutlineDropTarget& other) const
        {
            return path == other.path && disposition == other.disposition;
        }
    };

    // The TreeView-shaped adapter Document Outline's newui::TreeView
    // actually binds to (TreeController::setModel() requires a real
    // TreeModel*, which ViewDesignerModel deliberately isn't - it's a
    // plain Model shared with ViewDesignerController too, see its own
    // header comment). Owns no tree data itself - every call forwards
    // straight through to source()'s own childCount()/viewAt(), and this
    // class's own onChanged is just source()'s onChanged relayed, so a
    // TreeController subscribed here reacts to a change made through
    // *either* ViewDesignerController or this Outline's own selection
    // calls, or a raw refresh() from DesignerEditor::load()/Toolbox.
    class DocumentOutlineModel : public newui::TreeModel
    {
    public:
        // Non-owning, like Controller::setModel()'s own convention -
        // source_ outlives this (DesignerEditor owns the
        // ViewDesignerModel; Workspace owns this, and Workspace's own
        // lifetime is nested inside DesignerEditor's). Subscribes to
        // source's onChanged and relays it as this model's own, then
        // fires onChanged() once immediately so an already-attached
        // TreeController rebuilds against the new source right away.
        void setSource(ViewDesignerModel* source);
        ViewDesignerModel* source() const { return source_; }

        std::size_t childCount(const std::vector<std::size_t>& path) const override;

        // Display text only - matches TreeModel's own generic contract
        // elsewhere (e.g. ToolboxModel::value()). DocumentOutlineItem's
        // own paint() calls source()->viewAt() directly instead, for the
        // two-tone name/type rendering a plain string can't carry.
        std::any value(const std::any& key) override;

    private:
        newui::SyncReturn handleSourceChanged(newui::Model& sender);

        ViewDesignerModel* source_ = nullptr;
    };

    // name (normal ControlText/HighlightText) + a smaller, dimmer real
    // Class name suffix (newui::reflection::classinfo(typeid(*view))->
    // name()) on the same row - matches designer-surface/Main.dc.html's
    // own ".tree-row" + ".tree-row .type" (dimmer, smaller, no text
    // transform). No per-type icons (Main.dc.html hand-draws a handful of
    // SVG glyphs) - text only, same "deferred, v1 is text-only" call
    // already made twice for Toolbox (see ToolboxItem's own class
    // comment).
    class DocumentOutlineItem : public newui::TreeItem
    {
    public:
        void paint(BLContext& ctx, const newui::Rect& rect, const std::vector<std::size_t>& path,
            newui::TreeController& controller) override;
    };

    class DocumentOutlineController : public newui::TreeController
    {
    public:
        static constexpr float kIconSize = 15.0f;
        static constexpr float kIconGap = 6.0f;

        newui::TreeItem* createItem(const std::vector<std::size_t>& path) override;

        // Real icon for the SubView at path (via ToolboxRegistry's own
        // class-name-to-icon table - Document Outline rows are real
        // instances of the same classes Toolbox lists) - nullopt if that
        // class has no icon yet.
        std::optional<std::string> iconFor(const std::vector<std::size_t>& path) const override;
        float iconSize() const override { return kIconSize; }
        float iconGap() const override { return kIconGap; }

        // The drag-and-drop drop this row is currently poised to receive, if any - set by
        // DocumentOutline's own mouse handling, read by DocumentOutlineItem::paint() to draw
        // either the Into highlight box or a Before/After insertion line. nullopt (the default)
        // draws nothing extra.
        void setPendingDropTarget(std::optional<DocumentOutlineDropTarget> target) { pendingDropTarget_ = std::move(target); }
        const std::optional<DocumentOutlineDropTarget>& pendingDropTarget() const { return pendingDropTarget_; }
        std::optional<DocumentOutlineDropDisposition> dispositionFor(const std::vector<std::size_t>& path) const
        {
            return pendingDropTarget_.has_value() && pendingDropTarget_->path == path
                ? std::optional<DocumentOutlineDropDisposition>(pendingDropTarget_->disposition) : std::nullopt;
        }

    private:
        std::optional<DocumentOutlineDropTarget> pendingDropTarget_;
    };

    // The Document Outline pane (designer-plan.md 6.1 item 4) - a real
    // newui::ScrollView hosting a real newui::TreeView, same
    // "ScrollView::addChild() redirects into its own viewport, TreeView
    // answers onQueryContentSize/onScrollOffsetChanged on its own" shape
    // Toolbox/PropertiesGrid both already established, backed by
    // DocumentOutlineModel (a thin adapter over the shared
    // ViewDesignerModel) instead of a synthetic registry/reflection walk.
    //
    // Selection sync with the canvas is bidirectional but this class
    // doesn't know ViewDesignerController exists (matching Toolbox's own
    // onEntryActivated shape) - DesignerEditor wires onSelectionActivated
    // to ViewDesignerController::setSelection() one way, and calls
    // setSelection() on this class from its own onSelectionChanged
    // handler the other way. Both directions share one real hazard (an
    // update from one side naively re-notifying the other, forever),
    // solved two different ways depending on which side is driving:
    //   - Reading an external (canvas-driven) change in: setSelection()
    //     below no-ops if the requested path set already matches this
    //     tree's own selectedPaths() - the only way it wouldn't is a
    //     genuine external change, so this alone breaks the cycle without
    //     needing a flag.
    //   - Applying that change: building an arbitrary path set needs
    //     several individual TreeView calls (clearSelection() +
    //     addToSelection() per path), each of which fires its own
    //     onSelectionChanged along the way - suppressed for the duration
    //     of that batch via applyingExternalSelection_.
    class DocumentOutline : public newui::ScrollView
    {
    public:
        DocumentOutline();

        // Points this outline at the shared ViewDesignerModel (non-owning,
        // see DocumentOutlineModel::setSource()'s own comment) - called
        // once, since the model's identity never changes for this
        // editor's lifetime. Expands root's own direct children by
        // default (path {0}) so they're visible with no click needed,
        // matching Main.dc.html's own outline; deeper nesting starts
        // collapsed like any other real tree.
        void setViewDesignerModel(ViewDesignerModel* model);
        void refresh();

        // Applies an external (canvas-driven) selection - see this
        // class's own header comment for why this is a no-op whenever the
        // resolved path set already matches selectedPaths().
        void setSelection(const std::vector<newui::SubView*>& views);

        // Fired whenever the user's own click/Ctrl+click actually changes
        // this tree's selection (never during setSelection() above's own
        // programmatic update - see applyingExternalSelection_) - carries
        // the resolved SubView* list in TreeView's own selectedPaths()
        // order. DesignerEditor wires this to ViewDesignerController::
        // setSelection().
        typedef newui::Delegate<DocumentOutline, const std::vector<newui::SubView*>&> SelectionActivatedDelegate;
        SelectionActivatedDelegate onSelectionActivated;

        // Real drag-and-drop reparenting AND same/cross-parent reordering, unified - mouse-down
        // on a row arms a potential drag (a kDragThresholdPixels dead zone before it counts as
        // one, same ordinary click-vs-drag distinction the canvas' own Move drag already uses),
        // mouse-move hit-tests which row is under the cursor and resolves what dropping there
        // right now would mean via dropTargetAt() (highlighted through the controller's own
        // setPendingDropTarget()), mouse-up fires this with whatever that last resolved to. This
        // class only ever detects/reports the gesture - DesignerEditor wires it to the real,
        // undo-aware View::setParent()/reorderChild() calls, same "expose a hook, don't reach
        // into the owner" shape onSelectionActivated above already established. referenceRow is
        // the row the cursor was actually over (a container to nest into for Into, or the
        // sibling to land next to for Before/After) - DesignerEditor resolves the real target
        // parent from it (referenceRow itself for Into, referenceRow->parent() otherwise).
        typedef newui::Delegate<DocumentOutline, newui::SubView*, newui::SubView*, DocumentOutlineDropDisposition> DropRequestedDelegate;
        DropRequestedDelegate onDropRequested;

        // Exposed for testability - same convention Toolbox::treeView()
        // already uses.
        newui::TreeView* treeView() const { return treeView_; }
        DocumentOutlineModel& model() { return model_; }

    private:
        newui::SyncReturn handleTreeSelectionChanged(newui::TreeView& sender);

        // Expands every proper-prefix ancestor of path (not path itself)
        // so a canvas-driven selection under a collapsed ancestor is
        // actually visible, not just logically selected.
        void expandAncestorsOf(const std::vector<std::size_t>& path);

        newui::SyncReturn handleTreeMouseDown(newui::View& sender, const newui::Point& pt, std::uint32_t btnMask, std::uint32_t keyMask);
        newui::SyncReturn handleTreeMouseMove(newui::View& sender, const newui::Point& pt, std::uint32_t btnMask, std::uint32_t keyMask);
        newui::SyncReturn handleTreeMouseUp(newui::View& sender, const newui::Point& pt, std::uint32_t btnMask, std::uint32_t keyMask);

        // Which visible row (if any) contains localPt (treeView_'s own local coordinate space,
        // already scroll-adjusted - the same space onMouseDown/Move/Up's own pt arrives in) -
        // a plain linear scan of controller().visibleCount()/pathAt()/rectForPath(), all public
        // TreeView/TreeController API - real trees here are small enough that this needs no
        // cleverer lookup.
        std::optional<std::vector<std::size_t>> rowPathAt(const newui::Point& localPt) const;

        // Resolves the full drop gesture at localPt - hit-tests the row, excludes draggedView_
        // itself and its own descendants (would create a structural cycle), then reads which
        // third of the hit row's rect localPt falls in to decide Before/Into/After (a
        // non-container row only ever offers Before/After - see DocumentOutlineDropDisposition's
        // own comment; the design root, path {0}, has no siblings inside this tree so it only
        // ever offers Into). nullopt whenever there's nothing draggable to report.
        std::optional<DocumentOutlineDropTarget> dropTargetAt(const newui::Point& localPt) const;

        static constexpr float kDragThresholdPixels = 3.0f;

        // How much of a container row's own height, at its top/bottom edge, still means
        // Before/After rather than Into - matches the visual "insertion line" zone most real
        // tree UIs use (a plain non-container row has no middle Into zone at all - see
        // dropTargetAt()).
        static constexpr float kEdgeZoneFraction = 0.25f;

        DocumentOutlineModel model_;
        newui::TreeView* treeView_ = nullptr;
        DocumentOutlineController* outlineController_ = nullptr;
        bool applyingExternalSelection_ = false;

        newui::SubView* draggedView_ = nullptr;
        newui::Point dragStartPt_;
        bool dragStarted_ = false;
    };
}
