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
        constexpr float kDialogHeight = 640.0f;
        constexpr float kRowHeight = 24.0f;
        constexpr float kLabelWidth = 70.0f;
        constexpr float kPreviewHeight = 96.0f;
        constexpr float kTrackHeight = 28.0f;
        constexpr float kColorPickerHeight = 100.0f;
        constexpr float kEditorRowSpacing = 6.0f;
        // colorPicker_ + one row of spacing + the hex row - selectedItemEditor_'s own real
        // vertical extent. This toolkit has no content-measurement pass (a container never
        // auto-sizes from its own children) - every fixed-size row in this dialog (previewBox_/
        // track_/this one) needs an explicit desiredSize(), or FlexLayout resolves its unweighted
        // main-axis size to 0 on the first layout pass. Missing this on selectedItemEditor_ itself
        // (colorPicker_'s own desiredSize() alone isn't enough - it's the *container* still
        // needing one) is exactly the real bug that shipped once live: colorPicker_ was there,
        // built correctly, and simply never got any height to render into.
        constexpr float kSelectedItemEditorHeight = kColorPickerHeight + kEditorRowSpacing + kRowHeight;
        constexpr float kStopHandleRadius = 7.0f;
        constexpr float kStopHandleHitRadius = 9.0f;
        constexpr float kPointHandleRadius = 7.0f;
        constexpr float kPointHandleHitRadius = 9.0f;
        constexpr float kPresetSwatchSize = 32.0f;
        constexpr float kPresetsRowSpacing = 6.0f;

        // Color::fromString() (real, already-tested hex parsing) rather than the raw (r,g,b)
        // float constructor - Color(255, 0, 0) would silently store (255.0f, 0.0f, 0.0f), wildly
        // out of the real [0,1] channel range every Color here needs (the exact bug this whole
        // file already found and fixed twice this session, once for stops' own default seeding and
        // once for points'). alpha is a real [0,1] scale value, applied after parsing (the mockup's
        // own preset data keeps offset/alpha as separate numbers too, not baked into the hex).
        newui::Color presetColor(const char* hex, float alpha = 1.0f)
        {
            newui::Color color;
            newui::Color::fromString(hex, color);
            color.a = alpha;
            return color;
        }

        // The 6 built-in presets, verbatim from design/reference/gradient_editor_dialog.html's own
        // `presets` array - each entry's own "angle" is deliberately dropped (see
        // GradientEditorDialog.h's own header comment on why: no real control surface for Linear's
        // own start/end geometry exists yet, so there's nothing to apply an angle onto). The last
        // preset (opaque-to-transparent same blue) exercises real alpha, same as everywhere else in
        // this dialog.
        std::vector<std::vector<newui::gfx::GradientStop>> builtinPresetStops()
        {
            return {
                { newui::gfx::GradientStop(0.0f, presetColor("#5A7CE9")), newui::gfx::GradientStop(1.0f, presetColor("#8A5CE0")) },
                { newui::gfx::GradientStop(0.0f, presetColor("#F45B8D")), newui::gfx::GradientStop(1.0f, presetColor("#F2B705")) },
                { newui::gfx::GradientStop(0.0f, presetColor("#2FBF71")), newui::gfx::GradientStop(1.0f, presetColor("#22C1D6")) },
                { newui::gfx::GradientStop(0.0f, presetColor("#1C1D1F")), newui::gfx::GradientStop(1.0f, presetColor("#4B4E52")) },
                { newui::gfx::GradientStop(0.0f, presetColor("#E9705A")), newui::gfx::GradientStop(0.5f, presetColor("#F2B705")),
                    newui::gfx::GradientStop(1.0f, presetColor("#2FBF71")) },
                { newui::gfx::GradientStop(0.0f, presetColor("#5A7CE9", 0.0f)), newui::gfx::GradientStop(1.0f, presetColor("#5A7CE9", 1.0f)) },
            };
        }

        // The one real, mutable preset list every GradientEditorDialog instance shares for the
        // rest of this process's run - a plain function-local static (constructed once, from
        // builtinPresetStops(), on first use) rather than a global, avoiding static initialization
        // order concerns. addPresetFromCurrent()/removePreset() mutate it directly; nothing here
        // persists it to disk - a real, later feature if ever wanted.
        std::vector<std::vector<newui::gfx::GradientStop>>& presetRegistry()
        {
            static std::vector<std::vector<newui::gfx::GradientStop>> presets = builtinPresetStops();
            return presets;
        }

        // Maps a real shapeBounds()-relative position into screenBounds' own local coordinates -
        // proportional, same "fit shapeBounds()'s own aspect into whatever's previewing it" idea
        // previewGradientFor()'s Linear/Radial/Conic cases already use for their own synthetic
        // geometry. Used both for rendering (previewGradientFor()'s Point case) and for
        // hit-testing/painting real point handles (PreviewBox) - one shared conversion so the two
        // can never disagree about where a point actually is on screen.
        newui::Point mapToScreen(const newui::Rect& shapeBounds, const newui::Rect& screenBounds, const newui::Point& shapeRelative)
        {
            float nx = shapeBounds.width() <= 0.0f ? 0.0f : (shapeRelative.x - shapeBounds.left()) / shapeBounds.width();
            float ny = shapeBounds.height() <= 0.0f ? 0.0f : (shapeRelative.y - shapeBounds.top()) / shapeBounds.height();
            return newui::Point(screenBounds.left() + nx * screenBounds.width(), screenBounds.top() + ny * screenBounds.height());
        }

        // The inverse of mapToScreen() - converts a real screen-space click/drag point (e.g. a
        // PreviewBox-local mouse position) back into shapeBounds()'s own coordinate space, the one
        // real GradientPoint positions are actually stored in.
        newui::Point mapToShapeSpace(const newui::Rect& shapeBounds, const newui::Rect& screenBounds, const newui::Point& screenPoint)
        {
            float nx = screenBounds.width() <= 0.0f ? 0.0f : (screenPoint.x - screenBounds.left()) / screenBounds.width();
            float ny = screenBounds.height() <= 0.0f ? 0.0f : (screenPoint.y - screenBounds.top()) / screenBounds.height();
            return newui::Point(shapeBounds.left() + nx * shapeBounds.width(), shapeBounds.top() + ny * shapeBounds.height());
        }

        // Copies source, then overwrites *only* its rendering-geometry fields (linearStart/End,
        // radialCenter/Radius, conicCenter, or every point's own position for Point) with values
        // fit to bounds. Linear/Radial/Conic's own geometry fields (graphics.h) are absolute
        // coordinates in whatever View the gradient is eventually painted onto (e.g. a Button's
        // own local bounds) - this dialog has no real control surface for editing those yet (a
        // later phase), so the preview can only ever show a representative rendering for those 3
        // kinds, fit to whatever box is previewing it, never the real final placement. Point is
        // different: a GradientPoint's own position() *is* the real committed data (there's no
        // separate "stops" list independent of placement the way Linear/Radial/Conic have) - so
        // this rescales the *real* positions (via mapToScreen(), shapeBounds()-relative to
        // bounds-relative) rather than fabricating throwaway ones, which is also what makes
        // clicking/dragging directly in the preview a real, correct edit for Point (see
        // PreviewBox's own comment). Either way, nothing here is ever written back into working_
        // itself - previewBox_'s own paint() throws this copy away every time.
        newui::gfx::Gradient previewGradientFor(const newui::gfx::Gradient& source, const newui::Rect& bounds, const newui::Rect& shapeBounds)
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
            case newui::gfx::GradientKind::Point: {
                for (newui::gfx::GradientPoint& point : preview.points()) {
                    point.setPosition(mapToScreen(shapeBounds, bounds, point.position()));
                }
                break;
            }
            case newui::gfx::GradientKind::Linear:
            default:
                preview.setLinearStart(newui::Point(bounds.left(), centerY));
                preview.setLinearEnd(newui::Point(bounds.right(), centerY));
                break;
            }
            return preview;
        }

        // Live-renders owner_.gradient() as it would actually look - checkerboard first, then the
        // resolved gradient (via previewGradientFor()) painted on top, plus (Point kind only) real
        // draggable point handles. Reads owner_'s current state fresh on every paint() call, so
        // there's nothing here to keep in sync separately - GradientEditorDialog::refreshPreview()
        // just requests a repaint, it never pushes state into this class.
        //
        // Mouse handling is active only for Point (every other kind ignores it - StopTrack/its own
        // hex-field-and-track combo is the real edit surface for those). A hit on an existing
        // point handle selects+drags it; a miss inserts a new point there (owner_.addPointAt(),
        // seeded from the nearest existing point's color) and starts dragging it immediately -
        // same "one click-drag both places and positions it" shape StopTrack's own
        // click-away-from-a-handle already has for stops.
        class PreviewBox : public newui::SubView
        {
        public:
            explicit PreviewBox(GradientEditorDialog& owner) : owner_(owner)
            {
                onMouseDown.add(this, &PreviewBox::handleMouseDown);
                onMouseMove.add(this, &PreviewBox::handleMouseMove);
                onMouseUp.add(this, &PreviewBox::handleMouseUp);
            }

            void paint(BLContext& ctx) override
            {
                newui::Rect bounds = getClientBounds();
                if (bounds.width() <= 0.0f || bounds.height() <= 0.0f) {
                    return;
                }

                paintCheckerboard(ctx, bounds);

                newui::gfx::Gradient preview = previewGradientFor(owner_.gradient(), bounds, owner_.shapeBounds());
                BLVar fill = preview.toBLVar(bounds);
                ctx.save();
                ctx.set_fill_style(fill);
                ctx.fill_round_rect(BLRect(bounds), kCornerRadius);
                ctx.restore();

                if (owner_.gradient().kind() == newui::gfx::GradientKind::Point) {
                    paintPointHandles(ctx, bounds);
                }
            }

        private:
            void paintPointHandles(BLContext& ctx, const newui::Rect& bounds)
            {
                const std::vector<newui::gfx::GradientPoint>& points = owner_.gradient().points();
                std::size_t selected = owner_.selectedPointIndex();
                for (std::size_t i = 0; i < points.size(); ++i) {
                    newui::Point center = mapToScreen(owner_.shapeBounds(), bounds, points[i].position());
                    ctx.save();
                    ctx.set_fill_style(points[i].color().toBLRgba32());
                    ctx.fill_circle(double(center.x), double(center.y), double(kPointHandleRadius));
                    // Same double-ring shape StopTrack's own stop handles use - a plain white ring
                    // alone all but disappears against a light/white point color.
                    ctx.set_stroke_style(BLRgba32(0, 0, 0, 140));
                    ctx.set_stroke_width(1.0);
                    ctx.stroke_circle(double(center.x), double(center.y), double(kPointHandleRadius) + 1.5);
                    ctx.set_stroke_style(BLRgba32(255, 255, 255));
                    ctx.set_stroke_width(2.0);
                    ctx.stroke_circle(double(center.x), double(center.y), double(kPointHandleRadius));
                    ctx.restore();

                    if (i == selected) {
                        ctx.save();
                        ctx.set_stroke_style(newui::UIColorManager::colorFor(newui::UIColorRole::HighlightBackground).toBLRgba32());
                        ctx.set_stroke_width(2.0);
                        ctx.stroke_circle(double(center.x), double(center.y), double(kPointHandleRadius) + 4.0);
                        ctx.restore();
                    }
                }
            }

            // Nearest handle within kPointHandleHitRadius of screenPt, or points.size() if none
            // qualify - squared-distance comparison, no <cmath> needed. Same shape StopTrack's own
            // hitTestHandle() uses.
            std::size_t hitTestPoint(const newui::Rect& bounds, const newui::Point& screenPt) const
            {
                const std::vector<newui::gfx::GradientPoint>& points = owner_.gradient().points();
                std::size_t best = points.size();
                float bestDistSq = kPointHandleHitRadius * kPointHandleHitRadius;
                for (std::size_t i = 0; i < points.size(); ++i) {
                    newui::Point center = mapToScreen(owner_.shapeBounds(), bounds, points[i].position());
                    float dx = screenPt.x - center.x;
                    float dy = screenPt.y - center.y;
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
                if (owner_.gradient().kind() != newui::gfx::GradientKind::Point) {
                    return newui::SyncReturn::Ignored;
                }
                newui::Rect bounds = getClientBounds();
                if (bounds.width() <= 0.0f || bounds.height() <= 0.0f) {
                    return newui::SyncReturn::Ignored;
                }

                std::size_t hit = hitTestPoint(bounds, pt);
                if (hit < owner_.gradient().points().size()) {
                    owner_.selectPoint(hit);
                } else {
                    owner_.addPointAt(mapToShapeSpace(owner_.shapeBounds(), bounds, pt));
                }
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
                owner_.setSelectedPointPosition(mapToShapeSpace(owner_.shapeBounds(), bounds, pt));
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
        // starts dragging it immediately, matching the mockup's own track click behavior. Only
        // ever shown for Linear/Radial/Conic (see showPageForKind()) - Point has no 1D stop
        // position at all, its own points are edited directly in previewBox_ instead.
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

        // One clickable preset swatch - paints its own preset's real resolved gradient (always
        // rendered as a plain left-to-right Linear fill, matching what applyPreset() actually
        // commits - see this class's own header comment on why "angle" is dropped). Plain SubView
        // + its own click detection (StopTrack/PreviewBox's own established shape), not
        // newui::Button, since the whole point is a custom gradient-filled background rather than
        // button chrome.
        //
        // A click on the swatch's own body always selects *and* applies it in one gesture
        // (owner_.applyPreset()) - no drag concept here, same as StopTrack's own
        // click-away-from-a-handle "add" gesture. Only the currently *selected* swatch
        // (owner_.selectedPresetIndex() == index_) draws a highlight ring and a small
        // delete-corner mark, and only a click inside that small corner region removes it
        // (owner_.removePreset()) - a real, user-raised concern with an earlier draft that showed
        // a delete mark on every swatch at once ("so we can tell which one is being deleted"):
        // with only the selected one ever showing it, there's never any ambiguity about which
        // preset a delete click would remove.
        class PresetButton : public newui::SubView
        {
        public:
            PresetButton(GradientEditorDialog& owner, std::size_t index, std::vector<newui::gfx::GradientStop> stops)
                : owner_(owner), index_(index), stops_(std::move(stops))
            {
                onMouseDown.add(this, &PresetButton::handleMouseDown);
            }

            void paint(BLContext& ctx) override
            {
                newui::Rect bounds = getClientBounds();
                if (bounds.width() <= 0.0f || bounds.height() <= 0.0f) {
                    return;
                }
                paintCheckerboard(ctx, bounds, 4.0);

                newui::gfx::Gradient preview;
                preview.setKind(newui::gfx::GradientKind::Linear);
                float midY = bounds.top() + bounds.height() * 0.5f;
                preview.setLinearStart(newui::Point(bounds.left(), midY));
                preview.setLinearEnd(newui::Point(bounds.right(), midY));
                for (const newui::gfx::GradientStop& stop : stops_) {
                    preview.stops().push_back(stop);
                }
                ctx.save();
                ctx.set_fill_style(preview.toBLVar(bounds));
                ctx.fill_round_rect(BLRect(bounds), kCornerRadius);
                ctx.restore();

                if (isSelected()) {
                    ctx.save();
                    ctx.set_stroke_style(newui::UIColorManager::colorFor(newui::UIColorRole::HighlightBackground).toBLRgba32());
                    ctx.set_stroke_width(2.0);
                    ctx.stroke_round_rect(BLRect(bounds), kCornerRadius);
                    ctx.restore();

                    paintDeleteCorner(ctx, bounds);
                }
            }

        private:
            bool isSelected() const
            {
                std::optional<std::size_t> selected = owner_.selectedPresetIndex();
                return selected.has_value() && *selected == index_;
            }

            static newui::Rect deleteCornerRect(const newui::Rect& bounds)
            {
                float size = 12.0f;
                return newui::Rect(bounds.right() - size - 2.0f, bounds.top() + 2.0f, size, size);
            }

            static void paintDeleteCorner(BLContext& ctx, const newui::Rect& bounds)
            {
                newui::Rect deleteRect = deleteCornerRect(bounds);
                double cx = double(deleteRect.left() + deleteRect.width() * 0.5f);
                double cy = double(deleteRect.top() + deleteRect.height() * 0.5f);
                double r = double(deleteRect.width()) * 0.5;
                ctx.save();
                ctx.set_fill_style(BLRgba32(20, 20, 20, 200));
                ctx.fill_circle(cx, cy, r);
                ctx.set_stroke_style(BLRgba32(255, 255, 255, 235));
                ctx.set_stroke_width(1.3);
                double mark = r * 0.5;
                ctx.stroke_line(cx - mark, cy - mark, cx + mark, cy + mark);
                ctx.stroke_line(cx - mark, cy + mark, cx + mark, cy - mark);
                ctx.restore();
            }

            newui::SyncReturn handleMouseDown(newui::View& /*sender*/, const newui::Point& pt,
                    std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/)
            {
                if (isSelected()) {
                    newui::Rect deleteRect = deleteCornerRect(getClientBounds());
                    if (pt.x >= deleteRect.left() && pt.x <= deleteRect.right()
                            && pt.y >= deleteRect.top() && pt.y <= deleteRect.bottom()) {
                        owner_.removePreset(index_);
                        return newui::SyncReturn::Handled;
                    }
                }
                owner_.applyPreset(index_);
                return newui::SyncReturn::Handled;
            }

            GradientEditorDialog& owner_;
            std::size_t index_;
            std::vector<newui::gfx::GradientStop> stops_;
        };

        // The trailing "+" swatch at the end of presetsRow_ - always present, appends the current
        // working gradient's own stops as a new preset (owner_.addPresetFromCurrent()). Same plain
        // SubView + own click detection shape as every other custom control in this file.
        class AddPresetButton : public newui::SubView
        {
        public:
            explicit AddPresetButton(GradientEditorDialog& owner) : owner_(owner)
            {
                onMouseDown.add(this, &AddPresetButton::handleMouseDown);
            }

            void paint(BLContext& ctx) override
            {
                newui::Rect bounds = getClientBounds();
                if (bounds.width() <= 0.0f || bounds.height() <= 0.0f) {
                    return;
                }
                ctx.save();
                ctx.set_fill_style(newui::UIColorManager::colorFor(newui::UIColorRole::ControlBackground).toBLRgba32());
                ctx.fill_round_rect(BLRect(bounds), kCornerRadius);
                ctx.set_stroke_style(newui::UIColorManager::colorFor(newui::UIColorRole::ControlBorder).toBLRgba32());
                ctx.set_stroke_width(1.0);
                ctx.stroke_round_rect(BLRect(bounds), kCornerRadius);

                double cx = double(bounds.left() + bounds.width() * 0.5f);
                double cy = double(bounds.top() + bounds.height() * 0.5f);
                double mark = double(bounds.width()) * 0.28;
                ctx.set_stroke_style(newui::UIColorManager::colorFor(newui::UIColorRole::ControlText).toBLRgba32());
                ctx.set_stroke_width(1.6);
                ctx.stroke_line(cx - mark, cy, cx + mark, cy);
                ctx.stroke_line(cx, cy - mark, cx, cy + mark);
                ctx.restore();
            }

        private:
            newui::SyncReturn handleMouseDown(newui::View& /*sender*/, const newui::Point& /*pt*/,
                    std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/)
            {
                owner_.addPresetFromCurrent();
                return newui::SyncReturn::Handled;
            }

            GradientEditorDialog& owner_;
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
        // seeds a real, empty-stops newui::gfx::Gradient() - correct, but leaves nothing at all to
        // edit. Seeding two reasonable default stops here keeps this dialog actually usable on
        // that real first-click case, same spirit as every other "0 isn't useful, give it a real
        // starting point" default elsewhere in this codebase (e.g. Gradient's own linearEnd_/
        // radialRadius_ class defaults).
        if (working_.kind() != newui::gfx::GradientKind::Point && working_.stops().empty()) {
            // newui::Color's own (r,g,b,a) constructor stores raw floats, unclamped/un-normalized -
            // Color(255, 255, 255) is *not* white, it's a wildly out-of-[0,1]-range value that
            // happens to still render as opaque white only because blend2d's own per-channel
            // stop-color packing (toBLGradient()) clamps independently. Real, [0,1]-scale values
            // here, matching Color::fromHSV()'s own convention elsewhere in this file.
            working_.stops().push_back(newui::gfx::GradientStop(0.0f, newui::Color(0.0f, 0.0f, 0.0f)));
            working_.stops().push_back(newui::gfx::GradientStop(1.0f, newui::Color(1.0f, 1.0f, 1.0f)));
        }
        normalizePointStateForEditing();
        selectedStopIndex_ = 0;
        selectedPointIndex_ = 0;
        kindControl_->setSelectedIndex(static_cast<std::size_t>(working_.kind()));
        showPageForKind(working_.kind());
    }

    void GradientEditorDialog::setKind(newui::gfx::GradientKind kind)
    {
        working_.setKind(kind);
        normalizePointStateForEditing();
        kindControl_->setSelectedIndex(static_cast<std::size_t>(kind));
        showPageForKind(kind);
    }

    void GradientEditorDialog::normalizePointStateForEditing()
    {
        if (working_.kind() != newui::gfx::GradientKind::Point) {
            return;
        }

        // Real, live-debugged bug (found via a real breakpoint in PreviewBox::paint(), not
        // guessed): a Point-kind Gradient arriving here can have pointBlendPower_/pointRasterMax_
        // as 0 - not their real C++ class defaults (2.0f/64) - however this specific object was
        // constructed before reaching this dialog (confirmed live: a real View's own
        // style().fill().gradient() already had both at 0, with 2 real points already present -
        // likely a reflection object-creation path for a nested Class-typed property that doesn't
        // run the real constructor's own in-class member initializers; a separate, deeper newui-
        // side gap, not fixed here). A pointRasterMax_ of 0 makes rasterizePoints()'s own scale
        // computation (pointRasterMax_/longSide) resolve to exactly 0, collapsing the baked raster
        // to a degenerate near-1x1 image with an undefined (divide-by-zero) pattern transform -
        // exactly why the live preview showed nothing but checkerboard, no visible blend at all,
        // regardless of how correct the point colors/positions themselves were. Normalizes to real,
        // sane defaults whenever they're degenerate, independent of whether points() also needs
        // seeding below - this real object already had 2 points, so the "seed if empty" guard alone
        // never touched these two fields at all.
        if (working_.pointBlendPower() <= 0.0f) {
            working_.setPointBlendPower(2.0f);
        }
        if (working_.pointRasterMax() <= 0) {
            working_.setPointRasterMax(64);
        }

        if (!working_.points().empty()) {
            return;
        }
        float px1 = shapeBounds_.left() + shapeBounds_.width() * 0.35f;
        float py1 = shapeBounds_.top() + shapeBounds_.height() * 0.35f;
        float px2 = shapeBounds_.left() + shapeBounds_.width() * 0.65f;
        float py2 = shapeBounds_.top() + shapeBounds_.height() * 0.65f;
        // Real, [0,1]-scale Color values (see setGradient()'s own comment on this exact mistake) -
        // this one actually matters visibly: rasterizePoints()'s weighted-average blend operates
        // on these values directly, with no per-channel clamping until the very end, so an
        // out-of-range 255.0f here (as opposed to 1.0f) can dominate the blend even from a tiny,
        // far-away weight - a second, separate real bug found the same live-debugging session.
        working_.points().push_back(newui::gfx::GradientPoint(newui::Point(px1, py1), newui::Color(0.0f, 0.0f, 0.0f)));
        working_.points().push_back(newui::gfx::GradientPoint(newui::Point(px2, py2), newui::Color(1.0f, 1.0f, 1.0f)));
    }

    void GradientEditorDialog::selectStop(std::size_t index)
    {
        std::size_t count = working_.stops().size();
        selectedStopIndex_ = count == 0 ? 0 : (index >= count ? count - 1 : index);
        refreshSelectedItemEditor();
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
            refreshSelectedItemEditor();
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

    void GradientEditorDialog::selectPoint(std::size_t index)
    {
        std::size_t count = working_.points().size();
        selectedPointIndex_ = count == 0 ? 0 : (index >= count ? count - 1 : index);
        refreshSelectedItemEditor();
    }

    void GradientEditorDialog::setSelectedPointColor(const newui::Color& color)
    {
        std::vector<newui::gfx::GradientPoint>& points = working_.points();
        if (selectedPointIndex_ < points.size()) {
            points[selectedPointIndex_].setColor(color);
            refreshSelectedItemEditor();
            refreshPreview();
        }
    }

    void GradientEditorDialog::setSelectedPointPosition(const newui::Point& position)
    {
        std::vector<newui::gfx::GradientPoint>& points = working_.points();
        if (selectedPointIndex_ < points.size()) {
            points[selectedPointIndex_].setPosition(position);
            refreshPreview();
        }
    }

    void GradientEditorDialog::addPointAt(const newui::Point& position)
    {
        std::vector<newui::gfx::GradientPoint>& points = working_.points();

        newui::Color color(0.0f, 0.0f, 0.0f);
        if (!points.empty()) {
            std::size_t nearest = 0;
            float bestDistSq = -1.0f;
            for (std::size_t i = 0; i < points.size(); ++i) {
                float dx = position.x - points[i].position().x;
                float dy = position.y - points[i].position().y;
                float distSq = dx * dx + dy * dy;
                if (bestDistSq < 0.0f || distSq < bestDistSq) {
                    bestDistSq = distSq;
                    nearest = i;
                }
            }
            color = points[nearest].color();
        }

        points.push_back(newui::gfx::GradientPoint(position, color));
        selectPoint(points.size() - 1);
        refreshPreview();
    }

    void GradientEditorDialog::deleteSelectedPoint()
    {
        std::vector<newui::gfx::GradientPoint>& points = working_.points();
        if (points.size() <= 1 || selectedPointIndex_ >= points.size()) {
            return;
        }
        points.erase(points.begin() + static_cast<std::ptrdiff_t>(selectedPointIndex_));
        selectPoint(0);
        refreshPreview();
    }

    std::size_t GradientEditorDialog::presetCount()
    {
        return presetRegistry().size();
    }

    void GradientEditorDialog::applyPreset(std::size_t index)
    {
        const std::vector<std::vector<newui::gfx::GradientStop>>& presets = presetRegistry();
        if (index >= presets.size()) {
            return;
        }
        // Always Linear - see this class's own header comment for why a preset's own "angle" is
        // dropped rather than applied to Linear's own (still uneditable) start/end geometry.
        working_.setKind(newui::gfx::GradientKind::Linear);
        working_.stops() = presets[index];
        kindControl_->setSelectedIndex(static_cast<std::size_t>(newui::gfx::GradientKind::Linear));
        selectStop(0);
        selectedPresetIndex_ = index;
        showPageForKind(newui::gfx::GradientKind::Linear);
        if (presetsRow_ != nullptr) {
            presetsRow_->redraw();
        }
    }

    void GradientEditorDialog::addPresetFromCurrent()
    {
        std::vector<std::vector<newui::gfx::GradientStop>>& presets = presetRegistry();
        presets.push_back(working_.stops());
        selectedPresetIndex_ = presets.size() - 1;
        rebuildPresetsRow();
    }

    void GradientEditorDialog::removePreset(std::size_t index)
    {
        std::vector<std::vector<newui::gfx::GradientStop>>& presets = presetRegistry();
        if (index >= presets.size()) {
            return;
        }
        presets.erase(presets.begin() + static_cast<std::ptrdiff_t>(index));
        selectedPresetIndex_.reset();
        rebuildPresetsRow();
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

        // previewBox_ - shared across every kind (renders Linear/Radial/Conic's resolved gradient,
        // or Point's real point-blend preview + draggable handles - see PreviewBox's own comment).
        // track_ - Linear/Radial/Conic's own 1D stop-position track, hidden for Point
        // (showPageForKind()) since a point has no single scalar position at all.
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

        // selectedItemEditor_ - one shared ColorPicker + hex TextField + delete button, retargeted
        // to whichever stop *or point* is selected (see refreshSelectedItemEditor()'s own
        // comment) - shown for every kind, unlike track_.
        newui::ViewBuilder<newui::SubView> editorBuilder;
        editorBuilder.name("gradientSelectedItemEditor")
            .visible(true)
            .desiredSize(newui::Size(0.0f, kSelectedItemEditorHeight))
            .layout<newui::FlexLayout>([](newui::FlexLayout& layout) {
                layout.setOrientation(newui::Orientation::Vertical);
                layout.setSpacing(kEditorRowSpacing);
            });
        selectedItemEditor_ = editorBuilder.build();
        contentRoot_->addChild(selectedItemEditor_);

        newui::ViewBuilder<ColorPicker> pickerBuilder;
        pickerBuilder.name("gradientColorPicker")
            .visible(true)
            .desiredSize(newui::Size(0.0f, kColorPickerHeight));
        colorPicker_ = pickerBuilder.build();
        colorPicker_->onColorChanged.add([this](ColorPicker& sender) {
            if (working_.kind() == newui::gfx::GradientKind::Point) {
                setSelectedPointColor(sender.color());
            } else {
                setSelectedStopColor(sender.color());
            }
            return newui::SyncReturn::Handled;
        });
        selectedItemEditor_->addChild(colorPicker_);

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

        newui::ViewBuilder<newui::Button> deleteItemBuilder;
        deleteItemBuilder.name("gradientDeleteItemButton")
            .visible(true)
            .desiredSize(newui::Size(60.0f, kRowHeight))
            .configure([](newui::Button& button) { button.setText("Delete"); });
        deleteItemButton_ = deleteItemBuilder.build();
        deleteItemButton_->onClick.add([this](newui::Control&) {
            deleteSelectedItem();
            return newui::SyncReturn::Handled;
        });
        hexRow->addChild(deleteItemButton_);

        selectedItemEditor_->addChild(hexRow);

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

        // Point editing is real now (Phase 4) - previewBox_ itself is the real edit surface (click
        // to add, drag to reposition), so this page just holds a usage hint rather than the old
        // "not built yet" placeholder.
        newui::ViewBuilder<newui::Label> pointHintBuilder;
        pointHintBuilder.name("gradientPointHint")
            .visible(true)
            .desiredSize(newui::Size(0.0f, kRowHeight))
            .configure([](newui::Label& label) { label.setText("Click the preview above to add a point - drag to reposition"); });
        pointPage_ = pointHintBuilder.build();
        pagesContainer_->addChild(pointPage_);

        // Presets (Phase 5) - a "Presets" label + a row of real, independently-clickable
        // PresetButton swatches, last content section before the footer, matching the mockup's own
        // layout order exactly. Hidden for Point alongside track_ (showPageForKind()).
        newui::ViewBuilder<newui::Label> presetsLabelBuilder;
        presetsLabelBuilder.name("gradientPresetsLabel")
            .visible(true)
            .desiredSize(newui::Size(0.0f, kRowHeight))
            .configure([](newui::Label& label) { label.setText("Presets"); });
        presetsLabel_ = presetsLabelBuilder.build();
        contentRoot_->addChild(presetsLabel_);

        newui::ViewBuilder<newui::SubView> presetsRowBuilder;
        presetsRowBuilder.name("gradientPresetsRow")
            .visible(true)
            .desiredSize(newui::Size(0.0f, kPresetSwatchSize))
            .layout<newui::FlexLayout>([](newui::FlexLayout& layout) {
                layout.setOrientation(newui::Orientation::Horizontal);
                layout.setSpacing(kPresetsRowSpacing);
            });
        presetsRow_ = presetsRowBuilder.build();
        contentRoot_->addChild(presetsRow_);

        rebuildPresetsRow();

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

        // track_/presets only apply to Linear/Radial/Conic's own stop-based editing - Point uses
        // direct 2D drag-in-preview instead (previewBox_ itself) and a preset is always a Linear
        // stop list, meaningless for Point's own scattered anchors - so both toggle by kind the
        // same way; previewBox_/selectedItemEditor_ stay visible for every kind now.
        bool showsStopBasedEditing = (kind != newui::gfx::GradientKind::Point);
        track_->setVisible(showsStopBasedEditing);
        presetsLabel_->setVisible(showsStopBasedEditing);
        presetsRow_->setVisible(showsStopBasedEditing);
        // setVisible() alone doesn't reflow anything (see its own definition, subview.cpp) -
        // contentRoot_'s FlexLayout needs to actually re-run so it gives pagesContainer_/the
        // footer the space these rows just gave up (or reclaims when they reappear).
        contentRoot_->updateLayout();

        refreshSelectedItemEditor();
        refreshPreview();
        // A layout reflow moves more than just track_ (pagesContainer_/the footer shift too) -
        // refreshPreview()'s own targeted redraw()s don't cover that, so this one still needs the
        // whole-window repaint.
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

    void GradientEditorDialog::refreshSelectedItemEditor()
    {
        if (working_.kind() == newui::gfx::GradientKind::Point) {
            const std::vector<newui::gfx::GradientPoint>& points = working_.points();
            // A single point still renders something real (Gradient::rasterizePoints() blends
            // however many there are) - unlike stops, which need at least 2 to mean anything, so
            // 1 is the real floor here, matching the mockup's own `state.points.length <= 1`.
            deleteItemButton_->setEnabled(points.size() > 1);
            if (selectedPointIndex_ >= points.size()) {
                return;
            }
            const newui::Color& color = points[selectedPointIndex_].color();
            colorPicker_->setColor(color);
            hexField_->setText(utf8ToWide(color.toString()));
            return;
        }

        const std::vector<newui::gfx::GradientStop>& stops = working_.stops();
        // Below the minimum (2), matching the mockup's own guard - can't delete any further.
        deleteItemButton_->setEnabled(stops.size() > 2);
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
        if (!newui::Color::fromString(wideToUtf8(hexField_->text()), color)) {
            return;
        }
        if (working_.kind() == newui::gfx::GradientKind::Point) {
            setSelectedPointColor(color);
        } else {
            setSelectedStopColor(color);
        }
    }

    void GradientEditorDialog::deleteSelectedItem()
    {
        if (working_.kind() == newui::gfx::GradientKind::Point) {
            deleteSelectedPoint();
        } else {
            deleteSelectedStop();
        }
    }

    void GradientEditorDialog::rebuildPresetsRow()
    {
        // removeChild() only detaches (never deletes) - same "copy the list first, delete each"
        // shape Workspace's own "New" button already uses.
        std::vector<newui::SubView*> old = presetsRow_->childViews();
        for (newui::SubView* child : old) {
            presetsRow_->removeChild(child);
            delete child;
        }

        const std::vector<std::vector<newui::gfx::GradientStop>>& presets = presetRegistry();
        for (std::size_t i = 0; i < presets.size(); ++i) {
            newui::ViewBuilder<PresetButton> presetBuilder(new PresetButton(*this, i, presets[i]));
            presetBuilder.name("gradientPresetButton")
                .visible(true)
                .desiredSize(newui::Size(kPresetSwatchSize, kPresetSwatchSize));
            presetsRow_->addChild(presetBuilder.build());
        }

        newui::ViewBuilder<AddPresetButton> addBuilder(new AddPresetButton(*this));
        addBuilder.name("gradientAddPresetButton")
            .visible(true)
            .desiredSize(newui::Size(kPresetSwatchSize, kPresetSwatchSize));
        presetsRow_->addChild(addBuilder.build());

        contentRoot_->updateLayout();
        presetsRow_->redraw();
    }
}
