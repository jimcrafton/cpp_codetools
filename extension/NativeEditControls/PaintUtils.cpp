#include "PaintUtils.h"

#include <newui/fontmanager.h>

namespace CodeToolsVsix
{
    double measureTextWidth(BLFont& font, const std::string& text)
    {
        if (text.empty()) {
            return 0.0;
        }
        BLGlyphBuffer glyphBuffer;
        glyphBuffer.set_utf8_text(text.c_str(), text.size());
        font.shape(glyphBuffer);
        BLTextMetrics textMetrics;
        font.get_text_metrics(glyphBuffer, textMetrics);
        return textMetrics.advance.x;
    }

    std::string truncateWithEllipsis(BLFont& font, const std::string& text, double maxWidth)
    {
        if (measureTextWidth(font, text) <= maxWidth) {
            return text;
        }

        static const std::string kEllipsis = "...";
        if (measureTextWidth(font, kEllipsis) > maxWidth) {
            return std::string();
        }

        std::size_t lo = 0;
        std::size_t hi = text.size();
        while (lo < hi) {
            std::size_t mid = lo + (hi - lo + 1) / 2;
            std::string candidate = text.substr(0, mid) + kEllipsis;
            if (measureTextWidth(font, candidate) <= maxWidth) {
                lo = mid;
            } else {
                hi = mid - 1;
            }
        }
        return text.substr(0, lo) + kEllipsis;
    }

    void paintText(BLContext& ctx, const newui::Rect& rect, const std::string& text, const newui::Color& color,
        newui::SystemUIFont fontRole)
    {
        if (text.empty() || rect.size().width <= 0.0f || rect.size().height <= 0.0f) {
            return;
        }

        newui::Font font = newui::FontManager::getSystemFont(fontRole);
        BLFont* blFont = font.blFont();
        if (blFont == nullptr || !blFont->is_valid()) {
            return;
        }

        std::string display = truncateWithEllipsis(*blFont, text, rect.size().width);
        if (display.empty()) {
            return;
        }

        const BLFontMetrics& fontMetrics = blFont->metrics();
        double textHeight = fontMetrics.ascent + fontMetrics.descent;
        double y = rect.top() + (rect.size().height - textHeight) * 0.5 + fontMetrics.ascent;

        ctx.save();
        ctx.clip_to_rect(BLRect(rect.left(), rect.top(), rect.size().width, rect.size().height));
        ctx.set_fill_style(color.toBLRgba32());
        ctx.fill_utf8_text(BLPoint(rect.left(), y), *blFont, display.c_str(), display.size());
        ctx.restore();
    }

    void paintCheckbox(BLContext& ctx, const newui::Rect& box, bool checked, const newui::Color& color)
    {
        ctx.save();
        ctx.set_stroke_style(color.toBLRgba32());
        ctx.set_stroke_width(1.0);
        ctx.stroke_rect(BLRect(box.left(), box.top(), box.size().width, box.size().height));
        if (checked) {
            BLPath check;
            check.move_to(box.left() + box.size().width * 0.2, box.top() + box.size().height * 0.55);
            check.line_to(box.left() + box.size().width * 0.42, box.top() + box.size().height * 0.78);
            check.line_to(box.left() + box.size().width * 0.82, box.top() + box.size().height * 0.22);
            ctx.set_stroke_width(1.6);
            ctx.stroke_path(check);
        }
        ctx.restore();
    }

    void paintSwatch(BLContext& ctx, const newui::Rect& box, const newui::Color& fill, const newui::Color& border)
    {
        ctx.save();
        ctx.set_fill_style(fill.toBLRgba32());
        ctx.fill_rect(BLRect(box.left(), box.top(), box.size().width, box.size().height));
        ctx.set_stroke_style(border.toBLRgba32());
        ctx.set_stroke_width(1.0);
        ctx.stroke_rect(BLRect(box.left(), box.top(), box.size().width, box.size().height));
        ctx.restore();
    }
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
