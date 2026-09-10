#include "ToolboxRegistry.h"

#include <newui/layout.h>
#include <newui/reflection.h>

#include <memory>
#include <typeindex>
#include <unordered_map>
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

        ToolboxEntry flexLayoutEntry(std::string displayName, newui::Orientation orientation, std::string iconResourceName)
        {
            return ToolboxEntry{
                std::move(displayName),
                [orientation]() -> newui::SubView* {
                    auto* view = new newui::SubView();
                    view->setVisible(true);
                    view->setLayout(std::make_unique<newui::FlexLayout>(orientation));
                    return view;
                },
                std::move(iconResourceName)
            };
        }

        // Maps a real reflected class's own name() to the icon file
        // extracted from bluesky/designer-surface/Main.dc.html (see
        // Resources/Images/icons/toolbox/) - a plain lookup table rather
        // than deriving the filename from the class name mechanically,
        // since a couple of names don't match 1:1 (e.g. "SubView" itself).
        // Absent entries (a real category class this table hasn't been
        // extended for) fall back to no icon at all, same as before this
        // table existed.
        const std::string& toolboxIconFor(const std::string& className)
        {
            static const std::unordered_map<std::string, std::string> table = {
                {"SubView", "Images/icons/toolbox/subview.svg"},
                {"ScrollView", "Images/icons/toolbox/scrollview.svg"},
                {"TabControl", "Images/icons/toolbox/tabcontrol.svg"},
                {"Button", "Images/icons/toolbox/button.svg"},
                {"Toggle", "Images/icons/toolbox/toggle.svg"},
                {"Label", "Images/icons/toolbox/label.svg"},
                {"Image", "Images/icons/toolbox/image.svg"},
                {"Progress", "Images/icons/toolbox/progress.svg"},
                {"Slider", "Images/icons/toolbox/slider.svg"},
                {"Stepper", "Images/icons/toolbox/stepper.svg"},
                {"TextField", "Images/icons/toolbox/textfield.svg"},
                {"TextControl", "Images/icons/toolbox/textcontrol.svg"},
                {"DropDownList", "Images/icons/toolbox/dropdownlist.svg"},
                {"ListView", "Images/icons/toolbox/listview.svg"},
                {"TreeView", "Images/icons/toolbox/treeview.svg"},
                {"ToolbarButton", "Images/icons/toolbox/toolbarbutton.svg"},
                {"ToolbarSeparator", "Images/icons/toolbox/toolbarseparator.svg"},
                {"Toolbar", "Images/icons/toolbox/toolbar.svg"},
                {"MenuBar", "Images/icons/toolbox/menubar.svg"},
            };
            static const std::string empty;
            auto it = table.find(className);
            return it != table.end() ? it->second : empty;
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
                    category.entries.push_back(flexLayoutEntry("FlexLayout (Vertical)", newui::Orientation::Vertical, "Images/icons/toolbox/flexlayout-vertical.svg"));
                    category.entries.push_back(flexLayoutEntry("FlexLayout (Horizontal)", newui::Orientation::Horizontal, "Images/icons/toolbox/flexlayout-horizontal.svg"));
                }

                for (const newui::reflection::Class* clazz : newui::reflection::ReflectionRegistry::classesWithCategory(slug)) {
                    if (!isSubViewDerived(clazz)) {
                        continue;
                    }
                    category.entries.push_back(ToolboxEntry{
                        clazz->name(),
                        [clazz]() { return createInstanceAsSubView(clazz); },
                        toolboxIconFor(clazz->name())
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

    const std::string& ToolboxRegistry::iconResourceNameFor(const std::string& className)
    {
        return toolboxIconFor(className);
    }

    bool ToolboxRegistry::isContainer(newui::SubView* view)
    {
        return view != nullptr && view->layout() != nullptr;
    }
}
