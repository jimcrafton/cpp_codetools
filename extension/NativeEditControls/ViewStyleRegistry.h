#pragma once

#include <newui/view.h>
#include <newui/viewstyle.h>

#include <functional>
#include <string>
#include <vector>

namespace CodeToolsVsix
{
    // One candidate ViewStyle subclass offered by the Layout/ViewStyle "type swap" popup
    // (ViewStylePropertyEditor, PropertyEditor.h) - a display name plus a factory returning a
    // fresh, unattached instance (ownership passes straight to Property::set()'s PtrSetter, same
    // shape ClassBuilder::createInstance() already hands a Reader).
    struct ViewStyleOption
    {
        std::string displayName;
        std::function<newui::ViewStyle*()> factory;
    };

    // Curated, hand-maintained table of which concrete newui::ViewStyle subclasses are safe to
    // offer on a given owner View - deliberately NOT a scan of every registered ViewStyle
    // subclass (of ~40, most - ThemedTrackbarThumbStyle, ThemedScrollbarArrowStyle,
    // ThemedTabItemStyle, ... - are internal sub-part styles a specific composite control's own
    // paint()/layout() code assumes via an internal cast; installing one on an arbitrary selected
    // View is a real crash risk, not just a cosmetic mismatch). Same spirit as ToolboxRegistry's
    // own hand-maintained category table.
    class ViewStyleRegistry
    {
    public:
        // Keyed by owner's real runtime class (dynamic_cast chain, most-derived match wins) -
        // never owner's declared/static type. Always returns at least the generic fallback
        // ({ViewStyle, ThemedViewStyle, FluentCardStyle}) for a plain container/unrecognized
        // owner; never empty.
        static std::vector<ViewStyleOption> optionsFor(newui::View* owner);
    };
}
