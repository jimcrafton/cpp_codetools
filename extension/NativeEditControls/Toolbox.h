#pragma once

#include "ToolboxRegistry.h"

#include <newui/controls.h>
#include <newui/delegate.h>
#include <newui/items.h>
#include <newui/models.h>

#include <any>
#include <vector>

namespace CodeToolsVsix
{
    // Backs the Toolbox's newui::TreeView (below) - category headers are
    // depth-1 paths, entries are depth-2 - reads straight through to
    // ToolboxRegistry::categories() rather than owning a copy, since that
    // registry is itself a static, built-once list.
    class ToolboxModel : public newui::TreeModel
    {
    public:
        std::size_t childCount(const std::vector<std::size_t>& path) const override;
        std::any value(const std::any& key) override;
    };

    // Category headers (uppercase, muted, no expand glyph/indent, never
    // shows a selection highlight) painted quite differently from
    // TreeItem's own default (which reserves indent+glyph space for real
    // tree navigation - not what a flat, always-expanded category listing
    // needs) - matches bluesky/designer-surface/Main.dc.html's own
    // ".tb-cat"/".tb-item" typography. No icons yet (deferred, per its own
    // Toolbox class comment) - text only.
    class ToolboxItem : public newui::TreeItem
    {
    public:
        void paint(BLContext& ctx, const newui::Rect& rect, const std::vector<std::size_t>& path,
            newui::TreeController& controller) override;
    };

    // Header rows taller than entry rows (Main.dc.html's own ".tb-cat"/
    // ".tb-item" padding) - createItem() returns ToolboxItem instead
    // of the reflection-constructed default TreeItem.
    class ToolboxController : public newui::TreeController
    {
    public:
        static constexpr float kCategoryRowHeight = 24.0f;
        static constexpr float kEntryRowHeight = 22.0f;

        newui::TreeItem* createItem(const std::vector<std::size_t>& path) override;
        float itemHeight(std::size_t visibleIndex) const override;
    };

    // The Toolbox pane (designer-plan.md 6.1 item 1) - a real
    // newui::ScrollView hosting a real newui::TreeView (category headers
    // as always-expanded parent nodes, entries as leaf children), so
    // hover/selection/scrolling all come from that already-tested
    // machinery for free instead of being reimplemented here.
    // ScrollView::addChild() already redirects into its own viewport, and
    // TreeView already answers View::onQueryContentSize/
    // onScrollOffsetChanged (its own "virtualized content" hooks, same
    // mechanism ScrollView's own class comment documents) - so this needs
    // no manual setContentSize() call, and Toolbox itself needs no custom
    // paint() at all. No custom icons for v1 - real icons (porting the
    // mockup's ~20 hand-drawn SVG glyphs to Blend2D) deferred.
    //
    // Real drag-and-drop is out of scope for v1 (newui::dragndrop.h is
    // shaped for OS-level file/text/image drags across the shell
    // boundary, not an in-process "create a new control instance and
    // attach it" gesture) - double-click an entry instead, which fires
    // onEntryActivated with a freshly-created, unattached instance.
    class Toolbox : public newui::ScrollView
    {
    public:
        Toolbox();

        // The caller (Workspace) is responsible for addChild()ing the
        // handed-back instance somewhere real - same raw-pointer
        // ownership handoff as View::addChild() itself; an instance with
        // no listener attaching it leaks.
        newui::Delegate<Toolbox, newui::SubView*> onEntryActivated;

        newui::TreeView* treeView() const { return treeView_; }

    private:
        newui::SyncReturn handleTreeDblClick(newui::View& sender, const newui::Point& pt,
            std::uint32_t btnMask, std::uint32_t keyMask);

        ToolboxModel model_;
        newui::TreeView* treeView_ = nullptr;
    };
}
