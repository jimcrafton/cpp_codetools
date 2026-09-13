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
    // --checker-a/b), not ImageFillStyle's configurable ones.
    void paintCheckerboard(BLContext& ctx, const newui::Rect& rect, double tile = 6.0);
}
