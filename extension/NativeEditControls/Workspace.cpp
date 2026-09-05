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
        frameBuilder.name("workspaceFrameProxy").layout<newui::AnchorLayout>();
        frameBuilder.child(rootViewProxy_);
        frameProxy_ = frameBuilder.build();

        newui::ViewBuilder<newui::SubView> propertiesBuilder;
        propertiesBuilder.name("workspacePropertiesPane");
        styleAsPane(propertiesBuilder, newui::UIColorRole::ControlBackground);
        propertiesPane_ = propertiesBuilder.build();

        // centerAndRight: design space (frameProxy_) | propertiesPane_, a horizontal split -
        // default Splitter orientation, so no explicit configure() for it is needed.
        newui::ViewBuilder<newui::Splitter> centerAndRightBuilder;
        centerAndRightBuilder.name("workspaceCenterAndRight")
            .configure([](newui::Splitter& s) { s.setSplitPosition(560.0f); });
        centerAndRightBuilder.child(frameProxy_).child(propertiesPane_);
        newui::Splitter* centerAndRight = centerAndRightBuilder.build();

        newui::ViewBuilder<newui::SubView> toolboxBuilder;
        toolboxBuilder.name("workspaceToolboxPane");
        styleAsPane(toolboxBuilder, newui::UIColorRole::ControlBackground);
        toolboxPane_ = toolboxBuilder.build();

        // mainRow: toolboxPane_ | centerAndRight, a horizontal split.
        newui::ViewBuilder<newui::Splitter> mainRowBuilder;
        mainRowBuilder.name("workspaceMainRow")
            .configure([](newui::Splitter& s) { s.setSplitPosition(180.0f); });
        mainRowBuilder.child(toolboxPane_).child(centerAndRight);
        newui::Splitter* mainRow = mainRowBuilder.build();

        newui::ViewBuilder<newui::SubView> animationBuilder;
        animationBuilder.name("workspaceAnimationPane");
        styleAsPane(animationBuilder, newui::UIColorRole::ControlBackground);
        animationPane_ = animationBuilder.build();

        // middle: mainRow over animationPane_, a vertical split.
        newui::ViewBuilder<newui::Splitter> middleBuilder;
        middleBuilder.name("workspaceMiddleArea")
            .layoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f))
            .configure([](newui::Splitter& s) { s.setOrientation(newui::Orientation::Vertical); });
        middleBuilder.child(mainRow).child(animationPane_);
        newui::Splitter* middle = middleBuilder.build();

        newui::ViewBuilder<newui::SubView> statusBuilder;
        statusBuilder.name("workspaceStatusBar").desiredSize(newui::Size(0.0f, kStatusBarHeight));
        styleAsPane(statusBuilder, newui::UIColorRole::ControlBackground);
        statusBar_ = statusBuilder.build();

        self.child(topBar_).child(middle).child(statusBar_);
    }
}
