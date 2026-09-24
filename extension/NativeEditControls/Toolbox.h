#pragma once

#include "ToolboxRegistry.h"

#include <newui/controls.h>
#include <newui/delegate.h>
#include <newui/dragndrop.h>
#include <newui/items.h>
#include <newui/models.h>

#include <any>
#include <optional>
#include <string>
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
    // ".tb-cat"/".tb-item" typography. Still a fully custom paint()
    // override (not the inherited TreeItem::paint() default) because of
    // that flat-layout difference, but the icon itself is drawn via the
    // same shared newui::Item::paintItemIcon() static (items.h) the default uses,
    // fed by ToolboxController::iconFor() below - not a second,
    // hand-rolled cache.
    class ToolboxItem : public newui::TreeItem
    {
    public:
        void paint(BLContext& ctx, const newui::Rect& rect, const std::vector<std::size_t>& path,
            newui::TreeController& controller) override;
    };

    // Header rows taller than entry rows (Main.dc.html's own ".tb-cat"/
    // ".tb-item" padding) - createItem() returns ToolboxItem instead
    // of the reflection-constructed default TreeItem. iconFor() resolves
    // through the real ToolboxEntry (ToolboxRegistry.h's own
    // iconResourceName) rather than the Model, matching this project's
    // "icon is presentation policy, lives on the Controller" decision -
    // ToolboxItem::paint() calls this directly (it doesn't use TreeItem's
    // own default paint(), which also calls iconFor(), since headers need
    // the flat, no-glyph layout described above).
    class ToolboxController : public newui::TreeController
    {
    public:
        static constexpr float kCategoryRowHeight = 24.0f;
        static constexpr float kEntryRowHeight = 22.0f;
        static constexpr float kIconSize = 15.0f;
        static constexpr float kIconGap = 9.0f;  // Main.dc.html's own ".tb-item { gap: 9px; }"

        newui::TreeItem* createItem(const std::vector<std::size_t>& path) override;
        float itemHeight(std::size_t visibleIndex) const override;

        std::optional<std::string> iconFor(const std::vector<std::size_t>& path) const override;
        float iconSize() const override { return kIconSize; }
        float iconGap() const override { return kIconGap; }
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
    // paint() at all. Real icons (Resources/Images/icons/toolbox/*.svg,
    // extracted from the mockup) are wired via ToolboxController::
    // iconFor() - not every entry has one yet (ToolboxRegistry.cpp's own
    // class-name-to-icon table), those fall back to text-only.
    //
    // Entries can be double-clicked (fires onEntryActivated with a freshly-created, unattached
    // instance) or dragged: the tree is an OLE text drag source whose payload names the entry
    // (dragPayloadFor()); a drop target - DesignerEditor's design surface - turns it back into a
    // fresh instance with createFromDragPayload(). Only entry rows drag, never category headers.
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

        // The text a drag of entry (categoryIndex, entryIndex) carries - a private prefix plus the
        // registry indices, so a drop target can tell it from arbitrary dragged text.
        static std::wstring dragPayloadFor(std::size_t categoryIndex, std::size_t entryIndex);

        // A fresh, unattached instance for a payload from dragPayloadFor(), or nullptr if text
        // isn't one (foreign text, out-of-range indices, a factory that fails). The caller owns
        // the result, same handoff as onEntryActivated.
        static newui::SubView* createFromDragPayload(const std::wstring& text);

        // Whether text is a payload naming a real registry entry - createFromDragPayload()'s
        // validity check without creating anything (for hover feedback, called on every drag move).
        static bool isDragPayload(const std::wstring& text);

    private:
        newui::SyncReturn handleProvideDragText(newui::DropSource& sender, std::wstring& outText);
        newui::SyncReturn handleTreeDblClick(newui::View& sender, const newui::Point& pt,
            std::uint32_t btnMask, std::uint32_t keyMask);

        newui::TreeView* treeView_ = nullptr;
    };
}
