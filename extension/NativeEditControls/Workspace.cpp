#include "Workspace.h"

#include <newui/layout.h>
#include <newui/reflection.h>
#include <newui/uicolormanager.h>
#include <newui/viewbuilder.h>

#include <memory>
#include <string>

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

        // A real newui::Toolbar (own ThemedRebarBandStyle chrome, not
        // styleAsPane()'s flat fill - Toolbar already themes itself) -
        // see newButton()/openButton()/.../modeControl()'s own header
        // comments for what each child actually does and why New/Open/
        // Save/Undo/Redo are only a temporary testing-phase convenience.
        newui::ViewBuilder<newui::Toolbar> topBuilder;
        topBuilder.name("workspaceTopBar").desiredSize(newui::Size(0.0f, kTopBarHeight));
        topBar_ = topBuilder.build();

        // Fixed widths matching examples/controls1.cpp's own real
        // ToolbarButton usage (50x24) - a ToolbarButton never
        // self-measures its own text the way this project's own Item-
        // based rows do (ToolbarButton::paint() just centers whatever
        // text within its already-assigned clientBounds()), so an
        // explicit desiredSize() is required, not optional.
        auto makeToolbarButton = [](const char* name, const std::string& text, const std::string& icon) {
            newui::ViewBuilder<newui::ToolbarButton> b;
            b.name(name).desiredSize(newui::Size(50.0f, 24.0f))
                .configure([&text, &icon](newui::ToolbarButton& btn) {
                    btn.setText(text);
                    btn.setIcon(icon);
                });
            return b.build();
        };

        newButton_ = makeToolbarButton("workspaceNewButton", "New", "Images/icons/toolbar/new.svg");
        openButton_ = makeToolbarButton("workspaceOpenButton", "Open", "Images/icons/toolbar/open.svg");
        saveButton_ = makeToolbarButton("workspaceSaveButton", "Save", "Images/icons/toolbar/save.svg");
        undoButton_ = makeToolbarButton("workspaceUndoButton", "Undo", "Images/icons/toolbar/undo.svg");
        redoButton_ = makeToolbarButton("workspaceRedoButton", "Redo", "Images/icons/toolbar/redo.svg");
        // Nothing has been undone/redone yet when this pane is first
        // built - DesignerEditor refreshes these via undoStack().
        // onActionPushed and its own undo()/redo() handlers from here on.
        undoButton_->setEnabled(false);
        redoButton_->setEnabled(false);

        newui::ViewBuilder<newui::ToolbarSeparator> fileUndoSepBuilder;
        fileUndoSepBuilder.name("workspaceToolbarFileUndoSep");
        newui::ToolbarSeparator* fileUndoSep = fileUndoSepBuilder.build();

        newui::ViewBuilder<newui::ToolbarSeparator> undoModeSepBuilder;
        undoModeSepBuilder.name("workspaceToolbarUndoModeSep");
        newui::ToolbarSeparator* undoModeSep = undoModeSepBuilder.build();

        // Design/Source/Data Flow - only Design is real (see
        // kSourceModeSegment/kDataFlowModeSegment's own comment,
        // Workspace.h), so those two segments are built present but
        // disabled rather than omitted, an honest "not built yet" signal
        // instead of missing chrome.
        newui::ViewBuilder<newui::SegmentedControl> modeControlBuilder;
        modeControlBuilder.name("workspaceModeControl")
            .configure([](newui::SegmentedControl& control) {
                control.setSegments({"Design", "Source", "Data Flow"});
                control.setSegmentEnabled(Workspace::kSourceModeSegment, false);
                control.setSegmentEnabled(Workspace::kDataFlowModeSegment, false);
            });
        modeControl_ = modeControlBuilder.build();
        modeControl_->setDesiredSize(modeControl_->naturalSize());

        // "100%" (Main.dc.html's own ".tb-zoom") - a static placeholder,
        // not wired to anything real (no canvas zoom feature exists yet -
        // see zoomLabel()'s own header comment). Fixed width, same
        // "ToolbarButton never self-measures, the caller assigns a size"
        // convention makeToolbarButton() above already follows.
        newui::ViewBuilder<newui::Label> zoomLabelBuilder;
        zoomLabelBuilder.name("workspaceZoomLabel")
            .desiredSize(newui::Size(40.0f, 24.0f))
            .configure([](newui::Label& label) { label.setText("100%"); });
        zoomLabel_ = zoomLabelBuilder.build();

        // A plain, invisible-content spacer with the only nonzero flex
        // weight in this Toolbar's FlexLayout (every button/separator/
        // the mode control above is weight-0, sized to its own
        // desiredSize()) - absorbs all left-over width, pushing the zoom
        // label and mode control to the right edge, matching
        // Main.dc.html's own ".tb-spacer { flex: 1 }".
        newui::ViewBuilder<newui::SubView> toolbarSpacerBuilder;
        toolbarSpacerBuilder.name("workspaceToolbarSpacer")
            .visible(true)
            .layoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
        newui::SubView* toolbarSpacer = toolbarSpacerBuilder.build();

        topBar_->addChild(newButton_);
        topBar_->addChild(openButton_);
        topBar_->addChild(saveButton_);
        topBar_->addChild(fileUndoSep);
        topBar_->addChild(undoButton_);
        topBar_->addChild(redoButton_);
        topBar_->addChild(undoModeSep);
        topBar_->addChild(toolbarSpacer);
        topBar_->addChild(zoomLabel_);
        topBar_->addChild(modeControl_);

        // isDesignTime() no longer defers to an owning RootView's flag
        // (view.cpp - a View reports only its own explicitly-set flag) -
        // set directly here rather than once on the hosting RootView the
        // way an earlier version did, which had made all of Workspace's
        // own permanent chrome (this toolbox/properties pane included)
        // report design-time too, with no way to tell them apart.
        newui::ViewBuilder<newui::RootViewProxy> rootViewProxyBuilder;
        rootViewProxyBuilder.name("workspaceRootViewProxy")
            .layoutParams<newui::AnchorLayoutParams>([](newui::AnchorLayoutParams& p) {
                p.anchors = newui::Anchor::Left | newui::Anchor::Top
                          | newui::Anchor::Right | newui::Anchor::Bottom;
                p.topMargin = newui::FrameProxy::kTitleBarHeight;
            })
            // rootViewProxy_'s own children (the edited document's real controls) get a real
            // AnchorLayout so a dragged/moved control's position is expressed as a real,
            // reflectable AnchorLayoutParams (leftMargin/topMargin/width/height - plain public
            // fields, unlike the private, non-property View::bounds_) rather than a raw
            // setBounds() call that would never round-trip through Bundle save/load.
            // AnchorLayout::arrange() already skips any child with no AnchorLayoutParams at all
            // ("unconfigured child - left exactly where it was", layout.cpp) - a real, already-
            // load-bearing behavior confirmed by reading, not assumed - so this is non-breaking
            // for every existing Toolbox-added child, which has none yet.
            .layout<newui::AnchorLayout>()
            .configure([](newui::RootViewProxy& v) {
                v.setDesignTime(true);
                // Matches FrameProxy's own body radius - see
                // RootViewProxy::setCornerRadius()'s own comment for why
                // this is needed at all (its square background fill would
                // otherwise overwrite the FrameProxy body's already-
                // rounded bottom corners, since it paints after them).
                v.setCornerRadius(newui::FrameProxy::kCornerRadius);
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
            })
            .configure([](newui::FrameProxy& f) { f.setDesignTime(true); });
        frameBuilder.child(rootViewProxy_);
        frameProxy_ = frameBuilder.build();

        // canvasWell_: the "pasteboard" frameProxy_ sits centered in - a
        // bespoke app color, not a real Windows system color (no uxtheme
        // concept maps to "design canvas background"), so a literal Color
        // here rather than routing through UIColorManager (whose whole job
        // is querying/inverting *real* system theme colors - inventing a
        // new role for this would mean fabricating a "system" value that
        // was never one to begin with). Lightened from the original
        // 0x2B2B2B - FrameProxy's own drop shadow (default color: black at
        // 0.6 alpha) barely registered against that near-black backdrop.
        newui::ViewBuilder<CanvasWell> canvasWellBuilder;
        canvasWellBuilder.name("workspaceCanvasWell")
            .visible(true)
            .layout<newui::AnchorLayout>()
            .style<newui::ViewStyle>([](newui::ViewStyle& style) {
                style.setBackgroundColor(newui::Color(0x4A4A4Au, false));
            });
        canvasWellBuilder.child(frameProxy_);
        canvasWell_ = canvasWellBuilder.build();

        // PropertiesGrid's own constructor already sets visible(true) and
        // its background color (matching FrameProxy/RootViewProxy/
        // Splitter's own convention of doing that in the constructor
        // itself, not styleAsPane()) - it's a ScrollView, not a plain
        // SubView, so styleAsPane() (typed for ViewBuilder<SubView>&)
        // doesn't apply here anyway. Replaces the old PropertiesPanel
        // (bluesky/property-grid-design.md's TreeView-based redesign,
        // built this session) - same setSelection(newui::SubView*) call
        // shape, so DesignerEditor::handleSelectionChanged() needed no
        // change at all.
        // Document Outline (designer-plan.md 6.1 item 4) - backed by
        // ViewDesignerModel via setViewDesignerModel(), wired once
        // DesignerEditor's own model exists (Workspace itself never
        // constructs one - see DocumentOutline.h's own header comment for
        // why that stays a plain, shared newui::Model rather than
        // something Workspace owns).
        newui::ViewBuilder<DocumentOutline> outlineBuilder;
        outlineBuilder.name("workspaceDocumentOutlinePane");
        documentOutlinePane_ = outlineBuilder.build();

        newui::ViewBuilder<PropertiesGrid> propertiesBuilder;
        propertiesBuilder.name("workspacePropertiesPane");
        propertiesPane_ = propertiesBuilder.build();

        // rightDock: documentOutlinePane_ over propertiesPane_, a vertical
        // split - fixedPane(First) is Splitter's own default (the Outline
        // pinned at kDocumentOutlinePaneHeight, Properties absorbs the
        // rest), matching Main.dc.html's own ".outline-panel"-over-
        // ".properties-panel" stack (a fixed proportion there; a real,
        // user-draggable Splitter here instead - see
        // kDocumentOutlinePaneHeight's own comment).
        newui::ViewBuilder<newui::Splitter> rightDockBuilder;
        rightDockBuilder.name("workspaceRightDock")
            .configure([](newui::Splitter& s) {
                s.setOrientation(newui::Orientation::Vertical);
                s.setSplitPosition(kDocumentOutlinePaneHeight);
                s.setDividerThickness(kDividerThickness);
            });
        rightDockBuilder.child(documentOutlinePane_).child(propertiesPane_);
        newui::Splitter* rightDock = rightDockBuilder.build();

        // centerAndRight: canvasWell_ (holding the design space) |
        // rightDock, a horizontal split - fixedPane(Second) so rightDock
        // (the Outline/Properties column) stays pinned at its own
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
        centerAndRightBuilder.child(canvasWell_).child(rightDock);
        newui::Splitter* centerAndRight = centerAndRightBuilder.build();

        // Double-click an entry creates it and attaches it directly onto
        // rootViewProxy_ - the design surface's own root - real drag-and-
        // drop being out of scope for v1 (see Toolbox's own class
        // comment). rootViewProxy_ is already built above, safe to
        // capture/use here.
        newui::ViewBuilder<Toolbox> toolboxBuilder;
        toolboxBuilder.name("workspaceToolboxPane")
            .configure([this](Toolbox& toolbox) {
                toolbox.onEntryActivated.add([this](Toolbox&, newui::SubView* created) {
                    // isDesignTime() no longer propagates from an owning
                    // RootView (view.cpp) - a freshly-created control the
                    // designer itself adds needs its own flag set
                    // explicitly, right here, same as rootViewProxy_/
                    // frameProxy_ are above.
                    created->setDesignTime(true);

                    if (undoStack_ == nullptr) {
                        rootViewProxy_->addChild(created);
                        onDesignSurfaceChanged(*this);
                        return newui::SyncReturn::Handled;
                    }

                    const newui::reflection::Class* clazz = newui::reflection::classinfo(typeid(*created));
                    newui::UndoableAction action;
                    action.description = "Add " + (clazz != nullptr ? clazz->name() : std::string("Control"));
                    action.doIt = [this, created]() {
                        rootViewProxy_->addChild(created);
                        onDesignSurfaceChanged(*this);
                    };
                    action.undoIt = [this, created]() {
                        rootViewProxy_->removeChild(created);
                        onDesignSurfaceChanged(*this);
                    };
                    undoStack_->push(action);
                    return newui::SyncReturn::Handled;
                });
            });
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

        // HighlightText pairs with the status bar's own HighlightBackground
        // above, same role pairing FrameProxy's title text already uses.
        // Empty until a real undo/redo action exists - refreshUndoRedoButtons()
        // (DesignerEditor.cpp) keeps this in sync from here on.
        newui::ViewBuilder<newui::Label> undoRedoStatusLabelBuilder;
        undoRedoStatusLabelBuilder.name("workspaceUndoRedoStatusLabel")
            .layoutParams<newui::AnchorLayoutParams>([](newui::AnchorLayoutParams& p) {
                // width/height, not desiredSize() - AnchorLayout only reads
                // desiredSize() for a stretched axis (Left+Right/Top+Bottom);
                // a single-edge anchor like this one sizes from these
                // params fields instead (real bug found live: this was
                // originally left at its 0.0f default, so the label always
                // painted into a zero-width rect regardless of its text).
                p.anchors = newui::Anchor::Left | newui::Anchor::CenterY;
                p.leftMargin = 8.0f;
                p.width = 400.0f;
                p.height = Workspace::kStatusBarHeight;
            })
            .configure([](newui::Label& label) {
                label.setTextColor(newui::UIColorManager::colorFor(newui::UIColorRole::HighlightText).toBLRgba32());
            });
        undoRedoStatusLabel_ = undoRedoStatusLabelBuilder.build();

        statusBuilder.layout<newui::AnchorLayout>().child(undoRedoStatusLabel_);
        statusBar_ = statusBuilder.build();

        self.child(topBar_).child(middle).child(statusBar_);
    }
}
