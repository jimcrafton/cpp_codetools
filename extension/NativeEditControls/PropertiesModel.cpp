#include "PropertiesModel.h"
#include "LayoutEditingPolicy.h"

namespace CodeToolsVsix
{
    using newui::reflection::Class;
    using newui::reflection::classinfo;
    using newui::reflection::Delegate;
    using newui::reflection::Property;

    namespace
    {
        // The properties the grid lists for cls - allProperties() minus collections (childViews):
        // a collection has no editor, and the tree it holds is edited on the canvas and in the
        // Outline. It stays a real, serialized property; this only hides it from the grid.
        void gridProperties(const Class* cls, std::vector<const Property*>& out)
        {
            std::vector<const Property*> all;
            cls->allProperties(all);
            for (const Property* property : all) {
                if (!property->isCollection()) {
                    out.push_back(property);
                }
            }
        }

        // Gates PropertiesModel::Node::readOnly for "bounds" specifically - the only Rect-typed
        // registered property (see RectPropertyEditor's own commitValue() override comment,
        // PropertyEditor.h), so ownerInstance here is always a real View* at runtime once
        // property->name() == "bounds" is confirmed. Only ever called from childOf()'s Root case
        // (below), never from the generic classifyProperty() a nested PropertyGroup also uses -
        // that keeps this safe without needing its own type check: a nested class's own instance
        // address (e.g. a ViewStyle) is never what's passed in here, only the selected View
        // itself. Mirrors DesignerEditor's own Move-drag arming check exactly (consult
        // policyFor(parent's real Layout), not a name/type guess) - FreePosition means bounds is
        // genuinely free to edit; anything else means the parent Layout itself computes some or
        // all of it, so a direct edit here would be silently lost (LinearReorder/GridCell) or
        // reverted (a stale AnchorLayoutParams case aside, which RectPropertyEditor's own commit
        // override handles separately) on the very next relayout.
        bool boundsReadOnlyReason(void* ownerInstance, const Property* property, std::string& reason)
        {
            if (property == nullptr || property->name() != "bounds") {
                return false;
            }
            auto* view = static_cast<newui::View*>(ownerInstance);
            newui::View* parent = view != nullptr ? view->parent() : nullptr;
            if (parent == nullptr) {
                return false;
            }
            if (policyFor(parent->layout()).kind() == GeometryEditKind::FreePosition) {
                return false;
            }
            const Class* layoutClass = parent->layout() != nullptr ? classinfo(typeid(*parent->layout())) : nullptr;
            reason = "Controlled by " + (layoutClass != nullptr ? layoutClass->name() : std::string("the parent's layout"));
            return true;
        }
    }

    void PropertiesModel::setSelection(newui::SubView* selected)
    {
        selected_ = selected;
        rootClass_ = selected_ != nullptr ? classinfo(typeid(*selected_)) : nullptr;
        onChanged(*this);
    }

    PropertiesModel::Node PropertiesModel::classifyProperty(const Property* property,
        const Class* ownerClass, void* ownerInstance)
    {
        Node node;
        node.property = property;
        node.ownerClass = ownerClass;
        node.ownerInstance = ownerInstance;

        auto editor = PropertyEditorRegistry::instance().createEditor(property, ownerClass, ownerInstance);

        // getClass(), not classinfo(type()) - a property whose declared
        // type is a polymorphic base with no data of its own (Layout/
        // LayoutParams) resolves to the real, concrete runtime subclass
        // instead (FlexLayout/AnchorLayoutParams/...), which is what
        // actually has properties worth expanding into. Harmless/
        // identical to classinfo(type()) for every non-polymorphic
        // property (ViewStyle, etc.) - see newui::reflection::Property::
        // getClass()'s own comment.
        const Class* nested = property->getClass(ownerInstance);
        bool isNestedGroup = nested != nullptr && property->isAddressable();

        // A Layout/ViewStyle-shaped property (LayoutPropertyEditor/ViewStylePropertyEditor,
        // PropertyEditor.h) is BOTH a registered EditStyle::Dialog editor (a "swap the concrete
        // subclass" type-swap popup) AND an addressable nested Class with real properties worth
        // expanding into (e.g. style -> font) - unlike every other Dialog editor (Color/Gradient/
        // FilePath, none of which are ever addressable nested classes), drilldown wins here: the
        // group still expands exactly as before (preserves the existing, tested "expand style to
        // edit its own current fields" UX), and PropertyItem's own group-header paint() attaches
        // the type-swap popup's "..." affordance to the header row instead of replacing the whole
        // row with a leaf - see that file's own comment.
        bool isTypeSwapGroup = editor != nullptr
            && editor->editStyle() == PropertyEditor::EditStyle::Dialog && isNestedGroup;

        if (editor != nullptr && !isTypeSwapGroup) {
            node.kind = editor->editStyle() == PropertyEditor::EditStyle::SubProperties
                ? Kind::PropertySubGroup : Kind::PropertyLeaf;
            return node;
        }

        if (isNestedGroup) {
            node.kind = Kind::PropertyGroup;
            return node;
        }

        node.kind = Kind::PropertyUnsupported;
        return node;
    }

    PropertiesModel::Node PropertiesModel::childOf(const Node& container, std::size_t index) const
    {
        Node node = childOfUnflagged(container, index);
        const bool editable = node.kind == Kind::PropertyLeaf || node.kind == Kind::PropertyGroup
            || node.kind == Kind::PropertySubGroup || node.kind == Kind::SubPropertyEntry;
        if (editable && !node.readOnly && selected_ != nullptr
            && selected_->hasDesignTimeFlag(newui::DesignTimeFlags::ReadOnly)) {
            node.readOnly = true;
            node.readOnlyReason = "Managed by its owning control";
        }
        return node;
    }

    PropertiesModel::Node PropertiesModel::childOfUnflagged(const Node& container, std::size_t index) const
    {
        switch (container.kind) {
        case Kind::Root: {
            std::size_t parentRow = showsParentPicker() ? 1 : 0;
            if (parentRow != 0 && index == 0) {
                Node node;
                node.kind = Kind::ParentPicker;
                node.ownerClass = container.ownerClass;
                node.ownerInstance = container.ownerInstance;
                return node;
            }
            std::size_t propertyIndex = index - parentRow;

            std::vector<const Property*> properties;
            gridProperties(container.ownerClass, properties);
            if (propertyIndex < properties.size()) {
                Node node = classifyProperty(properties[propertyIndex], container.ownerClass, container.ownerInstance);
                std::string reason;
                if (boundsReadOnlyReason(container.ownerInstance, node.property, reason)) {
                    node.readOnly = true;
                    node.readOnlyReason = reason;
                }
                return node;
            }
            std::vector<const Delegate*> delegates;
            container.ownerClass->allDelegates(delegates);
            if (propertyIndex == properties.size() && !delegates.empty()) {
                Node node;
                node.kind = Kind::DelegatesHeader;
                node.ownerClass = container.ownerClass;
                node.ownerInstance = container.ownerInstance;
                return node;
            }
            return Node();
        }
        case Kind::PropertyGroup: {
            const Class* nested = container.property->getClass(container.ownerInstance);
            void* nestedInstance = nested != nullptr ? container.property->address(container.ownerInstance) : nullptr;
            if (nested == nullptr || nestedInstance == nullptr) {
                return Node();
            }
            std::vector<const Property*> properties;
            gridProperties(nested, properties);
            if (index < properties.size()) {
                return classifyProperty(properties[index], nested, nestedInstance);
            }
            return Node();
        }
        case Kind::PropertySubGroup: {
            auto editor = PropertyEditorRegistry::instance().createEditor(
                container.property, container.ownerClass, container.ownerInstance);
            if (editor == nullptr) {
                return Node();
            }
            std::vector<std::string> names = editor->subPropertyNames();
            if (index >= names.size()) {
                return Node();
            }
            Node node;
            node.kind = Kind::SubPropertyEntry;
            node.property = container.property;
            node.ownerClass = container.ownerClass;
            node.ownerInstance = container.ownerInstance;
            node.subPropertyIndex = index;
            node.readOnly = container.readOnly;
            node.readOnlyReason = container.readOnlyReason;
            return node;
        }
        case Kind::DelegatesHeader: {
            std::vector<const Delegate*> delegates;
            container.ownerClass->allDelegates(delegates);
            if (index < delegates.size()) {
                Node node;
                node.kind = Kind::DelegateEntry;
                node.delegate = delegates[index];
                node.ownerInstance = container.ownerInstance;
                return node;
            }
            return Node();
        }
        default:
            return Node();
        }
    }

    std::size_t PropertiesModel::childCountOf(const Node& node) const
    {
        switch (node.kind) {
        case Kind::Root: {
            std::vector<const Property*> properties;
            gridProperties(node.ownerClass, properties);
            std::vector<const Delegate*> delegates;
            node.ownerClass->allDelegates(delegates);
            std::size_t parentRow = showsParentPicker() ? 1 : 0;
            return parentRow + properties.size() + (delegates.empty() ? 0 : 1);
        }
        case Kind::PropertyGroup: {
            const Class* nested = node.property->getClass(node.ownerInstance);
            void* nestedInstance = nested != nullptr ? node.property->address(node.ownerInstance) : nullptr;
            if (nested == nullptr || nestedInstance == nullptr) {
                return 0;
            }
            std::vector<const Property*> properties;
            gridProperties(nested, properties);
            return properties.size();
        }
        case Kind::PropertySubGroup: {
            auto editor = PropertyEditorRegistry::instance().createEditor(
                node.property, node.ownerClass, node.ownerInstance);
            return editor != nullptr ? editor->subPropertyNames().size() : 0;
        }
        case Kind::DelegatesHeader: {
            std::vector<const Delegate*> delegates;
            node.ownerClass->allDelegates(delegates);
            return delegates.size();
        }
        default:
            return 0;
        }
    }

    PropertiesModel::Node PropertiesModel::resolveNode(const std::vector<std::size_t>& path) const
    {
        if (rootClass_ == nullptr) {
            return Node();
        }

        Node current;
        current.kind = Kind::Root;
        current.ownerClass = rootClass_;
        current.ownerInstance = static_cast<void*>(selected_);

        for (std::size_t index : path) {
            current = childOf(current, index);
            if (current.kind == Kind::Invalid) {
                return current;
            }
        }
        return current;
    }

    std::size_t PropertiesModel::childCount(const std::vector<std::size_t>& path) const
    {
        return childCountOf(resolveNode(path));
    }

    std::any PropertiesModel::value(const std::any& key)
    {
        auto path = std::any_cast<std::vector<std::size_t>>(key);
        Node node = resolveNode(path);
        switch (node.kind) {
        case Kind::PropertyLeaf:
        case Kind::PropertyGroup:
        case Kind::PropertyUnsupported:
        case Kind::PropertySubGroup:
            return node.property->name();
        case Kind::SubPropertyEntry: {
            auto editor = PropertyEditorRegistry::instance().createEditor(
                node.property, node.ownerClass, node.ownerInstance);
            std::vector<std::string> names = editor != nullptr ? editor->subPropertyNames() : std::vector<std::string>();
            return node.subPropertyIndex < names.size() ? names[node.subPropertyIndex] : std::string();
        }
        case Kind::ParentPicker:
            return std::string("Parent");
        case Kind::DelegatesHeader:
            return std::string("Delegates");
        case Kind::DelegateEntry:
            return node.delegate->name();
        default:
            return std::string();
        }
    }

    PropertiesModel::Node PropertiesModel::nodeAt(const std::vector<std::size_t>& path) const
    {
        return resolveNode(path);
    }
}
