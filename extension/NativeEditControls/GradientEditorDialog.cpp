#include "GradientEditorDialog.h"
#include "PaintUtils.h"
#include "TextEncoding.h"

#include <newui/layout.h>
#include <newui/rootview.h>
#include <newui/uicolormanager.h>
#include <newui/viewbuilder.h>

#include <memory>

namespace CodeToolsVsix
{
    namespace
    {
        constexpr float kDialogWidth = 360.0f;
        constexpr float kDialogHeight = 560.0f;
        constexpr float kRowHeight = 24.0f;
        constexpr float kLabelWidth = 70.0f;
        constexpr float kPreviewHeight = 96.0f;
        constexpr float kTrackHeight = 28.0f;
        constexpr float kColorPickerHeight = 100.0f;
        constexpr float kEditorRowSpacing = 6.0f;
        // colorPicker_ + one row of spacing + the hex row - selectedStopEditor_'s own real
        // vertical extent. This toolkit has no content-measurement pass (a container never
        // auto-sizes from its own children - see e.g. Gradient's own class comment on
        // desiredSize()) - every fixed-size row in this dialog (previewBox_/track_/this one) needs
        // an explicit desiredSize(), or FlexLayout resolves its unweighted main-axis size to 0 on
        // the first layout pass. Missing this on selectedStopEditor_ itself (colorPicker_'s own
        // desiredSize() alone isn't enough - it's the *container* still needing one) is exactly
        // the real bug that shipped once live: colorPicker_ was there, built correctly, and simply
        // never got any height to render into.
        constexpr float kSelectedStopEditorHeight = kColorPickerHeight + kEditorRowSpacing + kRowHeight;
        constexpr float kStopHandleRadius = 7.0f;
        constexpr float kStopHandleHitRadius = 9.0f;

        // Copies source, then overwrites *only* its rendering-geometry fields (linearStart/End,
        // radialCenter/Radius, conicCenter) with values fit to bounds. Gradient's own real
        // geometry fields (graphics.h) are absolute coordinates in whatever View the gradient is
        // eventually painted onto (e.g. a Button's own local bounds) - this dialog has no real
        // control surface for editing those yet (a later phase), so this dialog's preview can
        // only ever show a representative rendering, fit to whatever box is previewing it, never
        // the real final placement. Never written back into working_ itself - previewBox_'s own
        // paint() throws this copy away every time, so the dialog's live preview can never corrupt
        // the real committed value with throwaway preview-box coordinates.
        newui::gfx::Gradient previewGradientFor(const newui::gfx::Gradient& source, const newui::Rect& bounds)
        {
            newui::gfx::Gradient preview = source;
            float centerX = bounds.left() + bounds.width() * 0.5f;
            float centerY = bounds.top() + bounds.height() * 0.5f;

            switch (preview.kind()) {
            case newui::gfx::GradientKind::Radial: {
                preview.setRadialCenter(newui::Point(centerX, centerY));
                preview.setRadialFocalOffset(newui::Point(0.0f, 0.0f));
                float radius = bounds.width() < bounds.height() ? bounds.width() : bounds.height();
                preview.setRadialRadius(radius * 0.5f);
                break;
            }
            case newui::gfx::GradientKind::Conic:
                preview.setConicCenter(newui::Point(centerX, centerY));
                break;
            case newui::gfx::GradientKind::Linear:
            default:
                preview.setLinearStart(newui::Point(bounds.left(), centerY));
                preview.setLinearEnd(newui::Point(bounds.right(), centerY));
                break;
            }
            return preview;
        }

        // Live-renders owner_.gradient() as it would actually look - checkerboard first, then the
        // resolved gradient (via previewGradientFor(), never the real committed geometry - see its
        // own comment) painted on top. Reads owner_'s current state fresh on every paint() call, so
        // there's nothing here to keep in sync separately - GradientEditorDialog::refreshPreview()
        // just requests a repaint, it never pushes state into this class.
        class PreviewBox : public newui::SubView
        {
        public:
            explicit PreviewBox(GradientEditorDialog& owner) : owner_(owner) {}

            void paint(BLContext& ctx) override
            {
                newui::Rect bounds = getClientBounds();
                if (bounds.width() <= 0.0f || bounds.height() <= 0.0f) {
                    return;
                }

                paintCheckerboard(ctx, bounds);

                newui::gfx::Gradient preview = previewGradientFor(owner_.gradient(), bounds);
                BLVar fill = preview.toBLVar(bounds);
                ctx.save();
                ctx.set_fill_style(fill);
                ctx.fill_rect(BLRect(bounds));
                ctx.restore();
            }

        private:
            GradientEditorDialog& owner_;
        };

        // Self-contained drag control, same shape as newui::Splitter (this file's own closest
        // precedent, splitter.cpp) - owns its own drag state and does its own internal per-handle
        // hit-testing, rather than being decomposed into N separate child SubViews (RootView
        // captures whichever SubView was actually hit at mouseDown - a plain SubView, not Control,
        // for the same reason Splitter itself isn't one: Control's base constructor
        // unconditionally claims every mouseDown anywhere in its own bounds, which would fight a
        // widget that only cares about specific small hit regions).
        //
        // Paints a thin position/color strip - always left-to-right, regardless of the real
        // gradient's kind, since this view only ever visualizes *where* each stop sits along
        // [0,1], never the resolved Linear/Radial/Conic shape (that's previewBox_'s job) - plus one
        // draggable circular handle per stop. Dragging a handle calls owner_.selectStop()/
        // setSelectedStopOffset() - the real public API, same "drive the real method" convention
        // this file's own tests already rely on for the kind SegmentedControl. A mouseDown away
        // from every existing handle inserts a new one there instead (owner_.addStopAt()) and
        // starts dragging it immediately, matching the mockup's own track click behavior.
        class StopTrack : public newui::SubView
        {
        public:
            explicit StopTrack(GradientEditorDialog& owner) : owner_(owner)
            {
                onMouseDown.add(this, &StopTrack::handleMouseDown);
                onMouseMove.add(this, &StopTrack::handleMouseMove);
                onMouseUp.add(this, &StopTrack::handleMouseUp);
            }

            void paint(BLContext& ctx) override
            {
                newui::Rect bounds = getClientBounds();
                if (bounds.width() <= 0.0f || bounds.height() <= 0.0f) {
                    return;
                }

                float lineHeight = 4.0f;
                newui::Rect lineRect(bounds.left(), bounds.top() + bounds.height() * 0.5f - lineHeight * 0.5f,
                    bounds.width(), lineHeight);
                paintCheckerboard(ctx, lineRect, 4.0);

                const std::vector<newui::gfx::GradientStop>& stops = owner_.gradient().stops();
                if (!stops.empty()) {
                    // A throwaway Linear gradient spanning lineRect - visualizes stop
                    // position/color only, never owner_.gradient()'s own real kind/geometry (see
                    // this class's own comment).
                    newui::gfx::Gradient line;
                    line.setKind(newui::gfx::GradientKind::Linear);
                    line.setLinearStart(newui::Point(lineRect.left(), lineRect.top()));
                    line.setLinearEnd(newui::Point(lineRect.right(), lineRect.top()));
                    for (const newui::gfx::GradientStop& stop : stops) {
                        line.stops().push_back(stop);
                    }
                    ctx.save();
                    ctx.set_fill_style(line.toBLVar(lineRect));
                    ctx.fill_rect(BLRect(lineRect));
                    ctx.restore();
                }

                std::size_t selected = owner_.selectedStopIndex();
                for (std::size_t i = 0; i < stops.size(); ++i) {
                    newui::Point center = handleCenter(bounds, stops[i].offset());
                    ctx.save();
                    ctx.set_fill_style(stops[i].color().toBLRgba32());
                    ctx.fill_circle(double(center.x), double(center.y), double(kStopHandleRadius));
                    // A plain white ring alone (the only one before this) all but disappears for
                    // a light/white stop color - a slightly larger, semi-transparent dark ring
                    // just outside it keeps the handle visible against any stop color, same idea
                    // as the mockup's own stop-handle box-shadow (a white border plus a darker
                    // outer ring, gradient_editor_dialog.html's .stop-handle).
                    ctx.set_stroke_style(BLRgba32(0, 0, 0, 140));
                    ctx.set_stroke_width(1.0);
                    ctx.stroke_circle(double(center.x), double(center.y), double(kStopHandleRadius) + 1.5);
                    ctx.set_stroke_style(BLRgba32(255, 255, 255));
                    ctx.set_stroke_width(2.0);
                    ctx.stroke_circle(double(center.x), double(center.y), double(kStopHandleRadius));
                    ctx.restore();

                    if (i == selected) {
                        ctx.save();
                        ctx.set_stroke_style(newui::UIColorManager::colorFor(newui::UIColorRole::HighlightBackground).toBLRgba32());
                        ctx.set_stroke_width(2.0);
                        ctx.stroke_circle(double(center.x), double(center.y), double(kStopHandleRadius) + 3.0);
                        ctx.restore();
                    }
                }
            }

        private:
            newui::Point handleCenter(const newui::Rect& bounds, float offset) const
            {
                float clamped = offset < 0.0f ? 0.0f : (offset > 1.0f ? 1.0f : offset);
                return newui::Point(bounds.left() + bounds.width() * clamped, bounds.top() + bounds.height() * 0.5f);
            }

            // Nearest handle within kStopHandleHitRadius of pt, or stops.size() if none qualify -
            // squared-distance comparison, no <cmath> needed.
            std::size_t hitTestHandle(const newui::Point& pt) const
            {
                newui::Rect bounds = getClientBounds();
                const std::vector<newui::gfx::GradientStop>& stops = owner_.gradient().stops();
                std::size_t best = stops.size();
                float bestDistSq = kStopHandleHitRadius * kStopHandleHitRadius;
                for (std::size_t i = 0; i < stops.size(); ++i) {
                    newui::Point center = handleCenter(bounds, stops[i].offset());
                    float dx = pt.x - center.x;
                    float dy = pt.y - center.y;
                    float distSq = dx * dx + dy * dy;
                    if (distSq <= bestDistSq) {
                        bestDistSq = distSq;
                        best = i;
                    }
                }
                return best;
            }

            newui::SyncReturn handleMouseDown(newui::View& /*sender*/, const newui::Point& pt,
                    std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/)
            {
                std::size_t hit = hitTestHandle(pt);
                if (hit < owner_.gradient().stops().size()) {
                    dragging_ = true;
                    owner_.selectStop(hit);
                    redraw();
                    return newui::SyncReturn::Handled;
                }

                // Away from every existing handle - insert a new stop here (matching the
                // mockup's own track pointerdown behavior) and start dragging it immediately, so
                // a single click-drag gesture both places and positions it.
                newui::Rect bounds = getClientBounds();
                if (bounds.width() <= 0.0f) {
                    return newui::SyncReturn::Ignored;
                }
                float t = (pt.x - bounds.left()) / bounds.width();
                owner_.addStopAt(t);
                dragging_ = true;
                redraw();
                return newui::SyncReturn::Handled;
            }

            newui::SyncReturn handleMouseMove(newui::View& /*sender*/, const newui::Point& pt,
                    std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/)
            {
                if (!dragging_) {
                    return newui::SyncReturn::Ignored;
                }
                newui::Rect bounds = getClientBounds();
                float t = bounds.width() <= 0.0f ? 0.0f : (pt.x - bounds.left()) / bounds.width();
                owner_.setSelectedStopOffset(t);
                return newui::SyncReturn::Handled;
            }

            newui::SyncReturn handleMouseUp(newui::View& /*sender*/, const newui::Point& /*pt*/,
                    std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/)
            {
                if (!dragging_) {
                    return newui::SyncReturn::Ignored;
                }
                dragging_ = false;
                return newui::SyncReturn::Handled;
            }

            GradientEditorDialog& owner_;
            bool dragging_ = false;
        };
    }

    GradientEditorDialog::GradientEditorDialog()
    {
        setTitle("Edit Gradient");
        setBounds(newui::Rect(120.0f, 120.0f, kDialogWidth, kDialogHeight));
        buildChrome();
        showPageForKind(working_.kind());
    }

    void GradientEditorDialog::setGradient(const newui::gfx::Gradient& gradient)
    {
        working_ = gradient;
        // A Fill whose backgroundFill was never a gradient before (the common first-click case)
        // seeds a real, empty-stops newui::gfx::Gradient() - correct, but leaves Phase 1 with
        // nothing at all to edit and no add-stop UI yet (that's the real track control, Phase 3).
        // Seeding two reasonable default stops here keeps this phase actually usable on that real
        // first-click case, same spirit as every other "0 isn't useful, give it a real starting
        // point" default elsewhere in this codebase (e.g. Gradient's own linearEnd_/radialRadius_
        // class defaults).
        if (working_.kind() != newui::gfx::GradientKind::Point && working_.stops().empty()) {
            working_.stops().push_back(newui::gfx::GradientStop(0.0f, newui::Color(0, 0, 0)));
            working_.stops().push_back(newui::gfx::GradientStop(1.0f, newui::Color(255, 255, 255)));
        }
        selectedStopIndex_ = 0;
        kindControl_->setSelectedIndex(static_cast<std::size_t>(working_.kind()));
        showPageForKind(working_.kind());
    }

    void GradientEditorDialog::setKind(newui::gfx::GradientKind kind)
    {
        working_.setKind(kind);
        kindControl_->setSelectedIndex(static_cast<std::size_t>(kind));
        showPageForKind(kind);
    }

    void GradientEditorDialog::selectStop(std::size_t index)
    {
        std::size_t count = working_.stops().size();
        selectedStopIndex_ = count == 0 ? 0 : (index >= count ? count - 1 : index);
        refreshSelectedStopEditor();
    }

    void GradientEditorDialog::setSelectedStopColor(const newui::Color& color)
    {
        std::vector<newui::gfx::GradientStop>& stops = working_.stops();
        if (selectedStopIndex_ < stops.size()) {
            stops[selectedStopIndex_].setColor(color);
            // Keeps colorPicker_/hexField_ in sync regardless of which one committed the change -
            // ColorPicker::setColor() no-ops if it's already showing this color (same contract
            // newui::Slider::setValue() documents), so this never fights whichever one is the
            // real source of a given edit.
            refreshSelectedStopEditor();
            refreshPreview();
        }
    }

    void GradientEditorDialog::setSelectedStopOffset(float offset)
    {
        std::vector<newui::gfx::GradientStop>& stops = working_.stops();
        if (selectedStopIndex_ < stops.size()) {
            float clamped = offset < 0.0f ? 0.0f : (offset > 1.0f ? 1.0f : offset);
            stops[selectedStopIndex_].setOffset(clamped);
            refreshPreview();
        }
    }

    void GradientEditorDialog::buildChrome()
    {
        // Real top-level surfaces elsewhere in this codebase (Workspace, DesignerEditor,
        // testharness's own main Frame) all explicitly set WindowBackground - there's no default
        // that isn't black, so this dialog painted solid black with nothing visibly readable
        // against it until this was added.
        rootView().style().setBackgroundColor(newui::UIColorManager::colorFor(newui::UIColorRole::WindowBackground));
        // AnchorLayout on rootView() itself is what makes contentRoot_'s stretch-fill
        // AnchorLayoutParams (below) actually apply - without a Layout attached here,
        // View::updateLayout() has nothing to call arrange() on, so contentRoot_ would just sit at
        // whatever fixed size it was built with even once the real native window (whose actual
        // client size only becomes known once ensureInitialized() creates it) turns out different -
        // exactly why the footer was landing outside the visible area.
        rootView().setLayout(std::make_unique<newui::AnchorLayout>());

        newui::ViewBuilder<newui::SubView> rootBuilder;
        rootBuilder.name("gradientEditorContent")
            .visible(true)
            .bounds(newui::Rect(0.0f, 0.0f, kDialogWidth, kDialogHeight))
            .layoutParams<newui::AnchorLayoutParams>([](newui::AnchorLayoutParams& params) {
                params.anchors = newui::Anchor::Left | newui::Anchor::Top
                                | newui::Anchor::Right | newui::Anchor::Bottom;
            })
            .layout<newui::FlexLayout>([](newui::FlexLayout& layout) {
                layout.setOrientation(newui::Orientation::Vertical);
                layout.setSpacing(8.0f);
                layout.setPadding(12.0f);
            });
        contentRoot_ = rootBuilder.build();
        rootView().addChild(contentRoot_);

        newui::ViewBuilder<newui::SegmentedControl> kindBuilder;
        kindBuilder.name("gradientKindControl")
            .visible(true)
            .configure([](newui::SegmentedControl& control) {
                control.setSegments({"Linear", "Radial", "Conic", "Point"});
            });
        kindControl_ = kindBuilder.build();
        kindControl_->setDesiredSize(kindControl_->naturalSize());
        kindControl_->onSelectionChanged.add([this](newui::SegmentedControl& sender) {
            setKind(static_cast<newui::gfx::GradientKind>(sender.selectedIndex()));
            return newui::SyncReturn::Handled;
        });
        contentRoot_->addChild(kindControl_);

        // previewBox_/track_ (Phase 2) - shared across Linear/Radial/Conic (not per-kind pages
        // like the stop rows below, since neither one's own content differs by kind beyond what
        // it reads live off owner_.gradient() - see their own class comments), hidden entirely
        // for Point by showPageForKind().
        newui::ViewBuilder<PreviewBox> previewBuilder(new PreviewBox(*this));
        previewBuilder.name("gradientPreviewBox")
            .visible(true)
            .desiredSize(newui::Size(0.0f, kPreviewHeight));
        previewBox_ = previewBuilder.build();
        contentRoot_->addChild(previewBox_);

        newui::ViewBuilder<StopTrack> trackBuilder(new StopTrack(*this));
        trackBuilder.name("gradientStopTrack")
            .visible(true)
            .desiredSize(newui::Size(0.0f, kTrackHeight));
        track_ = trackBuilder.build();
        contentRoot_->addChild(track_);

        // selectedStopEditor_ (Phase 3) - one shared ColorPicker + hex TextField, retargeted to
        // whichever stop is selected on track_ (see refreshSelectedStopEditor()'s own comment) -
        // same "shared, not per-kind" reasoning as previewBox_/track_ above, hidden alongside them
        // for Point.
        newui::ViewBuilder<newui::SubView> editorBuilder;
        editorBuilder.name("gradientSelectedStopEditor")
            .visible(true)
            .desiredSize(newui::Size(0.0f, kSelectedStopEditorHeight))
            .layout<newui::FlexLayout>([](newui::FlexLayout& layout) {
                layout.setOrientation(newui::Orientation::Vertical);
                layout.setSpacing(kEditorRowSpacing);
            });
        selectedStopEditor_ = editorBuilder.build();
        contentRoot_->addChild(selectedStopEditor_);

        newui::ViewBuilder<ColorPicker> pickerBuilder;
        pickerBuilder.name("gradientColorPicker")
            .visible(true)
            .desiredSize(newui::Size(0.0f, kColorPickerHeight));
        colorPicker_ = pickerBuilder.build();
        colorPicker_->onColorChanged.add([this](ColorPicker& sender) {
            setSelectedStopColor(sender.color());
            return newui::SyncReturn::Handled;
        });
        selectedStopEditor_->addChild(colorPicker_);

        newui::ViewBuilder<newui::SubView> hexRowBuilder;
        hexRowBuilder.name("gradientHexRow")
            .visible(true)
            .desiredSize(newui::Size(0.0f, kRowHeight))
            .layout<newui::FlexLayout>([](newui::FlexLayout& layout) {
                layout.setOrientation(newui::Orientation::Horizontal);
                layout.setSpacing(6.0f);
            });
        newui::SubView* hexRow = hexRowBuilder.build();

        newui::ViewBuilder<newui::Label> hexLabelBuilder;
        hexLabelBuilder.name("gradientHexLabel")
            .visible(true)
            .desiredSize(newui::Size(kLabelWidth, kRowHeight))
            .configure([](newui::Label& label) { label.setText("Hex"); });
        hexRow->addChild(hexLabelBuilder.build());

        newui::ViewBuilder<newui::TextField> hexFieldBuilder;
        hexFieldBuilder.name("gradientHexField")
            .visible(true)
            .layoutParams<newui::FlexLayoutParams>([](newui::FlexLayoutParams& params) { params.weight = 1.0f; });
        hexField_ = hexFieldBuilder.build();
        hexField_->onLostFocus.add([this](newui::View&) { commitHexField(); return newui::SyncReturn::Ignored; });
        hexField_->onReturnPressed.add([this](newui::TextField&) { commitHexField(); return newui::SyncReturn::Handled; });
        hexRow->addChild(hexField_);

        newui::ViewBuilder<newui::Button> deleteStopBuilder;
        deleteStopBuilder.name("gradientDeleteStopButton")
            .visible(true)
            .desiredSize(newui::Size(60.0f, kRowHeight))
            .configure([](newui::Button& button) { button.setText("Delete"); });
        deleteStopButton_ = deleteStopBuilder.build();
        deleteStopButton_->onClick.add([this](newui::Control&) {
            deleteSelectedStop();
            return newui::SyncReturn::Handled;
        });
        hexRow->addChild(deleteStopButton_);

        selectedStopEditor_->addChild(hexRow);

        // pagesContainer_ owns a CardLayout switching between the 4 pages below - built once
        // here, never destroyed/rebuilt on a kind change (see this class's own header comment for
        // why each kind gets its own real page instead of one shared, rebuilt-in-place container).
        newui::ViewBuilder<newui::SubView> pagesBuilder;
        pagesBuilder.name("gradientPages")
            .visible(true)
            .layoutParams<newui::FlexLayoutParams>([](newui::FlexLayoutParams& params) { params.weight = 1.0f; });
        pagesContainer_ = pagesBuilder.build();
        auto pagesLayout = std::make_unique<newui::CardLayout>();
        pagesLayout_ = pagesLayout.get();
        pagesContainer_->setLayout(std::move(pagesLayout));
        contentRoot_->addChild(pagesContainer_);

        // Linear/Radial/Conic each get their own real, currently-empty page (built identically via
        // this local helper) - added in that exact order, matching GradientKind's own numeric
        // values, so showPageForKind() can index pagesLayout_ directly off the enum with no lookup
        // table. Ready for each kind's own future controls (an angle dial, a shape toggle - see
        // this class's own header comment) - not dead weight just because nothing lives in them
        // yet.
        auto buildStopsPage = [](const char* name) {
            newui::ViewBuilder<newui::SubView> pageBuilder;
            pageBuilder.name(name)
                .visible(true)
                .layout<newui::FlexLayout>([](newui::FlexLayout& layout) {
                    layout.setOrientation(newui::Orientation::Vertical);
                    layout.setSpacing(4.0f);
                });
            return pageBuilder.build();
        };
        linearPage_ = buildStopsPage("gradientLinearPage");
        pagesContainer_->addChild(linearPage_);
        radialPage_ = buildStopsPage("gradientRadialPage");
        pagesContainer_->addChild(radialPage_);
        conicPage_ = buildStopsPage("gradientConicPage");
        pagesContainer_->addChild(conicPage_);

        // Point isn't built yet (Phase 4) - an honest "not built yet" placeholder, matching this
        // project's own convention elsewhere (Workspace's disabled Source/Data Flow segments)
        // rather than pretending capability that doesn't exist.
        newui::ViewBuilder<newui::Label> placeholderBuilder;
        placeholderBuilder.name("gradientPointPlaceholder")
            .visible(true)
            .desiredSize(newui::Size(0.0f, kRowHeight))
            .configure([](newui::Label& label) { label.setText("Point editing not built yet"); });
        pointPage_ = placeholderBuilder.build();
        pagesContainer_->addChild(pointPage_);

        newui::ViewBuilder<newui::SubView> footerBuilder;
        footerBuilder.name("gradientEditorFooter")
            .visible(true)
            .desiredSize(newui::Size(0.0f, 32.0f))
            .layout<newui::FlexLayout>([](newui::FlexLayout& layout) {
                layout.setOrientation(newui::Orientation::Horizontal);
                layout.setSpacing(8.0f);
            });
        newui::SubView* footer = footerBuilder.build();

        newui::ViewBuilder<newui::SubView> footerSpacerBuilder;
        footerSpacerBuilder.name("gradientEditorFooterSpacer")
            .visible(true)
            .layoutParams<newui::FlexLayoutParams>([](newui::FlexLayoutParams& params) { params.weight = 1.0f; });
        footer->addChild(footerSpacerBuilder.build());

        newui::ViewBuilder<newui::Button> cancelBuilder;
        cancelBuilder.name("gradientEditorCancelButton")
            .visible(true)
            .desiredSize(newui::Size(70.0f, 24.0f))
            .configure([](newui::Button& button) { button.setText("Cancel"); });
        newui::Button* cancelButton = cancelBuilder.build();
        cancelButton->onClick.add([this](newui::Control&) {
            close(newui::DialogResult::Cancel);
            return newui::SyncReturn::Handled;
        });
        footer->addChild(cancelButton);

        newui::ViewBuilder<newui::Button> applyBuilder;
        applyBuilder.name("gradientEditorApplyButton")
            .visible(true)
            .desiredSize(newui::Size(70.0f, 24.0f))
            .configure([](newui::Button& button) { button.setText("Apply"); });
        newui::Button* applyButton = applyBuilder.build();
        applyButton->onClick.add([this](newui::Control&) {
            close(newui::DialogResult::Ok);
            return newui::SyncReturn::Handled;
        });
        footer->addChild(applyButton);

        contentRoot_->addChild(footer);
    }

    void GradientEditorDialog::showPageForKind(newui::gfx::GradientKind kind)
    {
        pagesLayout_->show(static_cast<std::size_t>(kind));

        bool showSharedEditors = (kind != newui::gfx::GradientKind::Point);
        previewBox_->setVisible(showSharedEditors);
        track_->setVisible(showSharedEditors);
        selectedStopEditor_->setVisible(showSharedEditors);
        // setVisible() alone doesn't reflow anything (see its own definition, subview.cpp) -
        // contentRoot_'s FlexLayout needs to actually re-run so it gives pagesContainer_/the
        // footer the space these rows just gave up (or reclaims when they reappear).
        contentRoot_->updateLayout();

        refreshSelectedStopEditor();
        refreshPreview();
        // A layout reflow moves more than just previewBox_/track_/selectedStopEditor_
        // (pagesContainer_/the footer shift too) - refreshPreview()'s own targeted redraw()s don't
        // cover that, so this one still needs the whole-window repaint.
        rootView().markDirty();
    }

    void GradientEditorDialog::refreshPreview()
    {
        if (previewBox_ != nullptr) {
            previewBox_->redraw();
        }
        if (track_ != nullptr) {
            track_->redraw();
        }
    }

    void GradientEditorDialog::refreshSelectedStopEditor()
    {
        const std::vector<newui::gfx::GradientStop>& stops = working_.stops();
        // Below the minimum (2), matching the mockup's own guard - can't delete any further.
        deleteStopButton_->setEnabled(stops.size() > 2);
        if (selectedStopIndex_ >= stops.size()) {
            return;
        }
        const newui::Color& color = stops[selectedStopIndex_].color();
        colorPicker_->setColor(color);
        hexField_->setText(utf8ToWide(color.toString()));
    }

    void GradientEditorDialog::commitHexField()
    {
        newui::Color color;
        if (newui::Color::fromString(wideToUtf8(hexField_->text()), color)) {
            setSelectedStopColor(color);
        }
    }

    void GradientEditorDialog::addStopAt(float offset)
    {
        float clamped = offset < 0.0f ? 0.0f : (offset > 1.0f ? 1.0f : offset);
        std::vector<newui::gfx::GradientStop>& stops = working_.stops();

        newui::Color color;
        // Neither bracketing stop is found by vector index - stops() isn't guaranteed sorted by
        // offset (a drag can move one stop's offset past a neighbor's without reordering the
        // vector) - so this scans by offset value directly, same as StopTrack's own
        // handleCenter()-per-stop painting already does.
        const newui::gfx::GradientStop* left = nullptr;
        const newui::gfx::GradientStop* right = nullptr;
        for (const newui::gfx::GradientStop& stop : stops) {
            if (stop.offset() <= clamped && (left == nullptr || stop.offset() > left->offset())) {
                left = &stop;
            }
            if (stop.offset() >= clamped && (right == nullptr || stop.offset() < right->offset())) {
                right = &stop;
            }
        }
        if (left != nullptr && right != nullptr && left != right) {
            float span = right->offset() - left->offset();
            float t = span <= 0.0f ? 0.0f : (clamped - left->offset()) / span;
            const newui::Color& a = left->color();
            const newui::Color& b = right->color();
            color = newui::Color(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t,
                a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t);
        } else if (left != nullptr) {
            color = left->color();
        } else if (right != nullptr) {
            color = right->color();
        }

        stops.push_back(newui::gfx::GradientStop(clamped, color));
        selectStop(stops.size() - 1);
        refreshPreview();
    }

    void GradientEditorDialog::deleteSelectedStop()
    {
        std::vector<newui::gfx::GradientStop>& stops = working_.stops();
        if (stops.size() <= 2 || selectedStopIndex_ >= stops.size()) {
            return;
        }
        stops.erase(stops.begin() + static_cast<std::ptrdiff_t>(selectedStopIndex_));
        selectStop(0);
        refreshPreview();
    }
}
