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

        // Resources/-relative path (e.g. "Images/icons/toolbox/button.svg"),
        // resolved via newui::Bundle::resourcePath() - empty if this entry
        // has no icon yet (Toolbox.cpp falls back to text-only for those,
        // same as before every entry had one).
        std::string iconResourceName;
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

        // Real icon file for a reflected class's own name() (e.g.
        // "Button" -> "Images/icons/toolbox/button.svg"), from the same
        // table Toolbox itself uses - "" if this class has no icon yet.
        // Shared with Document Outline, which shows instances of these
        // same classes.
        static const std::string& iconResourceNameFor(const std::string& className);
    };
}
