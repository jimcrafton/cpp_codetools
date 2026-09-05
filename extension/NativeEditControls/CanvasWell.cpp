#include "CanvasWell.h"

#include <newui/uicolormanager.h>

#include <string>

namespace CodeToolsVsix
{
    namespace
    {
        // Below this margin, a dimension ruler wouldn't have room to read
        // (line + label + padding) - skipped rather than drawn cramped.
        constexpr float kMinRulerMargin = 24.0f;
        constexpr float kArrowSize = 5.0f;
        constexpr float kLabelGapPadding = 4.0f;

        // A small filled triangle whose tip sits at (tipX, tipY) - exactly
        // where the ruler line meets the guide line it's paired with - and
        // points back along (dirX, dirY) (a unit vector; only the sign/
        // ratio matters).
        void paintArrowhead(BLContext& ctx, double tipX, double tipY, double dirX, double dirY, BLRgba32 color)
        {
            double perpX = -dirY;
            double perpY = dirX;
            BLPath path;
            path.move_to(tipX, tipY);
            path.line_to(tipX - dirX * kArrowSize + perpX * kArrowSize * 0.5, tipY - dirY * kArrowSize + perpY * kArrowSize * 0.5);
            path.line_to(tipX - dirX * kArrowSize - perpX * kArrowSize * 0.5, tipY - dirY * kArrowSize - perpY * kArrowSize * 0.5);
            path.close();
            ctx.set_fill_style(color);
            ctx.fill_path(path);
        }

        // A "<---- label ---->"-style dimension ruler along the axis from
        // start to end (both in the same coordinate the axis runs along -
        // x for a horizontal/width ruler, y for a vertical/height one),
        // held at crossAxis on the other coordinate. label sits centered in
        // a gap in the middle; each side's line segment + arrowhead is
        // skipped if label is too wide for the available span. Arrowhead
        // tips land exactly at (start, crossAxis)/(end, crossAxis) (or the
        // x/y-swapped equivalent for vertical) - the same coordinates
        // CanvasWell::paint() draws its full-span structural guide lines
        // through, so the arrows always touch those guides.
        void paintDimensionRuler(BLContext& ctx, double start, double end, double crossAxis, bool vertical,
            const std::string& label, BLFont& font, BLRgba32 color)
        {
            BLGlyphBuffer glyphBuffer;
            glyphBuffer.set_utf8_text(label.c_str(), label.size());
            font.shape(glyphBuffer);
            BLTextMetrics textMetrics;
            font.get_text_metrics(glyphBuffer, textMetrics);
            const BLFontMetrics& fontMetrics = font.metrics();
            double textWidth = textMetrics.advance.x;
            double textHeight = fontMetrics.ascent + fontMetrics.descent;

            double mid = (start + end) * 0.5;
            double halfGap = textWidth * 0.5 + kLabelGapPadding;

            ctx.set_stroke_style(color);
            ctx.set_stroke_width(1.0);

            double dirX = vertical ? 0.0 : 1.0;
            double dirY = vertical ? 1.0 : 0.0;

            if (mid - halfGap > start) {
                double sx = vertical ? crossAxis : start;
                double sy = vertical ? start : crossAxis;
                double ex = vertical ? crossAxis : (mid - halfGap);
                double ey = vertical ? (mid - halfGap) : crossAxis;
                ctx.stroke_line(sx, sy, ex, ey);
                paintArrowhead(ctx, sx, sy, -dirX, -dirY, color);
            }
            if (mid + halfGap < end) {
                double sx = vertical ? crossAxis : (mid + halfGap);
                double sy = vertical ? (mid + halfGap) : crossAxis;
                double ex = vertical ? crossAxis : end;
                double ey = vertical ? end : crossAxis;
                ctx.stroke_line(sx, sy, ex, ey);
                paintArrowhead(ctx, ex, ey, dirX, dirY, color);
            }

            ctx.set_fill_style(color);
            if (!vertical) {
                double tx = mid - textWidth * 0.5;
                double ty = crossAxis + textHeight * 0.5 - fontMetrics.descent;
                ctx.fill_utf8_text(BLPoint(tx, ty), font, label.c_str(), label.size());
            } else {
                // Rotated -90deg so the label reads bottom-to-top alongside
                // the vertical ruler line, same convention a ruler/height
                // callout in a real design tool uses.
                ctx.save();
                ctx.translate(crossAxis, mid);
                ctx.rotate(-1.5707963267948966);
                ctx.fill_utf8_text(BLPoint(-textWidth * 0.5, textHeight * 0.5 - fontMetrics.descent), font, label.c_str(), label.size());
                ctx.restore();
            }
        }
    }

    CanvasWell::CanvasWell()
    {
        rulerFont_ = newui::FontManager::getSystemFont(newui::SystemUIFont::Caption);
    }

    void CanvasWell::paint(BLContext& ctx)
    {
        if (childViews().empty()) {
            return;
        }

        const newui::Rect& frameBounds = childViews()[0]->bounds();
        newui::Rect ownBounds(0.0f, 0.0f, bounds().size().width, bounds().size().height);

        BLRgba32 guideColor = newui::UIColorManager::colorFor(newui::UIColorRole::HighlightBackground).toBLRgba32();

        ctx.save();
        ctx.set_stroke_style(guideColor);
        ctx.set_stroke_width(1.0);
        ctx.stroke_line(ownBounds.left(), frameBounds.top(), ownBounds.right(), frameBounds.top());
        ctx.stroke_line(ownBounds.left(), frameBounds.bottom(), ownBounds.right(), frameBounds.bottom());
        ctx.stroke_line(frameBounds.left(), ownBounds.top(), frameBounds.left(), ownBounds.bottom());
        ctx.stroke_line(frameBounds.right(), ownBounds.top(), frameBounds.right(), ownBounds.bottom());
        ctx.restore();

        BLFont* blFont = rulerFont_.blFont();
        if (blFont == nullptr || !blFont->is_valid()) {
            return;
        }

        double topMargin = frameBounds.top() - ownBounds.top();
        if (topMargin >= kMinRulerMargin) {
            std::string widthLabel = std::to_string(static_cast<int>(frameBounds.size().width)) + " px";
            paintDimensionRuler(ctx, frameBounds.left(), frameBounds.right(), topMargin * 0.5, false, widthLabel, *blFont, guideColor);
        }

        double leftMargin = frameBounds.left() - ownBounds.left();
        if (leftMargin >= kMinRulerMargin) {
            std::string heightLabel = std::to_string(static_cast<int>(frameBounds.size().height)) + " px";
            paintDimensionRuler(ctx, frameBounds.top(), frameBounds.bottom(), leftMargin * 0.5, true, heightLabel, *blFont, guideColor);
        }
    }
}
