#pragma once

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

        Workspace();

        newui::SubView* topBar() const { return topBar_; }
        newui::SubView* toolboxPane() const { return toolboxPane_; }
        newui::SubView* propertiesPane() const { return propertiesPane_; }
        newui::SubView* animationPane() const { return animationPane_; }
        newui::SubView* statusBar() const { return statusBar_; }
        newui::FrameProxy* frameProxy() const { return frameProxy_; }
        newui::RootViewProxy* rootViewProxy() const { return rootViewProxy_; }

    private:
        newui::SubView* topBar_ = nullptr;
        newui::SubView* toolboxPane_ = nullptr;
        newui::SubView* propertiesPane_ = nullptr;
        newui::SubView* animationPane_ = nullptr;
        newui::SubView* statusBar_ = nullptr;
        newui::FrameProxy* frameProxy_ = nullptr;
        newui::RootViewProxy* rootViewProxy_ = nullptr;
    };
}
