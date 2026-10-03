#include "../extension/NativeEditControls/ControllerBindings.h"

#include <cpptools/parser.h>

#include <newui/controls.h>
#include <newui/frame.h>
#include <newui/reflection.h>
#include <newui/subview.h>

#include <gtest/gtest.h>

// registerReflectionData() is already run once for this whole binary by test_component_editor.cpp's
// ::testing::Environment, so the real newui classes are registered here.

using namespace CodeToolsVsix;
using cpptools::BindingStatus;

namespace {

bool addDescriptor(newui::View& view, const char* className, const char* delegateName, const char* descriptor) {
    const newui::reflection::Class* clazz = newui::reflection::classinfo(std::string(className));
    if (clazz == nullptr) {
        return false;
    }
    std::vector<const newui::reflection::Delegate*> delegates;
    clazz->allDelegates(delegates);
    for (const newui::reflection::Delegate* d : delegates) {
        if (d->name() == delegateName) {
            return d->addDescriptorListener(&view, descriptor);
        }
    }
    return false;
}

newui::Button* addButton(newui::View& parent, const char* name) {
    auto* button = new newui::Button();
    button->setName(name);
    parent.addChild(button);
    return button;
}

// A controller that compiles on its own (a stand-in newui: same Delegate::add shape), so this test
// needs no include paths.
const char* kController =
    "namespace newui {\n"
    "enum class SyncReturn { Handled, Ignored };\n"
    "template<typename S, typename... A> struct Delegate {\n"
    "    template<typename T> void add(T* i, SyncReturn (T::*m)(S&, A...)) {}\n"
    "};\n"
    "struct Control { Delegate<Control> onClick; };\n"
    "struct Button : Control {};\n"
    "class Component { protected: virtual bool internal_init() { return true; } };\n"
    "class RootController : public Component {};\n"
    "}\n"
    "class SaveDialogController : public newui::RootController {\n"
    "protected:\n"
    "    bool internal_init() override {\n"
    "        if (!newui::Component::internal_init()) { return false; }\n"
    "        saveButton_->onClick.add(this, &SaveDialogController::onSaveButtonClick);\n"
    "        return true;\n"
    "    }\n"
    "private:\n"
    "    newui::SyncReturn onSaveButtonClick(newui::Control& sender) { return newui::SyncReturn::Handled; }\n"
    "    newui::Button* saveButton_ = nullptr;\n"
    "};\n";

const char* kDescriptor = "this@SaveDialogController.onSaveButtonClick";

}  // namespace

TEST(CollectRecordedBindings, ReadsARecordedListenerWithItsViewFieldAndTheEventsSignature) {
    newui::Frame frame;
    newui::Button* button = addButton(frame.rootView(), "saveButton");
    ASSERT_TRUE(addDescriptor(*button, "Button", "onClick", kDescriptor));

    const std::vector<DesignerBinding> bindings = collectRecordedBindings(frame.rootView());
    ASSERT_EQ(bindings.size(), 1u);
    EXPECT_EQ(bindings[0].viewName, "saveButton");
    EXPECT_EQ(bindings[0].binding.viewField, "saveButton_");
    EXPECT_EQ(bindings[0].binding.delegate, "onClick");
    EXPECT_EQ(bindings[0].binding.descriptor, kDescriptor);
    EXPECT_EQ(bindings[0].binding.senderType, "newui::Control");
    EXPECT_TRUE(bindings[0].binding.argumentTypes.empty());
}

TEST(CollectRecordedBindings, IncludesEventsWithArgumentsAndNestedControls) {
    newui::Frame frame;
    auto* panel = new newui::SubView();
    panel->setName("panel");
    frame.rootView().addChild(panel);
    ASSERT_TRUE(addDescriptor(*panel, "SubView", "onSizeChanged", "presenter_@Presenter.onResized"));
    newui::Button* inner = addButton(*panel, "innerButton");
    ASSERT_TRUE(addDescriptor(*inner, "Button", "onClick", kDescriptor));

    const std::vector<DesignerBinding> bindings = collectRecordedBindings(frame.rootView());
    ASSERT_EQ(bindings.size(), 2u);
    const DesignerBinding* resized = bindings[0].viewName == "panel" ? &bindings[0] : &bindings[1];
    EXPECT_EQ(resized->binding.delegate, "onSizeChanged");
    EXPECT_EQ(resized->binding.senderType, "newui::View");
    ASSERT_EQ(resized->binding.argumentTypes.size(), 1u);
    EXPECT_EQ(resized->binding.argumentTypes[0], "const newui::Size&");
}

TEST(CollectRecordedBindings, SkipsADescriptorThatIsNotObjectAtClassMethod) {
    newui::Frame frame;
    newui::Button* bare = addButton(frame.rootView(), "bareButton");
    ASSERT_TRUE(addDescriptor(*bare, "Button", "onClick", "someFreeFunction"));   // a bare function name

    EXPECT_TRUE(collectRecordedBindings(frame.rootView()).empty());
}

TEST(CollectRecordedBindings, AControlGivenNoNameStillHasTheOneNewuiGeneratedForIt) {
    // setName("") doesn't leave a view nameless - newui hands it a default ("button1"), so the
    // controller field for it is that name plus an underscore.
    newui::Frame frame;
    newui::Button* button = addButton(frame.rootView(), "");
    ASSERT_FALSE(button->name().empty());
    ASSERT_TRUE(addDescriptor(*button, "Button", "onClick", kDescriptor));

    const std::vector<DesignerBinding> bindings = collectRecordedBindings(frame.rootView());
    ASSERT_EQ(bindings.size(), 1u);
    EXPECT_EQ(bindings[0].viewName, button->name());
    EXPECT_EQ(bindings[0].binding.viewField, button->name() + "_");
}

TEST(CollectRecordedBindings, IsEmptyWhenNothingIsRecorded) {
    newui::Frame frame;
    addButton(frame.rootView(), "saveButton");
    EXPECT_TRUE(collectRecordedBindings(frame.rootView()).empty());
}

TEST(VerifyBindings, ARecordedBindingThatMatchesTheControllerIsOk) {
    newui::Frame frame;
    ASSERT_TRUE(addDescriptor(*addButton(frame.rootView(), "saveButton"), "Button", "onClick", kDescriptor));
    const auto bindings = collectRecordedBindings(frame.rootView());

    const auto results = verifyBindings(bindings, "SaveDialogController", kController, cpptools::defaultCompileArgs());
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].viewName, "saveButton");
    EXPECT_EQ(results[0].delegate, "onClick");
    EXPECT_EQ(results[0].descriptor, kDescriptor);
    EXPECT_EQ(results[0].check.status, BindingStatus::Ok) << results[0].check.detail;
}

TEST(VerifyBindings, ADeletedHandlerAndAMissingControllerAreReported) {
    newui::Frame frame;
    newui::Button* button = addButton(frame.rootView(), "saveButton");
    ASSERT_TRUE(addDescriptor(*button, "Button", "onClick", "this@SaveDialogController.onGone"));
    const auto bindings = collectRecordedBindings(frame.rootView());

    EXPECT_EQ(verifyBindings(bindings, "SaveDialogController", kController, cpptools::defaultCompileArgs())[0].check.status,
              BindingStatus::MethodMissing);
    EXPECT_EQ(verifyBindings(bindings, "NoSuchController", kController, cpptools::defaultCompileArgs())[0].check.status,
              BindingStatus::ControllerMissing);
}

TEST(VerifyBindings, UnverifiableBindingsCarryTheReason) {
    newui::Frame frame;
    ASSERT_TRUE(addDescriptor(*addButton(frame.rootView(), "saveButton"), "Button", "onClick", kDescriptor));
    const auto results = unverifiableBindings(collectRecordedBindings(frame.rootView()), "the header can't be read");
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].check.status, BindingStatus::CannotVerify);
    EXPECT_EQ(results[0].check.detail, "the header can't be read");
}
