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

    Workspace::Workspace()
    {
        setVisible(true);
        style().setBackgroundColor(newui::UIColorManager::colorFor(newui::UIColorRole::WindowBackground));

        auto rootLayout = std::make_unique<newui::FlexLayout>(newui::Orientation::Vertical);
        rootLayout->setSpacing(0.0f);
        rootLayout->setPadding(0.0f);
        setLayout(std::move(rootLayout));

        newui::ViewBuilder<Workspace> self(this);

        self.child<newui::SubView>([this](newui::ViewBuilder<newui::SubView>& top) {
                top.name("workspaceTopBar").desiredSize(newui::Size(0.0f, kTopBarHeight));
                styleAsPane(top, newui::UIColorRole::ControlBackground);
                topBar_ = top.build();
            })
            .child<newui::Splitter>([this](newui::ViewBuilder<newui::Splitter>& middle) {
                middle.name("workspaceMiddleArea")
                    .layoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f))
                    .configure([](newui::Splitter& s) { s.setOrientation(newui::Orientation::Vertical); });

                // mainRow: toolbox | (design space + properties), a horizontal split -
                // default Splitter orientation, so the plain child<Splitter>() overload works.
                middle.child<newui::Splitter>([this](newui::ViewBuilder<newui::Splitter>& mainRow) {
                    mainRow.name("workspaceMainRow")
                        .configure([](newui::Splitter& s) { s.setSplitPosition(180.0f); });

                    mainRow.child<newui::SubView>([this](newui::ViewBuilder<newui::SubView>& toolbox) {
                            toolbox.name("workspaceToolboxPane");
                            styleAsPane(toolbox, newui::UIColorRole::ControlBackground);
                            toolboxPane_ = toolbox.build();
                        })
                        .child<newui::Splitter>([this](newui::ViewBuilder<newui::Splitter>& centerAndRight) {
                            centerAndRight.name("workspaceCenterAndRight")
                                .configure([](newui::Splitter& s) { s.setSplitPosition(560.0f); });

                            centerAndRight.child<newui::FrameProxy>([this](newui::ViewBuilder<newui::FrameProxy>& frame) {
                                    frame.name("workspaceFrameProxy")
                                        .layout<newui::AnchorLayout>();

                                    frame.child<newui::RootViewProxy>([this](newui::ViewBuilder<newui::RootViewProxy>& rv) {
                                        rv.name("workspaceRootViewProxy")
                                            .layoutParams<newui::AnchorLayoutParams>([](newui::AnchorLayoutParams& p) {
                                                p.anchors = newui::Anchor::Left | newui::Anchor::Top
                                                          | newui::Anchor::Right | newui::Anchor::Bottom;
                                                p.topMargin = newui::FrameProxy::kTitleBarHeight;
                                            });
                                        rootViewProxy_ = rv.build();
                                    });

                                    frameProxy_ = frame.build();
                                })
                                .child<newui::SubView>([this](newui::ViewBuilder<newui::SubView>& props) {
                                    props.name("workspacePropertiesPane");
                                    styleAsPane(props, newui::UIColorRole::ControlBackground);
                                    propertiesPane_ = props.build();
                                });
                        });
                });

                middle.child<newui::SubView>([this](newui::ViewBuilder<newui::SubView>& animation) {
                    animation.name("workspaceAnimationPane");
                    styleAsPane(animation, newui::UIColorRole::ControlBackground);
                    animationPane_ = animation.build();
                });
            })
            .child<newui::SubView>([this](newui::ViewBuilder<newui::SubView>& status) {
                status.name("workspaceStatusBar").desiredSize(newui::Size(0.0f, kStatusBarHeight));
                styleAsPane(status, newui::UIColorRole::ControlBackground);
                statusBar_ = status.build();
            });
    }
}
