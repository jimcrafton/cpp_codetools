#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <newui/reflection.h>
#include <newui/view.h>

namespace CodeToolsVsix
{
    // Design-time editor for one live newui::View - governs the design
    // surface's context menu (verbs) and double-click default action
    // (edit()). See bluesky/designer-plan.md §4.2 (same placement
    // reasoning as PropertyEditor.h - design-time-only, single consumer,
    // deliberately not in newui).
    class ComponentEditor
    {
    public:
        explicit ComponentEditor(newui::View* view) : view_(view) {}
        virtual ~ComponentEditor() = default;

        virtual std::size_t verbCount() const { return 0; }
        virtual std::string verb(std::size_t index) const { return {}; }
        virtual void executeVerb(std::size_t index) {}
        virtual void edit() {}  // double-click default - see §7 step 3's scope note

        newui::View* view() const { return view_; }

    protected:
        newui::View* view_;
    };

    // Keyed on const reflection::Class* alone - no generic/wildcard
    // ComponentEditor exists the way PropertyEditor has generic bool/int/
    // float/string editors, since there's no meaningful "default" verb set
    // for an arbitrary View. createEditor() walks owningClass's own
    // parentClass() chain (most-derived first) for the first exact match,
    // same order Class::allProperties() already uses - a control with no
    // editor of its own can inherit its base class's.
    class ComponentEditorRegistry
    {
    public:
        using Factory = std::function<std::unique_ptr<ComponentEditor>(newui::View*)>;

        static ComponentEditorRegistry& instance();

        void registerEditor(const newui::reflection::Class* owningClass, Factory factory);

        // nullptr if no class in owningClass's parentClass() chain has a
        // registered editor.
        std::unique_ptr<ComponentEditor> createEditor(const newui::reflection::Class* owningClass,
                                                        newui::View* view) const;

    private:
        std::vector<std::pair<const newui::reflection::Class*, Factory>> entries_;
    };
}
