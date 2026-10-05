#include <gtest/gtest.h>

#include <algorithm>

#include "cpptools/parser.h"
#include "cpptools_codegen/controllerwiring.h"

using namespace cpptools_codegen;

namespace {

// Just enough of newui for the generated code to type-check when reparsed: a Delegate whose add()
// takes a SyncReturn(Sender&) member, the Component::internal_init() hook, and RootController.
const char* kPreamble =
    "namespace newui {\n"
    "enum class SyncReturn { Handled, Ignored };\n"
    "template<typename S, typename... A> struct Delegate {\n"
    "    template<typename T> void add(T* i, SyncReturn (T::*m)(S&, A...)) {}\n"
    "};\n"
    "struct Control { Delegate<Control> onClick; Delegate<Control, unsigned> onKey; };\n"
    "struct Button : Control {};\n"
    "class Component { protected: virtual bool internal_init() { return true; } };\n"
    "class RootController : public Component {};\n"
    "}\n";

const char* kController =
    "class SaveDialogController : public newui::RootController {\n"
    "public:\n"
    "    SaveDialogController() = default;\n"
    "};\n";

// Edits are against the original content; same-offset insertions must keep the order given, so
// stable-sort ascending and splice from the back.
std::string applyPlan(const std::string& content, std::vector<DelegateWiringEdit> edits) {
    std::stable_sort(edits.begin(), edits.end(),
                     [](const DelegateWiringEdit& a, const DelegateWiringEdit& b) { return a.offset < b.offset; });
    std::string result = content;
    for (auto it = edits.rbegin(); it != edits.rend(); ++it) {
        result.insert(it->offset, it->text);
    }
    return result;
}

ControllerWiringRequest saveButtonClick() {
    ControllerWiringRequest r;
    r.className = "SaveDialogController";
    r.viewName = "saveButton";
    r.viewType = "newui::Button";
    r.delegateName = "onClick";
    r.senderType = "newui::Control";
    return r;
}

const cpptools::Symbol* findChild(const cpptools::Symbol& parent, const std::string& name) {
    for (const cpptools::Symbol& child : parent.children) {
        if (child.name == name) {
            return &child;
        }
    }
    return nullptr;
}

std::size_t countChildren(const cpptools::Symbol& parent, const std::string& name) {
    std::size_t n = 0;
    for (const cpptools::Symbol& child : parent.children) {
        n += child.name == name ? 1 : 0;
    }
    return n;
}

// Reparses text and returns the controller class; fails the test on any error diagnostic.
cpptools::ParseResult parseClean(const std::string& text) {
    cpptools::Parser parser;
    cpptools::ParseResult result = parser.parseBuffer("controller.h", text);
    for (const cpptools::Diagnostic& diagnostic : result.diagnostics) {
        EXPECT_LT(static_cast<int>(diagnostic.severity), static_cast<int>(cpptools::Severity::Error))
            << diagnostic.message << "\n---\n" << text;
    }
    return result;
}

const cpptools::Symbol* controllerIn(const cpptools::ParseResult& result) {
    for (const cpptools::Symbol& top : result.symbols) {
        if (top.name == "SaveDialogController") {
            return &top;
        }
    }
    return nullptr;
}

} // namespace

TEST(ControllerWiringNames, HandlerIsOnViewThenEventWithoutItsOnPrefix) {
    EXPECT_EQ(defaultHandlerName("saveButton", "onClick"), "onSaveButtonClick");
    EXPECT_EQ(defaultHandlerName("nameEdit", "onTextChanged"), "onNameEditTextChanged");
    EXPECT_EQ(defaultHandlerName("ok", "click"), "onOkClick");        // no "on" prefix to strip
    EXPECT_EQ(defaultHandlerName("one", "online"), "onOneOnline");    // "online" is not "on"+Uppercase
}

TEST(ControllerWiringNames, FieldIsTheViewNameWithATrailingUnderscore) {
    EXPECT_EQ(controllerFieldName("saveButton"), "saveButton_");
}

TEST(ControllerWiring, ReportsAMissingClass) {
    ControllerWiringRequest r = saveButtonClick();
    r.className = "Missing";
    const ControllerWiringPlan plan = planControllerDelegateWiring(std::string(kPreamble) + kController, r);
    EXPECT_EQ(plan.status, ControllerWiringStatus::ClassNotFound);
    EXPECT_TRUE(plan.edits.empty());
}

TEST(ControllerWiring, AddsTheFieldTheHandlerAndTheWiringAndTheResultReparsesClean) {
    const std::string content = std::string(kPreamble) + kController;
    const ControllerWiringPlan plan = planControllerDelegateWiring(content, saveButtonClick());
    ASSERT_EQ(plan.status, ControllerWiringStatus::Ok);
    EXPECT_EQ(plan.handlerName, "onSaveButtonClick");
    EXPECT_EQ(plan.fieldName, "saveButton_");

    const std::string generated = applyPlan(content, plan.edits);
    EXPECT_NE(generated.find("//@reflect connect=true\n    newui::Button* saveButton_ = nullptr;"), std::string::npos)
        << generated;
    EXPECT_NE(generated.find("saveButton_->onClick.add(this, &SaveDialogController::onSaveButtonClick);"),
              std::string::npos)
        << generated;

    const cpptools::ParseResult result = parseClean(generated);
    const cpptools::Symbol* controller = controllerIn(result);
    ASSERT_NE(controller, nullptr);
    EXPECT_NE(findChild(*controller, "saveButton_"), nullptr);
    EXPECT_NE(findChild(*controller, "onSaveButtonClick"), nullptr);
    EXPECT_EQ(countChildren(*controller, "internal_init"), 1u);
}

TEST(ControllerWiring, AHandlerReturnsHandledAndTakesTheDelegatesSenderByReference) {
    const std::string content = std::string(kPreamble) + kController;
    const std::string generated = applyPlan(content, planControllerDelegateWiring(content, saveButtonClick()).edits);
    EXPECT_NE(generated.find("newui::SyncReturn onSaveButtonClick(newui::Control& sender)"), std::string::npos)
        << generated;
    EXPECT_NE(generated.find("return newui::SyncReturn::Handled;"), std::string::npos);
}

TEST(ControllerWiring, ASecondWiringAppendsIntoTheSameInternalInit) {
    std::string content = std::string(kPreamble) + kController;
    content = applyPlan(content, planControllerDelegateWiring(content, saveButtonClick()).edits);

    ControllerWiringRequest cancel = saveButtonClick();
    cancel.viewName = "cancelButton";
    const ControllerWiringPlan second = planControllerDelegateWiring(content, cancel);
    ASSERT_EQ(second.status, ControllerWiringStatus::Ok);
    content = applyPlan(content, second.edits);

    const cpptools::ParseResult result = parseClean(content);
    const cpptools::Symbol* controller = controllerIn(result);
    ASSERT_NE(controller, nullptr);
    EXPECT_NE(findChild(*controller, "onSaveButtonClick"), nullptr);
    EXPECT_NE(findChild(*controller, "onCancelButtonClick"), nullptr);
    EXPECT_NE(findChild(*controller, "saveButton_"), nullptr);
    EXPECT_NE(findChild(*controller, "cancelButton_"), nullptr);
    EXPECT_EQ(countChildren(*controller, "internal_init"), 1u);

    // Both wirings sit before internal_init's return, not after it as dead code.
    const std::size_t ret = content.rfind("return true;");
    EXPECT_LT(content.find("saveButton_->onClick.add"), ret);
    EXPECT_LT(content.find("cancelButton_->onClick.add"), ret);
}

TEST(ControllerWiring, WiringTheSameEventTwiceIsRefusedNotDuplicated) {
    std::string content = std::string(kPreamble) + kController;
    content = applyPlan(content, planControllerDelegateWiring(content, saveButtonClick()).edits);

    const ControllerWiringPlan again = planControllerDelegateWiring(content, saveButtonClick());
    EXPECT_EQ(again.status, ControllerWiringStatus::HandlerExists);
    EXPECT_TRUE(again.edits.empty());
}

// A real controller header: its includes can't be found when it is parsed on its own, and neither can its base class.
TEST(ControllerWiring, AnExistingHandlerIsFoundEvenWhenTheHeadersIncludesAndBaseClassDoNotResolve) {
    const std::string content =
        "#include <newui/rootcontroller.h>\n"
        "#include <bojangle>\n"
        "class S1Controller : public newui::RootController {\n"
        "private:\n"
        "    newui::Button* button2_ = nullptr;\n"
        "\n"
        "private:\n"
        "    newui::SyncReturn onButton2Click(newui::Control& sender) {\n"
        "        return newui::SyncReturn::Handled;\n"
        "    }\n"
        "};\n";
    ControllerWiringRequest r;
    r.className = "S1Controller";
    r.viewName = "button2";
    r.viewType = "newui::Button";
    r.delegateName = "onClick";
    r.senderType = "newui::Control";
    const ControllerWiringPlan plan = planControllerDelegateWiring(content, r);
    EXPECT_EQ(plan.status, ControllerWiringStatus::HandlerExists);
    EXPECT_TRUE(plan.edits.empty());
}

TEST(ControllerWiring, AnAlreadyDeclaredFieldIsNotDeclaredAgain) {
    const std::string content = std::string(kPreamble) +
        "class SaveDialogController : public newui::RootController {\n"
        "private:\n"
        "    //@reflect connect=true\n"
        "    newui::Button* saveButton_ = nullptr;\n"
        "};\n";
    const ControllerWiringPlan plan = planControllerDelegateWiring(content, saveButtonClick());
    ASSERT_EQ(plan.status, ControllerWiringStatus::Ok);

    const cpptools::ParseResult result = parseClean(applyPlan(content, plan.edits));
    const cpptools::Symbol* controller = controllerIn(result);
    ASSERT_NE(controller, nullptr);
    EXPECT_EQ(countChildren(*controller, "saveButton_"), 1u);
    EXPECT_NE(findChild(*controller, "onSaveButtonClick"), nullptr);
}

TEST(ControllerWiring, ExtraDelegateArgumentsBecomeHandlerParameters) {
    ControllerWiringRequest r = saveButtonClick();
    r.delegateName = "onKey";
    r.arguments = {{"unsigned", "keyCode"}};
    const std::string content = std::string(kPreamble) + kController;
    const ControllerWiringPlan plan = planControllerDelegateWiring(content, r);
    ASSERT_EQ(plan.status, ControllerWiringStatus::Ok);
    EXPECT_EQ(plan.handlerName, "onSaveButtonKey");

    const std::string generated = applyPlan(content, plan.edits);
    EXPECT_NE(generated.find("onSaveButtonKey(newui::Control& sender, unsigned keyCode)"), std::string::npos)
        << generated;
    parseClean(generated);  // the .add() call type-checks against Delegate<Control, unsigned>
}

TEST(ControllerWiring, ACustomHandlerNameIsUsedVerbatim) {
    ControllerWiringRequest r = saveButtonClick();
    r.handlerName = "saveClicked";
    const std::string content = std::string(kPreamble) + kController;
    const ControllerWiringPlan plan = planControllerDelegateWiring(content, r);
    EXPECT_EQ(plan.handlerName, "saveClicked");
    EXPECT_NE(applyPlan(content, plan.edits).find("&SaveDialogController::saveClicked);"), std::string::npos);
}

TEST(ControllerWiringReuse, WiresASecondEventToAnExistingHandlerWithoutGeneratingOne) {
    // saveButton_ is already wired to onSaveButtonClick; reuse it for the key event.
    const std::string content = std::string(kPreamble) +
        "class SaveDialogController : public newui::RootController {\n"
        "public:\n"
        "    SaveDialogController() = default;\n"
        "\n"
        "protected:\n"
        "    bool internal_init() override {\n"
        "        if (!Component::internal_init()) {\n"
        "            return false;\n"
        "        }\n"
        "        return true;\n"
        "    }\n"
        "\n"
        "private:\n"
        "    //@reflect connect=true\n"
        "    newui::Button* saveButton_ = nullptr;\n"
        "    newui::SyncReturn onSaveButtonClick(newui::Control& sender) { return newui::SyncReturn::Handled; }\n"
        "};\n";

    ControllerWiringRequest r = saveButtonClick();
    r.handlerName = "onSaveButtonClick";
    r.reuseExistingHandler = true;

    const ControllerWiringPlan plan = planControllerDelegateWiring(content, r);
    ASSERT_EQ(plan.status, ControllerWiringStatus::Ok);
    const std::string generated = applyPlan(content, plan.edits);

    EXPECT_NE(generated.find("saveButton_->onClick.add(this, &SaveDialogController::onSaveButtonClick);"), std::string::npos)
        << generated;
    const cpptools::ParseResult result = parseClean(generated);
    const cpptools::Symbol* controller = controllerIn(result);
    ASSERT_NE(controller, nullptr);
    EXPECT_EQ(countChildren(*controller, "onSaveButtonClick"), 1u) << "no second handler was generated";
    EXPECT_EQ(countChildren(*controller, "saveButton_"), 1u) << "the field was already there";
    EXPECT_EQ(countChildren(*controller, "internal_init"), 1u);
}

TEST(ControllerWiringReuse, RefusesAHandlerTheClassDoesNotHave) {
    const std::string content = std::string(kPreamble) + kController;
    ControllerWiringRequest r = saveButtonClick();
    r.handlerName = "onNothingHere";
    r.reuseExistingHandler = true;

    const ControllerWiringPlan plan = planControllerDelegateWiring(content, r);
    EXPECT_EQ(plan.status, ControllerWiringStatus::HandlerNotFound);
    EXPECT_TRUE(plan.edits.empty());
}
