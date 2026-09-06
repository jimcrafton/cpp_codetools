#include "PropertiesModel.h"

namespace CodeToolsVsix
{
    using newui::reflection::Class;
    using newui::reflection::classinfo;
    using newui::reflection::Delegate;
    using newui::reflection::Property;

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

        if (PropertyEditorRegistry::instance().createEditor(property, ownerClass, ownerInstance) != nullptr) {
            node.kind = Kind::PropertyLeaf;
            return node;
        }

        const Class* nested = classinfo(property->type());
        if (nested != nullptr && property->isAddressable()) {
            node.kind = Kind::PropertyGroup;
            return node;
        }

        node.kind = Kind::PropertyUnsupported;
        return node;
    }

    PropertiesModel::Node PropertiesModel::childOf(const Node& container, std::size_t index) const
    {
        switch (container.kind) {
        case Kind::Root: {
            std::vector<const Property*> properties;
            container.ownerClass->allProperties(properties);
            if (index < properties.size()) {
                return classifyProperty(properties[index], container.ownerClass, container.ownerInstance);
            }
            std::vector<const Delegate*> delegates;
            container.ownerClass->allDelegates(delegates);
            if (index == properties.size() && !delegates.empty()) {
                Node node;
                node.kind = Kind::DelegatesHeader;
                node.ownerClass = container.ownerClass;
                node.ownerInstance = container.ownerInstance;
                return node;
            }
            return Node();
        }
        case Kind::PropertyGroup: {
            const Class* nested = classinfo(container.property->type());
            void* nestedInstance = nested != nullptr ? container.property->address(container.ownerInstance) : nullptr;
            if (nested == nullptr || nestedInstance == nullptr) {
                return Node();
            }
            std::vector<const Property*> properties;
            nested->allProperties(properties);
            if (index < properties.size()) {
                return classifyProperty(properties[index], nested, nestedInstance);
            }
            return Node();
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
            node.ownerClass->allProperties(properties);
            std::vector<const Delegate*> delegates;
            node.ownerClass->allDelegates(delegates);
            return properties.size() + (delegates.empty() ? 0 : 1);
        }
        case Kind::PropertyGroup: {
            const Class* nested = classinfo(node.property->type());
            void* nestedInstance = nested != nullptr ? node.property->address(node.ownerInstance) : nullptr;
            if (nested == nullptr || nestedInstance == nullptr) {
                return 0;
            }
            std::vector<const Property*> properties;
            nested->allProperties(properties);
            return properties.size();
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
            return node.property->name();
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
