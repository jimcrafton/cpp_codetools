#pragma once

#include "IssuePeek.h"

#include <newui/geometry.h>
#include <newui/popuptool.h>
#include <newui/view.h>

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace CodeToolsVsix
{
    // The Designer's "N problems" status-bar item opens this: a callout with the problems in a short
    // list, the selected one's surrounding source underneath (the error line marked and underlined),
    // `< 1 of N >` to step through them and an Open in editor button. Enter opens the selected problem
    // too, the arrow keys step, Esc closes.
    class IssuesPopup
    {
    public:
        static constexpr std::size_t kMaxVisibleRows = 3;   // more problems than this scroll in the list
        static constexpr float kWidth = 640.0f;
        static constexpr float kRowHeight = 30.0f;
        static constexpr float kPadding = 16.0f;   // between the callout's edge and what's in it
        static constexpr float kGap = 10.0f;
        static constexpr float kHeaderHeight = 30.0f;
        static constexpr float kFooterHeight = 34.0f;
        static constexpr float kNavButtonWidth = 30.0f;
        static constexpr float kOpenButtonWidth = 150.0f;

        // Opens the callout next to anchorScreenRect, inside owner's window. title is what the
        // problems are in (a file name). Choosing Open in editor (or Enter) dismisses the popup, then
        // calls onOpen(index of the selected problem) on the next tick. Returns the popup (it frees
        // itself on dismiss) or nullptr if there is nothing to show or it couldn't be created.
        static newui::PopupTool* show(newui::View& owner, const newui::Rect& anchorScreenRect,
                                      const std::string& title, std::vector<IssuePeek> issues,
                                      std::function<void(std::size_t)> onOpen);
    };
}
