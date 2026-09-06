#include "PropertyItem.h"

#include <newui/color.h>
#include <newui/fontmanager.h>
#include <newui/uicolormanager.h>

#include <algorithm>
#include <cctype>
#include <typeindex>

namespace CodeToolsVsix
{
    newui::TreeItem* PropertiesTreeController::createItem(const std::vector<std::size_t>& /*path*/)
    {
        return new PropertyItem();
    }

    void PropertiesTreeController::setKeyColumnFraction(float fraction)
    {
        float clamped = fraction < kMinKeyColumnFraction ? kMinKeyColumnFraction
            : fraction > kMaxKeyColumnFraction ? kMaxKeyColumnFraction : fraction;
        if (clamped == keyColumnFraction_) {
            return;
        }
        keyColumnFraction_ = clamped;
        onDataChanged(*this);
    }

    namespace
    {
        // Same priority (disabled beats selected beats normal) items.cpp's
        // own file-local itemTextColor() uses - reimplemented here since
        // that one isn't exported.
        BLRgba32 rowTextColor(const newui::Item& item)
        {
            newui::UIColorRole role = !item.isEnabled() ? newui::UIColorRole::DisabledText
                : item.isSelected() ? newui::UIColorRole::HighlightText
                : newui::UIColorRole::ControlText;
            return newui::UIColorManager::colorFor(role).toBLRgba32();
        }

        // The key column and group/section labels (Main.dc.html's own
        // ".prop-row .k"/".prop-cat"/".prop-group-head", all a dimmer
        // shade than the value column's own full-strength text) - same
        // priority as rowTextColor() otherwise (selection/disabled still
        // win). DisabledText is the closest *real* Windows-backed role to
        // the mockup's own arbitrary "--text-dim" token - same "no
        // fabricated system color" discipline CanvasWell's own background
        // comment already follows, and the same role ToolboxItem/
        // PropertyRow's own header treatment already reused for exactly
        // this "muted label" purpose.
        BLRgba32 dimTextColor(const newui::Item& item)
        {
            newui::UIColorRole role = !item.isEnabled() ? newui::UIColorRole::DisabledText
                : item.isSelected() ? newui::UIColorRole::HighlightText
                : newui::UIColorRole::DisabledText;
            return newui::UIColorManager::colorFor(role).toBLRgba32();
        }

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

        // Truncates text to fit within maxWidth, appending "..." - plain
        // byte-offset truncation (every string this Item ever paints is a
        // C++ identifier/English word, never multi-byte UTF-8), same
        // binary-search-the-longest-fit shape a real text-measuring
        // truncation needs. Returns text unchanged if it already fits, or
        // an empty string if even "..." alone doesn't fit.
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

        // Same BLFont/glyph-buffer/fill_utf8_text idiom items.cpp's own
        // file-local paintItemText() uses - reimplemented here (not
        // exported from there), same as ToolboxItem's own local
        // paintRowText() already does. Clips to rect and ellipsizes text
        // too wide for it - fill_utf8_text() itself never wraps/truncates,
        // and a long key name (e.g. "desiredSizeOverride") drawn unclipped
        // otherwise runs straight into the value column, a real collision
        // caught live in the testharness.
        void paintText(BLContext& ctx, const newui::Rect& rect, const std::string& text, BLRgba32 color,
            newui::SystemUIFont fontRole = newui::SystemUIFont::Message)
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
            ctx.set_fill_style(color);
            ctx.fill_utf8_text(BLPoint(rect.left(), y), *blFont, display.c_str(), display.size());
            ctx.restore();
        }

        // Same hand-drawn triangle items.cpp's own file-local
        // paintExpandGlyph() uses - reimplemented here, not exported.
        void paintExpandGlyph(BLContext& ctx, double centerX, double centerY, double size, bool expanded, BLRgba32 color)
        {
            double half = size * 0.5;
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

            ctx.save();
            ctx.set_fill_style(color);
            ctx.fill_path(path);
            ctx.restore();
        }

        // Inactive-row rendering of a bool property - no live newui::Toggle
        // (Items aren't Views, see items.h's own class comment), just a
        // hand-drawn checkbox glyph reflecting the current value.
        void paintCheckbox(BLContext& ctx, const newui::Rect& box, bool checked, BLRgba32 color)
        {
            ctx.save();
            ctx.set_stroke_style(color);
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

        // Inactive-row rendering of a Color property - matches
        // PropertyRow::build()'s own swatch preview.
        void paintSwatch(BLContext& ctx, const newui::Rect& box, BLRgba32 fill, BLRgba32 border)
        {
            ctx.save();
            ctx.set_fill_style(fill);
            ctx.fill_rect(BLRect(box.left(), box.top(), box.size().width, box.size().height));
            ctx.set_stroke_style(border);
            ctx.set_stroke_width(1.0);
            ctx.stroke_rect(BLRect(box.left(), box.top(), box.size().width, box.size().height));
            ctx.restore();
        }
    }

    void PropertyItem::paint(BLContext& ctx, const newui::Rect& rect, const std::vector<std::size_t>& path,
        newui::TreeController& controller)
    {
        auto* model = dynamic_cast<PropertiesModel*>(controller.model());
        if (model == nullptr) {
            newui::TreeItem::paint(ctx, rect, path, controller);
            return;
        }

        auto* propsController = dynamic_cast<PropertiesTreeController*>(&controller);
        float keyColumnFraction = propsController != nullptr
            ? propsController->keyColumnFraction() : PropertiesTreeController::kDefaultKeyColumnFraction;

        PropertiesModel::Node node = model->nodeAt(path);
        bool isGroupLike = node.kind == PropertiesModel::Kind::PropertyGroup
            || node.kind == PropertiesModel::Kind::DelegatesHeader;

        // Group-like rows never show the row-selection highlight fill -
        // they're expand/section nodes, not an editable value (same
        // "forced false, still call Item::paint()" reasoning ToolboxItem's
        // own header treatment already established - clientBounds() is
        // only ever computed as paint()'s own side effect, items.h).
        if (isGroupLike) {
            setSelected(false);
        }
        Item::paint(ctx, rect);

        if (isGroupLike) {
            // A second, slightly different panel shade (Main.dc.html's own
            // ".prop-cat"/".prop-group-head" background) sets a group/
            // section header row visually apart from the plain rows
            // around it - no real Windows system color maps to that
            // specific "alt panel" concept, so this derives one from a
            // real system color at low alpha instead of fabricating an
            // arbitrary hex value, the same technique Item::paint()'s own
            // hover fill already uses (a reduced-alpha HighlightBackground).
            ctx.save();
            ctx.set_fill_style(newui::UIColorManager::colorFor(newui::UIColorRole::ControlBorder).toBLRgba32());
            ctx.set_fill_alpha(0.12);
            ctx.fill_rect(BLRect(rect.left(), rect.top(), rect.size().width, rect.size().height));
            ctx.restore();
        }

        bool hasChildren = model->hasChildren(path);
        double indent = double(newui::treeDepthOf(path)) * newui::kTreeIndentWidth;
        double glyphCenterX = clientBounds().left() + indent + newui::kTreeGlyphWidth * 0.5;
        double glyphCenterY = clientBounds().top() + clientBounds().size().height * 0.5;
        if (hasChildren) {
            paintExpandGlyph(ctx, glyphCenterX, glyphCenterY, newui::kTreeGlyphWidth * 0.8,
                controller.isExpanded(path), rowTextColor(*this));
        }
        double textLeft = clientBounds().left() + indent + newui::kTreeGlyphWidth;

        if (isGroupLike) {
            newui::Rect labelRect(float(textLeft), clientBounds().top(),
                clientBounds().size().width - float(textLeft - clientBounds().left()), clientBounds().size().height);

            // Uppercase, dim, smaller-font label (Main.dc.html's own
            // ".prop-cat"/".prop-group-head" - uppercase, --text-faint,
            // 10.5px vs a regular row's 12px) - SystemUIFont::Status is
            // the closest real, Windows-backed smaller UI font role
            // (NONCLIENTMETRICS::lfStatusFont), not a fabricated size.
            // Only the name itself is uppercased - the "(TypeName)" suffix
            // stays as-is, matching ".prop-group-head .type
            // { text-transform: none }" exactly (a real, caught bug: an
            // earlier version uppercased the whole label, turning e.g.
            // "style (ViewStyle)" into "STYLE (VIEWSTYLE)").
            std::string name;
            std::string typeSuffix;
            if (node.kind == PropertiesModel::Kind::DelegatesHeader) {
                name = "Delegates";
            } else {
                const newui::reflection::Class* nested = newui::reflection::classinfo(node.property->type());
                name = node.property->name();
                typeSuffix = " (" + (nested != nullptr ? nested->name() : std::string("?")) + ")";
            }
            for (char& c : name) {
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            }
            paintText(ctx, labelRect, name + typeSuffix, dimTextColor(*this), newui::SystemUIFont::Status);
            return;
        }

        newui::Rect keyRect = keyRectFor(clientBounds(), path, keyColumnFraction);
        newui::Rect valueRect = valueRectFor(clientBounds(), path, keyColumnFraction);

        // A thin divider line at the shared column split, matching real
        // property grids (VS's own Properties window) - PropertiesGrid's
        // own drag handling (bluesky/property-grid-design.md's "resizable
        // key/value column divider" section) hit-tests against this exact
        // same X (clientBounds().left() + width * keyColumnFraction), so
        // this is the one visible cue for where that grab region is. Leaf
        // rows only - drawing it through group/section header rows too
        // (tried, then reverted per direct user feedback) looked wrong
        // crossing the expand glyph/full-width label; those rows get the
        // alt-panel background fill above as their own visual separation
        // instead.
        double dividerX = clientBounds().left() + double(clientBounds().size().width) * double(keyColumnFraction);
        BLRgba32 borderColor = newui::UIColorManager::colorFor(newui::UIColorRole::ControlBorder).toBLRgba32();
        ctx.save();
        ctx.set_stroke_style(borderColor);
        ctx.set_stroke_width(1.0);
        ctx.stroke_line(BLPoint(dividerX, clientBounds().top()), BLPoint(dividerX, clientBounds().bottom()));
        // Row separator (Main.dc.html's own ".prop-row { border-bottom }").
        ctx.stroke_line(BLPoint(clientBounds().left(), clientBounds().bottom()), BLPoint(clientBounds().right(), clientBounds().bottom()));
        ctx.restore();

        std::string keyText;
        if (node.kind == PropertiesModel::Kind::DelegateEntry) {
            keyText = node.delegate->name();
        } else if (node.property != nullptr) {
            keyText = node.property->name();
        }
        paintText(ctx, keyRect, keyText, dimTextColor(*this));

        if (node.kind == PropertiesModel::Kind::PropertyUnsupported) {
            paintText(ctx, valueRect, "(unsupported)", rowTextColor(*this));
            return;
        }

        if (node.kind == PropertiesModel::Kind::DelegateEntry) {
            std::vector<std::string> listeners = node.delegate->describedListeners(node.ownerInstance);
            std::string joined;
            for (std::size_t i = 0; i < listeners.size(); ++i) {
                if (i > 0) {
                    joined += ", ";
                }
                joined += listeners[i];
            }
            paintText(ctx, valueRect, joined.empty() ? "(no listeners)" : joined, rowTextColor(*this));
            return;
        }

        // Kind::PropertyLeaf - same registered PropertyEditor
        // (PropertyEditorRegistry, built 2026-09-03) PropertiesModel used
        // to classify this node as a leaf in the first place.
        std::unique_ptr<PropertyEditor> editor = PropertyEditorRegistry::instance()
            .createEditor(node.property, node.ownerClass, node.ownerInstance);
        if (editor == nullptr) {
            return;
        }

        if (node.property->type() == std::type_index(typeid(bool))) {
            newui::Rect box(valueRect.left(), valueRect.top() + (valueRect.size().height - kCheckboxSize) * 0.5f,
                kCheckboxSize, kCheckboxSize);
            paintCheckbox(ctx, box, editor->valueAsString() == "true", rowTextColor(*this));
            return;
        }

        if (node.property->type() == std::type_index(typeid(newui::Color))) {
            newui::Rect box(valueRect.left(), valueRect.top() + (valueRect.size().height - kSwatchSize) * 0.5f,
                kSwatchSize, kSwatchSize);
            newui::Color parsed;
            if (newui::Color::fromString(editor->valueAsString(), parsed)) {
                paintSwatch(ctx, box, parsed.toBLRgba32(), rowTextColor(*this));
            }
            newui::Rect textRect(valueRect.left() + kSwatchSize + 6.0f, valueRect.top(),
                valueRect.size().width - kSwatchSize - 6.0f, valueRect.size().height);
            paintText(ctx, textRect, editor->valueAsString(), rowTextColor(*this));
            return;
        }

        paintText(ctx, valueRect, editor->valueAsString(), rowTextColor(*this));
    }

    newui::Rect PropertyItem::keyRectFor(const newui::Rect& rowRect, const std::vector<std::size_t>& path, float keyColumnFraction)
    {
        double indent = double(newui::treeDepthOf(path)) * newui::kTreeIndentWidth;
        double textLeft = rowRect.left() + indent + newui::kTreeGlyphWidth;
        float keyWidth = rowRect.size().width * keyColumnFraction;
        return newui::Rect(float(textLeft), rowRect.top(),
            keyWidth - float(textLeft - rowRect.left()), rowRect.size().height);
    }

    newui::Rect PropertyItem::valueRectFor(const newui::Rect& rowRect, const std::vector<std::size_t>& /*path*/, float keyColumnFraction)
    {
        // Doesn't itself depend on indent - the value column starts at a
        // fixed fraction of the row's own full width regardless of
        // nesting depth (only the key column's left edge shifts with
        // indent, see keyRectFor()) - path kept in the signature purely so
        // both halves of this key/value split share one call shape.
        float keyWidth = rowRect.size().width * keyColumnFraction;
        return newui::Rect(rowRect.left() + keyWidth + kRowPadding, rowRect.top(),
            rowRect.size().width - keyWidth - kRowPadding, rowRect.size().height);
    }
}
