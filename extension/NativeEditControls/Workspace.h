#pragma once

#include "CanvasWell.h"
#include "PropertiesGrid.h"
#include "Toolbox.h"

#include <newui/frameproxy.h>
#include <newui/rootviewproxy.h>
#include <newui/splitter.h>
#include <newui/subview.h>

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

        Workspace();

        newui::SubView* topBar() const { return topBar_; }
        CanvasWell* canvasWell() const { return canvasWell_; }
        Toolbox* toolboxPane() const { return toolboxPane_; }
        PropertiesGrid* propertiesPane() const { return propertiesPane_; }
        newui::SubView* animationPane() const { return animationPane_; }
        newui::SubView* statusBar() const { return statusBar_; }
        newui::FrameProxy* frameProxy() const { return frameProxy_; }
        newui::RootViewProxy* rootViewProxy() const { return rootViewProxy_; }

    private:
        newui::SubView* topBar_ = nullptr;
        CanvasWell* canvasWell_ = nullptr;
        Toolbox* toolboxPane_ = nullptr;
        PropertiesGrid* propertiesPane_ = nullptr;
        newui::SubView* animationPane_ = nullptr;
        newui::SubView* statusBar_ = nullptr;
        newui::FrameProxy* frameProxy_ = nullptr;
        newui::RootViewProxy* rootViewProxy_ = nullptr;
    };
}
