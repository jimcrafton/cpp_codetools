#pragma once

#include <newui/geometry.h>
#include <newui/popuptool.h>
#include <newui/view.h>

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace CodeToolsVsix
{
    // A callout listing one line of text per problem (the Designer's "N problems" status-bar item
    // opens it) in a scrolling ListView. Clicking a row dismisses the popup, then calls
    // onChosen(rowIndex) on the next tick.
    class IssuesPopup
    {
    public:
        static constexpr std::size_t kMaxVisibleRows = 10;   // more rows than this scroll
        static constexpr float kWidth = 520.0f;
        static constexpr float kRowHeight = 32.0f;
        static constexpr float kPadding = 16.0f;   // between the callout's edge and the list

        // Opens the callout next to anchorScreenRect, inside owner's window. Returns the popup (it
        // frees itself on dismiss) or nullptr if it couldn't be created.
        static newui::PopupTool* show(newui::View& owner, const newui::Rect& anchorScreenRect,
                                      const std::vector<std::string>& rows,
                                      std::function<void(std::size_t)> onChosen);
    };
}
