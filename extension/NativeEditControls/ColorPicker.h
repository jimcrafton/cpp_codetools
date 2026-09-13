#pragma once

#include <newui/color.h>
#include <newui/delegate.h>
#include <newui/subview.h>

namespace CodeToolsVsix
{
    // Real SV-square + hue/alpha rail color picker (design source:
    // design/reference/gradient_editor_dialog.html's own .picker-row/.sv-square/.hue-rail/
    // .alpha-rail). GradientEditorDialog is this class's first real consumer (replacing the plain
    // hex-only TextField for the selected stop) - kept in cpp_codetools for now per the user's own
    // call, not newui, since more cpp_codetools-side reuse is expected before it's clear this needs
    // to be a general newui framework control. Uses newui::Color::toHSV()/fromHSV() (already real,
    // already tested) for all conversion math - no hand-rolled HSV<->RGB here, unlike the mockup's
    // own JS.
    //
    // Composed of 3 real, independent child SubViews (SVSquare/HueRail/AlphaRail,
    // ColorPicker.cpp's own anonymous namespace) - each owns its own drag state and does its own
    // painting/hit-testing, same self-contained shape newui::Splitter/GradientEditorDialog's own
    // StopTrack already use. ColorPicker itself holds the one real hue_/sat_/val_/alpha_ source of
    // truth (not each child), specifically because hue must survive a fully desaturated/black
    // color - RGB->HSV loses hue information entirely at s==0 or v==0, and a color picker that
    // forgot which hue you were just on every time you dragged value down to 0 would be unusable.
    class ColorPicker : public newui::SubView
    {
    public:
        ColorPicker();

        typedef newui::Delegate<ColorPicker> ColorChangedDelegate;
        // Fired whenever color() actually changes - both from a drag (SVSquare/HueRail/AlphaRail)
        // and from a direct setColor() call, same contract newui::Slider::onValueChanged already
        // documents for the same reason (so a caller doesn't need two separate code paths for
        // "the user dragged it" vs. "something else changed it").
        ColorChangedDelegate onColorChanged;

        newui::Color color() const;
        void setColor(const newui::Color& color);

        float hue() const { return hue_; }
        float saturation() const { return sat_; }
        float value() const { return val_; }
        float alpha() const { return alpha_; }

        // The real per-axis mutators SVSquare/HueRail/AlphaRail call on every drag - exposed so
        // tests can drive the exact same path a drag would, per this project's "call the real
        // method, don't simulate raw input" convention. Each clamps/wraps and no-ops (like
        // newui::Slider::setValue()) if the result doesn't actually change anything.
        void setHue(float degrees);
        void setSaturationValue(float saturation, float value);
        void setAlpha(float alpha);

        // Exposed for testability - same convention GradientEditorDialog::stopTrack()/
        // kindControl() already use for their own real child controls.
        newui::SubView* svSquare() const { return svSquare_; }
        newui::SubView* hueRail() const { return hueRail_; }
        newui::SubView* alphaRail() const { return alphaRail_; }

    private:
        // Redraws all 3 children - called after any setter actually changes state, since every
        // one of them reads hue_/sat_/val_/alpha_, not just its own axis (e.g. SVSquare's own base
        // fill depends on hue_; AlphaRail's gradient tint depends on hue_/sat_/val_ too).
        void refreshAll();

        float hue_ = 0.0f;   // degrees [0, 360)
        float sat_ = 0.0f;   // [0, 1] - 0 saturation + full value is white, this class's own default
        float val_ = 1.0f;   // [0, 1]
        float alpha_ = 1.0f; // [0, 1]

        newui::SubView* svSquare_ = nullptr;
        newui::SubView* hueRail_ = nullptr;
        newui::SubView* alphaRail_ = nullptr;
    };
}
