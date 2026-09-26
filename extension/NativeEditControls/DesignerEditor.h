#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include "ComponentEditor.h"
#include "DesignerArrange.h"
#include "MenuDesigner.h"
#include "NativeEditor.h"
#include "SelectionOverlay.h"
#include "ViewDesignerController.h"
#include "ViewDesignerModel.h"
#include "Workspace.h"

#include <newui/controllers.h>
#include <newui/dragndrop.h>
#include <newui/models.h>
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
    class DesignerEditor;

    // The designer's newui::Document - owns path/modified tracking and (via Document::save())
    // the ".bak" of the original file. readFromFile()/writeToFile() just hand back to the
    // editor's own Bundle-based load/save; see DesignerEditor::document().
    class DesignerDocument : public newui::Document
    {
    public:
        explicit DesignerDocument(DesignerEditor& editor) : editor_(editor) {}

    protected:
        bool readFromFile(const std::string& path) override;
        bool writeToFile(const std::string& path) override;

    private:
        DesignerEditor& editor_;
    };

    class DesignerEditor : public NativeEditor
    {
        friend class DesignerDocument;

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
        ViewDesignerModel& viewDesignerModel() { return *viewDesignerModel_; }
        const ViewDesignerModel& viewDesignerModel() const { return *viewDesignerModel_; }

        // Backs the toolbar's Undo/Redo buttons and PropertiesGrid's own
        // undo-aware property commits (PropertyEditor::setUndoStack(),
        // wired in setupUI()) - property edits only for now (see this
        // toolbar's own commit message/session notes for why adding/
        // deleting/moving controls isn't undo-aware yet).
        newui::UndoStack& undoStack() { return undoStack_; }
        const newui::UndoStack& undoStack() const { return undoStack_; }

        // A Toolbox entry dropped at rootLocalPt (this editor's root space, same as every mouse
        // handler's pt): creates the control from payload (Toolbox::dragPayloadFor()), adds it to
        // the innermost container under the point - rootViewProxy() if none - placed by that
        // container's own layout policy (free position at the drop point, flex insertion index,
        // grid cell, ...), as one undoable "Add <Class>" step. Returns false, changing nothing, for
        // a payload that isn't a Toolbox entry or a point off the design surface. Public so tests
        // can drive it without a real OLE drag.
        bool dropToolboxEntryAt(const std::wstring& payload, const newui::Point& rootLocalPt);

        // Copy / cut / paste / duplicate of the selected controls (Ctrl+C / X / V / D - keys arrive as newui::vk* codes - and the host's
        // Edit commands). A copy is each top-level selected control's whole subtree, serialized as a
        // saved file would hold it (DesignerClipboard) onto the system clipboard; a paste creates
        // fresh clones - new unique names, moved down-right by kPasteOffsetPixels per paste so they
        // don't sit exactly on the originals - into the selected container (else the nearest one
        // above the selection, else the design surface), re-kinds their LayoutParams for that
        // container's Layout, and selects them. Each is one undoable step. All return whether
        // anything was done.
        // Whether the canvas, rather than some other control, currently has keyboard attention: the
        // shortcuts above (and Delete) only act then. Decided by focus, which UIInputManager
        // resolves on every click - clicking the canvas clears it, clicking a field focuses that.
        bool canvasOwnsKeyboard() const;
        // Undo / redo one step (the toolbar buttons, Ctrl+Z / Shift+Ctrl+Z, and the host's Edit
        // commands all come here); false if there was nothing to undo/redo.
        bool undo();
        bool redo();
        bool copySelection();
        bool cutSelection();
        bool pasteFromClipboard();
        bool duplicateSelection();
        // Pastes already-serialized views (DesignerClipboard::serialize()) as pasteFromClipboard()
        // does, but from the given texts and offset instead of the system clipboard. Public so tests
        // can drive a paste without touching the real clipboard.
        bool pasteSerializedViews(const std::vector<std::string>& texts, float offset);
        static constexpr float kPasteOffsetPixels = 16.0f;

        // Arrange operations on the selection, each one undoable step (see DesignerArrange.h for the
        // rules): z-order within each parent (paint order - later is on top), and - for controls
        // whose position is their own to set (an AnchorLayout or layout-less parent) - alignment to
        // the primary (last-selected) control, even spacing, and matching its size. Views in a
        // Flex/Grid/Card parent are skipped: their layout owns their geometry. Return whether
        // anything changed.
        bool reorderSelection(ZOrderOp op);
        bool alignSelection(AlignKind kind);
        bool distributeSelection(DistributeKind kind);
        bool matchSizeSelection(MatchSizeKind kind);

        // The move-drag's cursor feedback: overrides view's own Cursor with a system kind (the
        // four-way arrow / hand), first parking its real one; endDragCursors() puts every parked
        // Cursor back. The real Cursor is a saved, user-editable property (Cursor.kind/path), so a
        // drag - or a plain click that only arms one - must never leave it changed. Public so tests
        // can check the save/restore without synthesizing mouse input.
        void beginDragCursor(newui::SubView* view, newui::CursorKind kind);
        void endDragCursors();

        // The ComponentEditor (per-class design-time verbs - see ComponentEditor.h) registered for
        // view's class, wired to this editor's undo stack and to refresh the outline/mark dirty/
        // repaint after each verb (and its undo/redo). nullptr if view's class has no editor or it
        // offers no verbs. Right-clicking a control shows these verbs as a context menu; public so
        // tests can run them without the blocking native popup.
        std::unique_ptr<ComponentEditor> createComponentEditorFor(newui::SubView* view);

        // Every ComponentEditor that applies to view: the class's own (createComponentEditorFor()) and,
        // when its Layout is a GridLayout, the row/column editor. The context menu lists each one's verbs.
        std::vector<std::unique_ptr<ComponentEditor>> createComponentEditorsFor(newui::SubView* view);

        // Double-click default action: the first applicable editor (class's own, then GridLayout)
        // that canEdit() opens its property in the Properties grid, selecting the property's owner
        // first when that's a child (a tab page). rootPt is where the double-click landed, if it
        // was one. Posted to the RunLoop when one is running, so the double-click's own focus
        // change can't close the new editor. Public for tests.
        void editComponent(newui::SubView* view, std::optional<newui::Point> rootPt = std::nullopt);

        // Menu editing on the design surface - open while a MenuBar is selected.
        MenuDesigner* menuDesigner() const { return menuDesigner_.get(); }

        // Source mode (the toolbar's Design/Source switch; public so tests can drive it). Entering
        // shows the document as text. Leaving applies edited text to the canvas as one undoable
        // step - or, if it doesn't parse, stays in Source with the error shown and returns false.
        bool setSourceMode(bool source);
        bool isSourceMode() const { return sourceMode_; }
        // The document as text - exactly what Save writes.
        std::string documentText() const;
        // Rebuilds the canvas from text as one undoable step (the old controls are detached and
        // kept, so undo puts them back and earlier undo steps stay valid). False, changing
        // nothing, with *error set if text doesn't parse.
        bool applyDocumentText(const std::string& text, std::string* error);

        // Hover feedback for a Toolbox drag at rootLocalPt (this editor's root space): highlights
        // the target container and shows where the control would land - an insertion line (flex),
        // cell (grid), or ghost outline (free position) - the same cues a canvas drag gets. Returns
        // whether a drop is possible there (false: foreign payload / off the surface - the drag
        // should show "not allowed"); either way stale feedback from an earlier point is cleared.
        bool hoverToolboxEntryAt(const std::wstring& payload, const newui::Point& rootLocalPt);
        void endToolboxHover();

        // Repaints the whole editor immediately (and forces the pending WM_PAINT through) -
        // RootView::markDirty() only schedules it on the RunLoop's idle queue, which doesn't run
        // while Windows' own modal DoDragDrop loop is in charge, so hover feedback needs this.
        void repaintNow();
        // What the hover is currently showing (nullptr / nullopt when none) - for tests.
        newui::SubView* toolboxHoverTarget() const { return toolboxHover_.active ? toolboxHover_.placement.target : nullptr; }
        std::optional<newui::Rect> toolboxHoverGhostRect() const { return toolboxHover_.active ? toolboxHover_.ghostRootRect : std::nullopt; }
        const GeometryEditResult* toolboxHoverResult() const { return toolboxHover_.active ? &toolboxHover_.placement.result : nullptr; }

        // The open document (path, modified flag, backup-on-first-overwrite) and the controller
        // that guards discarding it - New/Open go through documentController().
        // confirmDiscardChanges(); tests replace its prompt via setUnsavedChangesHandler().
        DesignerDocument& document() { return *document_; }
        newui::DocumentController& documentController() { return documentController_; }

        // NativeEditor's flag is replaced by document()'s own.
        // Unapplied Source edits count as unsaved changes too.
        bool isDirty() const override { return (document_ != nullptr && document_->isModified()) || hasUnappliedSource(); }
        void markDirty() override { if (document_ != nullptr) { document_->markModified(); } }

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

        // Blocks in a native popup until a verb is picked or the menu is dismissed.
        void showComponentContextMenu(newui::SubView* view, const newui::Point& rootLocalPt);

        // The selectable control under root-local pt on the design surface (nullptr on empty
        // canvas); false when pt isn't on the design surface at all.
        bool hitTestDesignSurface(const newui::Point& pt, newui::SubView*& target) const;

        // Left double-click on a control: editComponent().
        newui::SyncReturn handleMouseDblClick(newui::View& sender, const newui::Point& pt,
            std::uint32_t btnMask, std::uint32_t keyMask);

        // One authoritative onMouseMove/onMouseUp pair for root, covering both CanvasWell's
        // resize-guide drag and the canvas selection's own Move drag - deliberately NOT two
        // independent listeners on the same delegate (a real, found bug: Delegate<> calls every
        // listener unconditionally on every event, so two unrelated handlers each mutating shared
        // state - the cursor, especially - with no awareness of each other risks them fighting
        // over it). The two gestures are mutually exclusive by construction (handleMouseDownFor
        // Selection() never arms a move-drag when beginResizeDrag() already claimed the click), so
        // checking "is CanvasWell mid-resize" first, then "is a move-drag active", then falling
        // back to plain CanvasWell hover feedback, is an unambiguous priority order, not a guess.
        // Cursor feedback during either gesture is set directly on whatever View is actually being
        // dragged (entry.view/canvasWell), never on root/sender - RootView::cursorTargetAt()
        // (rootview.cpp) resolves whichever View WM_SETCURSOR actually reads from a *live
        // hit-test* at the current point whenever nothing is captured (true for any design-time
        // view, which a dragged canvas control always is), so setting the cursor anywhere else has
        // no visible effect at all - a real bug this consolidation also fixes.
        newui::SyncReturn handleMouseMove(newui::View& sender, const newui::Point& pt,
            std::uint32_t btnMask, std::uint32_t keyMask);
        newui::SyncReturn handleMouseUp(newui::View& sender, const newui::Point& pt,
            std::uint32_t btnMask, std::uint32_t keyMask);
        newui::SyncReturn handleMouseUpCore(newui::View& sender, const newui::Point& pt,
            std::uint32_t btnMask, std::uint32_t keyMask);

        // A MenuItem picked in the menu designer (nullptr: back to the MenuBar itself).
        void handleMenuItemSelected(newui::MenuItem* item);
        // Keeps the menu designer's columns on the bar as the canvas resizes.
        newui::SyncReturn handleCanvasWellSizeChanged(newui::View& sender, const newui::Size& size);

        // Deletes the current canvas selection (viewDesignerController_.
        // selected()) as one undo-aware step - only fires when the canvas
        // itself has focus (see setupUI()'s own comment on root->onKeyDown).
        newui::SyncReturn handleKeyDown(newui::View& sender, std::uint32_t keyMask,
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

        // Document Outline's own drag-and-drop gesture (DocumentOutline::onDropRequested) -
        // covers both reparenting (disposition Into) and same/cross-parent reordering
        // (Before/After), unified into one handler since the real mutation is the same shape
        // either way: resolve the real target parent + index, then setParent()/reorderChild().
        // Unlike buildReparentAction() above, there's no live drag/drop-point to speak of here
        // (the outline's own mouse handling only detects the gesture, see DocumentOutline.h's
        // own comment), so dragged's real, current bounds() stand in for both "where it starts"
        // (undo target) and "where it visually is right now" (converted into the new parent's
        // local space, the same on-screen-position-preserving placement buildReparentAction()
        // itself does) whenever the parent actually changes - pushes one real UndoableAction onto
        // undoStack_ directly, since there's no larger multi-entry gesture to compose into like
        // handleMouseUp() has.
        newui::SyncReturn handleOutlineDropRequested(DocumentOutline& sender, newui::SubView* dragged,
            newui::SubView* referenceRow, DocumentOutlineDropDisposition disposition);

        // PropertiesGrid's own Kind::ParentPicker provider/handler pair (wired in setupUI()) -
        // see each one's own definition comment (DesignerEditor.cpp) for the real reasoning.
        std::vector<std::pair<newui::SubView*, std::string>> parentCandidatesFor(newui::SubView* view) const;
        void handlePropertiesParentChangeRequested(newui::SubView* view, newui::SubView* newParent);

        // Refreshes viewDesignerModel_ after Workspace's own Toolbox-add
        // wiring mutates rootViewProxy()'s children directly (see
        // Workspace::onDesignSurfaceChanged's own comment).
        newui::SyncReturn handleDesignSurfaceChanged(Workspace& sender);

        newui::SyncReturn handleViewDesignerModelChanged(newui::Model& sender);

        // Toolbar handlers - New/Open/Save/Undo/Redo are a temporary
        // testing-phase convenience (see workspace()->newButton() etc.'s
        // own header comment, Workspace.h) - wired directly onto each
        // ToolbarButton's inherited Control::onClick in setupUI().
        newui::SyncReturn handleNewClicked(newui::Control& sender);

        // The design surface's drop target callbacks (positions are rootViewProxy()-local - see
        // dropToolboxEntryAt()/hoverToolboxEntryAt() for the root-space logic they forward to).
        newui::SyncReturn handleTextDragOver(newui::DropTarget& sender, const std::wstring& text,
            const newui::Point& localPt, newui::DropEffect& effect);
        newui::SyncReturn handleDragLeave(newui::DropTarget& sender);
        newui::SyncReturn handleTextDroppedAt(newui::DropTarget& sender, const std::wstring& text,
            const newui::Point& localPt);

        // Where a Toolbox control would be placed if dropped at rootLocalPt, as a fresh (unattached)
        // ghost view + the target container's policy resolution - shared by hover feedback and the
        // drop itself so they can't disagree. nullopt when the point is off the design surface.
        struct ToolboxPlacement
        {
            newui::SubView* target = nullptr;
            newui::Rect dropBounds;  // target-local, top-left at the point
            GeometryDragContext ctx;
            GeometryEditResult result;
            const LayoutEditingPolicy* policy = nullptr;
        };
        std::optional<ToolboxPlacement> toolboxPlacementAt(const newui::Point& rootLocalPt, newui::SubView* ghost) const;

        // Live state while a Toolbox drag hovers the surface (see hoverToolboxEntryAt()).
        struct ToolboxHover
        {
            bool active = false;
            ToolboxPlacement placement;
            std::optional<newui::Rect> ghostRootRect;  // FreePosition targets only
        };
        ToolboxHover toolboxHover_;
        std::unique_ptr<newui::SubView> hoverGhost_;  // stands in for the not-yet-created control

        // Empties the design surface back to a blank document: selection, every child of
        // rootViewProxy() (deleted), frameProxy() title, Outline, undo history, dirty flag. Shared
        // by New and load() - the Bundle reader merges into whatever children already exist
        // (position-based, in place) rather than replacing them, so load() must start from blank.
        // resetDocument: also return document() to untitled (New). load() passes false - the
        // Document adopts the new path itself once the load succeeds.
        void clearDocument(bool resetDocument);

        // The real load/save bodies, reached only through DesignerDocument (so path/modified
        // bookkeeping and the .bak happen in Document::load()/save()). UTF-8 paths.
        bool loadFromFile(const std::string& utf8Path);
        bool saveToFile(const std::string& utf8Path);

        newui::DocumentController documentController_;
        DesignerDocument* document_ = nullptr;  // owned by documentController_
        newui::SyncReturn handleOpenClicked(newui::Control& sender);
        newui::SyncReturn handleSaveClicked(newui::Control& sender);
        newui::SyncReturn handleUndoClicked(newui::Control& sender);
        newui::SyncReturn handleRedoClicked(newui::Control& sender);
        // Fired by undoStack_.onActionPushed (a real property commit) in
        // addition to the Undo/Redo handlers above - three real call
        // sites can each change canUndo()/canRedo(), so this is the one
        // place that actually pushes that state onto the two buttons.
        newui::SyncReturn handleUndoStackActionPushed(newui::UndoStack& sender, const newui::UndoableAction& action);
        // The Source page's own text history changed: the buttons follow it while it's showing.
        newui::SyncReturn handleSourceHistoryChanged(newui::text::TextModel& sender);
        // Follows whichever history is active: the document's UndoStack, or the Source page's text
        // history while that page is showing.
        void refreshUndoRedoButtons();

        // One dragged view's starting state, captured at mouseDown - parent is the view's real
        // parent() (never assumed to be rootViewProxy() - a selected view can be nested inside
        // any container), startBounds is its bounds() at drag start (parent-local, same space
        // setBounds() itself takes). policy is resolved once, at mouseDown, from parent->layout()
        // (see handleMouseDownForSelection()) - startResult is what policy->resolve() returned at
        // that same moment (zero delta), the undo target; lastResult is updated on every
        // handleMouseMove() call, the redo/doIt target and what drawCue() paints against.
        // pendingReparentTarget is non-null only while the drag point sits outside parent's own
        // bounds AND a different real container was found under the cursor there (see
        // findReparentTargetAt()) - tracked regardless of the source policy kind
        // (FreePosition/LinearReorder/GridCell); the within-parent resolve()/applyPreview() keeps
        // running unchanged the whole time (harmless even once the cursor is outside the
        // container - see handleMouseMove()'s own comment), buildReparentAction() takes
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

        struct ClonePlan
        {
            std::string text;             // DesignerClipboard::serialize() output
            newui::SubView* target;       // the container the clone is added to
        };
        // Creates a clone per plan and adds them all as one undoable step named description;
        // free-position targets place each clone at its source bounds moved by offset.
        bool insertClones(const std::vector<ClonePlan>& plans, const std::string& description, float offset);
        newui::SubView* pasteTargetForSelection() const;
        // Deletes the current selection as one undoable step. description empty = "Delete Control(s)".
        bool deleteSelection(const std::string& description = std::string());
        bool applyBoundsChanges(const std::vector<BoundsChange>& changes, const std::string& description);
        bool applyOrderChanges(const std::vector<OrderChange>& changes, const std::string& description);
        std::size_t pasteCount_ = 0;   // pastes since the last copy, for the cascading offset

        // Below this many pixels of total mouse movement since mouseDown, handleMouseMove()
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

        // The drag cursor (four-way arrow / hand) is shown by overriding the dragged view's own
        // Cursor - which is a real, editable, saved property (Cursor.kind/path). The original is
        // parked here for the duration of the drag and put back by endDragCursors(), so a drag (or
        // a plain click that arms one) never changes what the user chose. Keys are views with an
        // active override only.
        std::unordered_map<newui::SubView*, newui::Cursor> savedDragCursors_;
        newui::Point moveDragStartPt_;

        // Latches true the first time total movement crosses kMoveDragThresholdPixels since this
        // arming's own mouseDown - once true, stays true for the rest of this same drag (no need
        // to re-check every move once it's a real drag), reset to false each time
        // handleMouseDownForSelection() re-arms moveDragEntries_.
        bool moveDragStarted_ = false;

        Workspace* workspace_ = nullptr;
        SelectionOverlay* selectionOverlay_ = nullptr;
        ViewDesignerController viewDesignerController_;
        ViewDesignerModel* viewDesignerModel_ = nullptr;   // owned by viewDesignerController_
        newui::UndoStack undoStack_;
        // Cleared in the destructor; guards editComponent()'s posted task.
        std::shared_ptr<bool> aliveFlag_ = std::make_shared<bool>(true);
        std::unique_ptr<MenuDesigner> menuDesigner_;

        newui::SyncReturn handleModeChanged(newui::SegmentedControl& sender);
        // Whether Source is showing text that differs from what it last loaded.
        bool hasUnappliedSource() const;
        // While Source shows unedited text, keeps it in step with the document.
        void refreshSourceIfUnedited();
        void reloadSourceText();
        bool sourceMode_ = false;
        std::string sourceBaseline_;   // the text Source last loaded
        bool changingMode_ = false;
        // Removed in the destructor - the canvas well (in the root's tree) can outlive this editor
        // and keep laying out while the root is torn down.
        newui::Connection canvasWellSizeConnection_;
    };
}
