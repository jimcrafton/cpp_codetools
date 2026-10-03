#include "../extension/NativeEditControls/WiringRequestBuilder.h"

#include <newui/controls.h>
#include <newui/subview.h>

#include <gtest/gtest.h>

// registerReflectionData() is already run once for this whole binary by test_component_editor.cpp's
// ::testing::Environment, so the real newui classes are registered here.

using CodeToolsVsix::buildControllerWiringRequest;
using cpptools_codegen::ControllerWiringRequest;

namespace {

// The registered class, or null (each test ASSERTs it's there first).
const newui::reflection::Class* classNamed(const char* name) {
    return newui::reflection::classinfo(std::string(name));
}

}  // namespace

// The (spelling, name) pairs asserted here are the SAME literals test_controllerwiring_newui.cpp
// (cpptools_codegen_tests) compiles against the real Delegate::add() - keep the two in step.

TEST(WiringRequestBuilder, ButtonOnClickUsesTheDelegatesRealSenderType) {
    ASSERT_NE(classNamed("Button"), nullptr);
    ControllerWiringRequest r;
    ASSERT_TRUE(buildControllerWiringRequest(*classNamed("Button"), "onClick", "saveButton", "SaveDialogController", r));
    EXPECT_EQ(r.className, "SaveDialogController");
    EXPECT_EQ(r.viewName, "saveButton");
    EXPECT_EQ(r.viewType, "newui::Button");
    EXPECT_EQ(r.delegateName, "onClick");
    EXPECT_EQ(r.senderType, "newui::Control");  // declared on Control, found through Button's base chain
    EXPECT_TRUE(r.arguments.empty());
}

TEST(WiringRequestBuilder, AConstReferenceArgumentKeepsItsConstAndReference) {
    ASSERT_NE(classNamed("SubView"), nullptr);
    ControllerWiringRequest r;
    ASSERT_TRUE(buildControllerWiringRequest(*classNamed("SubView"), "onSizeChanged", "panel", "C", r));
    EXPECT_EQ(r.viewType, "newui::SubView");
    EXPECT_EQ(r.senderType, "newui::View");
    ASSERT_EQ(r.arguments.size(), 1u);
    EXPECT_EQ(r.arguments[0].first, "const newui::Size&");
    EXPECT_EQ(r.arguments[0].second, "size");
}

TEST(WiringRequestBuilder, PrimitiveArgumentsGetPositionalNamesAndClassArgumentsGetTypeNames) {
    ASSERT_NE(classNamed("SubView"), nullptr);
    ControllerWiringRequest r;
    ASSERT_TRUE(buildControllerWiringRequest(*classNamed("SubView"), "onMouseDown", "panel", "C", r));
    ASSERT_EQ(r.arguments.size(), 3u);
    EXPECT_EQ(r.arguments[0].first, "const newui::Point&");
    EXPECT_EQ(r.arguments[0].second, "point");
    EXPECT_EQ(r.arguments[1].first, "unsigned int");   // std::uint32_t
    EXPECT_EQ(r.arguments[1].second, "arg2");
    EXPECT_EQ(r.arguments[2].first, "unsigned int");
    EXPECT_EQ(r.arguments[2].second, "arg3");
}

TEST(WiringRequestBuilder, ADelegateReflectionCannotInvokeIsNotOfferedForWiring) {
    // Delegate<View, Size&> (non-const reference) is registered as a plain field, not a Delegate.
    ASSERT_NE(classNamed("SubView"), nullptr);
    ControllerWiringRequest r;
    EXPECT_FALSE(buildControllerWiringRequest(*classNamed("SubView"), "onQueryContentSize", "panel", "C", r));
}

TEST(WiringRequestBuilder, AnUnknownDelegateFailsAndLeavesTheRequestUntouched) {
    ASSERT_NE(classNamed("Button"), nullptr);
    ControllerWiringRequest r;
    r.className = "untouched";
    EXPECT_FALSE(buildControllerWiringRequest(*classNamed("Button"), "onNoSuchEvent", "b", "C", r));
    EXPECT_EQ(r.className, "untouched");
}

TEST(WiringRequestBuilder, EveryDelegateOnRealControlsHasASpellingForItsSenderAndArguments) {
    // Guards the compile-time spelling in reflection.h: nothing registered may come back blank.
    for (const char* name : {"Button", "SubView", "Toggle", "Label"}) {
        const newui::reflection::Class* clazz = newui::reflection::classinfo(std::string(name));
        if (clazz == nullptr) {
            continue;
        }
        std::vector<const newui::reflection::Delegate*> delegates;
        clazz->allDelegates(delegates);
        for (const newui::reflection::Delegate* delegate : delegates) {
            EXPECT_FALSE(delegate->senderSpelling().empty()) << name << "." << delegate->name();
            for (const newui::reflection::Argument& argument : delegate->arguments()) {
                EXPECT_FALSE(argument.spelling.empty()) << name << "." << delegate->name();
            }
        }
    }
}
