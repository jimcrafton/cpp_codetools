#pragma once

#include "CanvasWell.h"
#include "DocumentOutline.h"
#include "PropertiesGrid.h"
#include "Toolbox.h"

#include <newui/controls.h>
#include <newui/delegate.h>
#include <newui/frameproxy.h>
#include <newui/rootviewproxy.h>
#include <newui/segmentedcontrol.h>
#include <newui/splitter.h>
#include <newui/subview.h>
#include <newui/undostack.h>

#include <functional>

namespace CodeToolsVsix
{
    // Design-specific chrome skeleton for View Designer's own editor pane -
    // see bluesky/designer-plan.md's view-hierarchy section (this class
    // isn't itself documented there yet - built directly from the session's
    // design discussion). Assembles fixed top/status bars, a resizable
    // left toolbox / design space / right properties-outliner split, and a
    // resizable bottom animation dock, using newui::Splitter for every
    // resizable boundary and newui::ViewBuilder to build the whole tree in
    // one fluent pass. Design-time-only, single consumer (DesignerEditor) -
    // same placement reasoning as PropertyEditor/ComponentEditor
    // (deliberately not in newui).
    //
    // Owns the newui::FrameProxy/newui::RootViewProxy pair that "design
    // space" hosts - frameProxy()/rootViewProxy() expose them so
    // DesignerEditor can attach a loaded document's tree onto rootViewProxy()
    // directly (via reflection::ObjectReader::readNested<RootViewProxy>(),
    // not Bundle::loadRootView(), which is hardcoded to a real RootView&),
    // and so future panels (Outline/Properties/Toolbox) can reach the live
    // edited tree the same way.
    class Workspace : public newui::SubView
    {
    public:
        static constexpr float kTopBarHeight = 32.0f;
        static constexpr float kStatusBarHeight = 22.0f;

        // Fixed-dock sizes - matching bluesky/designer-surface/Main.dc.html's
        // own real CSS proportions (".toolbox { width: 220px }",
        // ".rightpane { width: 300px }", ".dock-head" 26px + ".dock-body"
        // 58px), not arbitrary guesses - see centerAndRight/mainRow/middle's
        // own setFixedPane()/setSplitPosition() calls (Workspace.cpp) for
        // which pane each of these actually sizes.
        static constexpr float kToolboxPaneWidth = 220.0f;
        static constexpr float kPropertiesPaneWidth = 300.0f;

        // Document Outline's own fixed height within the right-hand dock,
        // above Properties (which absorbs the rest) - Main.dc.html's own
        // ".outline-panel" is a relative "flex: 0 0 38%" of the whole
        // rightpane's height, not a literal px value the way
        // kPropertiesPaneWidth's 300px was, so this is a reasonable
        // initial default rather than a direct port - the boundary is a
        // real, user-draggable newui::Splitter (rightDock, Workspace.cpp),
        // not fixed like Main.dc.html's own flex proportion.
        static constexpr float kDocumentOutlinePaneHeight = 180.0f;

        // The Animations preview dock's own fixed height - kept small
        // deliberately (designer-plan.md's "in-context slice", not full
        // curve editing) relative to the main Toolbox/design-surface/
        // Properties row above it, which gets the rest.
        static constexpr float kAnimationDockHeight = 84.0f;

        // frameProxy_'s own fixed size, centered inside the darker canvas
        // well rather than stretched to fill it - matches
        // bluesky/designer-surface/Main.dc.html's own ".artboard" (640x460),
        // itself an arbitrary reference size, not derived from any real
        // document yet (an "idea, not built" gap: sizing this from the
        // loaded document's own real Frame bounds instead, once
        // DesignerEditor::load() has one to read).
        static constexpr float kDefaultCanvasWidth = 640.0f;
        static constexpr float kDefaultCanvasHeight = 460.0f;

        // Thinner than newui::Splitter's own generic 6px default - a
        // slimmer divider reads better across three of these stacked
        // side by side than the default would.
        static constexpr float kDividerThickness = 2.0f;

        // Design/Source/Data Flow, matching Main.dc.html's own ".segmented"
        // - only "Design" is real today, so Source/Data Flow are built
        // enabled=false (SegmentedControl::setSegmentEnabled()) rather than
        // omitted, an honest "not built yet" signal instead of missing
        // chrome. Indices into the segmented control's own segments() list
        // - not an enum, since nothing outside Workspace/DesignerEditor's
        // own wiring needs to name these yet.
        static constexpr std::size_t kDesignModeSegment = 0;
        static constexpr std::size_t kSourceModeSegment = 1;
        static constexpr std::size_t kDataFlowModeSegment = 2;

        Workspace();

        newui::Toolbar* topBar() const { return topBar_; }
        CanvasWell* canvasWell() const { return canvasWell_; }
        Toolbox* toolboxPane() const { return toolboxPane_; }
        DocumentOutline* documentOutlinePane() const { return documentOutlinePane_; }
        PropertiesGrid* propertiesPane() const { return propertiesPane_; }
        newui::SubView* animationPane() const { return animationPane_; }
        newui::SubView* statusBar() const { return statusBar_; }

        // Left-anchored text inside statusBar_ - DesignerEditor's own
        // refreshUndoRedoButtons() sets this to "Undo: <description>" /
        // "Redo: <description>" (whichever undoStack_.canUndo()/canRedo()
        // says is next), "" when neither is available. The rest of
        // Main.dc.html's own three-section status bar (bounds/filename)
        // isn't built yet - a separate, deferred piece.
        newui::Label* undoRedoStatusLabel() const { return undoRedoStatusLabel_; }
        newui::FrameProxy* frameProxy() const { return frameProxy_; }
        newui::RootViewProxy* rootViewProxy() const { return rootViewProxy_; }

        // New/Open/Save/Undo/Redo - temporary testing-phase conveniences,
        // per the user's own framing: testharness has no host IDE
        // providing these, but a real VS-hosted DesignerEditor eventually
        // won't need them either (File>Open/Save/Undo/Redo already exist
        // at the IDE level there). Exposed as real child pointers rather
        // than Workspace inventing its own forwarding delegates - same
        // "expose the real child, DesignerEditor wires the actual
        // behavior" convention toolboxPane()/propertiesPane()/etc.
        // already follow (DesignerEditor::setupUI() subscribes directly
        // to each button's own inherited Control::onClick).
        newui::ToolbarButton* newButton() const { return newButton_; }
        newui::ToolbarButton* openButton() const { return openButton_; }
        newui::ToolbarButton* saveButton() const { return saveButton_; }
        newui::ToolbarButton* undoButton() const { return undoButton_; }
        newui::ToolbarButton* redoButton() const { return redoButton_; }

        // Design/Source/Data Flow mode switch - see kDesignModeSegment
        // etc.'s own comment above for why Source/Data Flow are present
        // but disabled. DesignerEditor subscribes to its
        // onSelectionChanged the same "reach the real child" way as the
        // buttons above.
        newui::SegmentedControl* modeControl() const { return modeControl_; }

        // Static "100%" placeholder (Main.dc.html's own ".tb-zoom") - no
        // real canvas zoom exists yet, so this never changes on its own;
        // exposed only so a future real zoom feature has something to
        // update rather than needing to rebuild the toolbar.
        newui::Label* zoomLabel() const { return zoomLabel_; }

        // Fired right after anything here mutates rootViewProxy()'s own
        // children directly (today: just the Toolbox double-click-to-add
        // wiring below) - DesignerEditor subscribes to this to know when
        // its own ViewDesignerModel needs refresh()ing, without Workspace
        // needing to know that type exists (same "expose a delegate,
        // don't reach into the owner" shape Toolbox::onEntryActivated
        // already established).
        newui::Delegate<Workspace> onDesignSurfaceChanged;

        // Same "nullptr = direct commit, no undo" convention PropertyEditor::
        // setUndoStack() already established - makes the Toolbox
        // double-click-to-add wiring below undo-aware once DesignerEditor
        // sets a real stack (setupUI()).
        void setUndoStack(newui::UndoStack* undoStack) { undoStack_ = undoStack; }

        // Consulted by the Toolbox double-click-to-add wiring below to decide where a newly
        // created control actually lands: the current primary selection, when one exists and is
        // a real design-time container (its registered @reflect category=containers - the same
        // tag ToolboxRegistry's own "containers" grouping already uses), otherwise
        // rootViewProxy() as before. Left unset (the default, no provider) always adds to
        // rootViewProxy() - matches every existing test constructing a Workspace directly.
        // Same "expose a hook, don't reach into the owner" shape setUndoStack()/
        // onDesignSurfaceChanged already established - Workspace doesn't need to know
        // ViewDesignerController exists to ask it this one question.
        void setPrimarySelectionProvider(std::function<newui::SubView*()> provider) {
            primarySelectionProvider_ = std::move(provider);
        }

    private:
        newui::Toolbar* topBar_ = nullptr;
        CanvasWell* canvasWell_ = nullptr;
        Toolbox* toolboxPane_ = nullptr;
        DocumentOutline* documentOutlinePane_ = nullptr;
        PropertiesGrid* propertiesPane_ = nullptr;
        newui::SubView* animationPane_ = nullptr;
        newui::SubView* statusBar_ = nullptr;
        newui::FrameProxy* frameProxy_ = nullptr;
        newui::RootViewProxy* rootViewProxy_ = nullptr;
        newui::ToolbarButton* newButton_ = nullptr;
        newui::ToolbarButton* openButton_ = nullptr;
        newui::ToolbarButton* saveButton_ = nullptr;
        newui::ToolbarButton* undoButton_ = nullptr;
        newui::ToolbarButton* redoButton_ = nullptr;
        newui::SegmentedControl* modeControl_ = nullptr;
        newui::Label* zoomLabel_ = nullptr;
        newui::Label* undoRedoStatusLabel_ = nullptr;
        newui::UndoStack* undoStack_ = nullptr;
        std::function<newui::SubView*()> primarySelectionProvider_;
    };
}
