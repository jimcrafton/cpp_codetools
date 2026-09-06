#pragma once

#include "PropertiesModel.h"

#include <newui/controllers.h>
#include <newui/items.h>

#include <vector>

namespace CodeToolsVsix
{
    // PropertiesGrid's own newui::TreeController - createItem() (returns a
    // real PropertyItem, same shape ToolboxController already established
    // for Toolbox - direct construction, not setDefaultItemClassName(),
    // since cpp_codetools classes aren't reflectgen-scanned/registered the
    // way newui's own Item subclasses are, so the reflection-based default
    // TreeController::createItem() can't instantiate one by name) plus the
    // one piece of real, shared, mutable state the resizable key/value
    // divider needs (bluesky/property-grid-design.md): "One shared
    // keyColumnWidth float, owned by whichever class orchestrates the
    // whole grid (the TreeView subclass, or its TreeController/model)".
    // Lives here (not PropertiesGrid.h) so PropertyItem::paint() can read
    // it straight off the newui::TreeController& it's already handed,
    // without PropertyItem depending upward on PropertiesGrid.
    class PropertiesTreeController : public newui::TreeController
    {
    public:
        static constexpr float kDefaultKeyColumnFraction = 0.42f;
        static constexpr float kMinKeyColumnFraction = 0.15f;
        static constexpr float kMaxKeyColumnFraction = 0.85f;

        newui::TreeItem* createItem(const std::vector<std::size_t>& path) override;

        float keyColumnFraction() const { return keyColumnFraction_; }

        // Clamped to [kMinKeyColumnFraction, kMaxKeyColumnFraction]. Fires
        // onDataChanged() (already wired to TreeView::handleDataChanged(),
        // controls.cpp - style().markDirty()/onContentSizeChanged()) if
        // the clamped value actually changed, so a hosting TreeView
        // repaints with the new split through the same real event this
        // controller already fires for any other geometry-affecting
        // change, not a second, new one.
        void setKeyColumnFraction(float fraction);

    private:
        float keyColumnFraction_ = kDefaultKeyColumnFraction;
    };

    // Paint-only newui::TreeItem for the Properties panel's newui::TreeView
    // (PropertiesModel is the backing Model) - replaces PropertyRow's
    // hand-rolled SubView rows (bluesky/property-grid-design.md). Renders
    // every row's *inactive* state only - key label + a plain-text/
    // checkbox-glyph/swatch rendering of the value; the live, editable
    // widget for whichever row is currently selected is a separate, real
    // SubView the orchestrating TreeView positions on top of this Item's
    // own painted value column - not built yet, see the design doc's own
    // build order.
    //
    // A PropertyGroup/DelegatesHeader row (real children per
    // PropertiesModel::childCount()) reuses TreeItem's own real expand/
    // collapse glyph + indent geometry (reimplemented here directly, not
    // inherited - TreeItem::paint() itself is monolithic, see its own
    // header comment for why nothing about it is reusable piecemeal) and
    // paints one full-width label ("name (TypeName)" for a group,
    // "Delegates" for the header) - no key/value split, matching
    // PropertyRow::buildGroupHeader()/buildSectionHeader()'s own
    // single-label shape. Every other row (PropertyLeaf/
    // PropertyUnsupported/DelegateEntry) is a real leaf - no glyph, indent
    // still reserved for alignment (same "leaves stay aligned with
    // siblings" reasoning TreeItem::paint() already documents) - and gets
    // the real key/value split.
    //
    // The key/value column split reads keyColumnFraction from the real
    // newui::TreeController& paint() (and keyRectFor()/valueRectFor()
    // below) are handed - a PropertiesTreeController's own real, shared,
    // drag-adjustable value if that's genuinely what's attached, falling
    // back to kDefaultKeyColumnFraction (matching PropertiesTreeController::
    // kDefaultKeyColumnFraction) for any other newui::TreeController, the
    // same defensive-fallback shape paint()'s own PropertiesModel
    // dynamic_cast already uses.
    class PropertyItem : public newui::TreeItem
    {
    public:
        static constexpr float kDefaultKeyColumnFraction = PropertiesTreeController::kDefaultKeyColumnFraction;
        static constexpr float kRowPadding = 8.0f;
        static constexpr float kSwatchSize = 14.0f;
        static constexpr float kCheckboxSize = 14.0f;

        void paint(BLContext& ctx, const newui::Rect& rect, const std::vector<std::size_t>& path,
            newui::TreeController& controller) override;

        // The same key/value column split paint() uses for a leaf row,
        // exposed so a live editor widget (PropertiesGrid, positioned via
        // newui::TreeView::rectForPath()) lands exactly where this Item's
        // own inactive rendering would have drawn that column - one source
        // of truth for both, rather than two independently-drifting copies
        // of the same indent/glyph/keyColumnFraction math. rowRect is the
        // row's own rect (e.g. straight from rectForPath()) - both leaf
        // rows and (via the same indent math TreeItem::paint() already
        // documents) group rows still reserve identical indent/glyph
        // space, so this is valid for any path, not leaf rows only.
        static newui::Rect keyRectFor(const newui::Rect& rowRect, const std::vector<std::size_t>& path, float keyColumnFraction);
        static newui::Rect valueRectFor(const newui::Rect& rowRect, const std::vector<std::size_t>& path, float keyColumnFraction);
    };
}
