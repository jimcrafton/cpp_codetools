#include "../extension/NativeEditControls/PropertiesModel.h"

#include <newui/controls.h>
#include <newui/reflection.h>

#include <gtest/gtest.h>

using newui::reflection::Class;
using newui::reflection::classinfo;
using newui::reflection::Delegate;
using newui::reflection::Property;
using CodeToolsVsix::PropertiesModel;

// registerReflectionData() is already run once globally for this whole
// binary by test_component_editor.cpp's own ::testing::Environment - real
// newui::Button properties/delegates (plus everything it inherits from
// Control/View/SubView) are already registered by the time these tests run.

class PropertiesModelTest : public ::testing::Test {
protected:
    void SetUp() override {
        CodeToolsVsix::PropertyEditorRegistry::instance().registerBuiltinEditors();
        buttonClass_ = classinfo(typeid(newui::Button));
        ASSERT_NE(buttonClass_, nullptr);
        buttonClass_->allProperties(properties_);
        buttonClass_->allDelegates(delegates_);
        ASSERT_FALSE(properties_.empty());
        ASSERT_FALSE(delegates_.empty());

        model_.setSelection(&button_);
    }

    const Class* buttonClass_ = nullptr;
    std::vector<const Property*> properties_;
    std::vector<const Delegate*> delegates_;
    newui::Button button_;
    PropertiesModel model_;
};

TEST_F(PropertiesModelTest, NoSelectionHasAnEmptyRootAndInvalidNodes)
{
    PropertiesModel empty;
    EXPECT_EQ(empty.childCount({}), 0u);
    EXPECT_EQ(empty.nodeAt({}).kind, PropertiesModel::Kind::Invalid);
    EXPECT_EQ(std::any_cast<std::string>(empty.value(std::vector<std::size_t>{})), std::string());
}

TEST_F(PropertiesModelTest, SetSelectionFiresOnChanged)
{
    int changedCount = 0;
    model_.onChanged.add([&changedCount](newui::Model&) {
        ++changedCount;
        return newui::SyncReturn::Handled;
    });
    model_.setSelection(&button_);
    EXPECT_EQ(changedCount, 1);
}

TEST_F(PropertiesModelTest, RootChildCountIsPropertyCountPlusDelegatesHeader)
{
    EXPECT_EQ(model_.childCount({}), properties_.size() + 1);
}

TEST_F(PropertiesModelTest, EveryRootPropertyClassifiesConsistentlyWithPropertyEditorRegistry)
{
    for (std::size_t i = 0; i < properties_.size(); ++i) {
        PropertiesModel::Node node = model_.nodeAt({i});
        EXPECT_EQ(node.property, properties_[i]);
        EXPECT_EQ(node.ownerClass, buttonClass_);
        EXPECT_EQ(node.ownerInstance, static_cast<void*>(&button_));
        EXPECT_EQ(std::any_cast<std::string>(model_.value(std::vector<std::size_t>{i})), properties_[i]->name());

        bool hasEditor = CodeToolsVsix::PropertyEditorRegistry::instance()
            .createEditor(properties_[i], buttonClass_, &button_) != nullptr;
        if (hasEditor) {
            EXPECT_EQ(node.kind, PropertiesModel::Kind::PropertyLeaf) << properties_[i]->name();
            continue;
        }

        const Class* nested = classinfo(properties_[i]->type());
        if (nested != nullptr && properties_[i]->isAddressable()) {
            EXPECT_EQ(node.kind, PropertiesModel::Kind::PropertyGroup) << properties_[i]->name();
        } else {
            EXPECT_EQ(node.kind, PropertiesModel::Kind::PropertyUnsupported) << properties_[i]->name();
        }
    }
}

TEST_F(PropertiesModelTest, DelegatesHeaderIsTheLastRootChild)
{
    std::vector<std::size_t> headerPath{properties_.size()};
    PropertiesModel::Node node = model_.nodeAt(headerPath);
    EXPECT_EQ(node.kind, PropertiesModel::Kind::DelegatesHeader);
    EXPECT_EQ(model_.childCount(headerPath), delegates_.size());
    EXPECT_EQ(std::any_cast<std::string>(model_.value(headerPath)), "Delegates");
}

TEST_F(PropertiesModelTest, DelegateEntriesResolveToTheRealDelegates)
{
    for (std::size_t i = 0; i < delegates_.size(); ++i) {
        std::vector<std::size_t> path{properties_.size(), i};
        PropertiesModel::Node node = model_.nodeAt(path);
        EXPECT_EQ(node.kind, PropertiesModel::Kind::DelegateEntry);
        EXPECT_EQ(node.delegate, delegates_[i]);
        EXPECT_EQ(model_.childCount(path), 0u);
        EXPECT_EQ(std::any_cast<std::string>(model_.value(path)), delegates_[i]->name());
    }
}

TEST_F(PropertiesModelTest, FirstGroupPropertyDescendsIntoItsOwnNestedProperties)
{
    // A property can classify as PropertyGroup (a registered, addressable
    // nested Class) yet still have a currently-null address() - e.g. a
    // pointer-typed property that happens to be unset on a fresh instance.
    // childOf()/childCountOf() already treat that as "no children right
    // now" rather than a classification error (matches
    // PropertiesPanel::buildPropertyRows()'s own original behavior: it
    // still shows the group header, it just doesn't descend into it) - so
    // this test specifically needs a group property whose address()
    // resolves, to exercise the actual descend path.
    std::size_t groupIndex = properties_.size();
    void* nestedInstance = nullptr;
    for (std::size_t i = 0; i < properties_.size(); ++i) {
        if (model_.nodeAt({i}).kind != PropertiesModel::Kind::PropertyGroup) {
            continue;
        }
        void* candidateInstance = properties_[i]->address(&button_);
        if (candidateInstance != nullptr) {
            groupIndex = i;
            nestedInstance = candidateInstance;
            break;
        }
    }
    ASSERT_LT(groupIndex, properties_.size())
        << "expected newui::Button to have at least one SubProperties-style property with a resolvable address";

    const Property* groupProperty = properties_[groupIndex];
    const Class* nested = classinfo(groupProperty->type());
    ASSERT_NE(nested, nullptr);

    std::vector<const Property*> nestedProperties;
    nested->allProperties(nestedProperties);
    EXPECT_EQ(model_.childCount({groupIndex}), nestedProperties.size());

    if (!nestedProperties.empty()) {
        PropertiesModel::Node node = model_.nodeAt({groupIndex, 0});
        EXPECT_EQ(node.property, nestedProperties[0]);
        EXPECT_EQ(node.ownerClass, nested);
        EXPECT_EQ(node.ownerInstance, nestedInstance);
        EXPECT_EQ(std::any_cast<std::string>(model_.value(std::vector<std::size_t>{groupIndex, 0})), nestedProperties[0]->name());
    }
}

TEST_F(PropertiesModelTest, OutOfRangeRootIndexIsInvalid)
{
    std::vector<std::size_t> path{properties_.size() + 100};
    EXPECT_EQ(model_.nodeAt(path).kind, PropertiesModel::Kind::Invalid);
    EXPECT_EQ(model_.childCount(path), 0u);
}
