#include "ToolboxRegistry.h"

#include <newui/layout.h>
#include <newui/reflection.h>

#include <memory>
#include <typeindex>
#include <utility>

namespace CodeToolsVsix
{
    namespace
    {
        // clazz->createInstance(&raw)'s raw is only ever safe to
        // static_cast<SubView*> when clazz genuinely, statically derives
        // from SubView via real (single, non-virtual) C++ inheritance -
        // confirmed by walking parentClass(), same idiom
        // ComponentEditorRegistry::createEditor() already uses
        // (ComponentEditor.cpp). Not the same situation as the
        // FrameProxy/RootViewProxy read-side proxy-substitution case found
        // unsafe earlier this session - those are deliberately *not* real
        // subclasses of what they stand in for, which is exactly why that
        // cast would have been unsafe; every class reaching this function
        // is a real SubView subclass by construction (checked below).
        bool isSubViewDerived(const newui::reflection::Class* clazz)
        {
            const newui::reflection::Class* subViewClass =
                newui::reflection::ReflectionRegistry::getClass(std::type_index(typeid(newui::SubView)));
            for (const newui::reflection::Class* c = clazz; c != nullptr; c = c->parentClass()) {
                if (c == subViewClass) {
                    return true;
                }
            }
            return false;
        }

        newui::SubView* createInstanceAsSubView(const newui::reflection::Class* clazz)
        {
            void* raw = nullptr;
            clazz->createInstance(&raw);
            return static_cast<newui::SubView*>(raw);
        }

        ToolboxEntry flexLayoutEntry(std::string displayName, newui::Orientation orientation)
        {
            return ToolboxEntry{
                std::move(displayName),
                [orientation]() -> newui::SubView* {
                    auto* view = new newui::SubView();
                    view->setVisible(true);
                    view->setLayout(std::make_unique<newui::FlexLayout>(orientation));
                    return view;
                }
            };
        }

        // Display order/labels matching Main.dc.html's own Toolbox exactly -
        // the slugs are what "@reflect category=..." annotations actually
        // carry (the shared annotation regex only allows identifier-ish
        // characters, no spaces/"&" - see reflectgen.py's
        // REFLECT_ANNOTATION_PAIR_RE), so this is the one place that maps
        // a slug to what the user actually sees.
        const std::vector<std::pair<std::string, std::string>>& categoryDisplayOrder()
        {
            static const std::vector<std::pair<std::string, std::string>> order = {
                {"containers", "Containers"},
                {"basic", "Basic"},
                {"textinput", "Text & Input"},
                {"data", "Data"},
                {"menutoolbar", "Menu & Toolbar"},
            };
            return order;
        }
    }

    const std::vector<ToolboxCategory>& ToolboxRegistry::categories()
    {
        static const std::vector<ToolboxCategory> result = [] {
            std::vector<ToolboxCategory> built;

            for (const auto& [slug, displayLabel] : categoryDisplayOrder()) {
                ToolboxCategory category{displayLabel, {}};

                if (slug == "containers") {
                    // Two composite entries a reflection scan can't
                    // produce on its own - "a SubView with a specific
                    // FlexLayout orientation attached" isn't a distinct
                    // registered type (see designer-plan.md 6.1 item 1).
                    category.entries.push_back(flexLayoutEntry("FlexLayout (Vertical)", newui::Orientation::Vertical));
                    category.entries.push_back(flexLayoutEntry("FlexLayout (Horizontal)", newui::Orientation::Horizontal));
                }

                for (const newui::reflection::Class* clazz : newui::reflection::ReflectionRegistry::classesWithCategory(slug)) {
                    if (!isSubViewDerived(clazz)) {
                        continue;
                    }
                    category.entries.push_back(ToolboxEntry{
                        clazz->name(),
                        [clazz]() { return createInstanceAsSubView(clazz); }
                    });
                }

                if (!category.entries.empty()) {
                    built.push_back(std::move(category));
                }
            }

            return built;
        }();
        return result;
    }
}
