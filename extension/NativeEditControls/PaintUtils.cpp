#include "PaintUtils.h"

namespace CodeToolsVsix
{
    void paintCheckerboard(BLContext& ctx, const newui::Rect& rect, double tile)
    {
        if (rect.width() <= 0.0f || rect.height() <= 0.0f) {
            return;
        }
        BLRgba32 colorA(0x5b, 0x5c, 0x60);
        BLRgba32 colorB(0x3a, 0x3b, 0x3e);

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
    }
}
