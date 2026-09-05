#include "Workspace.h"

#include <newui/layout.h>
#include <newui/uicolormanager.h>
#include <newui/viewbuilder.h>

#include <memory>

namespace CodeToolsVsix
{
    namespace
    {
        void styleAsPane(newui::ViewBuilder<newui::SubView>& builder, newui::UIColorRole role)
        {
            // A plain newui::SubView (unlike Splitter/FrameProxy/
            // RootViewProxy, which each call setVisible(true) in their own
            // constructor) stays invisible by default (View::visible_'s own
            // default) - real, caught bug: FlexLayout::arrange() silently
            // skips invisible children, so every plain pane built through
            // this helper needs it explicitly.
            builder.visible(true).style<newui::ViewStyle>([role](newui::ViewStyle& style) {
                style.setBackgroundColor(newui::UIColorManager::colorFor(role));
            });
        }
    }

    // Built bottom-up as flat, independent ViewBuilder locals - each one
    // configured with a plain chained call (no lambda, no capture), then
    // handed to its eventual parent's child(SubView*) overload once built.
    // Reads the same as the tree it produces (leaves first, containers
    // last), unlike the nested child<ChildT>(fn) lambda pyramid this
    // replaced, which grew a closure-capture level per nesting depth.
    Workspace::Workspace()
    {
        setVisible(true);
        style().setBackgroundColor(newui::UIColorManager::colorFor(newui::UIColorRole::WindowBackground));

        newui::ViewBuilder<Workspace> self(this);
        self.layout<newui::FlexLayout>([](newui::FlexLayout& l) {
            l.setOrientation(newui::Orientation::Vertical);
            l.setSpacing(0.0f);
            l.setPadding(0.0f);
        });

        newui::ViewBuilder<newui::SubView> topBuilder;
        topBuilder.name("workspaceTopBar").desiredSize(newui::Size(0.0f, kTopBarHeight));
        styleAsPane(topBuilder, newui::UIColorRole::ControlBackground);
        topBar_ = topBuilder.build();

        newui::ViewBuilder<newui::RootViewProxy> rootViewProxyBuilder;
        rootViewProxyBuilder.name("workspaceRootViewProxy")
            .layoutParams<newui::AnchorLayoutParams>([](newui::AnchorLayoutParams& p) {
                p.anchors = newui::Anchor::Left | newui::Anchor::Top
                          | newui::Anchor::Right | newui::Anchor::Bottom;
                p.topMargin = newui::FrameProxy::kTitleBarHeight;
            });
        rootViewProxy_ = rootViewProxyBuilder.build();

        newui::ViewBuilder<newui::FrameProxy> frameBuilder;
        frameBuilder.name("workspaceFrameProxy")
            .layout<newui::AnchorLayout>()
            .layoutParams<newui::AnchorLayoutParams>([](newui::AnchorLayoutParams& p) {
                // Centered, fixed-size artboard, not stretched to fill
                // canvasWell_ - matches Main.dc.html's own ".artboard"
                // (a fixed 640x460 inside ".canvas-wrap"'s centering flex
                // box), same "dock stays fixed, well/content around it
                // adapts" spirit as the Splitter proportions above, just
                // expressed via AnchorLayout's CenterX/CenterY instead
                // (width/height come from these params, not the child's
                // own desiredSize() - see AnchorLayoutParams's own comment).
                p.anchors = newui::Anchor::CenterX | newui::Anchor::CenterY;
                p.width = Workspace::kDefaultCanvasWidth;
                p.height = Workspace::kDefaultCanvasHeight;
            });
        frameBuilder.child(rootViewProxy_);
        frameProxy_ = frameBuilder.build();

        // canvasWell_: the darker "pasteboard" frameProxy_ sits centered
        // in - a bespoke app color, not a real Windows system color (no
        // uxtheme concept maps to "design canvas background"), so a
        // literal Color here rather than routing through UIColorManager
        // (whose whole job is querying/inverting *real* system theme
        // colors - inventing a new role for this would mean fabricating a
        // "system" value that was never one to begin with).
        newui::ViewBuilder<newui::SubView> canvasWellBuilder;
        canvasWellBuilder.name("workspaceCanvasWell")
            .visible(true)
            .layout<newui::AnchorLayout>()
            .style<newui::ViewStyle>([](newui::ViewStyle& style) {
                style.setBackgroundColor(newui::Color(0x2B2B2Bu, false));
            });
        canvasWellBuilder.child(frameProxy_);
        canvasWell_ = canvasWellBuilder.build();

        newui::ViewBuilder<newui::SubView> propertiesBuilder;
        propertiesBuilder.name("workspacePropertiesPane");
        styleAsPane(propertiesBuilder, newui::UIColorRole::ControlBackground);
        propertiesPane_ = propertiesBuilder.build();

        // centerAndRight: canvasWell_ (holding the design space) |
        // propertiesPane_, a horizontal split - fixedPane(Second) so
        // propertiesPane_ (the right-hand dock) stays pinned at its own
        // configured width and canvasWell_ is the one that grows/shrinks
        // on any resize, matching the standard docking-IDE convention (and
        // bluesky/designer-surface/Main.dc.html's own
        // ".rightpane { width: 300px }" / ".canvas-wrap { flex: 1 }").
        newui::ViewBuilder<newui::Splitter> centerAndRightBuilder;
        centerAndRightBuilder.name("workspaceCenterAndRight")
            .configure([](newui::Splitter& s) {
                s.setFixedPane(newui::SplitterFixedPane::Second);
                s.setSplitPosition(kPropertiesPaneWidth);
                s.setDividerThickness(kDividerThickness);
            });
        centerAndRightBuilder.child(canvasWell_).child(propertiesPane_);
        newui::Splitter* centerAndRight = centerAndRightBuilder.build();

        newui::ViewBuilder<newui::SubView> toolboxBuilder;
        toolboxBuilder.name("workspaceToolboxPane");
        styleAsPane(toolboxBuilder, newui::UIColorRole::ControlBackground);
        toolboxPane_ = toolboxBuilder.build();

        // mainRow: toolboxPane_ | centerAndRight, a horizontal split -
        // fixedPane(First) is Splitter's own default (toolboxPane_ pinned,
        // centerAndRight grows), already matching the same convention, so
        // no setFixedPane() call is needed here.
        newui::ViewBuilder<newui::Splitter> mainRowBuilder;
        mainRowBuilder.name("workspaceMainRow")
            .configure([](newui::Splitter& s) {
                s.setSplitPosition(kToolboxPaneWidth);
                s.setDividerThickness(kDividerThickness);
            });
        mainRowBuilder.child(toolboxPane_).child(centerAndRight);
        newui::Splitter* mainRow = mainRowBuilder.build();

        newui::ViewBuilder<newui::SubView> animationBuilder;
        animationBuilder.name("workspaceAnimationPane");
        styleAsPane(animationBuilder, newui::UIColorRole::ControlBackground);
        animationPane_ = animationBuilder.build();

        // middle: mainRow over animationPane_, a vertical split -
        // fixedPane(Second) so animationPane_ (the Animations preview
        // dock) stays pinned at kAnimationDockHeight and mainRow (the main
        // Toolbox/design-surface/Properties row) absorbs the rest, same
        // convention as centerAndRight above. Directly expressible now
        // that setSplitPosition() means "the fixed pane's own size" under
        // fixedPane(Second) - no more setMinPaneSize()-plus-huge-value
        // workaround needed.
        newui::ViewBuilder<newui::Splitter> middleBuilder;
        middleBuilder.name("workspaceMiddleArea")
            .layoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f))
            .configure([](newui::Splitter& s) {
                s.setOrientation(newui::Orientation::Vertical);
                s.setFixedPane(newui::SplitterFixedPane::Second);
                s.setSplitPosition(Workspace::kAnimationDockHeight);
                s.setDividerThickness(kDividerThickness);
            });
        middleBuilder.child(mainRow).child(animationPane_);
        newui::Splitter* middle = middleBuilder.build();

        // HighlightBackground (the real DWM accent color) - same role pair
        // FrameProxy's own title bar uses (frameproxy.cpp), matching
        // Main.dc.html's own ".statusbar { background: var(--accent) }".
        newui::ViewBuilder<newui::SubView> statusBuilder;
        statusBuilder.name("workspaceStatusBar").desiredSize(newui::Size(0.0f, kStatusBarHeight));
        styleAsPane(statusBuilder, newui::UIColorRole::HighlightBackground);
        statusBar_ = statusBuilder.build();

        self.child(topBar_).child(middle).child(statusBar_);
    }
}
