#include "Toolbox.h"

#include <newui/fontmanager.h>
#include <newui/layout.h>
#include <newui/uicolormanager.h>

#include <cctype>
#include <memory>

namespace CodeToolsVsix
{
    std::size_t ToolboxModel::childCount(const std::vector<std::size_t>& path) const
    {
        const auto& categories = ToolboxRegistry::categories();
        if (path.empty()) {
            return categories.size();
        }
        if (path.size() == 1 && path[0] < categories.size()) {
            return categories[path[0]].entries.size();
        }
        return 0;
    }

    std::any ToolboxModel::value(const std::any& key)
    {
        auto path = std::any_cast<std::vector<std::size_t>>(key);
        const auto& categories = ToolboxRegistry::categories();
        if (path.size() == 1 && path[0] < categories.size()) {
            return categories[path[0]].displayName;
        }
        if (path.size() == 2 && path[0] < categories.size() && path[1] < categories[path[0]].entries.size()) {
            return categories[path[0]].entries[path[1]].displayName;
        }
        return std::string();
    }

    namespace
    {
        const ToolboxEntry* entryAtPath(const std::vector<std::size_t>& path)
        {
            const auto& categories = ToolboxRegistry::categories();
            if (path.size() == 2 && path[0] < categories.size() && path[1] < categories[path[0]].entries.size()) {
                return &categories[path[0]].entries[path[1]];
            }
            return nullptr;
        }

        // Same BLFont/glyph-buffer/fill_utf8_text idiom items.cpp's own
        // file-local paintItemText() uses (not exported from there, so
        // reimplemented here rather than reaching into another
        // translation unit's anonymous namespace).
        void paintRowText(BLContext& ctx, const newui::Rect& rect, const std::string& text, BLRgba32 color)
        {
            if (text.empty() || rect.size().width <= 0.0f || rect.size().height <= 0.0f) {
                return;
            }

            newui::Font font = newui::FontManager::getSystemFont(newui::SystemUIFont::Message);
            BLFont* blFont = font.blFont();
            if (blFont == nullptr || !blFont->is_valid()) {
                return;
            }

            const BLFontMetrics& fontMetrics = blFont->metrics();
            double textHeight = fontMetrics.ascent + fontMetrics.descent;
            double y = rect.top() + (rect.size().height - textHeight) * 0.5 + fontMetrics.ascent;

            ctx.save();
            ctx.set_fill_style(color);
            ctx.fill_utf8_text(BLPoint(rect.left(), y), *blFont, text.c_str(), text.size());
            ctx.restore();
        }
    }

    void ToolboxItem::paint(BLContext& ctx, const newui::Rect& rect, const std::vector<std::size_t>& path,
        newui::TreeController& controller)
    {
        bool isHeader = (newui::treeDepthOf(path) == 0);

        // Headers never show the hover/selected flat-fill highlight
        // Item::paint() would otherwise draw - they're section labels,
        // not clickable rows, matching Main.dc.html's own ".tb-cat" (no
        // hover state at all) - forced false here before calling it,
        // overriding whatever TreeView's own generic hover/selection
        // tracking set (it has no idea this path is a "header", nothing
        // here stops a header from genuinely getting hovered/selected).
        // Still has to actually run Item::paint() either way, though,
        // even though the *visual* result is a no-op for a header -
        // clientBounds() below is only ever computed as paint()'s own
        // side effect (see its doc comment, items.h), so skipping the
        // call entirely (as an earlier version of this did) left it
        // permanently stale/zero for every header row - invisible text
        // drawn into a zero-sized rect, not a missing-text bug.
        if (isHeader) {
            setSelected(false);
            setHighlighted(false);
        }
        Item::paint(ctx, rect);

        newui::TreeModel* model = controller.model();
        std::any value = model != nullptr ? model->value(path) : std::any();
        std::string text = value.has_value() ? std::any_cast<std::string>(value) : std::string();

        if (isHeader) {
            for (char& c : text) {
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            }
        }

        // Flat - no tree indent/expand-glyph reserved space (unlike
        // TreeItem's own default paint()) - both header and entry rows
        // share the same left margin, matching Main.dc.html's own
        // ".tb-cat"/".tb-item" (a real hierarchy needing collapse/expand
        // navigation would want the inherited indent; this is a flat,
        // always-expanded category listing instead).
        newui::Rect textRect(clientBounds().left() + 12.0f, clientBounds().top(),
            clientBounds().size().width - 12.0f, clientBounds().size().height);

        BLRgba32 color = isHeader
            ? newui::UIColorManager::colorFor(newui::UIColorRole::DisabledText).toBLRgba32()
            : newui::UIColorManager::colorFor(isSelected() ? newui::UIColorRole::HighlightText : newui::UIColorRole::ControlText).toBLRgba32();

        paintRowText(ctx, textRect, text, color);
    }

    newui::TreeItem* ToolboxController::createItem(const std::vector<std::size_t>& /*path*/)
    {
        return new ToolboxItem();
    }

    float ToolboxController::itemHeight(std::size_t visibleIndex) const
    {
        return newui::treeDepthOf(pathAt(visibleIndex)) == 0 ? kCategoryRowHeight : kEntryRowHeight;
    }

    Toolbox::Toolbox()
    {
        setVisible(true);
        style().setBackgroundColor(newui::UIColorManager::colorFor(newui::UIColorRole::ControlBackground));

        treeView_ = new newui::TreeView();
        treeView_->setName("toolboxTreeView");
        treeView_->setVisible(true);
        treeView_->setController(std::make_unique<ToolboxController>());
        treeView_->setModel(&model_);

        // Every category always expanded on build - the mockup shows
        // them that way, and nothing here needs the collapsed state
        // TreeController otherwise defaults every path to.
        for (std::size_t i = 0; i < ToolboxRegistry::categories().size(); ++i) {
            treeView_->controller().setExpanded({i}, true);
        }

        treeView_->onMouseDblClick.add(this, &Toolbox::handleTreeDblClick);

        // ScrollView::addChild() redirects into its own viewport - not a
        // second, separate wrapping layer, this *is* Toolbox's whole
        // content.
        addChild(treeView_);
    }

    newui::SyncReturn Toolbox::handleTreeDblClick(newui::View& /*sender*/, const newui::Point& /*pt*/,
        std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/)
    {
        auto path = treeView_->selectedPath();
        if (!path) {
            return newui::SyncReturn::Ignored;
        }

        const ToolboxEntry* entry = entryAtPath(*path);
        if (entry == nullptr) {
            return newui::SyncReturn::Ignored;
        }

        newui::SubView* created = entry->factory();
        if (created == nullptr) {
            return newui::SyncReturn::Ignored;
        }

        onEntryActivated(*this, created);
        return newui::SyncReturn::Handled;
    }
}
