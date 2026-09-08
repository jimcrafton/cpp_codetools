#include "DesignerEditor.h"
#include "Logging.h"
#include "TextEncoding.h"

#include <newui/rootview.h>
#include <newui/rootviewproxy.h>
#include <newui/layout.h>
#include <newui/uicolormanager.h>
#include <newui/bundle.h>
#include <newui/dialogs.h>
#include <newui/frame.h>
#include <newui/viewbuilder.h>
#include <newui/keyboard_constants.h>

#include <utility>

namespace CodeToolsVsix
{
    namespace
    {
        // filePath is never assumed to be null-terminated (see
        // NativeEditor.h) - same convention CppEditor.cpp's own copyPath()
        // uses.
        std::wstring copyPath(const wchar_t* filePath, std::size_t filePathLength)
        {
            if (!filePath || filePathLength == 0)
            {
                return std::wstring();
            }
            return std::wstring(filePath, filePathLength);
        }

        // Splits at the final \ or / into (everything before, everything
        // after) - empty first element if there's no separator at all.
        std::pair<std::wstring, std::wstring> splitLastComponent(const std::wstring& path)
        {
            std::size_t pos = path.find_last_of(L"\\/");
            if (pos == std::wstring::npos)
            {
                return { std::wstring(), path };
            }
            return { path.substr(0, pos), path.substr(pos + 1) };
        }

        std::wstring stripExtension(const std::wstring& fileName)
        {
            std::size_t pos = fileName.find_last_of(L'.');
            return pos == std::wstring::npos ? fileName : fileName.substr(0, pos);
        }

        // Bare filename, minus extension - for the throwaway scratchFrame's
        // own name in load() below. Empty if path has no filename at all.
        std::string bundleDisplayNameFor(const std::wstring& path)
        {
            auto [fileDir, fileName] = splitLastComponent(path);
            return fileName.empty() ? std::string() : wideToUtf8(stripExtension(fileName));
        }

        // The real newui::Dialog::ShowOpenFile()/ShowSaveFile() (native
        // IFileDialog, dialogs.h) - not a raw GetOpenFileNameW()/
        // GetSaveFileNameW() call, which would've duplicated a real,
        // already-built mechanism. Filtered to *.newui specifically,
        // since load()/save() only ever accept a real "<root>\Resources\
        // <bundleName>.newui" path anyway (resolveBundleNameAndRoot()
        // above). Backs the toolbar's own Open/Save buttons - see their
        // own header comment (Workspace.h) for why those are only a
        // temporary testing-phase convenience. Returns an empty string
        // if the user cancels or the dialog couldn't be shown.
        std::wstring showNewuiFileDialog(HWND hwndOwner, bool forSave)
        {
            newui::FileDialogOptions options;
            options.title = forSave ? "Save Designer Document" : "Open Designer Document";
            options.defaultExtension = "newui";
            options.filters.push_back({"newui files", "*.newui"});

            std::string utf8Path;
            bool ok = forSave ? newui::Dialog::ShowSaveFile(hwndOwner, options, utf8Path)
                               : newui::Dialog::ShowOpenFile(hwndOwner, options, utf8Path);
            return ok ? utf8ToWide(utf8Path) : std::wstring();
        }
    }

    DesignerEditor::~DesignerEditor()
    {
        // See this declaration's own doc comment (DesignerEditor.h) - must
        // run before viewDesignerController_'s own destruction, which an
        // implicit/defaulted destructor wouldn't guarantee relative to
        // root's base-owned Overlay.
        if (getRootView() != nullptr) {
            getRootView()->setOverlay(nullptr);
        }
    }

    DesignerEditor::DesignerEditor(newui::RootView* rootView, newui::SubView* contentHost)
    {
        rootViewOwned_ = true;
        if (!setupUI(rootView, contentHost))
        {
            return;
        }

        setRootView(std::unique_ptr<newui::RootView>(rootView));
    }

    DesignerEditor::DesignerEditor(HWND hwndParent, int x, int y, int width, int height)
    {
        logToDebugOut(L"DesignerEditor");

        auto root = std::make_unique<newui::RootView>(
            hwndParent, NativeEditManager::moduleHandle(),
            newui::Rect(static_cast<float>(x), static_cast<float>(y), static_cast<float>(width), static_cast<float>(height)),
            "designerEditorRoot");

        if (!setupUI(root.get()))
        {
            return;
        }

        setRootView(std::move(root));
        logToDebugOut(L"DesignerEditor completed");
    }

    bool DesignerEditor::setupUI(newui::RootView* root, newui::SubView* contentHost)
    {
        root->style().setBackgroundColor(newui::UIColorManager::colorFor(newui::UIColorRole::WindowBackground));

        // root itself is never marked design-time - it's just this
        // editor's own hosting pane, not part of the edited document.
        // isDesignTime() no longer propagates from an owning RootView
        // (view.cpp) - Workspace's own constructor sets it explicitly on
        // exactly the pieces that need it (frameProxy_/rootViewProxy_),
        // and load() below does too for whatever gets read into
        // rootViewProxy_ - see each of those for why.

        // Workspace fills the whole pane - root itself is never the edited
        // document (see this class's own header comment); load()/save()
        // work against workspace_->rootViewProxy() instead. contentHost
        // (nullptr by default - see this class's own constructor comment)
        // is where workspace_'s own layout/child placement actually goes;
        // root-level concerns below (overlay, global mouse/key hooks,
        // initialize()) still always target root itself.
        newui::View* host = contentHost != nullptr ? static_cast<newui::View*>(contentHost) : static_cast<newui::View*>(root);
        auto hostLayout = std::make_unique<newui::FlexLayout>(newui::Orientation::Vertical);
        hostLayout->setSpacing(0.0f);
        hostLayout->setPadding(0.0f);
        host->setLayout(std::move(hostLayout));

        newui::ViewBuilder<Workspace> workspaceBuilder;
        workspaceBuilder.layoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
        workspace_ = workspaceBuilder.build();
        host->addChild(workspace_);

        // Selection + handles (designer-plan.md 6.1 item 3) - root owns the
        // overlay (Overlay isn't a SubView, see overlay.h), painted last on
        // top of the whole pane every repaint. root->onMouseDown fires
        // unconditionally for every mouse down anywhere in the pane (unlike
        // a hit-tested child's own onMouseDown - RootView::mouseDown(),
        // rootview.cpp), which is what lets handleMouseDownForSelection()
        // below check "is this click inside the design surface at all"
        // before hit-testing into it - a handler attached to rootViewProxy_
        // itself would never fire for a click that lands on one of its own
        // children instead (hitTestChildren() always dispatches to the
        // deepest hit target, not its ancestors).
        // workspace_->canvasWell(), not rootViewProxy()/frameProxy() - a
        // real, caught mistake: frameProxy_ is a *fixed* kDefaultCanvasWidth/
        // Height (640x460) rect, centered inside canvasWell_ (the actual
        // Splitter-constrained viewport pane - see Workspace.cpp's own
        // "canvasWell_ is the one that grows/shrinks" comment). Clipping to
        // frameProxy_/rootViewProxy_'s own bounds does nothing once the
        // window is narrow enough that the fixed 640px canvas genuinely
        // extends past canvasWell_'s own right edge, into the same screen
        // region the Properties pane occupies - canvasWell_ itself is the
        // one view guaranteed never to overlap the Toolbox/Properties
        // panes either way, so it's the right clip target regardless of
        // how frameProxy_'s own fixed size compares to it.
        auto selectionOverlay = std::make_unique<SelectionOverlay>(viewDesignerController_, workspace_->canvasWell());
        selectionOverlay_ = selectionOverlay.get();
        root->setOverlay(std::move(selectionOverlay));
        root->onMouseDown.add(this, &DesignerEditor::handleMouseDownForSelection);
        root->onMouseMove.add(this, &DesignerEditor::handleMouseMoveForResize);
        root->onMouseUp.add(this, &DesignerEditor::handleMouseUpForResize);

        // root's own onKeyDown only fires when nothing else has focus
        // (RootView::keyEvent(), rootview.cpp - it dispatches to
        // focusedSubView_ instead when one exists) - a design-time canvas
        // control never actually receives focus at all (RootView's own
        // design-time input gating), so this is exactly "Delete while the
        // canvas, not a Properties field or the Outline, has attention."
        root->onKeyDown.add(this, &DesignerEditor::handleKeyDownForDelete);

        // PropertiesGrid and Document Outline both learn about selection
        // changes this way - through ViewDesignerController's own
        // notification, not because this class knows they exist.
        viewDesignerController_.onSelectionChanged.add(this, &DesignerEditor::handleSelectionChanged);

        // viewDesignerModel_ is the real Model behind both
        // viewDesignerController_ (Controller::setModel(), previously
        // left unpointed - see ViewDesignerModel.h's own header comment)
        // and Document Outline's own DocumentOutlineModel adapter - wired
        // to workspace_->rootViewProxy() once, here, since that pointer's
        // identity never changes for this editor's lifetime (only its
        // children do, over time - see refresh()'s own call sites below
        // and in load()).
        viewDesignerModel_.setRoot(workspace_->rootViewProxy());
        viewDesignerController_.setModel(&viewDesignerModel_);
        workspace_->documentOutlinePane()->setViewDesignerModel(&viewDesignerModel_);
        workspace_->documentOutlinePane()->onSelectionActivated.add(this, &DesignerEditor::handleOutlineSelectionActivated);
        workspace_->onDesignSurfaceChanged.add(this, &DesignerEditor::handleDesignSurfaceChanged);

        // undoStack_ backs PropertiesGrid's own undo-aware property
        // commits, Workspace's own Toolbox-add wiring, this class's own
        // Delete handling below, and the toolbar's Undo/Redo buttons -
        // onActionPushed keeps their enabled state honest after any of
        // those, not just after an undo()/redo() click.
        workspace_->propertiesPane()->setUndoStack(&undoStack_);
        workspace_->setUndoStack(&undoStack_);
        undoStack_.onActionPushed.add(this, &DesignerEditor::handleUndoStackActionPushed);

        // New/Open/Save/Undo/Redo - temporary testing-phase convenience,
        // see workspace_->newButton() etc.'s own header comment
        // (Workspace.h) for why. Control::onClick, inherited by
        // ToolbarButton - not a ToolbarButton-specific delegate.
        workspace_->newButton()->onClick.add(this, &DesignerEditor::handleNewClicked);
        workspace_->openButton()->onClick.add(this, &DesignerEditor::handleOpenClicked);
        workspace_->saveButton()->onClick.add(this, &DesignerEditor::handleSaveClicked);
        workspace_->undoButton()->onClick.add(this, &DesignerEditor::handleUndoClicked);
        workspace_->redoButton()->onClick.add(this, &DesignerEditor::handleRedoClicked);

        if (!this->rootViewOwned_) {
            if (!root->initialize())
            {
                // Leave the base's RootView null - windowHandle() reports failure the same way
                // CppEditor's own constructor does on this same failure path.
                logToDebugOut(L"!root->initialize()");
                return false;
            }
        }

        // View::addChild()/setLayout() (which the ViewBuilder chain above
        // runs through) only ever call updateLayout() - never markDirty()/
        // invalidate() (see RootView::setBounds()'s own comment: only an
        // actual resize, via resizeImageBuffer(), triggers a real repaint
        // synchronously). On a RootView that's already shown (true for
        // this editor's testharness-hosted rootViewOwned_ case, and for
        // any real VS document pane by the time a later Open reaches
        // load() below), nothing else would ever ask Windows to paint the
        // Workspace tree just built - it would just sit correctly laid
        // out but never drawn until some unrelated interaction (e.g.
        // dragging a Splitter, which goes through Control's own hover/
        // press style().markDirty()) happened to touch it.
        root->markDirty();
        return true;
    }

    newui::SyncReturn DesignerEditor::handleMouseDownForSelection(newui::View& /*sender*/, const newui::Point& pt,
        std::uint32_t /*btnMask*/, std::uint32_t keyMask)
    {
        // Checked first, so grabbing a guide line always starts a resize
        // rather than a selection - CanvasWell.h's own class comment has
        // the full reasoning for why this is driven from here rather than
        // CanvasWell's own onMouseDown.
        CanvasWell* canvasWell = workspace_ ? workspace_->canvasWell() : nullptr;
        newui::Rect canvasWellBounds = canvasWell != nullptr ? SelectionOverlay::boundsInRootView(canvasWell) : newui::Rect();
        if (canvasWell != nullptr) {
            newui::Point canvasLocalPt(pt.x - canvasWellBounds.left(), pt.y - canvasWellBounds.top());
            if (canvasWell->beginResizeDrag(canvasLocalPt)) {
                return newui::SyncReturn::Handled;
            }
        }

        // Gate on canvasWellBounds first, before ever consulting
        // rootViewProxy()'s own bounds below - a real, reported bug
        // otherwise (same root cause SelectionOverlay's own clipView_ was
        // added to fix): frameProxy_ is a *fixed* 640x460 rect that can
        // extend past canvasWell's own edge into the Toolbox/Properties
        // panes' screen region once the window is narrow enough, so a
        // click on e.g. a Properties row's own expand arrow could still
        // satisfy surfaceBounds.contains(pt) below and silently change the
        // canvas selection underneath it. canvasWell is the one view
        // guaranteed never to overlap those panes, so nothing outside it
        // can possibly be a real click on the design surface.
        if (canvasWell == nullptr || !canvasWellBounds.contains(pt)) {
            return newui::SyncReturn::Ignored;
        }

        newui::RootViewProxy* surface = workspace_ ? workspace_->rootViewProxy() : nullptr;
        if (surface == nullptr || !surface->isVisible()) {
            return newui::SyncReturn::Ignored;
        }

        // pt is already root-local (same space RootView::mouseDown() passes
        // to onMouseDown), matching what boundsInRootView() computes - the
        // canvasWellBounds gate above already excluded anything outside
        // the visible canvas viewport; this one still matters on its own
        // terms too (frameProxy_'s fixed size can leave real empty margin
        // *inside* canvasWell around a smaller/centered canvas).
        newui::Rect surfaceBounds = SelectionOverlay::boundsInRootView(surface);
        if (!surfaceBounds.contains(pt)) {
            return newui::SyncReturn::Ignored;
        }

        newui::Point localPt(pt.x - surfaceBounds.left(), pt.y - surfaceBounds.top());
        newui::Point unused;
        newui::SubView* target = surface->hitTestChildren(localPt, unused);

        if ((keyMask & newui::kmCtrl) != 0) {
            viewDesignerController_.toggleSelection(target);
        } else {
            viewDesignerController_.selectExclusive(target);
        }

        // Same reasoning as setupUI()'s/load()'s own markDirty() calls -
        // nothing else asks Windows to repaint just because the overlay's
        // own selection state changed.
        getRootView()->markDirty();
        return newui::SyncReturn::Ignored;
    }

    newui::SyncReturn DesignerEditor::handleKeyDownForDelete(newui::View& /*sender*/, std::uint32_t /*keyMask*/,
        int /*keyCharVal*/, int /*repeatCount*/, std::uint32_t VKeyCode)
    {
        if (VKeyCode != static_cast<std::uint32_t>(newui::vkDelete) || workspace_ == nullptr) {
            return newui::SyncReturn::Ignored;
        }

        std::vector<newui::SubView*> selected = viewDesignerController_.selected();
        if (selected.empty()) {
            return newui::SyncReturn::Ignored;
        }

        // A selected view's real parent can be any real SubView in the
        // tree, not just rootViewProxy() directly (e.g. one nested inside
        // a container row) - removeChild()/addChild() must target that
        // real parent, not rootViewProxy() itself, or the view is left
        // attached where it already was (delete no-ops) and a later
        // undo re-attaches the same instance a second time, corrupting
        // the tree (two parents pointing at one child).
        std::vector<std::pair<newui::SubView*, newui::View*>> toDelete;
        for (newui::SubView* view : selected) {
            if (newui::View* parent = view->parent()) {
                toDelete.emplace_back(view, parent);
            }
        }
        if (toDelete.empty()) {
            return newui::SyncReturn::Ignored;
        }

        viewDesignerController_.clearSelection();

        newui::UndoableAction action;
        action.description = toDelete.size() == 1 ? "Delete Control" : "Delete Controls";
        // removeChild()/addChild() only ever detach/attach - never delete -
        // same raw-pointer ownership handoff Workspace's own Toolbox-add
        // wiring already relies on, so undoIt() can safely re-attach the
        // exact same instances rather than reconstructing them. Known,
        // narrow gap: if this pending action is discarded while its views
        // are in the detached state (undoStack_.clear(), e.g. from a
        // later "New") without ever running undoIt() first, those views
        // leak - accepted for now, same "New is a temporary testing-phase
        // convenience" scoping this toolbar's other buttons already carry.
        action.doIt = [this, toDelete]() {
            for (const auto& [view, parent] : toDelete) {
                parent->removeChild(view);
            }
            viewDesignerModel_.refresh();
            markDirty();
            getRootView()->markDirty();
        };
        action.undoIt = [this, toDelete]() {
            for (const auto& [view, parent] : toDelete) {
                parent->addChild(view);
            }
            viewDesignerModel_.refresh();
            markDirty();
            getRootView()->markDirty();
        };
        undoStack_.push(action);
        return newui::SyncReturn::Handled;
    }

    newui::SyncReturn DesignerEditor::handleMouseMoveForResize(newui::View& /*sender*/, const newui::Point& pt,
        std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/)
    {
        CanvasWell* canvasWell = workspace_ ? workspace_->canvasWell() : nullptr;
        if (canvasWell == nullptr) {
            return newui::SyncReturn::Ignored;
        }

        newui::Rect canvasWellBounds = SelectionOverlay::boundsInRootView(canvasWell);
        newui::Point canvasLocalPt(pt.x - canvasWellBounds.left(), pt.y - canvasWellBounds.top());

        if (canvasWell->isResizingDrag()) {
            canvasWell->continueResizeDrag(canvasLocalPt);
            // Same reasoning as setupUI()'s/load()'s own markDirty() calls -
            // nothing else asks Windows to repaint just because
            // continueResizeDrag() moved FrameProxy.
            getRootView()->markDirty();
            return newui::SyncReturn::Handled;
        }

        // Not dragging - still gives hover feedback (a resize cursor) when
        // the mouse is over a grabbable guide line.
        canvasWell->updateHoverCursor(canvasLocalPt);
        return newui::SyncReturn::Ignored;
    }

    newui::SyncReturn DesignerEditor::handleMouseUpForResize(newui::View& /*sender*/, const newui::Point& /*pt*/,
        std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/)
    {
        CanvasWell* canvasWell = workspace_ ? workspace_->canvasWell() : nullptr;
        if (canvasWell == nullptr || !canvasWell->isResizingDrag()) {
            return newui::SyncReturn::Ignored;
        }
        canvasWell->endResizeDrag();
        return newui::SyncReturn::Handled;
    }

    newui::SyncReturn DesignerEditor::handleSelectionChanged(ViewDesignerController& sender)
    {
        if (workspace_ != nullptr) {
            workspace_->propertiesPane()->setSelection(sender.primary());
            workspace_->documentOutlinePane()->setSelection(sender.selected());
        }
        return newui::SyncReturn::Ignored;
    }

    newui::SyncReturn DesignerEditor::handleOutlineSelectionActivated(DocumentOutline& /*sender*/,
        const std::vector<newui::SubView*>& views)
    {
        viewDesignerController_.setSelection(views);
        // Same reasoning as handleMouseDownForSelection()'s own markDirty()
        // call - nothing else repaints just because the overlay's selection
        // state changed.
        getRootView()->markDirty();
        return newui::SyncReturn::Ignored;
    }

    newui::SyncReturn DesignerEditor::handleDesignSurfaceChanged(Workspace& /*sender*/)
    {
        viewDesignerModel_.refresh();
        markDirty();
        return newui::SyncReturn::Ignored;
    }

    newui::SyncReturn DesignerEditor::handleNewClicked(newui::Control& /*sender*/)
    {
        newui::RootViewProxy* surface = workspace_ != nullptr ? workspace_->rootViewProxy() : nullptr;
        if (surface == nullptr) {
            return newui::SyncReturn::Ignored;
        }

        viewDesignerController_.clearSelection();

        // removeChild() only detaches - it never deletes (same "raw-
        // pointer ownership handoff" contract View::addChild() itself
        // has, see Toolbox::onEntryActivated's own comment) - copy the
        // list first since removeChild() mutates the live childViews().
        std::vector<newui::SubView*> children = surface->childViews();
        for (newui::SubView* child : children) {
            surface->removeChild(child);
            delete child;
        }

        workspace_->frameProxy()->setTitle(std::string());
        viewDesignerModel_.refresh();
        undoStack_.clear();
        refreshUndoRedoButtons();
        clearDirty();
        getRootView()->markDirty();
        return newui::SyncReturn::Handled;
    }

    newui::SyncReturn DesignerEditor::handleOpenClicked(newui::Control& /*sender*/)
    {
        std::wstring path = showNewuiFileDialog(windowHandle(), /*forSave=*/false);
        if (path.empty()) {
            return newui::SyncReturn::Ignored;
        }
        if (!load(path.c_str(), path.size())) {
            logToDebugOut(L"DesignerEditor: toolbar Open failed");
        }
        return newui::SyncReturn::Handled;
    }

    newui::SyncReturn DesignerEditor::handleSaveClicked(newui::Control& /*sender*/)
    {
        std::wstring path = showNewuiFileDialog(windowHandle(), /*forSave=*/true);
        if (path.empty()) {
            return newui::SyncReturn::Ignored;
        }
        if (!save(path.c_str(), path.size())) {
            logToDebugOut(L"DesignerEditor: toolbar Save failed");
        }
        return newui::SyncReturn::Handled;
    }

    newui::SyncReturn DesignerEditor::handleUndoClicked(newui::Control& /*sender*/)
    {
        if (undoStack_.canUndo()) {
            undoStack_.undo();
        }
        refreshUndoRedoButtons();
        getRootView()->markDirty();
        return newui::SyncReturn::Handled;
    }

    newui::SyncReturn DesignerEditor::handleRedoClicked(newui::Control& /*sender*/)
    {
        if (undoStack_.canRedo()) {
            undoStack_.redo();
        }
        refreshUndoRedoButtons();
        getRootView()->markDirty();
        return newui::SyncReturn::Handled;
    }

    newui::SyncReturn DesignerEditor::handleUndoStackActionPushed(newui::UndoStack& /*sender*/,
        const newui::UndoableAction& /*action*/)
    {
        refreshUndoRedoButtons();
        return newui::SyncReturn::Ignored;
    }

    void DesignerEditor::refreshUndoRedoButtons()
    {
        if (workspace_ == nullptr) {
            return;
        }
        workspace_->undoButton()->setEnabled(undoStack_.canUndo());
        workspace_->redoButton()->setEnabled(undoStack_.canRedo());

        // Undo takes priority when both exist - matches the toolbar's own
        // left-to-right Undo-then-Redo button order.
        std::string status;
        if (undoStack_.canUndo()) {
            status = "Undo: " + undoStack_.undoDescription();
        } else if (undoStack_.canRedo()) {
            status = "Redo: " + undoStack_.redoDescription();
        }
        workspace_->undoRedoStatusLabel()->setText(status);
    }

    bool DesignerEditor::load(const wchar_t* filePath, std::size_t filePathLength)
    {
        if (!workspace_)
        {
            logToDebugOut(L"DesignerEditor::load: workspace is null (construction must have failed)");
            return false;
        }

        std::wstring path = copyPath(filePath, filePathLength);
        std::string absolutePath = wideToUtf8(path);
        std::string bundleName = bundleDisplayNameFor(path);

        // designMode=true propagates setDesignTime(true) onto freshly-
        // constructed children (reflection.h's TypedClass<T>::read()).
        if (!newui::Bundle::instance().loadRootViewFromFile(*workspace_->rootViewProxy(), absolutePath, /*designMode=*/true))
        {
            logToDebugOut(L"DesignerEditor::load: Bundle::loadRootViewFromFile failed");
            return false;
        }

        // A real top-level RootView's own saved "bounds" is its own OS
        // window's size - meaningless once rootViewProxy_ is a managed
        // AnchorLayout child of frameProxy_ instead, but loadRootView()
        // just read it as an ordinary property and called setBounds() with
        // it regardless (rootViewProxy_'s bounds are no different from any
        // other View property to the reflection read path). SubView::
        // setBounds() only ever re-arranges *its own* children via
        // updateLayout() (subview.cpp) - it never asks its own parent to
        // re-verify its position, the same one-directional mechanism
        // Splitter's own resize cascade relies on, just never in reverse -
        // so nothing corrects this on its own. frameProxy_'s own
        // AnchorLayout is what actually owns rootViewProxy_'s real
        // position/size (its AnchorLayoutParams, set up in Workspace's
        // constructor); re-running it here reasserts that, overriding
        // whatever bogus size the file's own bounds happened to contain.
        workspace_->frameProxy()->updateLayout();

        // The file's own top-level "title" is a Frame property, a sibling
        // of "rootView" (see writeRootView()'s own comment) - not something
        // loadRootView() above ever touches. Bundle has no lighter-weight
        // way to read just that one property, so this uses loadFrame() on
        // a throwaway, never-initialize()'d Frame purely to read its
        // title() back out; scratchFrame (and whatever rootView tree
        // loadFrame() reconstructs onto it, which this never touches) goes
        // out of scope right after. A failed load here just leaves
        // frameProxy_'s title unset - not fatal to the real rootView load
        // above, which already succeeded.
        newui::Frame scratchFrame;
        scratchFrame.setName(bundleName);
        if (newui::Bundle::instance().loadFrameFromFile(scratchFrame, absolutePath))
        {
            workspace_->frameProxy()->setTitle(scratchFrame.getTitle());
        }

        // A real top-level RootView's own "visible" flag is never actually
        // exercised (a real OS window's visibility is controlled by
        // ShowWindow, not this field), so it's commonly saved as false -
        // loadRootView() just faithfully applied that onto rootViewProxy_,
        // undoing Workspace's own constructor setVisible(true) call.
        // RootViewProxy is an ordinary child in this pane's paint tree
        // (unlike a real RootView), so an invisible one means
        // paintChildren() skips its whole loaded subtree. Re-assert it here
        // rather than in Bundle::loadRootView() itself, since this is only
        // meaningful for a target standing in for a real top-level window.
        workspace_->rootViewProxy()->setVisible(true);

        // Same reasoning as setupUI()'s own markDirty() call - loadRootView()
        // just repopulated rootViewProxy()'s children in place, and nothing
        // in that path asks Windows to actually paint the result.
        getRootView()->markDirty();

        // loadRootView() repopulated rootViewProxy()'s children directly,
        // bypassing viewDesignerModel_ entirely (same "View never
        // notifies a Model of structural changes on its own" gap
        // refresh()'s own header comment documents) - Document Outline
        // (and anything else reading viewDesignerModel_) needs this to
        // pick up the freshly loaded tree.
        viewDesignerModel_.refresh();

        // A freshly loaded document has no undo history of its own - same
        // "New" already does (handleNewClicked()). Without this, a stale
        // Undo/Redo state from before this load (or from editing a
        // previous document without ever clicking New) would carry over.
        viewDesignerController_.clearSelection();
        undoStack_.clear();
        refreshUndoRedoButtons();

        clearDirty();
        return true;
    }

    bool DesignerEditor::save(const wchar_t* filePath, std::size_t filePathLength)
    {
        if (!workspace_)
        {
            logToDebugOut(L"DesignerEditor::save: workspace is null (construction must have failed)");
            return false;
        }

        std::wstring path = copyPath(filePath, filePathLength);
        std::string absolutePath = wideToUtf8(path);

        // No Bundle::setExecutableDirOverride() call here - see load()'s
        // own comment. writeRootViewToFile() resolves directly against
        // absolutePath.
        if (!newui::Bundle::instance().writeRootViewToFile(*workspace_->rootViewProxy(), absolutePath, /*designMode=*/true))
        {
            logToDebugOut(L"DesignerEditor::save: Bundle::writeRootViewToFile failed");
            return false;
        }

        clearDirty();
        return true;
    }

    bool DesignerEditor::execCommand(EditorCommand /*command*/, std::uint32_t /*flags*/, const EditorCommandArgs* /*args*/)
    {
        logToDebugOut(L"DesignerEditor::execCommand: no document model yet, stub");
        return true;
    }
}
