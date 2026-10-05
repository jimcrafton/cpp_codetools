#pragma once

#include "ExplorerModel.h"

#include <newui/controllers.h>
#include <newui/items.h>

#include <optional>
#include <string>
#include <vector>

namespace CodeToolsVsix
{
    // A row of the project explorer: expand glyph, a tinted icon, the name, then muted detail after it, and
    // any cells (ExplorerNode::cells) in right-aligned columns at the row's right edge, colored by tone.
    // Reads what it shows from the ExplorerTreeModel's node, not from the model's display string.
    class ExplorerItem : public newui::TreeItem
    {
    public:
        static constexpr float kCellWidth = 84.0f;   // one cell column
        static constexpr float kEdgePad = 8.0f;      // between the last column and the row's right edge
        static constexpr float kNameGap = 12.0f;     // between a name and its detail, or the first column

        void paint(BLContext& ctx, const newui::Rect& rect, const std::vector<std::size_t>& path,
                   newui::TreeController& controller) override;
    };

    // The controller that makes ExplorerItems and names each row's icon (explorerIconFor).
    class ExplorerController : public newui::TreeController
    {
    public:
        static constexpr float kRowHeight = 22.0f;
        static constexpr float kIconSize = 16.0f;
        static constexpr float kIconGap = 6.0f;

        newui::TreeItem* createItem(const std::vector<std::size_t>& path) override;
        float itemHeight(std::size_t visibleIndex) const override;

        std::optional<std::string> iconFor(const std::vector<std::size_t>& path) const override;
        float iconSize() const override { return kIconSize; }
        float iconGap() const override { return kIconGap; }
    };
}
