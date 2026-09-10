#include "DocumentOutline.h"
#include "ToolboxRegistry.h"

#include <newui/fontmanager.h>
#include <newui/layout.h>
#include <newui/reflection.h>
#include <newui/uicolormanager.h>

#include <set>
#include <typeindex>

namespace CodeToolsVsix
{
    void DocumentOutlineModel::setSource(ViewDesignerModel* source)
    {
        source_ = source;
        if (source_ != nullptr) {
            source_->onChanged.add(this, &DocumentOutlineModel::handleSourceChanged);
        }
        onChanged(*this);
    }

    newui::SyncReturn DocumentOutlineModel::handleSourceChanged(newui::Model& /*sender*/)
    {
        onChanged(*this);
        return newui::SyncReturn::Ignored;
    }

    std::size_t DocumentOutlineModel::childCount(const std::vector<std::size_t>& path) const
    {
        return source_ != nullptr ? source_->childCount(path) : 0;
    }

    std::any DocumentOutlineModel::value(const std::any& key)
    {
        auto path = std::any_cast<std::vector<std::size_t>>(key);
        newui::SubView* view = source_ != nullptr ? source_->viewAt(path) : nullptr;
        if (view == nullptr) {
            return std::string();
        }

        std::string name = view->name();
        const newui::reflection::Class* clazz = newui::reflection::classinfo(typeid(*view));
        std::string typeName = clazz != nullptr ? clazz->name() : std::string("?");
        return name.empty() ? typeName : name + " (" + typeName + ")";
    }

    namespace
    {
        // Same emerald "valid drop target" color as the canvas' own reparent-target highlight
        // (SelectionOverlay.cpp's kReparentTargetColor) - kept as its own local copy rather than
        // shared, matching this project's usual "duplicate a small color constant per file"
        // precedent (e.g. LayoutEditingPolicy.cpp's own grid-tracker gray) over a cross-file
        // dependency for one RGB value.
        const BLRgba32 kDropTargetColor(0x10, 0xB9, 0x81, 0xFF);

        // Same priority (disabled beats selected beats normal) items.cpp's
        // own file-local itemTextColor() uses - reimplemented here (not
        // exported from there), same as ToolboxItem's/PropertyItem's own
        // local copies.
        BLRgba32 rowTextColor(const newui::Item& item)
        {
            newui::UIColorRole role = !item.isEnabled() ? newui::UIColorRole::DisabledText
                : item.isSelected() ? newui::UIColorRole::HighlightText
                : newui::UIColorRole::ControlText;
            return newui::UIColorManager::colorFor(role).toBLRgba32();
        }

        // The type suffix's own dimmer shade (Main.dc.html's own
        // ".tree-row .type" color) - same priority as rowTextColor()
        // otherwise, matching PropertyItem.cpp's own dimTextColor().
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

        // Same binary-search-the-longest-fit truncation PropertyItem.cpp's
        // own truncateWithEllipsis() uses - reimplemented here, not
        // exported from there.
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

        // Draws text left-aligned/vertically centered in rect, clipped and
        // ellipsis-truncated to fit - same idiom PropertyItem.cpp's own
        // paintText() uses, reimplemented here (not exported from there
        // either). Returns the actual on-screen width consumed, so the
        // caller can place a second run (the type suffix) right after
        // this one - PropertyItem's own version never needed this since
        // its "name (Type)" is always one single string.
        double paintText(BLContext& ctx, const newui::Rect& rect, const std::string& text, BLRgba32 color,
            newui::SystemUIFont fontRole)
        {
            if (text.empty() || rect.size().width <= 0.0f || rect.size().height <= 0.0f) {
                return 0.0;
            }

            newui::Font font = newui::FontManager::getSystemFont(fontRole);
            BLFont* blFont = font.blFont();
            if (blFont == nullptr || !blFont->is_valid()) {
                return 0.0;
            }

            std::string display = truncateWithEllipsis(*blFont, text, rect.size().width);
            if (display.empty()) {
                return 0.0;
            }

            const BLFontMetrics& fontMetrics = blFont->metrics();
            double textHeight = fontMetrics.ascent + fontMetrics.descent;
            double y = rect.top() + (double(rect.size().height) - textHeight) * 0.5 + fontMetrics.ascent;

            ctx.save();
            ctx.clip_to_rect(BLRect(rect.left(), rect.top(), rect.size().width, rect.size().height));
            ctx.set_fill_style(color);
            ctx.fill_utf8_text(BLPoint(rect.left(), y), *blFont, display.c_str(), display.size());
            ctx.restore();

            return measureTextWidth(*blFont, display);
        }

        // Same hand-drawn triangle items.cpp's own file-local
        // paintExpandGlyph() uses - reimplemented here, not exported (same
        // as PropertyItem.cpp's own copy).
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
    }

    void DocumentOutlineItem::paint(BLContext& ctx, const newui::Rect& rect, const std::vector<std::size_t>& path,
        newui::TreeController& controller)
    {
        Item::paint(ctx, rect);

        if (auto* outlineController = dynamic_cast<DocumentOutlineController*>(&controller)) {
            if (outlineController->isPendingDropTarget(path)) {
                ctx.set_stroke_style(kDropTargetColor);
                ctx.set_stroke_width(1.0);
                ctx.stroke_rect(BLRect(rect.left(), rect.top(), rect.size().width, rect.size().height));
            }
        }

        auto* model = dynamic_cast<DocumentOutlineModel*>(controller.model());
        newui::SubView* view = model != nullptr && model->source() != nullptr ? model->source()->viewAt(path) : nullptr;

        bool hasChildren = model != nullptr && model->hasChildren(path);
        double indent = double(newui::treeDepthOf(path)) * newui::kTreeIndentWidth;
        double glyphCenterX = clientBounds().left() + indent + newui::kTreeGlyphWidth * 0.5;
        double glyphCenterY = clientBounds().top() + clientBounds().size().height * 0.5;
        if (hasChildren) {
            paintExpandGlyph(ctx, glyphCenterX, glyphCenterY, newui::kTreeGlyphWidth * 0.8,
                controller.isExpanded(path), rowTextColor(*this));
        }

        if (view == nullptr) {
            return;
        }

        double textLeft = clientBounds().left() + indent + newui::kTreeGlyphWidth;
        textLeft += newui::Item::paintItemIcon(ctx, textLeft, glyphCenterY,
            controller.iconFor(path), controller.iconSize(), controller.iconGap());

        newui::Rect textRect(float(textLeft), clientBounds().top(),
            clientBounds().size().width - float(textLeft - clientBounds().left()), clientBounds().size().height);

        std::string name = view->name();
        if (name.empty()) {
            name = "(unnamed)";
        }
        const newui::reflection::Class* clazz = newui::reflection::classinfo(typeid(*view));
        std::string typeName = clazz != nullptr ? clazz->name() : std::string("?");

        double nameWidth = paintText(ctx, textRect, name, rowTextColor(*this), newui::SystemUIFont::Message);

        // Matches Main.dc.html's own ".tree-row .type { margin-left: 4px }".
        constexpr double kTypeSuffixMargin = 4.0;
        float typeLeft = float(textRect.left() + nameWidth + kTypeSuffixMargin);
        float typeWidth = textRect.size().width - float(nameWidth + kTypeSuffixMargin);
        if (typeWidth > 0.0f) {
            newui::Rect typeRect(typeLeft, textRect.top(), typeWidth, textRect.size().height);
            paintText(ctx, typeRect, typeName, dimTextColor(*this), newui::SystemUIFont::Status);
        }
    }

    newui::TreeItem* DocumentOutlineController::createItem(const std::vector<std::size_t>& /*path*/)
    {
        return new DocumentOutlineItem();
    }

    std::optional<std::string> DocumentOutlineController::iconFor(const std::vector<std::size_t>& path) const
    {
        const auto* docModel = dynamic_cast<const DocumentOutlineModel*>(model());
        newui::SubView* view = docModel != nullptr && docModel->source() != nullptr
            ? docModel->source()->viewAt(path) : nullptr;
        if (view == nullptr) {
            return std::nullopt;
        }

        const newui::reflection::Class* clazz = newui::reflection::classinfo(typeid(*view));
        if (clazz == nullptr) {
            return std::nullopt;
        }

        const std::string& icon = ToolboxRegistry::iconResourceNameFor(clazz->name());
        return icon.empty() ? std::nullopt : std::optional<std::string>(icon);
    }

    DocumentOutline::DocumentOutline()
    {
        setVisible(true);
        style().setBackgroundColor(newui::UIColorManager::colorFor(newui::UIColorRole::ControlBackground));

        treeView_ = new newui::TreeView();
        treeView_->setName("documentOutlineTreeView");
        treeView_->setVisible(true);
        auto controller = std::make_unique<DocumentOutlineController>();
        outlineController_ = controller.get();
        treeView_->setController(std::move(controller));
        treeView_->setModel(&model_);
        treeView_->onSelectionChanged.add(this, &DocumentOutline::handleTreeSelectionChanged);

        // A second, independent set of listeners alongside TreeView's own private mouse
        // handling (row selection, expand-glyph clicks) - Delegate<> already supports multiple
        // listeners on the same event without interference, the same pattern DesignerEditor's
        // own CanvasWell-resize + canvas Move drag already establishes on root's onMouseMove/
        // onMouseUp.
        treeView_->onMouseDown.add(this, &DocumentOutline::handleTreeMouseDown);
        treeView_->onMouseMove.add(this, &DocumentOutline::handleTreeMouseMove);
        treeView_->onMouseUp.add(this, &DocumentOutline::handleTreeMouseUp);

        // ScrollView::addChild() redirects into its own viewport - not a
        // second, separate wrapping layer, this *is* DocumentOutline's
        // whole content (same shape as Toolbox's own constructor).
        addChild(treeView_);
    }

    void DocumentOutline::setViewDesignerModel(ViewDesignerModel* model)
    {
        model_.setSource(model);
        // Root's own direct children start expanded - matches
        // Main.dc.html's own outline (fileMenu/textControl_/
        // outlineControl_ visible with no click needed); deeper nesting
        // starts collapsed like any other real tree, same TreeController
        // default every other consumer here gets.
        treeView_->controller().setExpanded({0}, true);
    }

    void DocumentOutline::refresh()
    {
        if (model_.source() != nullptr) {
            model_.source()->refresh();
        }
    }

    void DocumentOutline::setSelection(const std::vector<newui::SubView*>& views)
    {
        if (model_.source() == nullptr) {
            return;
        }

        std::set<std::vector<std::size_t>> targetPaths;
        for (newui::SubView* view : views) {
            if (auto path = model_.source()->pathFor(view)) {
                targetPaths.insert(*path);
            }
        }

        if (targetPaths == treeView_->selectedPaths()) {
            // Already in sync - this is what actually breaks the
            // DesignerEditor <-> Outline notification cycle (see this
            // class's own header comment) - no TreeView mutation means no
            // onSelectionChanged fires, so there's nothing to
            // re-forward.
            return;
        }

        applyingExternalSelection_ = true;
        treeView_->clearSelection();
        for (const auto& path : targetPaths) {
            expandAncestorsOf(path);
            treeView_->addToSelection(path);
        }
        applyingExternalSelection_ = false;
    }

    void DocumentOutline::expandAncestorsOf(const std::vector<std::size_t>& path)
    {
        std::vector<std::size_t> prefix;
        for (std::size_t i = 0; i + 1 < path.size(); ++i) {
            prefix.push_back(path[i]);
            treeView_->controller().setExpanded(prefix, true);
        }
    }

    std::optional<std::vector<std::size_t>> DocumentOutline::rowPathAt(const newui::Point& localPt) const
    {
        newui::TreeController& controller = treeView_->controller();
        std::size_t count = controller.visibleCount();
        for (std::size_t i = 0; i < count; ++i) {
            const std::vector<std::size_t>& path = controller.pathAt(i);
            if (auto rect = treeView_->rectForPath(path)) {
                if (rect->contains(localPt)) {
                    return path;
                }
            }
        }
        return std::nullopt;
    }

    newui::SyncReturn DocumentOutline::handleTreeMouseDown(newui::View& /*sender*/, const newui::Point& pt,
        std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/)
    {
        draggedView_ = nullptr;
        dragStarted_ = false;
        if (model_.source() == nullptr) {
            return newui::SyncReturn::Ignored;
        }
        if (auto path = rowPathAt(pt)) {
            draggedView_ = model_.source()->viewAt(*path);
            dragStartPt_ = pt;
        }
        // Never claims the event - TreeView's own row-selection handling (a separate listener
        // on this same onMouseDown) still needs to run regardless.
        return newui::SyncReturn::Ignored;
    }

    newui::SyncReturn DocumentOutline::handleTreeMouseMove(newui::View& /*sender*/, const newui::Point& pt,
        std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/)
    {
        if (draggedView_ == nullptr) {
            return newui::SyncReturn::Ignored;
        }
        if (!dragStarted_) {
            newui::Point delta = pt - dragStartPt_;
            if (delta.x * delta.x + delta.y * delta.y < kDragThresholdPixels * kDragThresholdPixels) {
                return newui::SyncReturn::Ignored;
            }
            dragStarted_ = true;
        }

        newui::SubView* candidate = nullptr;
        if (auto path = rowPathAt(pt)) {
            if (newui::SubView* hit = model_.source() != nullptr ? model_.source()->viewAt(*path) : nullptr) {
                bool isSelfOrDescendant = false;
                for (newui::View* v = hit; v != nullptr; v = v->parent()) {
                    if (v == draggedView_) {
                        isSelfOrDescendant = true;
                        break;
                    }
                }
                if (!isSelfOrDescendant && hit != draggedView_->parent() && ToolboxRegistry::isContainer(hit)) {
                    candidate = hit;
                }
            }
        }

        outlineController_->setPendingDropTargetPath(
            candidate != nullptr ? model_.source()->pathFor(candidate) : std::nullopt);
        treeView_->redraw();
        return newui::SyncReturn::Ignored;
    }

    newui::SyncReturn DocumentOutline::handleTreeMouseUp(newui::View& /*sender*/, const newui::Point& /*pt*/,
        std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/)
    {
        newui::SubView* dragged = draggedView_;
        bool started = dragStarted_;
        draggedView_ = nullptr;
        dragStarted_ = false;

        std::optional<std::vector<std::size_t>> targetPath = outlineController_->pendingDropTargetPath();
        outlineController_->setPendingDropTargetPath(std::nullopt);
        treeView_->redraw();

        if (!started || dragged == nullptr || !targetPath.has_value() || model_.source() == nullptr) {
            return newui::SyncReturn::Ignored;
        }
        newui::SubView* target = model_.source()->viewAt(*targetPath);
        if (target == nullptr) {
            return newui::SyncReturn::Ignored;
        }
        onReparentRequested(*this, dragged, target);
        return newui::SyncReturn::Handled;
    }

    newui::SyncReturn DocumentOutline::handleTreeSelectionChanged(newui::TreeView& sender)
    {
        if (applyingExternalSelection_ || model_.source() == nullptr) {
            return newui::SyncReturn::Ignored;
        }

        std::vector<newui::SubView*> views;
        for (const auto& path : sender.selectedPaths()) {
            if (newui::SubView* view = model_.source()->viewAt(path)) {
                views.push_back(view);
            }
        }
        onSelectionActivated(*this, views);
        return newui::SyncReturn::Ignored;
    }
}
