#pragma once

#include "PropertyEditor.h"

#include <newui/models.h>
#include <newui/reflection.h>
#include <newui/subview.h>
#include <newui/view.h>

#include <any>
#include <string>
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
        enum class Kind {
            Root, PropertyLeaf, PropertyGroup, PropertyUnsupported, DelegatesHeader, DelegateEntry,
            // A PropertyEditor::EditStyle::SubProperties leaf (e.g. bounds,
            // a Rect) - PropertySubGroup is the group-header row itself
            // (paints like PropertyGroup, but its children are synthetic,
            // not a real nested Class/address()); SubPropertyEntry is one
            // such synthetic child (e.g. "x") - see PropertyEditor::
            // subPropertyNames()/subPropertyValueAsString()'s own comment
            // for why these can't just be real PropertyGroup/PropertyLeaf
            // nodes.
            PropertySubGroup, SubPropertyEntry,
            // A synthetic "Parent" row, injected as the first Root child whenever
            // showsParentPicker() (below) says so - not backed by a newui::reflection::Property at
            // all (View::parent() is deliberately @reflect ignore=true, see its own comment in
            // view.h: a registered Property would make ObjectWriter/ObjectReader walk straight
            // back into the same subtree they're already recursing through to reach this View - a
            // real serialization cycle, not a style choice). ownerInstance is the selected View
            // itself (the same instance the Root node already carries); there is no ownerClass/
            // property to speak of. See PropertiesGrid's own ParentCandidatesProvider/
            // ParentChangeRequestedHandler for how this actually commits a reparent, and
            // showsParentPicker() below for why this only ever appears for a real, attached View
            // selection - a future non-View selection (an Animation, a Component, ...) must never
            // get one.
            ParentPicker,
            Invalid
        };

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
            // Kind::SubPropertyEntry only - property/ownerClass/
            // ownerInstance above still describe the *parent* compound
            // property (e.g. "bounds"), same as they would for its own
            // PropertySubGroup node; this is the synthetic child's own
            // index into PropertyEditor::subPropertyNames().
            std::size_t subPropertyIndex = 0;
            // Kind::PropertySubGroup/SubPropertyEntry only, set for "bounds" specifically once its
            // owning View's real parent Layout affords something other than free pixel
            // positioning (see boundsReadOnlyReason(), PropertiesModel.cpp) - LinearReorder/
            // GridCell/None all mean some or all of bounds is actually computed by that Layout, so
            // committing a directly-typed edit here would either be silently reverted on the next
            // relayout or (LinearReorder/GridCell) never even land where typed. PropertiesGrid
            // refuses to build a live editor at all once this is true; PropertyItem shows
            // readOnlyReason instead of pretending the field is freely editable. Deliberately
            // whole-Rect, not per-axis (e.g. a FlexLayout row still leaves the cross-axis size
            // free in principle) - GeometryEditKind itself has no per-axis granularity today, so
            // this matches that same resolution rather than inventing a finer one.
            bool readOnly = false;
            std::string readOnlyReason;
            // Group nodes only: whether every child is listed (no filter, or this group's own name
            // matched the filter) as opposed to only the children that match it - see setFilter().
            bool showAllChildren = true;
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

        // How the rows are presented - view settings that, unlike the selection, persist across
        // setSelection() so the grid keeps its filter and order as different controls are picked.
        // Each fires onChanged() when it actually changes.
        //
        // setFilter(): only properties whose name contains text (case-insensitive) are listed - a
        // group is kept when its own name matches (then all of its fields show) or when any field
        // inside it does (then only the matching ones show), so "margin" finds layoutParams >
        // leftMargin. Nothing is filtered when text is blank. Sub-property rows (a Rect's x/y/width/
        // height, a flags enum's bits) are matched by name but never filtered out of a kept group.
        void setFilter(const std::string& text);
        const std::string& filter() const { return filter_; }
        // setAlphabetical(): rows are ordered A-Z by name instead of in reflection order - name and
        // bounds first at the top level, the synthetic Parent row and the Delegates group where
        // they always are. Groups are ordered the same way inside.
        void setAlphabetical(bool alphabetical);
        bool alphabetical() const { return alphabetical_; }

        std::size_t childCount(const std::vector<std::size_t>& path) const override;

        // Display text only - matches TreeModel's own generic contract
        // elsewhere (e.g. ToolboxModel::value()). Use nodeAt() below for
        // the real Property*/Class*/instance a PropertyEditor needs.
        std::any value(const std::any& key) override;

        Node nodeAt(const std::vector<std::size_t>& path) const;

    private:
        Node resolveNode(const std::vector<std::size_t>& path) const;
        // Also marks every editable row read-only when the selected view is
        // newui::DesignTimeFlags::ReadOnly (its owning control manages its state).
        Node childOf(const Node& container, std::size_t index) const;
        std::size_t childCountOf(const Node& node) const;

        // Filter/order machinery - see setFilter()/setAlphabetical().
        bool filterActive() const { return !filterLower_.empty(); }
        bool matches(const std::string& name) const;
        bool propertyPasses(const newui::reflection::Property* property,
            const newui::reflection::Class* ownerClass, void* ownerInstance, int depth) const;
        std::vector<const newui::reflection::Property*> listedProperties(
            const newui::reflection::Class* cls, void* instance, bool showAll, bool topLevel) const;
        std::vector<const newui::reflection::Delegate*> listedDelegates(const newui::reflection::Class* cls) const;
        bool parentRowVisible() const;
        bool delegatesHeaderVisible(const newui::reflection::Class* cls) const;
        std::string filter_;
        std::string filterLower_;
        bool alphabetical_ = false;

        static Node classifyProperty(const newui::reflection::Property* property,
            const newui::reflection::Class* ownerClass, void* ownerInstance);

        // Gates the synthetic ParentPicker row (above) to a real newui::View selection that
        // actually has a parent() to reparent *from* - a plain dynamic_cast on the live selected_
        // pointer, not a reflection-class check, so this stays correct without any registry lookup
        // once PropertiesModel eventually supports selecting a non-View object too (an Animation,
        // a Component, ...): those simply aren't View-derived, so this returns false for them with
        // no extra gating needed at the call site. selected_ is already a SubView* (View-derived)
        // for every selection today, so the dynamic_cast is always true right now - it exists for
        // that future case, not this one. The parent() != nullptr half matters today, though: a
        // standalone SubView built in isolation (no addChild() onto anything, e.g. most of this
        // class's own unit test fixtures) has nothing to reparent from - showing the row for one
        // would be misleading (there's no real "current parent" to display or move away from).
        bool showsParentPicker() const {
            const newui::View* view = dynamic_cast<const newui::View*>(selected_);
            return view != nullptr && view->parent() != nullptr;
        }

        Node childOfUnflagged(const Node& container, std::size_t index) const;
        newui::SubView* selected_ = nullptr;
        const newui::reflection::Class* rootClass_ = nullptr;
    };
}
