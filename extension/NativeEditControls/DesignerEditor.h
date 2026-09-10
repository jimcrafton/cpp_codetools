#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "NativeEditor.h"
#include "SelectionOverlay.h"
#include "ViewDesignerController.h"
#include "ViewDesignerModel.h"
#include "Workspace.h"

#include <newui/undostack.h>

namespace CodeToolsVsix
{
    // What NativeEditManager::createEditor constructs for DocumentType::Designer (see
    // NativeEditControlApi.h) - a real newui::RootView hosting a Workspace (the Designer's own
    // chrome skeleton - Toolbox/Outline/Properties panes around a FrameProxy/RootViewProxy design
    // surface, see Workspace.h and bluesky/designer-plan.md's view-hierarchy section), so that
    // dispatch has a genuinely distinct second NativeEditor subclass to hand off to rather than
    // always falling back to CppEditor. Unlike CppEditor, it deliberately has no TextControl of
    // its own - this editor's own hosting RootView is never itself the edited document (see
    // FrameProxy/RootViewProxy's own class comments for why a real RootView can't host a loaded
    // document's tree directly alongside Workspace's chrome) - load()/save() work against
    // workspace()->rootViewProxy() instead.
    class DesignerEditor : public NativeEditor
    {
    public:
        DesignerEditor(HWND hwndParent, int x, int y, int width, int height);

        // contentHost: see NativeEditManager::createEditor()'s own comment - nullptr (default)
        // means workspace_ is added directly to rootView, matching every pre-existing caller.
        // root-level concerns (background, the selection overlay, the global mouse/key hooks
        // below) always target rootView itself regardless - only workspace_'s own layout/
        // placement moves to contentHost when one is given.
        DesignerEditor(newui::RootView* rootView, newui::SubView* contentHost = nullptr);

        // Explicit (not implicit-default): root's own Overlay (set in
        // setupUI() below) holds a const reference into
        // viewDesignerController_ (via SelectionOverlay), and root itself
        // is owned by the *base* NativeEditor - base-class subobjects
        // always outlive derived members' own destruction (C++'s ordinary
        // member/base teardown order), so without this,
        // viewDesignerController_ would already be destroyed by the time
        // root's Overlay (and the reference it holds) actually goes away,
        // a real dangling-reference window - most concretely in the
        // rootViewOwned_ case (the newui::RootView* constructor below),
        // where NativeEditor's own destructor releases rootView_ instead
        // of destroying it at all, so root can outlive *this* by an
        // arbitrary amount. Explicitly clearing the overlay here runs
        // before any member/base destruction begins, while
        // viewDesignerController_ still exists.
        ~DesignerEditor() override;

        // contentHost: see the constructor's own comment above.
        bool setupUI(newui::RootView* root, newui::SubView* contentHost = nullptr);

        // Non-owning - workspace_ is owned by the View tree (root->addChild()'d in setupUI()),
        // freed when root is. Exposes the Toolbox/Outline/Properties panes and the FrameProxy/
        // RootViewProxy pair for future panel wiring, and for tests.
        Workspace* workspace() const { return workspace_; }

        // Non-owning - selectionOverlay_ is owned by root itself (root->
        // setOverlay() in setupUI()), freed when root is.
        SelectionOverlay* selectionOverlay() const { return selectionOverlay_; }

        // Owns the real selection state/logic (ViewDesignerController.h) -
        // PropertiesGrid and Document Outline both subscribe to its
        // onSelectionChanged independently, without this class needing to
        // know they exist (see the onSelectionChanged wiring in setupUI()).
        ViewDesignerController& viewDesignerController() { return viewDesignerController_; }
        const ViewDesignerController& viewDesignerController() const { return viewDesignerController_; }

        // The real Model behind viewDesignerController() (Controller::
        // setModel(), wired in setupUI()) and Document Outline's own
        // DocumentOutlineModel (a thin TreeModel adapter over this, see
        // DocumentOutline.h) - one shared, live view onto
        // workspace()->rootViewProxy()'s tree, not a copy. refresh()d
        // whenever that tree structurally changes elsewhere (load()
        // below, Workspace's own Toolbox-add wiring via
        // onDesignSurfaceChanged).
        ViewDesignerModel& viewDesignerModel() { return viewDesignerModel_; }
        const ViewDesignerModel& viewDesignerModel() const { return viewDesignerModel_; }

        // Backs the toolbar's Undo/Redo buttons and PropertiesGrid's own
        // undo-aware property commits (PropertyEditor::setUndoStack(),
        // wired in setupUI()) - property edits only for now (see this
        // toolbar's own commit message/session notes for why adding/
        // deleting/moving controls isn't undo-aware yet).
        newui::UndoStack& undoStack() { return undoStack_; }
        const newui::UndoStack& undoStack() const { return undoStack_; }

        // filePath can be any real, absolute ".newui" path - a user's
        // project document, unrelated to this DLL's own resource root.
        // Uses Bundle::loadRootViewFromFile()/loadFrameFromFile() directly,
        // not setExecutableDirOverride() (see bundle.h).
        bool load(const wchar_t* filePath, std::size_t filePathLength) override;

        // Write-side counterpart to load() - Bundle::writeRootViewToFile().
        bool save(const wchar_t* filePath, std::size_t filePathLength) override;

        bool execCommand(EditorCommand command, std::uint32_t flags, const EditorCommandArgs* args) override;

    private:
        // Selection + handles (designer-plan.md 6.1 item 3), and (checked
        // first, so a resize drag always wins over starting a new
        // selection) starting a CanvasWell guide-line resize drag - both
        // hooked onto root's own onMouseDown (fires unconditionally for
        // every mouse down, unlike a hit-tested child's onMouseDown - see
        // RootView::mouseDown(), rootview.cpp) so a click anywhere in the
        // pane can be checked against the design surface's/canvas well's
        // own bounds and, if inside, hit-tested against its real children.
        // See DesignerEditor.cpp for the full reasoning.
        // Also arms a potential move-drag (see moveDragEntries_'s own comment below) for whatever
        // the selection ends up being right after this same click, when the click landed on a
        // real control (target != nullptr) - the same click that selects a control is also the
        // one that can start dragging it, matching ordinary design-tool click-and-drag behavior.
        newui::SyncReturn handleMouseDownForSelection(newui::View& sender, const newui::Point& pt,
            std::uint32_t btnMask, std::uint32_t keyMask);

        // Continues/ends a CanvasWell resize drag started above, or (when
        // not dragging) just updates its hover cursor - hooked onto root's
        // own onMouseMove/onMouseUp for the same "fires unconditionally,
        // before hit-testing" reason handleMouseDownForSelection() is.
        newui::SyncReturn handleMouseMoveForResize(newui::View& sender, const newui::Point& pt,
            std::uint32_t btnMask, std::uint32_t keyMask);
        newui::SyncReturn handleMouseUpForResize(newui::View& sender, const newui::Point& pt,
            std::uint32_t btnMask, std::uint32_t keyMask);

        // Moving the current canvas selection (designer-plan.md's deferred "Move" piece, built
        // after Add/Delete) - a separate onMouseMove/onMouseUp pair rather than folded into the
        // CanvasWell-resize handlers above (those are about dragging the artboard's own guide
        // lines, an unrelated gesture; Delegate<> already supports more than one listener on the
        // same root->onMouseMove/onMouseUp). Live feedback during the drag is a plain setBounds()
        // per dragged view (works the same regardless of whether that view's real parent has a
        // Layout of its own - a view nested in a layout-managed container just snaps back to
        // wherever that Layout puts it next, an accepted, honest gap rather than special-cased
        // away); only mouseUp commits one real UndoableAction, and only for a real, nonzero total
        // drag - a plain click that never actually moved the mouse pushes nothing.
        newui::SyncReturn handleMouseMoveForMove(newui::View& sender, const newui::Point& pt,
            std::uint32_t btnMask, std::uint32_t keyMask);
        newui::SyncReturn handleMouseUpForMove(newui::View& sender, const newui::Point& pt,
            std::uint32_t btnMask, std::uint32_t keyMask);

        // Deletes the current canvas selection (viewDesignerController_.
        // selected()) as one undo-aware step - only fires when the canvas
        // itself has focus (see setupUI()'s own comment on root->onKeyDown).
        newui::SyncReturn handleKeyDownForDelete(newui::View& sender, std::uint32_t keyMask,
            int keyCharVal, int repeatCount, std::uint32_t VKeyCode);

        // Pushes viewDesignerController_'s new selection into both
        // Properties (primary() only - it's a single-object panel) and
        // Document Outline (the full selected() list, since Outline's own
        // TreeView supports real multi-select - see DocumentOutline::
        // setSelection()'s own comment for why this doesn't loop forever
        // with handleOutlineSelectionActivated below).
        newui::SyncReturn handleSelectionChanged(ViewDesignerController& sender);

        // The reverse direction - Document Outline's own row click/
        // Ctrl+click resolved a new selection on its own (real TreeView
        // input, not a programmatic setSelection() call - see
        // DocumentOutline::onSelectionActivated's own comment) - applies
        // it to the shared controller, which in turn calls
        // handleSelectionChanged() above (a no-op back into the Outline,
        // since it's already showing exactly this selection).
        newui::SyncReturn handleOutlineSelectionActivated(DocumentOutline& sender, const std::vector<newui::SubView*>& views);

        // Document Outline's own drag-and-drop reparent gesture (DocumentOutline::
        // onReparentRequested) - unlike buildReparentAction() above, there's no live drag/
        // drop-point to speak of here (the outline's own mouse handling only detects the
        // gesture, see DocumentOutline.h's own comment), so dragged's real, current bounds()
        // stand in for both "where it starts" (undo target) and "where it visually is right
        // now" (converted into target's local space, the same on-screen-position-preserving
        // placement buildReparentAction() itself does) - pushes one real UndoableAction onto
        // undoStack_ directly, since there's no larger multi-entry gesture to compose into
        // like handleMouseUpForMove() has.
        newui::SyncReturn handleOutlineReparentRequested(DocumentOutline& sender, newui::SubView* dragged, newui::SubView* target);

        // Refreshes viewDesignerModel_ after Workspace's own Toolbox-add
        // wiring mutates rootViewProxy()'s children directly (see
        // Workspace::onDesignSurfaceChanged's own comment).
        newui::SyncReturn handleDesignSurfaceChanged(Workspace& sender);

        // Toolbar handlers - New/Open/Save/Undo/Redo are a temporary
        // testing-phase convenience (see workspace()->newButton() etc.'s
        // own header comment, Workspace.h) - wired directly onto each
        // ToolbarButton's inherited Control::onClick in setupUI().
        newui::SyncReturn handleNewClicked(newui::Control& sender);
        newui::SyncReturn handleOpenClicked(newui::Control& sender);
        newui::SyncReturn handleSaveClicked(newui::Control& sender);
        newui::SyncReturn handleUndoClicked(newui::Control& sender);
        newui::SyncReturn handleRedoClicked(newui::Control& sender);
        // Fired by undoStack_.onActionPushed (a real property commit) in
        // addition to the Undo/Redo handlers above - three real call
        // sites can each change canUndo()/canRedo(), so this is the one
        // place that actually pushes that state onto the two buttons.
        newui::SyncReturn handleUndoStackActionPushed(newui::UndoStack& sender, const newui::UndoableAction& action);
        void refreshUndoRedoButtons();

        // One dragged view's starting state, captured at mouseDown - parent is the view's real
        // parent() (never assumed to be rootViewProxy() - a selected view can be nested inside
        // any container), startBounds is its bounds() at drag start (parent-local, same space
        // setBounds() itself takes). policy is resolved once, at mouseDown, from parent->layout()
        // (see handleMouseDownForSelection()) - startResult is what policy->resolve() returned at
        // that same moment (zero delta), the undo target; lastResult is updated on every
        // handleMouseMoveForMove() call, the redo/doIt target and what drawCue() paints against.
        // pendingReparentTarget is non-null only while the drag point sits outside parent's own
        // bounds AND a different real container was found under the cursor there (see
        // findReparentTargetAt()) - tracked regardless of the source policy kind
        // (FreePosition/LinearReorder/GridCell); the within-parent resolve()/applyPreview() keeps
        // running unchanged the whole time (harmless even once the cursor is outside the
        // container - see handleMouseMoveForMove()'s own comment), buildReparentAction() takes
        // over on drop if this ends up set.
        struct MoveDragEntry {
            newui::SubView* view;
            newui::View* parent;
            newui::Rect startBounds;
            const LayoutEditingPolicy* policy;
            GeometryEditResult startResult;
            GeometryEditResult lastResult;
            newui::Point lastPt;
            newui::SubView* pendingReparentTarget = nullptr;
        };

        // Builds the context resolve()/applyPreview()/commit() all need for entry, given the
        // drag's current point (moveDragStartPt_ is always the start) - shared by every mouse
        // handler below so ctx construction can't drift out of sync between them.
        GeometryDragContext dragContextFor(const MoveDragEntry& entry, const newui::Point& currentPt) const;

        // Where entry.view would be on screen right now (root-local) if it had been freely
        // FreePosition-dragged the whole time - the same delta math FreePositionPolicy::
        // resolve() itself uses, computed independently of entry.view's own real bounds().
        // Needed because a LinearReorder/GridCell-sourced entry's real bounds() are managed by
        // its own *source* container's layout the whole drag, never tracking the cursor at all -
        // used by both buildReparentAction() (at drop) and activeMoveDragCues() (live, for the
        // reparent-target insertion-line cue) so they can't disagree about where the view
        // "really" is.
        newui::Rect currentDraggedRootLocalBounds(const MoveDragEntry& entry) const;

        // The innermost real container (ToolboxRegistry::isContainer()) under rootLocalPt, or
        // rootViewProxy() itself as the ultimate fallback (always a valid drop target regardless
        // of its own container-ness) - walks up from whatever's actually hit there via parent().
        // dragged and its own descendants are never returned (excluded from the walk's starting
        // point entirely) - a FreePosition drag already tracks the cursor via setBounds(), so an
        // ordinary hit-test would otherwise just re-hit the thing being dragged. Returns nullptr
        // only when rootLocalPt falls outside the design surface entirely.
        newui::SubView* findReparentTargetAt(const newui::Point& rootLocalPt, newui::SubView* dragged) const;

        // Builds the UndoableAction for one entry whose drop actually reparents it - view moves to
        // target via the real, safe View::setParent(), then target's own policy places it (resolved
        // fresh, zero-delta, from the view's current on-screen position converted into target's
        // local space) same as a brand-new placement would be; undoIt() reverses via entry's own
        // original policy/parent/startResult - already exactly what's needed to restore the
        // pre-drag state. Not const - its own doIt()/undoIt() capture `this` to call
        // viewDesignerModel_.refresh()/markDirty() (real structural changes, unlike an ordinary
        // same-parent Move commit), which need non-const access.
        newui::UndoableAction buildReparentAction(const MoveDragEntry& entry, newui::SubView* target);

        // What SelectionOverlay's own paint() should draw for whatever Move drag is active right
        // now - wired in via selectionOverlay_->setActiveDragCuesProvider() in setupUI(). Empty
        // whenever moveDragEntries_ is empty or the drag never crossed kMoveDragThresholdPixels
        // (a plain click has nothing to show a cue for).
        std::vector<ActiveGeometryDrag> activeMoveDragCues() const;

        // The pending reparent target(s) (see MoveDragEntry's own comment) SelectionOverlay's
        // paint() should highlight right now - wired in via
        // selectionOverlay_->setReparentTargetProvider() in setupUI().
        std::vector<newui::SubView*> reparentTargets() const;

        // Below this many pixels of total mouse movement since mouseDown, handleMouseMoveForMove()
        // doesn't touch bounds() at all - a real, caught bug otherwise: with no threshold at all,
        // an ordinary click-to-select (armed the same as any potential drag, see
        // handleMouseDownForSelection()'s own comment) still generates at least one WM_MOUSEMOVE
        // before the matching mouseUp even with no perceptible hand movement, calling setBounds()
        // "live" for a plain click - confirmed hitting a live newui::SubView::setBounds()
        // breakpoint from nothing but a click. Matches ordinary design-tool click-vs-drag
        // distinction (a small dead zone before a click gesture counts as dragging).
        static constexpr float kMoveDragThresholdPixels = 3.0f;

        // Non-empty only between a mouseDown that hit a real control and the matching mouseUp -
        // root-local (moveDragStartPt_) since pt itself always is; a pure translation delta
        // (currentPt - moveDragStartPt_) is the same delta in every view's own parent-local space
        // too (nothing in this tree scales or rotates), so startBounds + that delta is always the
        // right new parent-local position regardless of how deep view sits.
        std::vector<MoveDragEntry> moveDragEntries_;
        newui::Point moveDragStartPt_;

        // Latches true the first time total movement crosses kMoveDragThresholdPixels since this
        // arming's own mouseDown - once true, stays true for the rest of this same drag (no need
        // to re-check every move once it's a real drag), reset to false each time
        // handleMouseDownForSelection() re-arms moveDragEntries_.
        bool moveDragStarted_ = false;

        Workspace* workspace_ = nullptr;
        SelectionOverlay* selectionOverlay_ = nullptr;
        ViewDesignerController viewDesignerController_;
        ViewDesignerModel viewDesignerModel_;
        newui::UndoStack undoStack_;
    };
}
