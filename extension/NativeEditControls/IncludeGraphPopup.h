#pragma once

#include <newui/geometry.h>
#include <newui/popuptool.h>
#include <newui/view.h>

#include <functional>
#include <memory>
#include <string>

namespace cpptools { class IncludeGraph; }

namespace CodeToolsVsix
{
    // The Includes tab's "Open include graph": a callout with the files around one file, who includes it on the
    // left and what it includes on the right (IncludeGraphLayout). A click on a box centres the graph on that file,
    // a double-click opens it, Esc closes.
    class IncludeGraphPopup
    {
    public:
        static constexpr float kWidth = 940.0f;
        static constexpr float kPadding = 16.0f;
        static constexpr float kGap = 8.0f;
        static constexpr float kHeaderHeight = 28.0f;
        static constexpr float kFooterHeight = 22.0f;
        static constexpr float kBoxWidth = 148.0f;
        static constexpr float kBoxHeight = 28.0f;
        static constexpr float kRowGap = 8.0f;
        static constexpr float kColumnGap = 46.0f;
        static constexpr std::size_t kMostRows = 9;   // the most boxes in a column, "+N more" included

        // Opens the callout next to anchorScreenRect with the graph around `focus`. Opening a file dismisses the
        // popup, then calls onOpen(path) on the next tick. Returns the popup (it frees itself on dismiss), or nullptr
        // if the file is not in the graph or the popup could not be created.
        static newui::PopupTool* show(newui::View& owner, const newui::Rect& anchorScreenRect,
                                      std::shared_ptr<const cpptools::IncludeGraph> graph, const std::string& focus,
                                      std::function<void(const std::string&)> onOpen);
    };
}
