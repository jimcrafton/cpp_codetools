#pragma once

#include <newui/color.h>
#include <newui/font.h>
#include <newui/geometry.h>

#include <blend2d/blend2d.h>

#include <string>

namespace CodeToolsVsix
{
    // Alternating-square checkerboard behind anything painted with per-pixel alpha - same
    // algorithm newui::ImageFillStyle::paint() already uses (viewstyle.cpp) for the identical
    // reason. Factored out here (rather than living inside GradientEditorDialog.cpp, its first
    // consumer) since ColorPicker's own alpha rail needs the exact same thing and neither one is
    // an ImageFillStyle instance (no checkerSize()/checkerColorA()/colorB() to call through).
    // Uses the mockup's own neutral dialog-chrome checker colors (gradient_editor_dialog.html's
    // --checker-a/b), not ImageFillStyle's configurable ones. Pass the same radius the caller is
    // about to fill_round_rect() with right after this (e.g. kCornerRadius below) so the checker
    // itself stays inside the rounded shape instead of squaring off past its corners - radius 0.0
    // (the default) keeps the old plain-rect tiling, fine for thin unrounded strips like
    // StopTrack's own lineRect.
    void paintCheckerboard(BLContext& ctx, const newui::Rect& rect, double tile = 6.0, double radius = 0.0);

    // One shared corner radius for every custom-painted gradient-dialog surface (previewBox_,
    // ColorPicker's SV-square/hue/alpha rails, preset swatches) - a single, consistent visual
    // language across GradientEditorDialog.cpp and ColorPicker.cpp rather than each picking its
    // own value independently.
    constexpr double kCornerRadius = 6.0;

    // Shared inactive-row/value glyph sizes - a PropertyEditor's own paintValue()/
    // paintSubPropertyValue() override (PropertyEditor.h) and PropertiesGrid's live-editor-widget
    // placement (a real Toggle sized to match, so the switch from inactive glyph to live widget
    // is seamless) both need the exact same numbers, not two independently-drifting copies.
    constexpr float kSwatchSize = 14.0f;
    constexpr float kCheckboxSize = 14.0f;

    // Measures text's rendered width under font - shared by truncateWithEllipsis() below and any
    // caller that needs to lay out space around a piece of text before painting it.
    double measureTextWidth(BLFont& font, const std::string& text);

    // Truncates text to fit within maxWidth, appending "..." - plain byte-offset truncation
    // (every string this codebase paints through here is a C++ identifier/English word, never
    // multi-byte UTF-8), a binary-search-the-longest-fit shape. Returns text unchanged if it
    // already fits, or an empty string if even "..." alone doesn't fit.
    std::string truncateWithEllipsis(BLFont& font, const std::string& text, double maxWidth);

    // Same BLFont/glyph-buffer/fill_utf8_text idiom items.cpp's own file-local paintItemText()
    // uses (not exported from there) - the one shared way every PropertyItem row and
    // PropertyEditor::paintValue()/paintSubPropertyValue() override paints a line of text. Clips
    // to rect and ellipsizes text too wide for it - fill_utf8_text() itself never wraps/truncates,
    // and an unclipped long value can otherwise run straight into whatever's next to it (a real,
    // caught collision in testharness.exe). Takes newui::Color, not BLRgba32 - the codebase's own
    // higher-level color type (real HSV/HSL conversions, CSS-hex round-trip, ...), converted to
    // BLRgba32 only right at the ctx.set_fill_style() call site that actually needs it - same
    // reasoning callers already apply everywhere a Color-typed property flows through this code.
    void paintText(BLContext& ctx, const newui::Rect& rect, const std::string& text, const newui::Color& color,
        newui::SystemUIFont fontRole = newui::SystemUIFont::Message);

    // Inactive-row rendering of a bool-shaped value - no live newui::Toggle (a paint-only
    // PropertyItem isn't a View, see items.h's own class comment; a PropertyEditor's own
    // paintValue() override isn't one either), just a hand-drawn checkbox glyph reflecting the
    // current value. Shared by BoolPropertyEditor::paintValue() and FlagsEnumPropertyEditor::
    // paintSubPropertyValue() (PropertyEditor.h/.cpp).
    void paintCheckbox(BLContext& ctx, const newui::Rect& box, bool checked, const newui::Color& color);

    // Inactive-row rendering of a Color value - matches PropertyRow::build()'s own original
    // swatch preview. Shared by ColorPropertyEditor::paintValue() (PropertyEditor.h/.cpp).
    void paintSwatch(BLContext& ctx, const newui::Rect& box, const newui::Color& fill, const newui::Color& border);
}
