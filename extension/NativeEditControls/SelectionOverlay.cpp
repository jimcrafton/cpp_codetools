#include "SelectionOverlay.h"

#include "Settings.h"

#include <newui/fontmanager.h>
#include <newui/reflection.h>
#include <newui/uicolormanager.h>

namespace CodeToolsVsix
{
    float SelectionOverlay::handleSize()
    {
        return static_cast<float>(Settings::instance().getInt(Settings::kHandleSize, 3, 24));
    }

    namespace
    {

        // Mirrors Main.dc.html's own ".handle" (7x7 white square, 1.5px
        // accent border) - centered on the given point, matching the
        // mockup's corner-of-the-box placement (h-tl/h-tr/h-bl/h-br).
        void paintHandle(BLContext& ctx, float cx, float cy, BLRgba32 borderColor)
        {
            const double size = SelectionOverlay::handleSize();
            BLRect r(cx - size * 0.5, cy - size * 0.5, size, size);
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

            float badgeWidth = static_cast<float>(metrics.advance.x) + SelectionOverlay::kBadgePaddingX * 2.0f;
            float badgeX = parentBounds.left();
            float badgeY = parentBounds.top() - SelectionOverlay::kBadgeHeight - SelectionOverlay::kBadgeGap;

            BLRoundRect badgeRect(badgeX, badgeY, badgeWidth, SelectionOverlay::kBadgeHeight, 4.0);
            ctx.set_fill_style(SelectionOverlay::kLayoutAdornmentColor);
            ctx.fill_round_rect(badgeRect);

            const BLFontMetrics& fontMetrics = blFont->metrics();
            double tx = badgeX + SelectionOverlay::kBadgePaddingX;
            double ty = badgeY + (SelectionOverlay::kBadgeHeight - (fontMetrics.ascent + fontMetrics.descent)) * 0.5 + fontMetrics.ascent;
            ctx.set_fill_style(SelectionOverlay::kLayoutAdornmentTextColor);
            ctx.fill_utf8_text(BLPoint(tx, ty), *blFont, text.c_str(), text.size());
        }

        // The amber box+badge for the selection-time layout adornment (shows which container/
        // Layout the current primary selection belongs to) - the badge names the governing
        // Layout, which is useful context when you're just looking at a selection, not actively
        // dragging.
        void paintContainerHighlight(BLContext& ctx, const newui::Rect& containerBounds, const std::string& label)
        {
            ctx.set_stroke_style(SelectionOverlay::kLayoutAdornmentColor);
            ctx.set_stroke_width(SelectionOverlay::kParentBoxWidth);
            ctx.stroke_box(containerBounds.left(), containerBounds.top(), containerBounds.right(), containerBounds.bottom());
            paintLayoutBadge(ctx, containerBounds, label);
        }

        // The thinner, un-badged emerald box for the drag-time reparent-target highlight - see
        // kReparentTargetColor's own comment for why this is deliberately a separate, simpler
        // treatment from paintContainerHighlight() above.
        void paintReparentTargetHighlight(BLContext& ctx, const newui::Rect& containerBounds)
        {
            ctx.set_stroke_style(SelectionOverlay::kReparentTargetColor);
            ctx.set_stroke_width(SelectionOverlay::kReparentTargetBoxWidth);
            ctx.stroke_box(containerBounds.left(), containerBounds.top(), containerBounds.right(), containerBounds.bottom());
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
        // No early return on an empty selection: a Toolbox drag hovering over the surface paints
        // its cues below with nothing selected. (The selection-only sections just loop over / test
        // an empty selection.)
        const std::vector<newui::SubView*>& selected = controller_.selected();

        BLRgba32 accent = newui::UIColorManager::colorFor(newui::UIColorRole::HighlightBackground).toBLRgba32();

        // See the constructor's own comment - without this, a selection
        // outline/handle paints straight over the Toolbox/Properties
        // chrome around the design surface, since Overlay paints on top
        // of the *entire* hosting RootView pane, not just clipView_'s own
        // area.
        if (clipView_ != nullptr && !clipView_->isVisible()) {
            return;   // the design surface is hidden (Source mode) - nothing of it to mark up
        }
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
                    paintContainerHighlight(ctx, boundsInRootView(parent), layoutDisplayName(parent));
                }
            }
        }

        // Layout-specific drag cue(s) (ghost gap highlight, grid trackers, ...) - see
        // ActiveGeometryDrag's own comment. Empty whenever no drag is active, or this overlay's
        // provider was never set (e.g. an older/simpler test construction).
        if (activeDragCuesProvider_) {
            for (const ActiveGeometryDrag& drag : activeDragCuesProvider_()) {
                if (drag.policy != nullptr) {
                    drag.policy->drawCue(ctx, drag.ctx, drag.result, drag.isReparentTargetCue ? kReparentTargetColor : accent);
                }
            }
        }

        // Container the current drag is poised to reparent its dragged view into, if any - see
        // DesignerEditor::reparentTargets()'s own comment.
        if (reparentTargetProvider_) {
            for (newui::SubView* target : reparentTargetProvider_()) {
                if (target != nullptr) {
                    paintReparentTargetHighlight(ctx, boundsInRootView(target));
                }
            }
        }

        // "A new control would land here" outline for a Toolbox drag over a free-position target.
        if (dropGhostProvider_) {
            if (std::optional<newui::Rect> ghost = dropGhostProvider_()) {
                ctx.set_fill_style(BLRgba32(0x10, 0xB9, 0x81, 0x30));
                ctx.fill_rect(BLRect(ghost->left(), ghost->top(), ghost->size().width, ghost->size().height));
                ctx.set_stroke_style(kReparentTargetColor);
                ctx.set_stroke_width(kReparentTargetBoxWidth);
                ctx.stroke_box(ghost->left(), ghost->top(), ghost->right(), ghost->bottom());
            }
        }

        if (clipping) {
            ctx.restore();
        }
    }
}
