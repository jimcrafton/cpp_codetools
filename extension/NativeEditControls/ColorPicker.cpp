#include "ColorPicker.h"
#include "PaintUtils.h"

#include <newui/layout.h>
#include <newui/viewbuilder.h>

#include <cmath>
#include <memory>

namespace CodeToolsVsix
{
    namespace
    {
        constexpr float kHueRailWidth = 18.0f;
        constexpr float kAlphaRailWidth = 18.0f;
        constexpr float kRailThumbHeight = 7.0f;
        constexpr float kSVThumbRadius = 6.0f;
        constexpr int kHueGradientStopCount = 7;

        // Same double-ring shape GradientEditorDialog::StopTrack's own stop handles use (a
        // semi-transparent dark ring just outside a white one) - a plain white marker alone all
        // but disappears against a light/white color, the exact bug the user caught live on that
        // control; applying the same fix here up front rather than waiting to be asked again.
        void paintHandleRing(BLContext& ctx, double cx, double cy, double radius)
        {
            ctx.set_stroke_style(BLRgba32(0, 0, 0, 140));
            ctx.set_stroke_width(1.0);
            ctx.stroke_circle(cx, cy, radius + 1.5);
            ctx.set_stroke_style(BLRgba32(255, 255, 255));
            ctx.set_stroke_width(2.0);
            ctx.stroke_circle(cx, cy, radius);
        }

        // Same idea as paintHandleRing(), for a rail's thin rounded-bar thumb instead of a circle.
        void paintRailThumbRing(BLContext& ctx, const BLRect& thumbRect)
        {
            BLRect outer(thumbRect.x - 1.0, thumbRect.y - 1.0, thumbRect.w + 2.0, thumbRect.h + 2.0);
            ctx.set_stroke_style(BLRgba32(0, 0, 0, 140));
            ctx.set_stroke_width(1.0);
            ctx.stroke_round_rect(outer, outer.h * 0.5);
        }

        // The 2D saturation(x)/value(y) drag surface - base hue fill, a white->transparent overlay
        // (saturation, left->right) and a transparent->black overlay (value, top->bottom - top is
        // full brightness), matching the mockup's own .sv-square gradient stack exactly. Thumb at
        // (sat, 1-val) - y is inverted since value increases upward on screen but owner_.value()
        // itself is a plain [0,1] "how bright" quantity, not a screen coordinate.
        class SVSquare : public newui::SubView
        {
        public:
            explicit SVSquare(ColorPicker& owner) : owner_(owner)
            {
                onMouseDown.add(this, &SVSquare::handleMouseDown);
                onMouseMove.add(this, &SVSquare::handleMouseMove);
                onMouseUp.add(this, &SVSquare::handleMouseUp);
            }

            void paint(BLContext& ctx) override
            {
                newui::Rect bounds = getClientBounds();
                if (bounds.width() <= 0.0f || bounds.height() <= 0.0f) {
                    return;
                }
                BLRect rect(bounds);

                ctx.save();
                ctx.set_fill_style(newui::Color::fromHSV(owner_.hue(), 1.0f, 1.0f).toBLRgba32());
                ctx.fill_round_rect(rect, kCornerRadius);

                BLGradient whiteToClear(BLLinearGradientValues(rect.x, rect.y, rect.x + rect.w, rect.y));
                whiteToClear.add_stop(0.0, BLRgba32(255, 255, 255, 255));
                whiteToClear.add_stop(1.0, BLRgba32(255, 255, 255, 0));
                ctx.set_fill_style(whiteToClear);
                ctx.fill_round_rect(rect, kCornerRadius);

                BLGradient clearToBlack(BLLinearGradientValues(rect.x, rect.y, rect.x, rect.y + rect.h));
                clearToBlack.add_stop(0.0, BLRgba32(0, 0, 0, 0));
                clearToBlack.add_stop(1.0, BLRgba32(0, 0, 0, 255));
                ctx.set_fill_style(clearToBlack);
                ctx.fill_round_rect(rect, kCornerRadius);
                ctx.restore();

                double cx = double(bounds.left()) + double(owner_.saturation()) * double(bounds.width());
                double cy = double(bounds.top()) + double(1.0f - owner_.value()) * double(bounds.height());
                ctx.save();
                paintHandleRing(ctx, cx, cy, double(kSVThumbRadius));
                ctx.restore();
            }

        private:
            void updateFromPoint(const newui::Point& pt)
            {
                newui::Rect bounds = getClientBounds();
                float s = bounds.width() <= 0.0f ? 0.0f : (pt.x - bounds.left()) / bounds.width();
                float v = bounds.height() <= 0.0f ? 0.0f : 1.0f - (pt.y - bounds.top()) / bounds.height();
                owner_.setSaturationValue(s, v);
            }

            newui::SyncReturn handleMouseDown(newui::View& /*sender*/, const newui::Point& pt,
                    std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/)
            {
                dragging_ = true;
                updateFromPoint(pt);
                return newui::SyncReturn::Handled;
            }

            newui::SyncReturn handleMouseMove(newui::View& /*sender*/, const newui::Point& pt,
                    std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/)
            {
                if (!dragging_) {
                    return newui::SyncReturn::Ignored;
                }
                updateFromPoint(pt);
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

            ColorPicker& owner_;
            bool dragging_ = false;
        };

        // Vertical hue spectrum rail (0-360 degrees, red->yellow->green->cyan->blue->magenta->red,
        // matching the mockup's own .hue-rail) - thumb is a thin horizontal rounded bar (the
        // mockup's own .rail-thumb), not a circle, since a hue rail only ever has one axis.
        class HueRail : public newui::SubView
        {
        public:
            explicit HueRail(ColorPicker& owner) : owner_(owner)
            {
                onMouseDown.add(this, &HueRail::handleMouseDown);
                onMouseMove.add(this, &HueRail::handleMouseMove);
                onMouseUp.add(this, &HueRail::handleMouseUp);
            }

            void paint(BLContext& ctx) override
            {
                newui::Rect bounds = getClientBounds();
                if (bounds.width() <= 0.0f || bounds.height() <= 0.0f) {
                    return;
                }
                BLRect rect(bounds);

                BLGradient hueSpectrum(BLLinearGradientValues(rect.x, rect.y, rect.x, rect.y + rect.h));
                for (int i = 0; i < kHueGradientStopCount; ++i) {
                    float degrees = 360.0f * float(i) / float(kHueGradientStopCount - 1);
                    double t = double(i) / double(kHueGradientStopCount - 1);
                    hueSpectrum.add_stop(t, newui::Color::fromHSV(degrees, 1.0f, 1.0f).toBLRgba32());
                }
                ctx.save();
                ctx.set_fill_style(hueSpectrum);
                ctx.fill_round_rect(rect, kCornerRadius);
                ctx.restore();

                paintThumb(ctx, bounds, owner_.hue() / 360.0f);
            }

        private:
            void paintThumb(BLContext& ctx, const newui::Rect& bounds, float t)
            {
                double thumbY = double(bounds.top()) + double(t) * double(bounds.height());
                BLRect thumbRect(double(bounds.left()) - 1.0, thumbY - double(kRailThumbHeight) * 0.5,
                    double(bounds.width()) + 2.0, double(kRailThumbHeight));
                ctx.save();
                ctx.set_fill_style(BLRgba32(255, 255, 255));
                ctx.fill_round_rect(thumbRect, thumbRect.h * 0.5);
                paintRailThumbRing(ctx, thumbRect);
                ctx.restore();
            }

            void updateFromPoint(const newui::Point& pt)
            {
                newui::Rect bounds = getClientBounds();
                float t = bounds.height() <= 0.0f ? 0.0f : (pt.y - bounds.top()) / bounds.height();
                owner_.setHue(t * 360.0f);
            }

            newui::SyncReturn handleMouseDown(newui::View& /*sender*/, const newui::Point& pt,
                    std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/)
            {
                dragging_ = true;
                updateFromPoint(pt);
                return newui::SyncReturn::Handled;
            }

            newui::SyncReturn handleMouseMove(newui::View& /*sender*/, const newui::Point& pt,
                    std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/)
            {
                if (!dragging_) {
                    return newui::SyncReturn::Ignored;
                }
                updateFromPoint(pt);
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

            ColorPicker& owner_;
            bool dragging_ = false;
        };

        // Vertical alpha rail (transparent->opaque of the *current* hue/saturation/value, matching
        // the mockup's own .alpha-rail) - checkerboard first (PaintUtils.h, same reason
        // GradientEditorDialog's own preview/track need it: alpha is real and typeable here, not
        // hypothetical), same rounded-bar thumb shape as HueRail.
        class AlphaRail : public newui::SubView
        {
        public:
            explicit AlphaRail(ColorPicker& owner) : owner_(owner)
            {
                onMouseDown.add(this, &AlphaRail::handleMouseDown);
                onMouseMove.add(this, &AlphaRail::handleMouseMove);
                onMouseUp.add(this, &AlphaRail::handleMouseUp);
            }

            void paint(BLContext& ctx) override
            {
                newui::Rect bounds = getClientBounds();
                if (bounds.width() <= 0.0f || bounds.height() <= 0.0f) {
                    return;
                }
                BLRect rect(bounds);
                paintCheckerboard(ctx, bounds, 4.0);

                newui::Color rgb = newui::Color::fromHSV(owner_.hue(), owner_.saturation(), owner_.value());
                BLGradient alphaSpectrum(BLLinearGradientValues(rect.x, rect.y, rect.x, rect.y + rect.h));
                alphaSpectrum.add_stop(0.0, newui::Color(rgb.r, rgb.g, rgb.b, 0.0f).toBLRgba32());
                alphaSpectrum.add_stop(1.0, newui::Color(rgb.r, rgb.g, rgb.b, 1.0f).toBLRgba32());
                ctx.save();
                ctx.set_fill_style(alphaSpectrum);
                ctx.fill_round_rect(rect, kCornerRadius);
                ctx.restore();

                double thumbY = double(bounds.top()) + double(owner_.alpha()) * double(bounds.height());
                BLRect thumbRect(double(bounds.left()) - 1.0, thumbY - double(kRailThumbHeight) * 0.5,
                    double(bounds.width()) + 2.0, double(kRailThumbHeight));
                ctx.save();
                ctx.set_fill_style(BLRgba32(255, 255, 255));
                ctx.fill_round_rect(thumbRect, thumbRect.h * 0.5);
                paintRailThumbRing(ctx, thumbRect);
                ctx.restore();
            }

        private:
            void updateFromPoint(const newui::Point& pt)
            {
                newui::Rect bounds = getClientBounds();
                float t = bounds.height() <= 0.0f ? 0.0f : (pt.y - bounds.top()) / bounds.height();
                owner_.setAlpha(t);
            }

            newui::SyncReturn handleMouseDown(newui::View& /*sender*/, const newui::Point& pt,
                    std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/)
            {
                dragging_ = true;
                updateFromPoint(pt);
                return newui::SyncReturn::Handled;
            }

            newui::SyncReturn handleMouseMove(newui::View& /*sender*/, const newui::Point& pt,
                    std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/)
            {
                if (!dragging_) {
                    return newui::SyncReturn::Ignored;
                }
                updateFromPoint(pt);
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

            ColorPicker& owner_;
            bool dragging_ = false;
        };
    }

    ColorPicker::ColorPicker()
    {
        setVisible(true);

        auto flexLayout = std::make_unique<newui::FlexLayout>();
        flexLayout->setOrientation(newui::Orientation::Horizontal);
        flexLayout->setSpacing(8.0f);
        setLayout(std::move(flexLayout));

        newui::ViewBuilder<SVSquare> svBuilder(new SVSquare(*this));
        svBuilder.name("colorPickerSVSquare")
            .visible(true)
            .layoutParams<newui::FlexLayoutParams>([](newui::FlexLayoutParams& params) { params.weight = 1.0f; });
        svSquare_ = svBuilder.build();
        addChild(svSquare_);

        newui::ViewBuilder<HueRail> hueBuilder(new HueRail(*this));
        hueBuilder.name("colorPickerHueRail")
            .visible(true)
            .desiredSize(newui::Size(kHueRailWidth, 0.0f));
        hueRail_ = hueBuilder.build();
        addChild(hueRail_);

        newui::ViewBuilder<AlphaRail> alphaBuilder(new AlphaRail(*this));
        alphaBuilder.name("colorPickerAlphaRail")
            .visible(true)
            .desiredSize(newui::Size(kAlphaRailWidth, 0.0f));
        alphaRail_ = alphaBuilder.build();
        addChild(alphaRail_);
    }

    newui::Color ColorPicker::color() const
    {
        return newui::Color::fromHSV(hue_, sat_, val_, alpha_);
    }

    void ColorPicker::setColor(const newui::Color& color)
    {
        newui::HSVColor hsv = color.toHSV();
        if (hsv.h == hue_ && hsv.s == sat_ && hsv.v == val_ && color.a == alpha_) {
            return;
        }
        hue_ = hsv.h;
        sat_ = hsv.s;
        val_ = hsv.v;
        alpha_ = color.a;
        refreshAll();
        onColorChanged(*this);
    }

    void ColorPicker::setHue(float degrees)
    {
        float wrapped = std::fmod(degrees, 360.0f);
        if (wrapped < 0.0f) {
            wrapped += 360.0f;
        }
        if (wrapped == hue_) {
            return;
        }
        hue_ = wrapped;
        refreshAll();
        onColorChanged(*this);
    }

    void ColorPicker::setSaturationValue(float saturation, float value)
    {
        float s = saturation < 0.0f ? 0.0f : (saturation > 1.0f ? 1.0f : saturation);
        float v = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
        if (s == sat_ && v == val_) {
            return;
        }
        sat_ = s;
        val_ = v;
        refreshAll();
        onColorChanged(*this);
    }

    void ColorPicker::setAlpha(float alpha)
    {
        float a = alpha < 0.0f ? 0.0f : (alpha > 1.0f ? 1.0f : alpha);
        if (a == alpha_) {
            return;
        }
        alpha_ = a;
        refreshAll();
        onColorChanged(*this);
    }

    void ColorPicker::refreshAll()
    {
        if (svSquare_ != nullptr) {
            svSquare_->redraw();
        }
        if (hueRail_ != nullptr) {
            hueRail_->redraw();
        }
        if (alphaRail_ != nullptr) {
            alphaRail_->redraw();
        }
    }
}
