// findDelegateWirings / verifyDelegateBindings (cpptools, libclang) against controllers compiled with the
// REAL newui headers, so `.add()` resolves to the real newui::Delegate exactly as in a real build.

#include <gtest/gtest.h>

#include <algorithm>
#include <sstream>

#include "cpptools_codegen/controllerwiring.h"
#include "cpptools/delegatebindings.h"

using namespace cpptools;
using cpptools_codegen::ControllerWiringRequest;
using cpptools_codegen::ControllerWiringPlan;
using cpptools_codegen::DelegateWiringEdit;
using cpptools_codegen::planControllerDelegateWiring;

namespace {

std::vector<std::string> newuiCompileArgs() {
    std::vector<std::string> args = {"-std=c++17", "-xc++", "-DWIN32_LEAN_AND_MEAN", "-DNOMINMAX"};
    std::stringstream includes(CODEGEN_TEST_NEWUI_INCLUDES);  // '|' separated, set by CMake
    for (std::string dir; std::getline(includes, dir, '|');) {
        args.push_back("-I" + dir);
    }
    return args;
}

const char* kIncludes =
    "#include <newui/rootcontroller.h>\n"
    "#include <newui/controls.h>\n"
    "#include <newui/subview.h>\n";

// A controller the way the generator writes it (onClick wired to a handler on the controller itself).
std::string generatedController() {
    const std::string content = std::string(kIncludes) +
        "class SaveDialogController : public newui::RootController {\n"
        "public:\n"
        "    using RootController::RootController;\n"
        "};\n";
    ControllerWiringRequest r;
    r.className = "SaveDialogController";
    r.viewName = "saveButton";
    r.viewType = "newui::Button";
    r.delegateName = "onClick";
    r.senderType = "newui::Control";
    const ControllerWiringPlan plan = planControllerDelegateWiring(content, r);
    std::vector<DelegateWiringEdit> edits = plan.edits;
    std::stable_sort(edits.begin(), edits.end(),
                     [](const DelegateWiringEdit& a, const DelegateWiringEdit& b) { return a.offset < b.offset; });
    std::string result = content;
    for (auto it = edits.rbegin(); it != edits.rend(); ++it) {
        result.insert(it->offset, it->text);
    }
    return result;
}

// A controller with a helper member that owns the handler.
std::string memberTargetController(const std::string& memberDecl, const std::string& presenterDef) {
    return std::string(kIncludes) + presenterDef +
        "class SaveDialogController : public newui::RootController {\n"
        "public:\n"
        "    using RootController::RootController;\n"
        "protected:\n"
        "    bool internal_init() override {\n"
        "        if (!newui::Component::internal_init()) { return false; }\n"
        "        saveButton_->onClick.add(&presenter_, &Presenter::onSave);\n"
        "        return true;\n"
        "    }\n"
        "private:\n"
        "    newui::Button* saveButton_ = nullptr;\n"
        "    " + memberDecl + "\n"
        "};\n";
}

const char* kPresenter =
    "struct Presenter {\n"
    "    newui::SyncReturn onSave(newui::Control& sender) { return newui::SyncReturn::Handled; }\n"
    "};\n";

RecordedBinding saveClick(const std::string& descriptor = "this@SaveDialogController.onSaveButtonClick") {
    RecordedBinding b;
    b.viewField = "saveButton_";
    b.delegate = "onClick";
    b.descriptor = descriptor;
    b.senderType = "newui::Control";
    return b;
}

BindingCheck verifyOne(const std::string& content, const RecordedBinding& binding) {
    const auto checks = verifyDelegateBindings(content, "SaveDialogController", {binding}, newuiCompileArgs());
    EXPECT_EQ(checks.size(), 1u);
    return checks.empty() ? BindingCheck{BindingStatus::CannotVerify, "no result"} : checks[0];
}

std::string without(std::string text, const std::string& piece) {
    const std::size_t at = text.find(piece);
    EXPECT_NE(at, std::string::npos) << piece;
    if (at != std::string::npos) {
        text.erase(at, piece.size());
    }
    return text;
}

} // namespace

TEST(FindDelegateWirings, ReportsTheGeneratedCallWithItsViewEventTargetAndMethod) {
    const auto wirings = findDelegateWirings(generatedController(), "SaveDialogController", newuiCompileArgs());
    ASSERT_EQ(wirings.size(), 1u);
    EXPECT_EQ(wirings[0].viewField, "saveButton_");
    EXPECT_EQ(wirings[0].delegate, "onClick");
    EXPECT_EQ(wirings[0].target, "this");
    EXPECT_EQ(wirings[0].methodClass, "SaveDialogController");
    EXPECT_EQ(wirings[0].method, "onSaveButtonClick");
}

TEST(FindDelegateWirings, ReportsAMemberObjectTargetByItsMemberName) {
    const auto wirings = findDelegateWirings(memberTargetController("Presenter presenter_;", kPresenter),
                                             "SaveDialogController", newuiCompileArgs());
    ASSERT_EQ(wirings.size(), 1u);
    EXPECT_EQ(wirings[0].target, "presenter_");
    EXPECT_EQ(wirings[0].methodClass, "Presenter");
    EXPECT_EQ(wirings[0].method, "onSave");
}

TEST(FindDelegateWirings, IsEmptyForAMissingClassOrAnUnwiredController) {
    EXPECT_TRUE(findDelegateWirings(generatedController(), "Nope", newuiCompileArgs()).empty());
    const std::string unwired = std::string(kIncludes) + "class SaveDialogController : public newui::RootController {};\n";
    EXPECT_TRUE(findDelegateWirings(unwired, "SaveDialogController", newuiCompileArgs()).empty());
}

TEST(VerifyDelegateBindings, AGeneratedWiringIsOk) {
    const BindingCheck check = verifyOne(generatedController(), saveClick());
    EXPECT_EQ(check.status, BindingStatus::Ok) << check.detail;
}

TEST(VerifyDelegateBindings, WithRealIncludePathsTheHeaderItselfHasNoDiagnostics) {
    std::vector<Diagnostic> diagnostics;
    verifyDelegateBindings(generatedController(), "SaveDialogController", {saveClick()}, newuiCompileArgs(), &diagnostics);
    // newui's own headers warn about themselves (fromMainFile false) - not the controller's problem.
    for (const Diagnostic& d : diagnostics) {
        EXPECT_FALSE(d.fromMainFile) << d.location.line << ":" << d.location.column << " " << d.message;
    }
}

TEST(VerifyDelegateBindings, AnIncludeThatCantBeFoundIsReportedAtItsLine) {
    // No include paths: <newui/rootcontroller.h> can't be found - a real problem for this parse.
    std::vector<Diagnostic> diagnostics;
    verifyDelegateBindings(generatedController(), "SaveDialogController", {saveClick()}, {"-std=c++17", "-xc++"}, &diagnostics);
    const auto missing = std::find_if(diagnostics.begin(), diagnostics.end(), [](const Diagnostic& d) {
        return d.message.find("rootcontroller.h") != std::string::npos;
    });
    ASSERT_NE(missing, diagnostics.end());
    EXPECT_TRUE(missing->fromMainFile);
    EXPECT_GE(missing->severity, Severity::Error);
    EXPECT_GT(missing->location.line, 0u);
}

TEST(VerifyDelegateBindings, AMissingControllerClassIsReportedForEveryBinding) {
    const auto checks = verifyDelegateBindings(generatedController(), "Nope", {saveClick(), saveClick()}, newuiCompileArgs());
    ASSERT_EQ(checks.size(), 2u);
    EXPECT_EQ(checks[0].status, BindingStatus::ControllerMissing);
    EXPECT_EQ(checks[1].status, BindingStatus::ControllerMissing);
}

TEST(VerifyDelegateBindings, ARenamedOrDeletedHandlerIsMethodMissing) {
    const BindingCheck check = verifyOne(generatedController(), saveClick("this@SaveDialogController.onGone"));
    EXPECT_EQ(check.status, BindingStatus::MethodMissing) << check.detail;
}

TEST(VerifyDelegateBindings, AHandlerThatNoLongerTakesWhatTheEventPassesIsASignatureMismatch) {
    // The user changed the handler's parameter type in code.
    std::string source = generatedController();
    const std::size_t at = source.find("newui::Control& sender");
    ASSERT_NE(at, std::string::npos);
    source.replace(at, std::string("newui::Control& sender").size(), "newui::View& sender");
    // ... which also breaks the .add(), so this checks the method-level signature rule on its own:
    const BindingCheck check = verifyOne(source, saveClick());
    EXPECT_EQ(check.status, BindingStatus::SignatureMismatch) << check.detail;
}

TEST(VerifyDelegateBindings, ExpectingDifferentArgumentsThanTheHandlerTakesIsASignatureMismatch) {
    RecordedBinding wrong = saveClick();
    wrong.argumentTypes = {"const newui::Size&"};
    EXPECT_EQ(verifyOne(generatedController(), wrong).status, BindingStatus::SignatureMismatch);
}

TEST(VerifyDelegateBindings, ADeletedAddCallIsNotWiredEvenThoughTheMethodStillExists) {
    const std::string source =
        without(generatedController(), "saveButton_->onClick.add(this, &SaveDialogController::onSaveButtonClick);");
    const BindingCheck check = verifyOne(source, saveClick());
    EXPECT_EQ(check.status, BindingStatus::NotWired) << check.detail;
}

TEST(VerifyDelegateBindings, AWiringOnADifferentEventDoesNotCount) {
    RecordedBinding wrongEvent = saveClick();
    wrongEvent.delegate = "onMouseDown";
    EXPECT_EQ(verifyOne(generatedController(), wrongEvent).status, BindingStatus::NotWired);
}

TEST(VerifyDelegateBindings, AMemberObjectTargetIsVerifiedThroughItsOwnClass) {
    const std::string source = memberTargetController("Presenter presenter_;", kPresenter);
    EXPECT_EQ(verifyOne(source, saveClick("presenter_@Presenter.onSave")).status, BindingStatus::Ok);
    EXPECT_EQ(verifyOne(source, saveClick("presenter_@Presenter.onGone")).status, BindingStatus::MethodMissing);
    EXPECT_EQ(verifyOne(source, saveClick("nobody_@Presenter.onSave")).status, BindingStatus::ObjectMissing);
}

TEST(VerifyDelegateBindings, APointerMemberTargetIsFollowedToItsType) {
    // A `Presenter*` member is resolved through the pointer; the wiring itself uses this expression form.
    std::string source = memberTargetController("Presenter* presenter_ = nullptr;", kPresenter);
    source = without(source, "&presenter_, ");
    source.replace(source.find("&Presenter::onSave"), 0, "presenter_, ");
    const BindingCheck check = verifyOne(source, saveClick("presenter_@Presenter.onSave"));
    EXPECT_NE(check.status, BindingStatus::ObjectMissing) << check.detail;
    EXPECT_NE(check.status, BindingStatus::CannotVerify) << check.detail;
}

TEST(VerifyDelegateBindings, AMemberWhoseTypeIsOnlyForwardDeclaredCannotBeVerified) {
    const std::string source =
        std::string(kIncludes) + "struct Presenter;\n"
        "class SaveDialogController : public newui::RootController {\n"
        "private:\n"
        "    Presenter* presenter_ = nullptr;\n"
        "};\n";
    const BindingCheck check = verifyOne(source, saveClick("presenter_@Presenter.onSave"));
    EXPECT_EQ(check.status, BindingStatus::CannotVerify) << check.detail;
}

// The bug the user hit live: a generated controller that includes only rootcontroller.h names
// newui::Control/Button, which controls.h declares. Clang turns the unknown type into a placeholder,
// so the handler's parameter reads as int& - that is a header that doesn't compile, not a changed signature.
TEST(VerifyDelegateBindings, AHandlerUsingATypeTheHeaderCannotResolveIsHeaderErrorsNotASignatureMismatch) {
    const std::string source =
        std::string("#include <newui/rootcontroller.h>\n") +   // no controls.h: Control and Button are not declared
        "class SaveDialogController : public newui::RootController {\n"
        "private:\n"
        "    newui::SyncReturn onSaveButtonClick(newui::Control& sender) { return newui::SyncReturn::Handled; }\n"
        "};\n";
    const BindingCheck check = verifyOne(source, saveClick());
    EXPECT_EQ(check.status, BindingStatus::HeaderErrors) << check.detail;
    EXPECT_NE(check.detail.find("#include"), std::string::npos) << check.detail;
}

TEST(VerifyDelegateBindings, TheSameHandlerWithTheIncludePresentIsOkNotHeaderErrors) {
    const std::string source =
        std::string("#include <newui/controls.h>\n#include <newui/rootcontroller.h>\n") +
        "class SaveDialogController : public newui::RootController {\n"
        "public:\n"
        "    newui::SyncReturn onSaveButtonClick(newui::Control& sender) { return newui::SyncReturn::Handled; }\n"
        "protected:\n"
        "    bool internal_init() override {\n"
        "        saveButton_->onClick.add(this, &SaveDialogController::onSaveButtonClick);\n"
        "        return true;\n"
        "    }\n"
        "private:\n"
        "    newui::Button* saveButton_ = nullptr;\n"
        "};\n";
    EXPECT_EQ(verifyOne(source, saveClick()).status, BindingStatus::Ok);
}

TEST(VerifyDelegateBindings, AMalformedDescriptorCannotBeVerified) {
    for (const char* bad : {"", "noat", "@Class.method", "obj@Class", "obj@.method", "obj@Class."}) {
        EXPECT_EQ(verifyOne(generatedController(), saveClick(bad)).status, BindingStatus::CannotVerify) << bad;
    }
}

TEST(VerifyDelegateBindings, ResultsLineUpWithTheBindingsGiven) {
    const auto checks = verifyDelegateBindings(
        generatedController(), "SaveDialogController",
        {saveClick(), saveClick("this@SaveDialogController.onGone"), saveClick()}, newuiCompileArgs());
    ASSERT_EQ(checks.size(), 3u);
    EXPECT_EQ(checks[0].status, BindingStatus::Ok);
    EXPECT_EQ(checks[1].status, BindingStatus::MethodMissing);
    EXPECT_EQ(checks[2].status, BindingStatus::Ok);
}
