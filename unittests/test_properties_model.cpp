#include "../extension/NativeEditControls/PropertiesModel.h"

#include <newui/controls.h>
#include <newui/dialogs.h>
#include <newui/layout.h>
#include <newui/reflection.h>
#include <newui/undostack.h>

#include <gtest/gtest.h>

#include <fstream>
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
        std::vector<const Property*> all;
        buttonClass_->allProperties(all);
        for (const Property* property : all) {
            if (!property->isCollection()) {   // the grid hides collections (childViews)
                properties_.push_back(property);
            }
        }
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

        // getClass(), not classinfo(type()) - matches childOf()'s own
        // resolution (see PropertiesModel::classifyProperty()'s comment).
        const Class* nested = properties_[i]->getClass(&button_);
        bool isNestedGroup = nested != nullptr && properties_[i]->isAddressable();

        // A Layout/ViewStyle-shaped property (a registered EditStyle::Dialog editor that's ALSO
        // an addressable nested Class) stays an expandable PropertyGroup - drilldown into its
        // current fields wins over the type-swap editor, which attaches to the group header's own
        // "..." button instead (PropertyItem.cpp) rather than replacing the row - see
        // PropertiesModel::classifyProperty()'s own comment.
        bool isTypeSwapGroup = editor != nullptr
            && editor->editStyle() == CodeToolsVsix::PropertyEditor::EditStyle::Dialog && isNestedGroup;

        if (editor != nullptr && !isTypeSwapGroup) {
            PropertiesModel::Kind expectedKind = editor->editStyle() == CodeToolsVsix::PropertyEditor::EditStyle::SubProperties
                ? PropertiesModel::Kind::PropertySubGroup : PropertiesModel::Kind::PropertyLeaf;
            EXPECT_EQ(node.kind, expectedKind) << properties_[i]->name();
            continue;
        }

        if (isNestedGroup) {
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

// "layout" stays Kind::PropertyGroup even now that LayoutPropertyEditor (PropertyEditor.h) is
// registered at typeid(newui::Layout) - classifyProperty() special-cases a Dialog-style editor
// that's ALSO an addressable nested Class (Layout/ViewStyle-shaped) to keep drilldown working
// (preserves the pre-existing "expand style to edit its own current fields" UX this project
// already had and tested - see the other PropertyGroup test above) rather than replacing the row
// with a leaf - see classifyProperty()'s own comment. The type-swap editor still exists for this
// row; it's just attached to the group header's own "..." button (PropertyItem.cpp) instead.
TEST_F(PropertiesModelTest, LayoutStaysAnExpandablePropertyGroupWithATypeSwapEditorAttached)
{
    std::size_t layoutIndex = properties_.size();
    for (std::size_t i = 0; i < properties_.size(); ++i) {
        if (properties_[i]->name() == "layout") {
            layoutIndex = i;
            break;
        }
    }
    ASSERT_LT(layoutIndex, properties_.size());

    button_.setLayout(std::make_unique<newui::FlexLayout>());
    EXPECT_EQ(model_.nodeAt({layoutIndex}).kind, PropertiesModel::Kind::PropertyGroup);

    const Class* flexLayoutClass = classinfo(typeid(newui::FlexLayout));
    ASSERT_NE(flexLayoutClass, nullptr);
    std::vector<const Property*> flexProperties;
    flexLayoutClass->allProperties(flexProperties);
    ASSERT_FALSE(flexProperties.empty());
    EXPECT_EQ(model_.childCount({layoutIndex}), flexProperties.size());

    auto editor = CodeToolsVsix::PropertyEditorRegistry::instance().createEditor(
        properties_[layoutIndex], buttonClass_, &button_);
    ASSERT_NE(editor, nullptr);
    EXPECT_EQ(editor->editStyle(), CodeToolsVsix::PropertyEditor::EditStyle::Dialog);
    EXPECT_NE(dynamic_cast<CodeToolsVsix::LayoutPropertyEditor*>(editor.get()), nullptr);
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

TEST_F(PropertiesModelTest, EveryEditableRowIsReadOnlyForAReadOnlyFlaggedView)
{
    std::size_t editableRows = 0;
    for (std::size_t i = 0; i < model_.childCount({}); ++i) {
        PropertiesModel::Node node = model_.nodeAt({i});
        if (node.kind == PropertiesModel::Kind::PropertyLeaf || node.kind == PropertiesModel::Kind::PropertyGroup
            || node.kind == PropertiesModel::Kind::PropertySubGroup) {
            EXPECT_FALSE(node.readOnly && node.readOnlyReason == "Managed by its owning control") << i;
            ++editableRows;
        }
    }
    ASSERT_GT(editableRows, 0u);

    button_.setDesignTimeFlag(newui::DesignTimeFlags::ReadOnly);
    for (std::size_t i = 0; i < model_.childCount({}); ++i) {
        PropertiesModel::Node node = model_.nodeAt({i});
        if (node.kind == PropertiesModel::Kind::PropertyLeaf || node.kind == PropertiesModel::Kind::PropertyGroup
            || node.kind == PropertiesModel::Kind::PropertySubGroup) {
            EXPECT_TRUE(node.readOnly) << i;
        }
    }
}

// ---------------------------------------------------------------------------
// layoutParams - the selected control's per-child layout data, edited in place. Its fields are
// real reflected properties (getter/setter pairs), so the group expands and each row commits.
// ---------------------------------------------------------------------------

namespace
{
    // The root row for the "layoutParams" property, or npos.
    std::size_t layoutParamsRow(PropertiesModel& model)
    {
        for (std::size_t i = 0; i < model.childCount({}); ++i) {
            PropertiesModel::Node node = model.nodeAt({i});
            if (node.property != nullptr && node.property->name() == "layoutParams") {
                return i;
            }
        }
        return static_cast<std::size_t>(-1);
    }

    // The child of group row `group` whose property is `name`, or Invalid.
    PropertiesModel::Node childNamed(PropertiesModel& model, std::size_t group, const std::string& name)
    {
        for (std::size_t c = 0; c < model.childCount({group}); ++c) {
            PropertiesModel::Node node = model.nodeAt({group, c});
            if (node.property != nullptr && node.property->name() == name) {
                return node;
            }
        }
        return PropertiesModel::Node();
    }
}

TEST_F(PropertiesModelTest, AnchorLayoutParamsExpandsIntoItsEditableFields)
{
    button_.setLayoutParams(std::make_unique<newui::AnchorLayoutParams>());
    model_.setSelection(&button_);

    std::size_t row = layoutParamsRow(model_);
    ASSERT_NE(row, static_cast<std::size_t>(-1));
    EXPECT_EQ(model_.nodeAt({row}).kind, PropertiesModel::Kind::PropertyGroup);
    EXPECT_EQ(model_.childCount({row}), 7u);

    EXPECT_EQ(childNamed(model_, row, "anchors").kind, PropertiesModel::Kind::PropertySubGroup);  // flags
    for (const char* name : { "leftMargin", "topMargin", "rightMargin", "bottomMargin", "width", "height" }) {
        EXPECT_EQ(childNamed(model_, row, name).kind, PropertiesModel::Kind::PropertyLeaf) << name;
    }
}

TEST_F(PropertiesModelTest, EditingAnAnchorLayoutParamsFieldWritesThroughAndUndoes)
{
    auto* params = new newui::AnchorLayoutParams();
    button_.setLayoutParams(std::unique_ptr<newui::LayoutParams>(params));
    model_.setSelection(&button_);

    std::size_t row = layoutParamsRow(model_);
    ASSERT_NE(row, static_cast<std::size_t>(-1));
    PropertiesModel::Node margin = childNamed(model_, row, "leftMargin");
    ASSERT_EQ(margin.kind, PropertiesModel::Kind::PropertyLeaf);

    auto editor = CodeToolsVsix::PropertyEditorRegistry::instance()
        .createEditor(margin.property, margin.ownerClass, margin.ownerInstance);
    ASSERT_NE(editor, nullptr);
    newui::UndoStack undo;
    editor->setUndoStack(&undo);

    editor->setValueFromString("12");
    EXPECT_FLOAT_EQ(params->leftMargin(), 12.0f);
    undo.undo();
    EXPECT_FLOAT_EQ(params->leftMargin(), 0.0f);
    undo.redo();
    EXPECT_FLOAT_EQ(params->leftMargin(), 12.0f);
}

TEST_F(PropertiesModelTest, FlexAndGridLayoutParamsExposeTheirFieldsToo)
{
    button_.setLayoutParams(std::make_unique<newui::FlexLayoutParams>());
    model_.setSelection(&button_);
    std::size_t row = layoutParamsRow(model_);
    ASSERT_NE(row, static_cast<std::size_t>(-1));
    EXPECT_EQ(childNamed(model_, row, "weight").kind, PropertiesModel::Kind::PropertyLeaf);

    button_.setLayoutParams(std::make_unique<newui::GridLayoutParams>());
    model_.setSelection(&button_);
    row = layoutParamsRow(model_);
    ASSERT_NE(row, static_cast<std::size_t>(-1));
    for (const char* name : { "row", "column", "rowSpan", "columnSpan", "horizontalAlignment", "verticalAlignment" }) {
        EXPECT_EQ(childNamed(model_, row, name).kind, PropertiesModel::Kind::PropertyLeaf) << name;
    }
}

TEST_F(PropertiesModelTest, GridLayoutParamsCellFieldsTakeUnsignedIntegersOnly)
{
    auto* params = new newui::GridLayoutParams();
    button_.setLayoutParams(std::unique_ptr<newui::LayoutParams>(params));
    model_.setSelection(&button_);
    std::size_t row = layoutParamsRow(model_);
    ASSERT_NE(row, static_cast<std::size_t>(-1));

    PropertiesModel::Node column = childNamed(model_, row, "column");
    auto editor = CodeToolsVsix::PropertyEditorRegistry::instance()
        .createEditor(column.property, column.ownerClass, column.ownerInstance);
    ASSERT_NE(editor, nullptr);

    editor->setValueFromString("3");
    EXPECT_EQ(params->column(), 3u);
    EXPECT_EQ(editor->valueAsString(), "3");

    editor->setValueFromString("-1");   // would wrap to a huge index
    editor->setValueFromString("2.5");
    editor->setValueFromString("abc");
    editor->setValueFromString("");
    EXPECT_EQ(params->column(), 3u);
}

TEST_F(PropertiesModelTest, TheChildViewsCollectionIsNotListed)
{
    std::size_t rows = model_.childCount({});
    for (std::size_t i = 0; i < rows; ++i) {
        PropertiesModel::Node node = model_.nodeAt({i});
        if (node.property != nullptr) {
            EXPECT_NE(node.property->name(), "childViews");
            EXPECT_FALSE(node.property->isCollection());
        }
    }
    // ...yet it is still a real reflected property of the class (the file format needs it).
    EXPECT_NE(classinfo(typeid(newui::View))->property("childViews"), nullptr);
}

TEST_F(PropertiesModelTest, DerivedClientBoundsAndTransientOriginAreNotListed)
{
    for (std::size_t i = 0; i < model_.childCount({}); ++i) {
        PropertiesModel::Node node = model_.nodeAt({i});
        if (node.property != nullptr) {
            EXPECT_NE(node.property->name(), "clientBounds");
            EXPECT_NE(node.property->name(), "origin");
        }
    }
}

TEST_F(PropertiesModelTest, TheCursorKindRowIsAnEditableDropdownThatWritesThrough)
{
    std::size_t cursorRow = static_cast<std::size_t>(-1);
    for (std::size_t i = 0; i < model_.childCount({}); ++i) {
        PropertiesModel::Node node = model_.nodeAt({i});
        if (node.property != nullptr && node.property->name() == "cursor") {
            cursorRow = i;
        }
    }
    ASSERT_NE(cursorRow, static_cast<std::size_t>(-1));

    PropertiesModel::Node kind;
    for (std::size_t c = 0; c < model_.childCount({cursorRow}); ++c) {
        PropertiesModel::Node child = model_.nodeAt({cursorRow, c});
        if (child.property != nullptr && child.property->name() == "kind") {
            kind = child;
        }
    }
    ASSERT_EQ(kind.kind, PropertiesModel::Kind::PropertyLeaf);

    auto editor = CodeToolsVsix::PropertyEditorRegistry::instance()
        .createEditor(kind.property, kind.ownerClass, kind.ownerInstance);
    ASSERT_NE(editor, nullptr);
    editor->setValueFromString("Hand");
    EXPECT_EQ(button_.cursorKind(), newui::CursorKind::Hand);
}

// ---------------------------------------------------------------------------
// cursor.path - an image file path: gets the "..." file picker (a Dialog-style FilePath editor,
// chosen by the property's "filepath" tag), offers .png/.svg, and changing it switches the kind.
// ---------------------------------------------------------------------------

namespace
{
    PropertiesModel::Node cursorChild(PropertiesModel& model, const std::string& name)
    {
        for (std::size_t i = 0; i < model.childCount({}); ++i) {
            PropertiesModel::Node node = model.nodeAt({i});
            if (node.property == nullptr || node.property->name() != "cursor") {
                continue;
            }
            for (std::size_t c = 0; c < model.childCount({i}); ++c) {
                PropertiesModel::Node child = model.nodeAt({i, c});
                if (child.property != nullptr && child.property->name() == name) {
                    return child;
                }
            }
        }
        return PropertiesModel::Node();
    }
}

TEST_F(PropertiesModelTest, CursorPathIsAFilePathRowWithTheEllipsisPickerAndAnImageFilter)
{
    PropertiesModel::Node path = cursorChild(model_, "path");
    ASSERT_EQ(path.kind, PropertiesModel::Kind::PropertyLeaf);

    auto editor = CodeToolsVsix::PropertyEditorRegistry::instance()
        .createEditor(path.property, path.ownerClass, path.ownerInstance);
    ASSERT_NE(editor, nullptr);
    EXPECT_NE(dynamic_cast<CodeToolsVsix::FilePathPropertyEditor*>(editor.get()), nullptr);
    EXPECT_EQ(editor->editStyle(), CodeToolsVsix::PropertyEditor::EditStyle::Dialog);
    EXPECT_TRUE(editor->hasDialog());  // what makes PropertiesGrid paint the "..." button

    std::vector<newui::FileDialogFilter> filters = CodeToolsVsix::FilePathPropertyEditor::filtersFor(path.property);
    ASSERT_EQ(filters.size(), 2u);
    EXPECT_NE(filters[0].pattern.find("*.png"), std::string::npos);
    EXPECT_NE(filters[0].pattern.find("*.svg"), std::string::npos);
    EXPECT_EQ(filters[1].pattern, "*.*");

    // A filepath property without the "image" tag gets no filter.
    EXPECT_TRUE(CodeToolsVsix::FilePathPropertyEditor::filtersFor(nullptr).empty());
    PropertiesModel::Node kind = cursorChild(model_, "kind");
    EXPECT_TRUE(CodeToolsVsix::FilePathPropertyEditor::filtersFor(kind.property).empty());
}

TEST_F(PropertiesModelTest, ChangingCursorPathSwitchesTheKindToCustomAndUndoRestoresIt)
{
    char tempPath[MAX_PATH]{};
    ::GetTempPathA(MAX_PATH, tempPath);
    const std::string svg = std::string(tempPath) + "PropertiesModelCursor.svg";
    {
        std::ofstream file(svg, std::ios::binary);
        file << R"(<svg xmlns="http://www.w3.org/2000/svg" width="32" height="32"><rect width="32" height="32" fill="blue"/></svg>)";
    }

    button_.setCursor(newui::Cursor(newui::CursorKind::Hand));
    ASSERT_EQ(button_.cursorKind(), newui::CursorKind::Hand);

    PropertiesModel::Node path = cursorChild(model_, "path");
    auto editor = CodeToolsVsix::PropertyEditorRegistry::instance()
        .createEditor(path.property, path.ownerClass, path.ownerInstance);
    ASSERT_NE(editor, nullptr);
    newui::UndoStack undo;
    editor->setUndoStack(&undo);

    editor->setValueFromString(svg);
    EXPECT_EQ(button_.cursorKind(), newui::CursorKind::Custom);
    EXPECT_EQ(button_.cursor().path(), svg);

    undo.undo();   // the path goes back to "", and the cursor to what it was
    EXPECT_EQ(button_.cursorKind(), newui::CursorKind::Hand);

    undo.redo();
    EXPECT_EQ(button_.cursorKind(), newui::CursorKind::Custom);

    ::DeleteFileA(svg.c_str());
}
