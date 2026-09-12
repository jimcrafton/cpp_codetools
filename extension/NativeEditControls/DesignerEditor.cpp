#include "DesignerEditor.h"
#include "Logging.h"
#include "TextEncoding.h"

#include <newui/rootview.h>
#include <newui/rootviewproxy.h>
#include <newui/cursor.h>
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
        // Mirrors View::hitTestChildren()'s own recursive logic (view.cpp) exactly, but skips
        // exclude (and never recurses into its subtree at all) - needed because hitTestChildren()
        // itself has no such concept, and a FreePosition drag's cursor is *always* inside the
        // dragged view's own live-tracked bounds (a geometric invariant: the cursor's offset
        // within it never changes once grabbed), so an ordinary hit-test re-hits the thing being
        // dragged before it ever reaches whatever real container is actually underneath/around it -
        // a real, reported bug once a dragged view's own parent is rootViewProxy() itself (the
        // dragged view becomes rootViewProxy()'s own frontmost child, permanently self-occluding
        // every nested sibling container).
        newui::SubView* hitTestExcluding(const newui::View& container, const newui::Point& localPt, newui::SubView* exclude)
        {
            newui::Point contentPt(localPt.x + container.origin().x, localPt.y + container.origin().y);
            const std::vector<newui::SubView*>& children = container.childViews();
            for (auto it = children.rbegin(); it != children.rend(); ++it) {
                newui::SubView* child = *it;
                if (child == exclude || !child->isVisible()) {
                    continue;
                }
                const newui::Rect& bounds = child->bounds();
                if (!bounds.contains(contentPt)) {
                    continue;
                }
                newui::Point childLocalPt(contentPt.x - bounds.left(), contentPt.y - bounds.top());
                if (newui::SubView* deeper = hitTestExcluding(*child, childLocalPt, exclude)) {
                    return deeper;
                }
                return child;
            }
            return nullptr;
        }

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
        selectionOverlay_->setActiveDragCuesProvider([this]() { return activeMoveDragCues(); });
        selectionOverlay_->setReparentTargetProvider([this]() { return reparentTargets(); });
        root->setOverlay(std::move(selectionOverlay));
        root->onMouseDown.add(this, &DesignerEditor::handleMouseDownForSelection);
        root->onMouseMove.add(this, &DesignerEditor::handleMouseMove);
        root->onMouseUp.add(this, &DesignerEditor::handleMouseUp);

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
        workspace_->documentOutlinePane()->onDropRequested.add(this, &DesignerEditor::handleOutlineDropRequested);
        workspace_->onDesignSurfaceChanged.add(this, &DesignerEditor::handleDesignSurfaceChanged);

        // undoStack_ backs PropertiesGrid's own undo-aware property
        // commits, Workspace's own Toolbox-add wiring, this class's own
        // Delete handling below, and the toolbar's Undo/Redo buttons -
        // onActionPushed keeps their enabled state honest after any of
        // those, not just after an undo()/redo() click.
        workspace_->propertiesPane()->setUndoStack(&undoStack_);
        // PropertiesModel::Kind::ParentPicker's own candidate list/commit path - see
        // parentCandidatesFor()/handlePropertiesParentChangeRequested()'s own comments.
        workspace_->propertiesPane()->setParentCandidatesProvider(
            [this](newui::SubView* view) { return parentCandidatesFor(view); });
        workspace_->propertiesPane()->setParentChangeRequestedHandler(
            [this](newui::SubView* view, newui::SubView* newParent) {
                handlePropertiesParentChangeRequested(view, newParent);
            });
        workspace_->setUndoStack(&undoStack_);
        workspace_->setPrimarySelectionProvider([this]() { return viewDesignerController_.primary(); });
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

        // Arms a potential move-drag for whatever's selected now (empty if target was null and
        // nothing else was already selected, or if a Ctrl+click just toggled the only selected
        // view off) - handleMouseMove()/handleMouseUp() do nothing with a move-drag at all while
        // moveDragEntries_ is empty. A view with no real parent() (shouldn't happen for anything
        // actually attached to the tree, but real - a freshly-toolbox-created, not-yet-attached
        // instance could theoretically reach here) is skipped rather than captured with a null
        // parent. A view whose parent's real Layout affords no per-child geometry edit at all
        // (policyFor(...).kind() == None - CardLayout, or any future/unrecognized Layout subtype)
        // is skipped too - matching how the real running app behaves, that Layout always computes
        // this view's position; there's no free "wherever you left it" input to drag in the first
        // place, so dragging is refused outright rather than allowed and left to silently disagree
        // with the layout later. AnchorLayout/no-Layout, FlexLayout, and GridLayout parents are
        // all now draggable (previously only AnchorLayout/no-Layout was) - see
        // LayoutEditingPolicy.h's own FreePosition/LinearReorder/GridCell policies.
        moveDragEntries_.clear();
        moveDragStarted_ = false;
        // TEMPORARY diagnostic - remove once the "no move-drag entry armed" investigation is done.
        
        if (target != nullptr) {
            moveDragStartPt_ = pt;
            for (newui::SubView* view : viewDesignerController_.selected()) {
                newui::View* parent = view->parent();
                if (parent == nullptr) {                    
                    continue;
                }
                const LayoutEditingPolicy& policy = policyFor(parent->layout());
                if (policy.kind() == GeometryEditKind::None) {
                    
                    continue;
                }
                
                newui::Rect startBounds = view->bounds();
                GeometryDragContext ctx;
                ctx.view = view;
                ctx.parent = parent;
                ctx.startBounds = startBounds;
                ctx.startPt = pt;
                ctx.currentPt = pt;
                GeometryEditResult startResult = policy.resolve(ctx);
                moveDragEntries_.push_back({ view, parent, startBounds, &policy, startResult, startResult, pt });
            }
        }
        return newui::SyncReturn::Ignored;
    }

    GeometryDragContext DesignerEditor::dragContextFor(const MoveDragEntry& entry, const newui::Point& currentPt) const
    {
        GeometryDragContext ctx;
        ctx.view = entry.view;
        ctx.parent = entry.parent;
        ctx.startBounds = entry.startBounds;
        ctx.startPt = moveDragStartPt_;
        ctx.currentPt = currentPt;
        return ctx;
    }

    std::vector<ActiveGeometryDrag> DesignerEditor::activeMoveDragCues() const
    {
        std::vector<ActiveGeometryDrag> cues;
        if (!moveDragStarted_) {
            return cues;  // a plain click that never crossed the drag threshold has nothing to show
        }
        cues.reserve(moveDragEntries_.size());
        for (const MoveDragEntry& entry : moveDragEntries_) {
            if (entry.pendingReparentTarget != nullptr) {
                // Poised to reparent into a different container - the ordinary same-parent cue
                // (still resolved against entry.parent) would be misleading now, so it's skipped
                // entirely in favor of the reparent-target box (SelectionOverlay's own
                // reparentTargetProvider_). A LinearReorder target additionally gets a real
                // insertion-line cue here, resolved fresh against the target itself - the same
                // "view isn't actually attached there yet, don't trust its own bounds()" trap
                // buildReparentAction() already has to work around.
                const LayoutEditingPolicy& targetPolicy = policyFor(entry.pendingReparentTarget->layout());
                if (targetPolicy.kind() == GeometryEditKind::LinearReorder) {
                    newui::Rect viewRootLocalBounds = currentDraggedRootLocalBounds(entry);
                    newui::Point targetRootLocalOrigin = SelectionOverlay::boundsInRootView(entry.pendingReparentTarget).pos();
                    newui::Rect targetLocalBounds(
                        viewRootLocalBounds.left() - targetRootLocalOrigin.x, viewRootLocalBounds.top() - targetRootLocalOrigin.y,
                        viewRootLocalBounds.size().width, viewRootLocalBounds.size().height);

                    GeometryDragContext targetCtx;
                    targetCtx.view = entry.view;
                    targetCtx.parent = entry.pendingReparentTarget;
                    targetCtx.startBounds = targetLocalBounds;
                    targetCtx.startPt = entry.lastPt;
                    targetCtx.currentPt = entry.lastPt;

                    ActiveGeometryDrag drag;
                    drag.policy = &targetPolicy;
                    drag.ctx = targetCtx;
                    drag.result = targetPolicy.resolve(targetCtx);
                    drag.isReparentTargetCue = true;
                    cues.push_back(drag);
                }
                continue;
            }

            // Still within its own current parent - LinearReorder gets no cue at all here (the
            // real live reflow, siblings physically shifting out of the way, already reads as
            // clear feedback on its own; a real, caught bug otherwise: this branch used to also
            // draw the insertion line for an ordinary same-row reorder - "visually too much"
            // once that line started drawing something real instead of the old highlight box it
            // originally replaced). FreePosition/GridCell still get theirs, unaffected.
            if (entry.policy->kind() == GeometryEditKind::LinearReorder) {
                continue;
            }

            ActiveGeometryDrag drag;
            drag.policy = entry.policy;
            drag.ctx = dragContextFor(entry, entry.lastPt);
            drag.result = entry.lastResult;
            cues.push_back(drag);
        }
        return cues;
    }

    newui::SubView* DesignerEditor::findReparentTargetAt(const newui::Point& rootLocalPt, newui::SubView* dragged) const
    {
        newui::RootViewProxy* surface = workspace_ ? workspace_->rootViewProxy() : nullptr;
        if (surface == nullptr) {
            return nullptr;
        }
        newui::Rect surfaceBounds = SelectionOverlay::boundsInRootView(surface);
        if (!surfaceBounds.contains(rootLocalPt)) {
            return nullptr;
        }

        newui::Point localPt(rootLocalPt.x - surfaceBounds.left(), rootLocalPt.y - surfaceBounds.top());
        newui::SubView* hit = hitTestExcluding(*surface, localPt, dragged);
        newui::View* start = (hit != nullptr) ? static_cast<newui::View*>(hit) : static_cast<newui::View*>(surface);

        for (newui::View* v = start; v != nullptr; v = v->parent()) {
            if (v == surface) {
                return surface;  // always a valid fallback, regardless of its own container-ness
            }
            if (auto* sub = dynamic_cast<newui::SubView*>(v)) {
                if (ToolboxRegistry::isContainer(sub)) {
                    return sub;
                }
            }
        }
        return surface;
    }

    newui::Rect DesignerEditor::currentDraggedRootLocalBounds(const MoveDragEntry& entry) const
    {
        newui::Rect oldParentRootLocalBounds = SelectionOverlay::boundsInRootView(entry.parent);
        newui::Point dragDelta = entry.lastPt - moveDragStartPt_;
        return newui::Rect(
            oldParentRootLocalBounds.left() + entry.startBounds.left() + dragDelta.x,
            oldParentRootLocalBounds.top() + entry.startBounds.top() + dragDelta.y,
            entry.startBounds.size().width, entry.startBounds.size().height);
    }

    newui::UndoableAction DesignerEditor::buildReparentAction(const MoveDragEntry& entry, newui::SubView* target)
    {
        newui::SubView* view = entry.view;
        newui::View* oldParent = entry.parent;
        const LayoutEditingPolicy* oldPolicy = entry.policy;

        newui::Rect viewRootLocalBounds = currentDraggedRootLocalBounds(entry);

        // The view's current on-screen position, converted from root-local into target's own
        // local space - the same "translation only, no scale/rotate anywhere" invariant every
        // other geometry computation in this file already relies on.
        newui::Point targetRootLocalOrigin = SelectionOverlay::boundsInRootView(target).pos();
        newui::Rect targetLocalBounds(
            viewRootLocalBounds.left() - targetRootLocalOrigin.x, viewRootLocalBounds.top() - targetRootLocalOrigin.y,
            viewRootLocalBounds.size().width, viewRootLocalBounds.size().height);

        GeometryDragContext newCtx;
        newCtx.view = view;
        newCtx.parent = target;
        newCtx.startBounds = targetLocalBounds;
        newCtx.startPt = entry.lastPt;
        newCtx.currentPt = entry.lastPt;
        const LayoutEditingPolicy& newPolicy = policyFor(target->layout());
        GeometryEditResult newResult = newPolicy.resolve(newCtx);
        // Zero-delta commit (start == end) against the fresh context - reuses the same,
        // already-tested placement logic a brand-new same-parent commit uses (writes fresh
        // AnchorLayoutParams for FreePosition, a real reorderChild()/grid-cell set otherwise),
        // rather than a bare applyPreview() that would skip that bookkeeping. Only ever
        // .doIt() is used from this - it isn't pushed onto undoStack_ itself.
        newui::UndoableAction newPlacement = newPolicy.commit(newCtx, newResult, newResult);

        GeometryDragContext oldCtx;
        oldCtx.view = view;
        oldCtx.parent = oldParent;
        oldCtx.startBounds = entry.startBounds;
        // Same reasoning, restoring the exact pre-drag placement under the original parent.
        newui::UndoableAction oldPlacement = oldPolicy->commit(oldCtx, entry.startResult, entry.startResult);

        newui::UndoableAction action;
        action.description = "Reparent Control";
        // Unlike an ordinary same-parent Move commit, this actually changes the tree's real
        // structure (which parent owns view) - viewDesignerModel_.refresh() is what Document
        // Outline's own TreeController re-derives its rows from (same call Add/Delete already
        // make for their own structural changes); without it, Outline keeps showing the
        // pre-reparent shape until something unrelated happens to trigger a refresh.
        action.doIt = [this, view, target, doIt = newPlacement.doIt]() {
            view->setParent(target);
            doIt();
            viewDesignerModel_.refresh();
            markDirty();
            getRootView()->markDirty();
        };
        action.undoIt = [this, view, oldParent, doIt = oldPlacement.doIt]() {
            view->setParent(oldParent);
            doIt();
            viewDesignerModel_.refresh();
            markDirty();
            getRootView()->markDirty();
        };
        return action;
    }

    namespace {
        // Recursive walk backing parentCandidatesFor() below - node == exclude short-circuits
        // before recursing into its children, so the whole subtree rooted at the View actually
        // being reparented (never a legal target for itself, per View::setParent()'s own cycle
        // guard) is skipped rather than just node itself. isRoot always includes
        // viewDesignerModel_.root() (rootViewProxy()) regardless of ToolboxRegistry::isContainer()
        // - it has no Layout of its own to satisfy that check, but Workspace's own Toolbox-add
        // fallback (targetParent = isContainer(selected) ? selected : rootViewProxy_) already
        // treats it as an always-valid container, so this matches that same convention.
        void collectParentCandidates(newui::SubView* node, const std::string& breadcrumb, newui::SubView* exclude,
            bool isRoot, std::vector<std::pair<newui::SubView*, std::string>>& out)
        {
            if (node == nullptr || node == exclude) {
                return;
            }
            std::string label = breadcrumb.empty() ? node->name() : breadcrumb + " > " + node->name();
            if (isRoot || ToolboxRegistry::isContainer(node)) {
                out.emplace_back(node, label);
            }
            for (newui::SubView* child : node->childViews()) {
                collectParentCandidates(child, label, exclude, false, out);
            }
        }
    }

    // Backs PropertiesGrid's own ParentPicker dropdown (PropertiesModel::Kind::ParentPicker) -
    // every real container view currently reachable, excluding view itself and its own
    // descendants (the same cycle guard View::setParent() already enforces, applied here too so
    // the dropdown never even offers an illegal choice), labeled with a full breadcrumb path
    // rather than a plain name() - names aren't guaranteed unique across the tree (two sibling
    // SubViews can easily share a default name), so a flat name list could be ambiguous.
    std::vector<std::pair<newui::SubView*, std::string>> DesignerEditor::parentCandidatesFor(newui::SubView* view) const
    {
        std::vector<std::pair<newui::SubView*, std::string>> candidates;
        collectParentCandidates(viewDesignerModel_.root(), std::string(), view, true, candidates);
        return candidates;
    }

    // Commits a Parent-dropdown pick through the exact same undo-aware reparent path the canvas
    // drag and Document Outline drag already use (disposition Into == "become a child of
    // newParent, appended at the end") - one shared code path, not a third, parallel
    // implementation of the same reparent-with-position-preserved logic.
    void DesignerEditor::handlePropertiesParentChangeRequested(newui::SubView* view, newui::SubView* newParent)
    {
        if (workspace_ == nullptr || workspace_->documentOutlinePane() == nullptr) {
            return;
        }
        handleOutlineDropRequested(*workspace_->documentOutlinePane(), view, newParent, DocumentOutlineDropDisposition::Into);
    }

    std::vector<newui::SubView*> DesignerEditor::reparentTargets() const
    {
        std::vector<newui::SubView*> targets;
        if (!moveDragStarted_) {
            return targets;
        }
        for (const MoveDragEntry& entry : moveDragEntries_) {
            if (entry.pendingReparentTarget != nullptr) {
                targets.push_back(entry.pendingReparentTarget);
            }
        }
        return targets;
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

    newui::SyncReturn DesignerEditor::handleMouseMove(newui::View& /*sender*/, const newui::Point& pt,
        std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/)
    {
        // CanvasWell's own resize-guide drag takes priority - handleMouseDownForSelection() never
        // arms a move-drag at all once beginResizeDrag() has already claimed the click, so the two
        // gestures are mutually exclusive by construction; check first regardless.
        CanvasWell* canvasWell = workspace_ ? workspace_->canvasWell() : nullptr;
        if (canvasWell != nullptr && canvasWell->isResizingDrag()) {
            newui::Rect canvasWellBounds = SelectionOverlay::boundsInRootView(canvasWell);
            newui::Point canvasLocalPt(pt.x - canvasWellBounds.left(), pt.y - canvasWellBounds.top());
            canvasWell->continueResizeDrag(canvasLocalPt);
            // Same reasoning as setupUI()'s/load()'s own markDirty() calls -
            // nothing else asks Windows to repaint just because
            // continueResizeDrag() moved FrameProxy.
            getRootView()->markDirty();
            return newui::SyncReturn::Handled;
        }

        if (!moveDragEntries_.empty()) {
            if (!moveDragStarted_) {
                // Squared-distance compare - same threshold test, no sqrt needed.
                newui::Point delta = pt - moveDragStartPt_;
                float distSq = delta.x * delta.x + delta.y * delta.y;
                if (distSq < kMoveDragThresholdPixels * kMoveDragThresholdPixels) {
                    return newui::SyncReturn::Ignored;
                }
                moveDragStarted_ = true;
            }

            for (MoveDragEntry& entry : moveDragEntries_) {
                GeometryDragContext ctx = dragContextFor(entry, pt);
                entry.lastResult = entry.policy->resolve(ctx);
                entry.lastPt = pt;
                entry.policy->applyPreview(ctx, entry.lastResult);

                // Cross-container reparenting - works for any source policy kind (FreePosition,
                // LinearReorder, GridCell), not just FreePosition: the within-parent
                // resolve()/applyPreview() above keeps running exactly as it always did (harmless
                // to keep computing a FlexLayout reorder/grid-cell placement even once the cursor
                // hovers over a different container - it just settles at whichever end/cell that
                // math lands on until a real drop happens). Always hit-tests (no "only once outside
                // parent's own bounds" gate - a real, caught bug: once parent is rootViewProxy()
                // itself, every nested sibling container is *within* its bounds, so that gate could
                // never fire for the "move a top-level child into a nested container" direction at
                // all) - findReparentTargetAt() already excludes entry.view's own subtree from the
                // hit-test, so this is cheap and correct even while hovering directly over the
                // dragged view's own live position. On drop, buildReparentAction() doesn't care what
                // the source policy was - only entry.startResult (already tracked regardless of
                // kind) and target's own policy matter.
                entry.pendingReparentTarget = nullptr;
                newui::SubView* candidate = findReparentTargetAt(pt, entry.view);
                if (candidate != nullptr && candidate != entry.parent) {
                    entry.pendingReparentTarget = candidate;
                }

                // Real per-drag cursor feedback, same "a plain arrow gives no sense a drag is even
                // happening" reasoning as Document Outline's own drag cursor - a hand once this
                // entry has found a real cross-container reparent target, an ordinary move
                // (four-way arrow) otherwise. Set directly on entry.view, the actual dragged
                // control - NOT root/sender: RootView::cursorTargetAt() (rootview.cpp) resolves
                // whichever View WM_SETCURSOR reads via a live hit-test whenever nothing is
                // captured (always true here, since a dragged canvas control is design-time
                // content, which capturedSubView_ never holds - see resolveInteractiveHit()), and
                // that hit-test always lands on the dragged view itself while its drag is live
                // (hitTestExcluding()'s own comment: the cursor never leaves its own bounds mid-
                // drag) - setting the cursor anywhere else is invisible, a real bug this
                // consolidation also fixes.
                entry.view->cursor().setCursorKind(
                    entry.pendingReparentTarget != nullptr ? newui::CursorKind::Hand : newui::CursorKind::SizeAll);
            }

            // Same reasoning as this method's own CanvasWell-drag branch above - nothing else asks
            // Windows to repaint just because applyPreview() moved/reordered/re-celled these views
            // (or just because a drag cue now needs painting where it didn't before).
            getRootView()->markDirty();
            return newui::SyncReturn::Handled;
        }

        // Neither gesture is active - root->onMouseMove fires for every mouse move anywhere in the
        // whole window, not just while actually hovering canvasWell, so only give it hover
        // feedback while pt is genuinely within its own real bounds (a real, found bug: this used
        // to call updateHoverCursor() unconditionally regardless of pt, so canvasWell's own
        // resize-guide hover/cursor logic ran - and kept resetting its cursor to Arrow - on every
        // single move, even while the cursor was over some other pane entirely that happens to sit
        // in front of/around its own screen area).
        if (canvasWell != nullptr) {
            newui::Rect canvasWellBounds = SelectionOverlay::boundsInRootView(canvasWell);
            if (canvasWellBounds.contains(pt)) {
                newui::Point canvasLocalPt(pt.x - canvasWellBounds.left(), pt.y - canvasWellBounds.top());
                canvasWell->updateHoverCursor(canvasLocalPt);
            }
        }
        return newui::SyncReturn::Ignored;
    }

    newui::SyncReturn DesignerEditor::handleMouseUp(newui::View& /*sender*/, const newui::Point& pt,
        std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/)
    {
        CanvasWell* canvasWell = workspace_ ? workspace_->canvasWell() : nullptr;
        if (canvasWell != nullptr && canvasWell->isResizingDrag()) {
            canvasWell->endResizeDrag();
            return newui::SyncReturn::Handled;
        }

        if (moveDragEntries_.empty()) {
            return newui::SyncReturn::Ignored;
        }

        std::vector<MoveDragEntry> entries = std::move(moveDragEntries_);
        moveDragEntries_.clear();
        bool started = moveDragStarted_;
        moveDragStarted_ = false;
        // Restores each dragged view's own cursor - see handleMouseMove()'s own comment on why
        // this has to be set on the actual dragged view, never root/sender.
        for (MoveDragEntry& entry : entries) {
            entry.view->cursor().setCursorKind(newui::CursorKind::Arrow);
        }

        // A plain click, or a real mouse-down/up pair that never crossed
        // kMoveDragThresholdPixels, leaves every view exactly where applyPreview() never touched -
        // skip pushing a no-op undo entry for it, matching Delete's own "nothing to do" early-outs
        // elsewhere in this file.
        if (!started) {
            return newui::SyncReturn::Ignored;
        }

        // One UndoableAction per dragged entry, each already resolved/committed against its own
        // real policy (a multi-select drag can freely mix FreePosition/LinearReorder/GridCell
        // entries, one per view's own parent) - composed below into a single combined step so a
        // multi-select Move still undoes/redoes as one action, matching every other multi-view
        // gesture in this file (see handleKeyDownForDelete()).
        std::vector<newui::UndoableAction> actions;
        actions.reserve(entries.size());
        for (const MoveDragEntry& entry : entries) {
            if (entry.pendingReparentTarget != nullptr && entry.pendingReparentTarget != entry.parent) {
                actions.push_back(buildReparentAction(entry, entry.pendingReparentTarget));
                continue;
            }
            GeometryDragContext ctx = dragContextFor(entry, pt);
            actions.push_back(entry.policy->commit(ctx, entry.startResult, entry.lastResult));
        }

        newui::UndoableAction action;
        action.description = actions.size() == 1 ? actions.front().description : "Move Controls";
        action.doIt = [this, actions]() {
            for (const newui::UndoableAction& sub : actions) {
                sub.doIt();
            }
            markDirty();
            getRootView()->markDirty();
        };
        action.undoIt = [this, actions]() {
            for (auto it = actions.rbegin(); it != actions.rend(); ++it) {
                it->undoIt();
            }
            markDirty();
            getRootView()->markDirty();
        };
        undoStack_.push(action);
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

    newui::SyncReturn DesignerEditor::handleOutlineDropRequested(DocumentOutline& /*sender*/,
        newui::SubView* dragged, newui::SubView* referenceRow, DocumentOutlineDropDisposition disposition)
    {
        if (dragged == nullptr || referenceRow == nullptr) {
            return newui::SyncReturn::Ignored;
        }
        newui::View* oldParent = dragged->parent();
        if (oldParent == nullptr) {
            return newui::SyncReturn::Ignored;
        }

        // Into means "become a child of referenceRow itself"; Before/After mean "become a
        // sibling of referenceRow, in referenceRow's own real parent" - referenceRow can never be
        // dragged itself or one of its own descendants (DocumentOutline::dropTargetAt() already
        // refuses that gesture outright), so newParent can never be dragged or a descendant of it
        // either.
        newui::View* newParent = disposition == DocumentOutlineDropDisposition::Into
            ? static_cast<newui::View*>(referenceRow) : referenceRow->parent();
        if (newParent == nullptr) {
            return newui::SyncReturn::Ignored;
        }

        // Desired index within newParent's real children once dragged is (or would be) taken out
        // of it - the same "list after the dragged view is removed" convention View::
        // reorderChild()/LinearReorderPolicy already use. Into always means "append at the end"
        // (the loop never breaks early, so targetIndex ends up counting every other real child);
        // Before/After mean "adjacent to referenceRow", computed identically whether newParent is
        // dragged's current parent (a pure reorder) or a different one (a position-precise
        // reparent).
        std::size_t targetIndex = 0;
        for (newui::SubView* sibling : newParent->childViews()) {
            if (sibling == dragged) {
                continue;
            }
            if (disposition != DocumentOutlineDropDisposition::Into && sibling == referenceRow) {
                break;
            }
            ++targetIndex;
        }
        if (disposition == DocumentOutlineDropDisposition::After) {
            ++targetIndex;
        }

        std::size_t startIndex = 0;
        for (newui::SubView* sibling : oldParent->childViews()) {
            if (sibling == dragged) {
                break;
            }
            ++startIndex;
        }

        if (newParent == oldParent && targetIndex == startIndex) {
            // A genuine no-op (already exactly there) - matches this file's own established
            // "don't push a no-op undo entry" restraint elsewhere (handleMouseUp, Delete).
            return newui::SyncReturn::Ignored;
        }

        if (newParent == oldParent) {
            // A pure reorder - no placement to preserve, the governing Layout already repaints
            // every sibling in its new position via updateLayout(), same as the canvas' own
            // LinearReorderPolicy.
            newui::UndoableAction action;
            action.description = "Reorder Control";
            action.doIt = [this, dragged, newParent, targetIndex]() {
                newParent->reorderChild(dragged, targetIndex);
                viewDesignerModel_.refresh();
                markDirty();
                getRootView()->markDirty();
            };
            action.undoIt = [this, dragged, oldParent, startIndex]() {
                oldParent->reorderChild(dragged, startIndex);
                viewDesignerModel_.refresh();
                markDirty();
                getRootView()->markDirty();
            };
            undoStack_.push(action);
            return newui::SyncReturn::Handled;
        }

        // Crosses a real parent boundary - same on-screen-position-preserving reparent this
        // handler always did for Into, now followed by an explicit reorderChild() to land at the
        // requested index (a no-op for Into, which already wants the end) instead of wherever
        // commit() would otherwise resolve/append it.
        //
        // No live drag here (see this method's own header comment) - dragged->bounds() right now
        // is both the undo target (oldCtx.startBounds below) and, converted into newParent's
        // local space, the "on-screen position preserved" placement buildReparentAction() itself
        // computes from a real drag's current point instead.
        newui::Rect startBounds = dragged->bounds();
        const LayoutEditingPolicy& oldPolicy = policyFor(oldParent->layout());
        GeometryDragContext oldCtx;
        oldCtx.view = dragged;
        oldCtx.parent = oldParent;
        oldCtx.startBounds = startBounds;
        GeometryEditResult startResult = oldPolicy.resolve(oldCtx);

        newui::Rect viewRootLocalBounds = SelectionOverlay::boundsInRootView(dragged);
        newui::Point targetRootLocalOrigin = SelectionOverlay::boundsInRootView(newParent).pos();
        newui::Rect targetLocalBounds(
            viewRootLocalBounds.left() - targetRootLocalOrigin.x, viewRootLocalBounds.top() - targetRootLocalOrigin.y,
            viewRootLocalBounds.size().width, viewRootLocalBounds.size().height);

        GeometryDragContext newCtx;
        newCtx.view = dragged;
        newCtx.parent = newParent;
        newCtx.startBounds = targetLocalBounds;
        const LayoutEditingPolicy& newPolicy = policyFor(newParent->layout());
        GeometryEditResult newResult = newPolicy.resolve(newCtx);
        newui::UndoableAction newPlacement = newPolicy.commit(newCtx, newResult, newResult);
        newui::UndoableAction oldPlacement = oldPolicy.commit(oldCtx, startResult, startResult);

        newui::UndoableAction action;
        action.description = "Reparent Control";
        action.doIt = [this, dragged, newParent, targetIndex, doIt = newPlacement.doIt]() {
            dragged->setParent(newParent);
            doIt();
            newParent->reorderChild(dragged, targetIndex);
            viewDesignerModel_.refresh();
            markDirty();
            getRootView()->markDirty();
        };
        action.undoIt = [this, dragged, oldParent, startIndex, doIt = oldPlacement.doIt]() {
            dragged->setParent(oldParent);
            doIt();
            oldParent->reorderChild(dragged, startIndex);
            viewDesignerModel_.refresh();
            markDirty();
            getRootView()->markDirty();
        };
        undoStack_.push(action);
        return newui::SyncReturn::Handled;
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
