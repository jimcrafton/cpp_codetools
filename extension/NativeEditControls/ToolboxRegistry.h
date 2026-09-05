#pragma once

#include <newui/subview.h>

#include <functional>
#include <string>
#include <vector>

namespace CodeToolsVsix
{
    // One draggable-in-spirit (see designer-plan.md 6.1 item 1 - real
    // drag-and-drop is out of scope for v1, double-click appends instead)
    // control type - a display name plus a factory building a fresh,
    // unattached instance the caller addChild()s wherever it wants.
    struct ToolboxEntry
    {
        std::string displayName;
        std::function<newui::SubView*()> factory;
    };

    struct ToolboxCategory
    {
        std::string displayName;
        std::vector<ToolboxEntry> entries;
    };

    // Static registry of designable control types, grouped by category -
    // matches bluesky/designer-surface/Main.dc.html's own Toolbox listing
    // (Containers/Basic/Text & Input/Data/Menu & Toolbar), built from real
    // newui::reflection data (each real class tagged "@reflect
    // category=...") rather than a hand-maintained list, per the user's
    // own proposal this session. MenuItem is deliberately not included -
    // the mockup itself flags it as needing to be dropped onto an
    // existing MenuBar/submenu specifically, which doesn't fit "double-
    // click appends to the design surface's own root" at all.
    class ToolboxRegistry
    {
    public:
        static const std::vector<ToolboxCategory>& categories();
    };
}
