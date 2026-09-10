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

        // The row a drag-and-drop reparent is currently poised to drop onto, if any - set by
        // DocumentOutline's own mouse handling, read by DocumentOutlineItem::paint() to draw a
        // highlight. nullopt (the default) draws nothing extra.
        void setPendingDropTargetPath(std::optional<std::vector<std::size_t>> path) { pendingDropTargetPath_ = std::move(path); }
        const std::optional<std::vector<std::size_t>>& pendingDropTargetPath() const { return pendingDropTargetPath_; }
        bool isPendingDropTarget(const std::vector<std::size_t>& path) const { return pendingDropTargetPath_ == path; }

    private:
        std::optional<std::vector<std::size_t>> pendingDropTargetPath_;
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

        // Real drag-and-drop reparenting - mouse-down on a row arms a potential drag (a
        // kDragThresholdPixels dead zone before it counts as one, same ordinary click-vs-drag
        // distinction the canvas' own Move drag already uses), mouse-move hit-tests which
        // *other* row is under the cursor and highlights it via the controller's own
        // setPendingDropTargetPath(), mouse-up fires this if a real, different, legitimate
        // container (ToolboxRegistry::isContainer()) was found there. This class only ever
        // detects/reports the gesture - DesignerEditor wires it to the real, undo-aware
        // View::setParent() call, same "expose a hook, don't reach into the owner" shape
        // onSelectionActivated above already established.
        typedef newui::Delegate<DocumentOutline, newui::SubView*, newui::SubView*> ReparentRequestedDelegate;
        ReparentRequestedDelegate onReparentRequested;

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

        static constexpr float kDragThresholdPixels = 3.0f;

        DocumentOutlineModel model_;
        newui::TreeView* treeView_ = nullptr;
        DocumentOutlineController* outlineController_ = nullptr;
        bool applyingExternalSelection_ = false;

        newui::SubView* draggedView_ = nullptr;
        newui::Point dragStartPt_;
        bool dragStarted_ = false;
    };
}
