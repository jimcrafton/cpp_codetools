#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>

#include "NativeEditor.h"
#include "SelectionOverlay.h"
#include "ViewDesignerController.h"
#include "Workspace.h"

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
        // PropertiesGrid (and, later, Document Outline) subscribe to its
        // onSelectionChanged independently, without this class needing to
        // know they exist (see the onSelectionChanged wiring in setupUI()).
        ViewDesignerController& viewDesignerController() { return viewDesignerController_; }
        const ViewDesignerController& viewDesignerController() const { return viewDesignerController_; }

        // filePath must be a real "<root>\Resources\<bundleName>.newui" -
        // derives bundleName/root from it (see resolveBundleNameAndRoot(),
        // DesignerEditor.cpp), points Bundle::instance() at root via
        // setExecutableDirOverride() (this DLL is hosted inside devenv.exe,
        // whose own exe dir has nothing to do with the user's project), then
        // Bundle::loadRootView()s just the "rootView" node into
        // workspace()->rootViewProxy(), not this editor's own hosting
        // RootView. isDesignTime() for the whole loaded tree already comes
        // from setupUI()'s root->setDesignTime(true) (View::isDesignTime()
        // defers to the owning RootView once attached - see setupUI()'s own
        // comment), so this doesn't need Bundle's designMode flag itself.
        // Returns false if the path isn't shaped that way, or for any of
        // loadRootView()'s own failure reasons.
        bool load(const wchar_t* filePath, std::size_t filePathLength) override;

        // Write-side counterpart to load() - Bundle::writeRootView() (design
        // mode on, so the saved file's "type" tag reads "RootView", not
        // "RootViewProxy" - unrelated to isDesignTime(), see Class::
        // proxyFor()'s own comment), which preserves any other top-level
        // keys (title, bounds, animations, ...) an existing Frame-shaped
        // file already has; only "rootView" is replaced. Same path/root
        // derivation and failure contract as load().
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

        // Pushes viewDesignerController_'s new primary() into the
        // Properties panel - the one place this class still knows about a
        // specific selection consumer, since PropertiesGrid is real and
        // built today; Document Outline (designer-plan.md 6.1 item 4, not
        // built yet) will subscribe its own handler onto
        // viewDesignerController_.onSelectionChanged the same way, once it
        // exists, without needing to touch this one.
        newui::SyncReturn handleSelectionChanged(ViewDesignerController& sender);

        Workspace* workspace_ = nullptr;
        SelectionOverlay* selectionOverlay_ = nullptr;
        ViewDesignerController viewDesignerController_;
    };
}
