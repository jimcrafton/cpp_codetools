#pragma once

#include <newui/geometry.h>

#include <blend2d/blend2d.h>

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
}
