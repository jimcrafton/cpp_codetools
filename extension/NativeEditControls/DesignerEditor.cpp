#include "DesignerEditor.h"
#include "Logging.h"
#include "TextEncoding.h"

#include <newui/rootview.h>
#include <newui/rootviewproxy.h>
#include <newui/layout.h>
#include <newui/uicolormanager.h>
#include <newui/bundle.h>
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

        // DesignerEditor edits "<root>\Resources\<bundleName>.newui" files -
        // the same convention bundle.h itself documents and Frame Map's own
        // naming already uses. Bundle::instance() has no idea where the
        // user's actual project lives (it's hosted inside devenv.exe, whose
        // own exe directory means nothing here) - this derives Bundle's
        // executableDir()-shaped root (the "<root>" above) plus the bare
        // bundleName from the real absolute path VS hands load()/save(),
        // to feed Bundle::setExecutableDirOverride() before every real call.
        // Returns false if path doesn't look like it's under a \Resources\
        // folder at all.
        bool resolveBundleNameAndRoot(const std::wstring& path, std::wstring& outRoot, std::string& outBundleName)
        {
            auto [fileDir, fileName] = splitLastComponent(path);
            if (fileDir.empty() || fileName.empty())
            {
                return false;
            }

            auto [root, resourcesFolder] = splitLastComponent(fileDir);
            if (root.empty() || _wcsicmp(resourcesFolder.c_str(), L"Resources") != 0)
            {
                return false;
            }

            outRoot = root;
            outBundleName = wideToUtf8(stripExtension(fileName));
            return true;
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

    DesignerEditor::DesignerEditor(newui::RootView* rootView)
    {
        rootViewOwned_ = true;
        if (!setupUI(rootView))
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

    bool DesignerEditor::setupUI(newui::RootView* root)
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
        // work against workspace_->rootViewProxy() instead.
        newui::ViewBuilder<newui::RootView> rootBuilder(root);
        rootBuilder.layout<newui::FlexLayout>([](newui::FlexLayout& l) {
            l.setOrientation(newui::Orientation::Vertical);
            l.setSpacing(0.0f);
            l.setPadding(0.0f);
        });
        newui::ViewBuilder<Workspace> workspaceBuilder;
        workspaceBuilder.layoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
        workspace_ = workspaceBuilder.build();
        rootBuilder.child(workspace_);

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

        // PropertiesGrid (and, later, Document Outline) learn about
        // selection changes this way - through ViewDesignerController's
        // own notification, not because this class knows they exist.
        viewDesignerController_.onSelectionChanged.add(this, &DesignerEditor::handleSelectionChanged);

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
        }
        return newui::SyncReturn::Ignored;
    }

    bool DesignerEditor::load(const wchar_t* filePath, std::size_t filePathLength)
    {
        if (!workspace_)
        {
            logToDebugOut(L"DesignerEditor::load: workspace is null (construction must have failed)");
            return false;
        }

        std::wstring path = copyPath(filePath, filePathLength);
        std::wstring overrideRoot;
        std::string bundleName;
        if (!resolveBundleNameAndRoot(path, overrideRoot, bundleName))
        {
            logToDebugOut(L"DesignerEditor::load: path is not under a \\Resources\\ folder, can't resolve a bundle name");
            return false;
        }

        newui::Bundle::instance().setExecutableDirOverride(wideToUtf8(overrideRoot));
        // designMode=true - propagates Component::setDesignTime(true) onto
        // every freshly-constructed child read from the file (reflection.h's
        // TypedClass<T>::read(), recurses through the whole loaded tree).
        // Genuinely meaningful now that isDesignTime() no longer defers to
        // an owning RootView's flag (view.cpp) - an earlier version left
        // this at the default false since the propagation was moot either
        // way back then.
        if (!newui::Bundle::instance().loadRootView(*workspace_->rootViewProxy(), bundleName, /*designMode=*/true))
        {
            logToDebugOut(L"DesignerEditor::load: Bundle::loadRootView failed");
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
        if (newui::Bundle::instance().loadFrame(scratchFrame))
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
        std::wstring overrideRoot;
        std::string bundleName;
        if (!resolveBundleNameAndRoot(path, overrideRoot, bundleName))
        {
            logToDebugOut(L"DesignerEditor::save: path is not under a \\Resources\\ folder, can't resolve a bundle name");
            return false;
        }

        newui::Bundle::instance().setExecutableDirOverride(wideToUtf8(overrideRoot));
        if (!newui::Bundle::instance().writeRootView(*workspace_->rootViewProxy(), bundleName, /*designMode=*/true))
        {
            logToDebugOut(L"DesignerEditor::save: Bundle::writeRootView failed");
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
