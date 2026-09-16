#include "ViewStyleRegistry.h"

#include <newui/controls.h>

namespace CodeToolsVsix
{
    namespace
    {
        std::vector<ViewStyleOption> genericFallback()
        {
            // ThemedViewStyle itself is abstract (pure virtual partId()/stateId(), viewstyle.h) -
            // ThemedGroupBoxStyle is its simplest concrete, generic-container-shaped subclass
            // (chrome only, no content of its own - controls.h's own GroupBox comment), standing
            // in for "a themed native chrome look" for a plain container that isn't specifically
            // one of the curated owner classes below.
            return {
                { "Plain", [] { return new newui::ViewStyle(); } },
                { "Themed", [] { return new newui::ThemedGroupBoxStyle(); } },
                { "Fluent Card", [] { return new newui::FluentCardStyle(); } },
            };
        }
    }

    std::vector<ViewStyleOption> ViewStyleRegistry::optionsFor(newui::View* owner)
    {
        // Most-derived match wins - a dynamic_cast chain, same spirit as ToolboxRegistry's own
        // isSubViewDerived()/isContainer() checks, checked narrowest-class-first so e.g. a
        // Toggle (which is also a Control, also a View) gets its own curated entry rather than
        // falling through to a more generic one.
        if (dynamic_cast<newui::Button*>(owner) != nullptr) {
            return {
                { "Plain", [] { return new newui::ButtonStyle(); } },
                { "Themed", [] { return new newui::ThemedButtonStyle(); } },
                { "Fluent", [] { return new newui::FluentButtonStyle(); } },
            };
        }
        if (dynamic_cast<newui::Toggle*>(owner) != nullptr) {
            return {
                { "Plain", [] { return new newui::CheckBoxStyle(); } },
                { "Themed", [] { return new newui::ThemedCheckBoxStyle(); } },
                { "Fluent", [] { return new newui::FluentCheckBoxStyle(); } },
            };
        }
        if (dynamic_cast<newui::Label*>(owner) != nullptr) {
            return {
                { "Plain", [] { return new newui::ViewStyle(); } },
                { "Label", [] { return new newui::LabelStyle(); } },
            };
        }
        return genericFallback();
    }
}
