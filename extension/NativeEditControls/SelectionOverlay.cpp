#include "SelectionOverlay.h"

#include <newui/fontmanager.h>
#include <newui/reflection.h>
#include <newui/uicolormanager.h>

namespace CodeToolsVsix
{
    namespace
    {
        constexpr float kHandleSize = 7.0f;
        constexpr float kHandleHalf = kHandleSize * 0.5f;
        constexpr float kOutlineWidth = 2.0f;
        constexpr float kBadgePaddingX = 6.0f;
        constexpr float kBadgeHeight = 18.0f;
        constexpr float kBadgeGap = 4.0f;  // between the badge's own bottom and the parent box's top
        constexpr float kParentBoxWidth = 1.5f;

        // Amber, deliberately distinct from the blue accent used for the primary selection
        // itself - Chrome DevTools/Figma-style "this is the container" color-coding, so the
        // parent adornment reads as a different semantic layer, not a second, confusingly
        // similar selection outline. No UIColorManager role fits (only Window/Control/
        // Highlight/Link roles exist) - same "hardcode it, design-time-only decoration"
        // precedent LayoutEditingPolicy.cpp's own grid-tracker lines already set.
        const BLRgba32 kLayoutAdornmentColor(0xD9, 0x77, 0x06, 0xFF);
        const BLRgba32 kLayoutAdornmentTextColor(0x2B, 0x1B, 0x00, 0xFF);

        // Mirrors Main.dc.html's own ".handle" (7x7 white square, 1.5px
        // accent border) - centered on the given point, matching the
        // mockup's corner-of-the-box placement (h-tl/h-tr/h-bl/h-br).
        void paintHandle(BLContext& ctx, float cx, float cy, BLRgba32 borderColor)
        {
            BLRect r(cx - kHandleHalf, cy - kHandleHalf, kHandleSize, kHandleSize);
            ctx.set_fill_style(BLRgba32(0xFFFFFFFFu));
            ctx.fill_rect(r);
            ctx.set_stroke_style(borderColor);
            ctx.set_stroke_width(1.5);
            ctx.stroke_rect(r);
        }

        // Names parent's real, attached Layout via the same reflection-name lookup Document
        // Outline/Toolbox already use for their own icon resolution (classinfo(typeid(*obj))) -
        // "No Layout" for a parent with none, matching how a real, unmanaged container behaves.
        std::string layoutDisplayName(const newui::View* parent)
        {
            if (parent == nullptr) {
                return std::string();
            }
            newui::Layout* layout = parent->layout();
            if (layout == nullptr) {
                return "No Layout";
            }
            const newui::reflection::Class* clazz = newui::reflection::classinfo(typeid(*layout));
            return clazz != nullptr ? clazz->name() : "Layout";
        }

        // A small rounded "chip" naming the governing layout, anchored just above parentBounds'
        // own top-left corner - Chrome DevTools/Figma-style element-inspector tag, not raw
        // floating text. Reuses FrameProxy's own title-bar text convention (FontManager::
        // getSystemFont(Caption) + fill_utf8_text) - the lightest real text-drawing path this
        // codebase has, deliberately not the heavier TextRenderer/lexer machinery.
        void paintLayoutBadge(BLContext& ctx, const newui::Rect& parentBounds, const std::string& text)
        {
            newui::Font font = newui::FontManager::getSystemFont(newui::SystemUIFont::Caption);
            BLFont* blFont = font.blFont();
            if (blFont == nullptr || !blFont->is_valid()) {
                return;
            }

            BLGlyphBuffer glyphBuffer;
            glyphBuffer.set_utf8_text(text.c_str(), text.size());
            blFont->shape(glyphBuffer);
            BLTextMetrics metrics;
            blFont->get_text_metrics(glyphBuffer, metrics);

            float badgeWidth = static_cast<float>(metrics.advance.x) + kBadgePaddingX * 2.0f;
            float badgeX = parentBounds.left();
            float badgeY = parentBounds.top() - kBadgeHeight - kBadgeGap;

            BLRoundRect badgeRect(badgeX, badgeY, badgeWidth, kBadgeHeight, 4.0);
            ctx.set_fill_style(kLayoutAdornmentColor);
            ctx.fill_round_rect(badgeRect);

            const BLFontMetrics& fontMetrics = blFont->metrics();
            double tx = badgeX + kBadgePaddingX;
            double ty = badgeY + (kBadgeHeight - (fontMetrics.ascent + fontMetrics.descent)) * 0.5 + fontMetrics.ascent;
            ctx.set_fill_style(kLayoutAdornmentTextColor);
            ctx.fill_utf8_text(BLPoint(tx, ty), *blFont, text.c_str(), text.size());
        }

        // Recursive step behind SelectionOverlay::boundsInRootView() below -
        // see that method's own comment for the math this composes.
        newui::Point rootLocalOrigin(const newui::View* view)
        {
            if (view == nullptr) {
                return newui::Point(0.0f, 0.0f);
            }
            const newui::View* parent = view->parent();
            if (parent == nullptr) {
                return newui::Point(0.0f, 0.0f);
            }
            newui::Point parentOrigin = rootLocalOrigin(parent);
            return newui::Point(
                parentOrigin.x + view->bounds().left() - parent->origin().x,
                parentOrigin.y + view->bounds().top() - parent->origin().y);
        }
    }

    newui::Rect SelectionOverlay::boundsInRootView(const newui::View* view)
    {
        if (view == nullptr) {
            return newui::Rect();
        }
        return newui::Rect(rootLocalOrigin(view), view->bounds().size());
    }

    void SelectionOverlay::paint(BLContext& ctx, const newui::Rect& /*rect*/)
    {
        const std::vector<newui::SubView*>& selected = controller_.selected();
        if (selected.empty()) {
            return;
        }

        BLRgba32 accent = newui::UIColorManager::colorFor(newui::UIColorRole::HighlightBackground).toBLRgba32();

        // See the constructor's own comment - without this, a selection
        // outline/handle paints straight over the Toolbox/Properties
        // chrome around the design surface, since Overlay paints on top
        // of the *entire* hosting RootView pane, not just clipView_'s own
        // area.
        bool clipping = clipView_ != nullptr;
        if (clipping) {
            newui::Rect clipRect = boundsInRootView(clipView_);
            ctx.save();
            ctx.clip_to_rect(BLRect(clipRect.left(), clipRect.top(), clipRect.size().width, clipRect.size().height));
        }

        for (newui::SubView* view : selected) {
            // Inset by 1px - matches Main.dc.html's own
            // ".node-wrap.selected { outline-offset: -1px }".
            newui::Rect outline = boundsInRootView(view).deflate(1.0f);
            ctx.set_stroke_style(accent);
            ctx.set_stroke_width(kOutlineWidth);
            ctx.stroke_box(outline.left(), outline.top(), outline.right(), outline.bottom());
        }

        if (newui::SubView* primaryView = controller_.primary()) {
            newui::Rect bounds = boundsInRootView(primaryView);
            paintHandle(ctx, bounds.left(), bounds.top(), accent);
            paintHandle(ctx, bounds.right(), bounds.top(), accent);
            paintHandle(ctx, bounds.left(), bounds.bottom(), accent);
            paintHandle(ctx, bounds.right(), bounds.bottom(), accent);

            // Layout adornment - shows which real container/Layout the primary selection
            // belongs to, on plain selection alone (no drag needed) - the always-on
            // counterpart to the drag cues above, which only ever appear mid-drag.
            // Skipped when parent is the absolute top-level hosting RootView itself
            // (parent->parent() == nullptr) - that's Workspace's own outer chrome host, not a
            // real design-time container, and highlighting "your parent is the entire canvas"
            // conveys nothing useful.
            if (newui::View* parent = primaryView->parent()) {
                if (parent->parent() != nullptr) {
                    newui::Rect parentBounds = boundsInRootView(parent);
                    ctx.set_stroke_style(kLayoutAdornmentColor);
                    ctx.set_stroke_width(kParentBoxWidth);
                    ctx.stroke_box(parentBounds.left(), parentBounds.top(), parentBounds.right(), parentBounds.bottom());
                    paintLayoutBadge(ctx, parentBounds, layoutDisplayName(parent));
                }
            }
        }

        // Layout-specific drag cue(s) (ghost gap highlight, grid trackers, ...) - see
        // ActiveGeometryDrag's own comment. Empty whenever no drag is active, or this overlay's
        // provider was never set (e.g. an older/simpler test construction).
        if (activeDragCuesProvider_) {
            for (const ActiveGeometryDrag& drag : activeDragCuesProvider_()) {
                if (drag.policy != nullptr) {
                    drag.policy->drawCue(ctx, drag.ctx, drag.result);
                }
            }
        }

        if (clipping) {
            ctx.restore();
        }
    }
}
