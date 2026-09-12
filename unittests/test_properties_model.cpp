#include "../extension/NativeEditControls/PropertiesModel.h"

#include <newui/controls.h>
#include <newui/layout.h>
#include <newui/reflection.h>

#include <gtest/gtest.h>

#include <memory>

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

        auto editor = CodeToolsVsix::PropertyEditorRegistry::instance()
            .createEditor(properties_[i], buttonClass_, &button_);
        if (editor != nullptr) {
            PropertiesModel::Kind expectedKind = editor->editStyle() == CodeToolsVsix::PropertyEditor::EditStyle::SubProperties
                ? PropertiesModel::Kind::PropertySubGroup : PropertiesModel::Kind::PropertyLeaf;
            EXPECT_EQ(node.kind, expectedKind) << properties_[i]->name();
            continue;
        }

        // getClass(), not classinfo(type()) - matches childOf()'s own
        // resolution (see PropertiesModel::classifyProperty()'s comment).
        const Class* nested = properties_[i]->getClass(&button_);
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
    // getClass(), not classinfo(type()) - matches childOf()'s own
    // resolution (see PropertiesModel::classifyProperty()'s comment): for
    // a polymorphic property (e.g. layout/layoutParams) whose real
    // attached instance is a concrete subclass, these two can genuinely
    // disagree, and childOf() always wins since it's what actually
    // decides this node's children.
    const Class* nested = groupProperty->getClass(&button_);
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

TEST_F(PropertiesModelTest, LayoutGroupDescendsIntoTheAttachedConcreteSubclassNotTheDeclaredBase)
{
    // newui::View::layout() is declared to return Layout* - a polymorphic
    // base with no properties of its own (all real data lives on a
    // concrete subclass like FlexLayout). classifyProperty()/childOf() use
    // Property::getClass(), not classinfo(property->type()), specifically
    // so this group expands into the real attached FlexLayout's own
    // properties rather than the always-empty declared Layout class.
    std::size_t layoutIndex = properties_.size();
    for (std::size_t i = 0; i < properties_.size(); ++i) {
        if (properties_[i]->name() == "layout") {
            layoutIndex = i;
            break;
        }
    }
    ASSERT_LT(layoutIndex, properties_.size());

    button_.setLayout(std::make_unique<newui::FlexLayout>());
    ASSERT_EQ(model_.nodeAt({layoutIndex}).kind, PropertiesModel::Kind::PropertyGroup);

    const Class* flexLayoutClass = classinfo(typeid(newui::FlexLayout));
    ASSERT_NE(flexLayoutClass, nullptr);
    std::vector<const Property*> flexProperties;
    flexLayoutClass->allProperties(flexProperties);
    ASSERT_FALSE(flexProperties.empty());

    EXPECT_EQ(model_.childCount({layoutIndex}), flexProperties.size());

    PropertiesModel::Node firstChild = model_.nodeAt({layoutIndex, 0});
    EXPECT_EQ(firstChild.ownerClass, flexLayoutClass);
    EXPECT_EQ(firstChild.property, flexProperties[0]);
    EXPECT_EQ(firstChild.ownerInstance, static_cast<void*>(button_.layout()));
}

TEST_F(PropertiesModelTest, LayoutGroupHasNoChildrenWhenNoConcreteLayoutIsAttached)
{
    std::size_t layoutIndex = properties_.size();
    for (std::size_t i = 0; i < properties_.size(); ++i) {
        if (properties_[i]->name() == "layout") {
            layoutIndex = i;
            break;
        }
    }
    ASSERT_LT(layoutIndex, properties_.size());
    ASSERT_EQ(button_.layout(), nullptr);  // nothing set - fresh Button default

    EXPECT_EQ(model_.nodeAt({layoutIndex}).kind, PropertiesModel::Kind::PropertyGroup);
    EXPECT_EQ(model_.childCount({layoutIndex}), 0u);
}

TEST_F(PropertiesModelTest, BoundsExpandsIntoFourSyntheticSubPropertyRows)
{
    // newui::View::bounds() returns const Rect& - never addressable (see
    // ClassBuilder::property()'s own "Never true for a const T&" comment,
    // reflection.h) - so this can never become a real Kind::PropertyGroup.
    // RectPropertyEditor's EditStyle::SubProperties is what lets it expand
    // anyway, into 4 synthetic x/y/width/height rows.
    std::size_t boundsIndex = properties_.size();
    for (std::size_t i = 0; i < properties_.size(); ++i) {
        if (properties_[i]->name() == "bounds") {
            boundsIndex = i;
            break;
        }
    }
    ASSERT_LT(boundsIndex, properties_.size());

    button_.setBounds(newui::Rect(10.0f, 20.0f, 300.0f, 40.0f));

    ASSERT_EQ(model_.nodeAt({boundsIndex}).kind, PropertiesModel::Kind::PropertySubGroup);
    ASSERT_EQ(model_.childCount({boundsIndex}), 4u);

    static const char* kNames[4] = {"x", "y", "width", "height"};
    static const float kValues[4] = {10.0f, 20.0f, 300.0f, 40.0f};
    for (std::size_t i = 0; i < 4; ++i) {
        PropertiesModel::Node child = model_.nodeAt({boundsIndex, i});
        EXPECT_EQ(child.kind, PropertiesModel::Kind::SubPropertyEntry) << kNames[i];
        EXPECT_EQ(child.subPropertyIndex, i) << kNames[i];
        // property/ownerClass/ownerInstance still describe the *parent*
        // "bounds" property, same as PropertySubGroup's own node.
        EXPECT_EQ(child.property, properties_[boundsIndex]) << kNames[i];
        EXPECT_EQ(child.ownerInstance, static_cast<void*>(&button_)) << kNames[i];
        EXPECT_EQ(std::any_cast<std::string>(model_.value(std::vector<std::size_t>{boundsIndex, i})),
            std::string(kNames[i]));

        auto editor = CodeToolsVsix::PropertyEditorRegistry::instance()
            .createEditor(child.property, child.ownerClass, child.ownerInstance);
        ASSERT_NE(editor, nullptr) << kNames[i];
        EXPECT_FLOAT_EQ(std::stof(editor->subPropertyValueAsString(child.subPropertyIndex)), kValues[i]) << kNames[i];
    }
}

// ---------------------------------------------------------------------------
// Node::readOnly - the BOUNDS-editing-vs-governing-Layout gap this whole
// LayoutEditingPolicy capability model was eventually meant to close (see
// bluesky's own long-standing note): "bounds" must not present as freely
// editable once its owning View's real parent Layout affords something other
// than free pixel positioning (LinearReorder/GridCell/None), since a directly
// typed edit there would be silently lost/reverted on the very next relayout.
// ---------------------------------------------------------------------------

TEST_F(PropertiesModelTest, BoundsIsReadOnlyWhenTheParentHasAFlexLayout)
{
    newui::SubView container;
    container.setName("container");
    container.setLayout(std::make_unique<newui::FlexLayout>());
    container.addChild(&button_);
    model_.setSelection(&button_);

    std::size_t boundsIndex = properties_.size();
    for (std::size_t i = 0; i < properties_.size(); ++i) {
        if (properties_[i]->name() == "bounds") {
            boundsIndex = i;
            break;
        }
    }
    ASSERT_LT(boundsIndex, properties_.size());
    boundsIndex += 1;  // the synthetic ParentPicker row (button_ now has a real parent) shifts
                        // every real property index by one - see showsParentPicker()'s own comment.

    PropertiesModel::Node boundsNode = model_.nodeAt({boundsIndex});
    ASSERT_EQ(boundsNode.kind, PropertiesModel::Kind::PropertySubGroup);
    EXPECT_TRUE(boundsNode.readOnly);
    EXPECT_NE(boundsNode.readOnlyReason.find("FlexLayout"), std::string::npos) << boundsNode.readOnlyReason;

    // Every synthetic x/y/width/height child inherits the same readOnly/reason from its own
    // PropertySubGroup parent - PropertiesGrid never builds a live editor for any of them either
    // way, but PropertyItem's own paint() dims each one individually.
    for (std::size_t i = 0; i < 4; ++i) {
        PropertiesModel::Node child = model_.nodeAt({boundsIndex, i});
        EXPECT_TRUE(child.readOnly) << i;
        EXPECT_EQ(child.readOnlyReason, boundsNode.readOnlyReason) << i;
    }

    container.removeChild(&button_);
}

TEST_F(PropertiesModelTest, BoundsStaysEditableWhenTheParentHasAnAnchorLayoutOrNoLayoutAtAll)
{
    newui::SubView anchoredContainer;
    anchoredContainer.setName("anchoredContainer");
    anchoredContainer.setLayout(std::make_unique<newui::AnchorLayout>());
    anchoredContainer.addChild(&button_);
    model_.setSelection(&button_);

    std::size_t boundsIndex = properties_.size();
    for (std::size_t i = 0; i < properties_.size(); ++i) {
        if (properties_[i]->name() == "bounds") {
            boundsIndex = i;
            break;
        }
    }
    ASSERT_LT(boundsIndex, properties_.size());
    boundsIndex += 1;

    PropertiesModel::Node boundsNode = model_.nodeAt({boundsIndex});
    EXPECT_FALSE(boundsNode.readOnly);
    EXPECT_TRUE(boundsNode.readOnlyReason.empty());

    anchoredContainer.removeChild(&button_);

    // No layout at all (button_'s own real parent for every other test in this file) is the same
    // "freely editable" case - already exercised by BoundsExpandsIntoFourSyntheticSubPropertyRows
    // above, which never sets a parent at all (readOnly defaults to false, untouched).
}

// A real, live-caught bug: newui::ViewStyle::backgroundFill() used to be const-only
// (`const gfx::Fill&`), which reflection.h's own property() addressability rule explicitly
// excludes (requires a non-const lvalue reference) - so despite gfx::Fill being a real,
// reflectable Class, this fell all the way through classifyProperty() to Kind::PropertyUnsupported
// in the actual running app, invisible to every other test in this file (none of which ever
// walked into "style" -> "backgroundFill" specifically) and to newui's own reflection tests
// (which only ever checked gfx::Fill's own data, or ViewStyle's class hierarchy, never
// backgroundFill's own addressability). Fixed in newui by giving backgroundFill() a non-const
// overload too (same pair-shape as View::style() itself) - this test exists so a future regression
// here fails immediately, at the same layer (PropertiesModel) the real bug actually showed up in.
TEST_F(PropertiesModelTest, BackgroundFillUnderStyleIsARealPropertyGroupNotUnsupported)
{
    std::size_t styleIndex = properties_.size();
    for (std::size_t i = 0; i < properties_.size(); ++i) {
        if (properties_[i]->name() == "style") {
            styleIndex = i;
            break;
        }
    }
    ASSERT_LT(styleIndex, properties_.size()) << "expected newui::Button to have a real \"style\" property";
    ASSERT_EQ(model_.nodeAt({styleIndex}).kind, PropertiesModel::Kind::PropertyGroup);

    const Class* styleClass = properties_[styleIndex]->getClass(&button_);
    ASSERT_NE(styleClass, nullptr);
    std::vector<const Property*> styleProperties;
    styleClass->allProperties(styleProperties);

    std::size_t backgroundFillIndex = styleProperties.size();
    for (std::size_t i = 0; i < styleProperties.size(); ++i) {
        if (styleProperties[i]->name() == "backgroundFill") {
            backgroundFillIndex = i;
            break;
        }
    }
    ASSERT_LT(backgroundFillIndex, styleProperties.size())
        << "expected the real style class to have a real \"backgroundFill\" property";

    PropertiesModel::Node backgroundFillNode = model_.nodeAt({styleIndex, backgroundFillIndex});
    EXPECT_EQ(backgroundFillNode.kind, PropertiesModel::Kind::PropertyGroup)
        << "backgroundFill must be a real, addressable PropertyGroup (newui::gfx::Fill), not (unsupported)";
    EXPECT_NE(backgroundFillNode.kind, PropertiesModel::Kind::PropertyUnsupported);
}

TEST_F(PropertiesModelTest, OutOfRangeRootIndexIsInvalid)
{
    std::vector<std::size_t> path{properties_.size() + 100};
    EXPECT_EQ(model_.nodeAt(path).kind, PropertiesModel::Kind::Invalid);
    EXPECT_EQ(model_.childCount(path), 0u);
}

// ---------------------------------------------------------------------------
// Kind::ParentPicker - a synthetic row, not backed by a real
// newui::reflection::Property (see its own Kind comment, PropertiesModel.h).
// PropertiesModelTest's own button_ has no parent() (never addChild()'d onto
// anything), which is exactly why every test above can keep using {i} to mean
// "the i-th real property" - showsParentPicker() correctly stays false for
// it, matching this suite's own established index convention. These tests
// use a real container instead specifically to exercise the "true" case.
// ---------------------------------------------------------------------------

TEST_F(PropertiesModelTest, ParentPickerIsAbsentWhenSelectedHasNoParent)
{
    ASSERT_EQ(button_.parent(), nullptr);
    EXPECT_EQ(model_.childCount({}), properties_.size() + 1);
    EXPECT_NE(model_.nodeAt({0}).kind, PropertiesModel::Kind::ParentPicker);
    EXPECT_EQ(model_.nodeAt({0}).property, properties_[0]);
}

TEST_F(PropertiesModelTest, ParentPickerIsTheFirstRootChildWhenSelectedHasAParent)
{
    newui::SubView container;
    container.setName("container");
    container.addChild(&button_);
    model_.setSelection(&button_);

    ASSERT_EQ(button_.parent(), &container);
    EXPECT_EQ(model_.childCount({}), properties_.size() + 2);  // +1 Parent row, +1 DelegatesHeader

    PropertiesModel::Node parentNode = model_.nodeAt({0});
    EXPECT_EQ(parentNode.kind, PropertiesModel::Kind::ParentPicker);
    EXPECT_EQ(parentNode.ownerInstance, static_cast<void*>(&button_));
    EXPECT_EQ(std::any_cast<std::string>(model_.value(std::vector<std::size_t>{0})), std::string("Parent"));

    // Every real property/DelegatesHeader shifts by exactly one slot to make room.
    for (std::size_t i = 0; i < properties_.size(); ++i) {
        EXPECT_EQ(model_.nodeAt({i + 1}).property, properties_[i]) << properties_[i]->name();
    }
    EXPECT_EQ(model_.nodeAt({properties_.size() + 1}).kind, PropertiesModel::Kind::DelegatesHeader);

    container.removeChild(&button_);
}

TEST_F(PropertiesModelTest, ParentPickerNeverAppearsForANonViewSelection)
{
    // showsParentPicker() dynamic_casts the live selected_ pointer to
    // newui::View - selected_ is always a real SubView* today (View-derived),
    // so this can't be exercised with a genuinely non-View object yet, but a
    // null selection is the one case reachable right now and must still
    // resolve to an empty, ParentPicker-free tree.
    PropertiesModel empty;
    EXPECT_EQ(empty.childCount({}), 0u);
    EXPECT_EQ(empty.nodeAt({0}).kind, PropertiesModel::Kind::Invalid);
}
