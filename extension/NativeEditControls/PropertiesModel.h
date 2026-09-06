#pragma once

#include "PropertyEditor.h"

#include <newui/models.h>
#include <newui/reflection.h>
#include <newui/subview.h>

#include <any>
#include <vector>

namespace CodeToolsVsix
{
    // Backs the Properties panel's newui::TreeView (PropertiesModel is the
    // M, PropertyItem the paint-only Item, both replacing PropertiesPanel/
    // PropertyRow's hand-rolled SubView rows - see
    // bluesky/property-grid-design.md for the full design discussion this
    // implements).
    //
    // Properties are flat per selected object
    // (newui::reflection::Class::allProperties()) but genuinely
    // hierarchical for display: a property whose type() resolves to
    // another registered, addressable Class (e.g. bounds() -> Rect,
    // style() -> ViewStyle&) has its own child properties, visible once
    // expanded - the same path-indexed shape ToolboxModel already uses for
    // category->entry nesting, just N levels deep instead of a fixed 2.
    //
    // Real Class::allDelegates() are appended as one further top-level
    // child after every real property - path {propertyCount} is a
    // "Delegates" pseudo-node (Kind::DelegatesHeader) whose own children
    // (path {propertyCount, i}) are the individual Delegate entries
    // (Kind::DelegateEntry) - matches Main.dc.html's own layout (a
    // Delegates section after the property rows), not nested under any
    // property group, and only ever appears at the root level.
    //
    // Whether a property counts as an expandable group (Kind::
    // PropertyGroup) or a plain editable leaf (Kind::PropertyLeaf) is
    // decided exactly the way PropertiesPanel::buildPropertyRows() already
    // decided it: a registered PropertyEditor wins first (a leaf, even if
    // its type also happens to resolve to a registered Class); only a
    // property with no registered editor *and* an addressable, registered
    // nested Class becomes a group. Anything else is Kind::
    // PropertyUnsupported - shown rather than silently omitted, same
    // reasoning as before.
    class PropertiesModel : public newui::TreeModel
    {
    public:
        enum class Kind { Root, PropertyLeaf, PropertyGroup, PropertyUnsupported, DelegatesHeader, DelegateEntry, Invalid };

        // Everything a caller (PropertyItem, the orchestrating TreeView)
        // needs beyond a display string to actually build a PropertyEditor
        // or read a Delegate's listeners - resolved fresh on every call,
        // not cached (same "small counts, cheap enough" reasoning
        // PropertiesPanel's own O(n) rebuildRows() already relied on).
        struct Node
        {
            Kind kind = Kind::Invalid;
            const newui::reflection::Property* property = nullptr;
            const newui::reflection::Class* ownerClass = nullptr;
            void* ownerInstance = nullptr;
            const newui::reflection::Delegate* delegate = nullptr;
        };

        // Re-points at a newly-selected object (nullptr clears to an empty
        // tree) and fires onChanged() so any attached TreeView/
        // TreeController rebuilds its own cached visible-row list. Does
        // not itself touch any TreeController's expand state - a group's
        // expand/collapse stays exactly as the TreeController already
        // tracks it, same "sticky across selection" convention
        // PropertiesPanel::expandedGroups_ used.
        void setSelection(newui::SubView* selected);
        newui::SubView* selected() const { return selected_; }

        std::size_t childCount(const std::vector<std::size_t>& path) const override;

        // Display text only - matches TreeModel's own generic contract
        // elsewhere (e.g. ToolboxModel::value()). Use nodeAt() below for
        // the real Property*/Class*/instance a PropertyEditor needs.
        std::any value(const std::any& key) override;

        Node nodeAt(const std::vector<std::size_t>& path) const;

    private:
        Node resolveNode(const std::vector<std::size_t>& path) const;
        Node childOf(const Node& container, std::size_t index) const;
        std::size_t childCountOf(const Node& node) const;
        static Node classifyProperty(const newui::reflection::Property* property,
            const newui::reflection::Class* ownerClass, void* ownerInstance);

        newui::SubView* selected_ = nullptr;
        const newui::reflection::Class* rootClass_ = nullptr;
    };
}
