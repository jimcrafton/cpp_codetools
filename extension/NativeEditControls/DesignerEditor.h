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

        DesignerEditor(newui::RootView* rootView);

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

        bool setupUI(newui::RootView* root);

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

        Workspace* workspace_ = nullptr;
        SelectionOverlay* selectionOverlay_ = nullptr;
        ViewDesignerController viewDesignerController_;
        ViewDesignerModel viewDesignerModel_;
        newui::UndoStack undoStack_;
    };
}
