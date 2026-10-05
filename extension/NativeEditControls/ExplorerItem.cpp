#include "ExplorerItem.h"

#include <newui/bundle.h>
#include <newui/fontmanager.h>
#include <newui/uicolormanager.h>

#include <algorithm>
#include <map>

namespace CodeToolsVsix
{
    namespace
    {
        using Tone = ExplorerNode::Tone;

        // The theme's own colors for everything but Good / Warn / Bad: Windows defines no success, warning or
        // error color (no system role to fall back to), so only those three are fixed here, in a lighter
        // variant on a dark theme.
        BLRgba32 toneColor(Tone tone, bool selected)
        {
            using newui::UIColorManager;
            using newui::UIColorRole;
            if (selected) return UIColorManager::colorFor(UIColorRole::HighlightText).toBLRgba32();
            const bool dark = UIColorManager::isDarkMode();
            switch (tone) {
                case Tone::Muted: return UIColorManager::colorFor(UIColorRole::DisabledText).toBLRgba32();
                case Tone::Accent: return UIColorManager::colorFor(UIColorRole::LinkText).toBLRgba32();
                case Tone::Good: return dark ? BLRgba32(0xFF6CC47Fu) : BLRgba32(0xFF1E7B34u);
                case Tone::Warn: return dark ? BLRgba32(0xFFE3B341u) : BLRgba32(0xFF9A6700u);
                case Tone::Bad: return dark ? BLRgba32(0xFFF47067u) : BLRgba32(0xFFC62828u);
                case Tone::Normal: break;
            }
            return UIColorManager::colorFor(UIColorRole::ControlText).toBLRgba32();
        }

        double textWidth(BLFont& font, const std::string& text)
        {
            if (text.empty()) return 0.0;
            BLGlyphBuffer glyphs;
            glyphs.set_utf8_text(text.c_str(), text.size());
            font.shape(glyphs);
            BLTextMetrics metrics;
            font.get_text_metrics(glyphs, metrics);
            return metrics.advance.x;
        }

        // `text` as it fits in `available` pixels: itself, or cut at a whole character with an ellipsis.
        std::string fitText(BLFont& font, const std::string& text, double available)
        {
            const double width = textWidth(font, text);
            if (width <= available) return text;
            const std::string ellipsis = "\xE2\x80\xA6";
            if (available <= 0.0) return std::string();

            // a first guess from the proportion, then shorter until it fits
            std::size_t length = std::min(text.size(), static_cast<std::size_t>(double(text.size()) * available / width));
            for (;;) {
                while (length > 0 && (static_cast<unsigned char>(text[length]) & 0xC0) == 0x80) --length;   // not mid-character
                if (length == 0) return ellipsis;
                const std::string candidate = text.substr(0, length) + ellipsis;
                if (textWidth(font, candidate) <= available) return candidate;
                --length;
            }
        }

        void drawText(BLContext& ctx, BLFont& font, double x, const newui::Rect& row, const std::string& text, BLRgba32 color)
        {
            if (text.empty()) return;
            const BLFontMetrics& metrics = font.metrics();
            const double y = row.top() + (row.size().height - (metrics.ascent + metrics.descent)) * 0.5 + metrics.ascent;
            ctx.set_fill_style(color);
            ctx.fill_utf8_text(BLPoint(x, y), font, text.c_str(), text.size());
        }

        // An older icon's SVG paints in currentColor, which the loader leaves at its default; keep the shape's
        // alpha and fill it with `color`, so it reads on a light or dark row. Cached per name, size, color.
        const BLImage* tintedIcon(const std::string& name, int size, BLRgba32 color)
        {
            static std::map<std::string, BLImage> cache;
            const std::string key = name + "@" + std::to_string(size) + "#" + std::to_string(color.value);
            auto found = cache.find(key);
            if (found != cache.end()) return &found->second;

            BLImage source;
            if (!newui::Bundle::instance().loadCachedImage(name, source, size, size)) return nullptr;
            BLImage tinted(size, size, BL_FORMAT_PRGB32);
            BLContext ctx(tinted);
            ctx.set_comp_op(BL_COMP_OP_SRC_COPY);
            ctx.blit_image(BLPoint(0, 0), source);
            ctx.set_comp_op(BL_COMP_OP_SRC_IN);
            ctx.set_fill_style(color);
            ctx.fill_all();
            ctx.end();
            return &cache.emplace(key, std::move(tinted)).first->second;
        }

        void drawExpandGlyph(BLContext& ctx, double centerX, double centerY, bool expanded, BLRgba32 color)
        {
            const double half = newui::kTreeGlyphWidth * 0.8 * 0.5;
            BLPath path;
            if (expanded) {
                path.move_to(centerX - half, centerY - half * 0.6);
                path.line_to(centerX + half, centerY - half * 0.6);
                path.line_to(centerX, centerY + half * 0.6);
            } else {
                path.move_to(centerX - half * 0.6, centerY - half);
                path.line_to(centerX - half * 0.6, centerY + half);
                path.line_to(centerX + half * 0.6, centerY);
            }
            path.close();
            ctx.set_fill_style(color);
            ctx.fill_path(path);
        }
    }

    void ExplorerItem::paint(BLContext& ctx, const newui::Rect& rect, const std::vector<std::size_t>& path,
                             newui::TreeController& controller)
    {
        Item::paint(ctx, rect);   // the row's background; also sets clientBounds()

        auto* model = dynamic_cast<ExplorerTreeModel*>(controller.model());
        const ExplorerNode* node = model != nullptr ? model->nodeAt(path) : nullptr;
        if (node == nullptr) return;

        newui::Font systemFont = newui::FontManager::getSystemFont(newui::SystemUIFont::Message);
        BLFont* font = systemFont.blFont();
        if (font == nullptr || !font->is_valid()) return;

        const newui::Rect& row = clientBounds();
        const bool selected = isSelected();
        const double centerY = row.top() + row.size().height * 0.5;
        const double right = row.left() + row.size().width;
        const BLRgba32 nameColor = toneColor(node->badge == ExplorerNode::Badge::Unreferenced ? Tone::Muted : Tone::Normal, selected);

        ctx.save();
        const double indent = double(newui::treeDepthOf(path)) * newui::kTreeIndentWidth;
        if (model->hasChildren(path)) {
            drawExpandGlyph(ctx, row.left() + indent + newui::kTreeGlyphWidth * 0.5, centerY, controller.isExpanded(path), nameColor);
        }

        double x = row.left() + indent + newui::kTreeGlyphWidth;
        const ExplorerIcon icon = explorerIconFor(*node);
        if (!icon.empty()) {
            const bool dark = newui::UIColorManager::isDarkMode();
            const std::string resource = explorerIconPath(icon, dark);
            const int size = int(controller.iconSize() + 0.5f);
            if (icon.themed) {
                // the colored set: drawn as it is, from the light or dark folder
                x += newui::Item::paintItemIcon(ctx, x, centerY, resource, controller.iconSize(), controller.iconGap());
            } else if (const BLImage* image = tintedIcon(resource, size, nameColor)) {   // the theme's text color
                ctx.blit_image(BLPoint(x, centerY - size * 0.5), *image);
                x += controller.iconSize() + controller.iconGap();
            }
        }

        // Cells fill the right edge, so the name and its detail get what is left of the left side. A column whose
        // cell is empty on this row (no line count on a text file) gives its room back to the name.
        const double cellWidth = node->cellWidth > 0.0f ? double(node->cellWidth) : double(kCellWidth);
        // If the whole name does not fit beside every column, columns go: by default the leftmost first (a size matters
        // more than a line count), or the rightmost first when the row says its first columns matter most. Always one
        // stays. The ones that stay sit at the right edge.
        const double nameWidth = textWidth(*font, node->text);
        std::size_t firstShown = 0;
        std::size_t endShown = node->cells.size();
        if (node->dropRightFirst) {
            while (endShown > 1 && x + nameWidth > right - kEdgePad - cellWidth * double(endShown) - kNameGap) --endShown;
        } else {
            while (firstShown < endShown && node->cells[firstShown].empty()) ++firstShown;
            while (firstShown + 1 < endShown &&
                   x + nameWidth > right - kEdgePad - cellWidth * double(endShown - firstShown) - kNameGap) {
                ++firstShown;
            }
        }
        const std::size_t shownCells = endShown - firstShown;
        const double cellsLeft = right - kEdgePad - cellWidth * double(shownCells);
        const double limit = shownCells == 0 ? right : cellsLeft - kNameGap;
        ctx.clip_to_rect(BLRect(x, row.top(), std::max(0.0, limit - x), row.size().height));

        if (node->bar >= 0.0f) {
            // a heat bar row: the name in its own column, then the bar out to the cells
            const double nameLimit = std::min(limit, x + double(kBarNameWidth));
            drawText(ctx, *font, x, row, fitText(*font, node->text, nameLimit - x), nameColor);
            const double barLeft = x + double(kBarNameWidth) + 8.0;
            const double barRight = limit - 4.0;
            if (barRight - barLeft >= double(kMinBarWidth)) {
                const double top = centerY - double(kBarHeight) * 0.5;
                BLRgba32 track = toneColor(Tone::Muted, false);
                track.value = (track.value & 0x00FFFFFFu) | 0x40000000u;
                ctx.set_fill_style(track);
                ctx.fill_round_rect(BLRoundRect(barLeft, top, barRight - barLeft, double(kBarHeight), 4.0, 4.0));
                const double filled = (barRight - barLeft) * double(std::min(node->bar, 1.0f));
                if (filled > 0.0) {
                    ctx.set_fill_style(toneColor(node->barTone, false));
                    ctx.fill_round_rect(BLRoundRect(barLeft, top, std::max(filled, double(kBarHeight)), double(kBarHeight), 4.0, 4.0));
                }
            }
        } else {
            const std::string name = fitText(*font, node->text, limit - x);
            drawText(ctx, *font, x, row, name, nameColor);
            if (!node->detail.empty() && name == node->text) {   // a shortened name leaves no room for its detail
                const double detailX = x + textWidth(*font, name) + kNameGap;
                const double room = limit - detailX;
                if (room >= kMinDetailWidth) {   // what does not fit ends in an ellipsis, like the name
                    drawText(ctx, *font, detailX, row, fitText(*font, node->detail, room), toneColor(node->detailTone, selected));
                }
            }
        }
        ctx.restore_clipping();

        for (std::size_t i = firstShown; i < endShown; ++i) {
            const Tone tone = i < node->cellTones.size() ? node->cellTones[i] : Tone::Normal;
            const double cellRight = right - kEdgePad - cellWidth * double(endShown - 1 - i);
            drawText(ctx, *font, cellRight - textWidth(*font, node->cells[i]), row, node->cells[i], toneColor(tone, selected));
        }
        ctx.restore();
    }

    newui::TreeItem* ExplorerController::createItem(const std::vector<std::size_t>& /*path*/)
    {
        return new ExplorerItem();
    }

    float ExplorerController::itemHeight(std::size_t /*visibleIndex*/) const
    {
        return kRowHeight;
    }

    std::optional<std::string> ExplorerController::iconFor(const std::vector<std::size_t>& path) const
    {
        auto* model = dynamic_cast<ExplorerTreeModel*>(const_cast<ExplorerController*>(this)->model());
        const ExplorerNode* node = model != nullptr ? model->nodeAt(path) : nullptr;
        if (node == nullptr) return std::nullopt;
        const ExplorerIcon icon = explorerIconFor(*node);
        if (icon.empty()) return std::nullopt;
        return explorerIconPath(icon, newui::UIColorManager::isDarkMode());
    }
}
