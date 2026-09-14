#include "PaintUtils.h"

namespace CodeToolsVsix
{
    void paintCheckerboard(BLContext& ctx, const newui::Rect& rect, double tile, double radius)
    {
        if (rect.width() <= 0.0f || rect.height() <= 0.0f) {
            return;
        }
        BLRgba32 colorA(0x5b, 0x5c, 0x60);
        BLRgba32 colorB(0x3a, 0x3b, 0x3e);

        if (radius <= 0.0) {
            ctx.save();
            ctx.set_comp_op(BL_COMP_OP_SRC_OVER);
            for (double y = 0.0; y < double(rect.height()); y += tile) {
                double h = (y + tile > double(rect.height())) ? double(rect.height()) - y : tile;
                int row = int(y / tile);
                for (double x = 0.0; x < double(rect.width()); x += tile) {
                    double w = (x + tile > double(rect.width())) ? double(rect.width()) - x : tile;
                    int col = int(x / tile);
                    ctx.set_fill_style((row + col) % 2 == 0 ? colorA : colorB);
                    ctx.fill_rect(BLRect(double(rect.left()) + x, double(rect.top()) + y, w, h));
                }
            }
            ctx.restore();
            return;
        }

        // blend2d's context here only exposes rectangular clip_to_rect (no path/rounded clip),
        // so the square tile loop above would poke checker squares out past a rounded corner.
        // Bake one 2x2-tile swatch into a tiny repeating pattern instead and fill it through the
        // exact same fill_round_rect() call the caller's own gradient fill uses right after this
        // - the rounding then comes from the fill call itself, not from clipping.
        int t = int(tile) < 1 ? 1 : int(tile);
        BLImage tileImg;
        tileImg.create(t * 2, t * 2, BL_FORMAT_PRGB32);
        {
            BLContext ictx(tileImg);
            ictx.set_fill_style(colorA);
            ictx.fill_rect(BLRect(0, 0, t, t));
            ictx.fill_rect(BLRect(t, t, t, t));
            ictx.set_fill_style(colorB);
            ictx.fill_rect(BLRect(t, 0, t, t));
            ictx.fill_rect(BLRect(0, t, t, t));
            ictx.end();
        }

        BLMatrix2D m = BLMatrix2D::make_identity();
        m.translate(double(rect.left()), double(rect.top()));

        ctx.save();
        ctx.set_fill_style(BLVar(BLPattern(tileImg, BL_EXTEND_MODE_REPEAT, m)));
        ctx.fill_round_rect(BLRect(rect), radius);
        ctx.restore();
    }
}
