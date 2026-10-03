// planControllerDelegateWiring's output checked against the REAL newui headers (not a stand-in):
// the generated controller is reparsed by libclang with newui's include paths, so the generated
// `.add(this, &Class::handler)` has to match newui::Delegate's actual add() overloads, and the
// connect field's type has to exist. Each positive test has a negative twin with a deliberately
// wrong handler signature that must FAIL to parse, so a pass can't be vacuous.

#include <gtest/gtest.h>

#include <algorithm>
#include <sstream>

#include "cpptools/parser.h"
#include "cpptools_codegen/controllerwiring.h"

using namespace cpptools_codegen;

namespace {

std::vector<std::string> newuiCompileArgs() {
    std::vector<std::string> args = {"-std=c++17", "-xc++", "-DWIN32_LEAN_AND_MEAN", "-DNOMINMAX"};
    std::stringstream includes(CODEGEN_TEST_NEWUI_INCLUDES);  // '|' separated, set by CMake
    for (std::string dir; std::getline(includes, dir, '|');) {
        args.push_back("-I" + dir);
    }
    return args;
}

const char* kHeaderIncludes =
    "#include <newui/rootcontroller.h>\n"
    "#include <newui/controls.h>\n"
    "#include <newui/subview.h>\n";

// The controller as a designer would find it: real includes, an empty RootController subclass.
std::string controllerSource() {
    return std::string(kHeaderIncludes) +
           "class SaveDialogController : public newui::RootController {\n"
           "public:\n"
           "    using RootController::RootController;\n"
           "};\n";
}

std::string applyPlan(const std::string& content, std::vector<DelegateWiringEdit> edits) {
    std::stable_sort(edits.begin(), edits.end(),
                     [](const DelegateWiringEdit& a, const DelegateWiringEdit& b) { return a.offset < b.offset; });
    std::string result = content;
    for (auto it = edits.rbegin(); it != edits.rend(); ++it) {
        result.insert(it->offset, it->text);
    }
    return result;
}

// Error-or-worse diagnostics from parsing text against the real headers.
std::vector<std::string> errorsOf(const std::string& text) {
    cpptools::Parser parser;
    const cpptools::ParseResult result = parser.parseBuffer("controller.h", text, newuiCompileArgs());
    std::vector<std::string> errors;
    for (const cpptools::Diagnostic& d : result.diagnostics) {
        if (static_cast<int>(d.severity) >= static_cast<int>(cpptools::Severity::Error)) {
            errors.push_back(d.message);
        }
    }
    return errors;
}

std::string joined(const std::vector<std::string>& lines) {
    std::string out;
    for (const std::string& line : lines) {
        out += line + "\n";
    }
    return out;
}

std::string replaceFirst(std::string text, const std::string& from, const std::string& to) {
    const std::size_t at = text.find(from);
    EXPECT_NE(at, std::string::npos) << "missing: " << from;
    if (at != std::string::npos) {
        text.replace(at, from.size(), to);
    }
    return text;
}

// The good text must parse clean, and the same text with one deliberate mistake must not - so a
// failure to find the headers at all can never make a "rejected" test pass.
void expectMutationRejected(const std::string& good, const std::string& from, const std::string& to) {
    ASSERT_TRUE(errorsOf(good).empty()) << "baseline must be clean first:\n" << joined(errorsOf(good));
    EXPECT_FALSE(errorsOf(replaceFirst(good, from, to)).empty()) << "mutation was accepted: " << from << " -> " << to;
}

ControllerWiringRequest onClick() {
    ControllerWiringRequest r;
    r.className = "SaveDialogController";
    r.viewName = "saveButton";
    r.viewType = "newui::Button";
    r.delegateName = "onClick";
    r.senderType = "newui::Control";  // Button::onClick is a Delegate<Control>
    return r;
}

ControllerWiringRequest onSizeChanged() {
    ControllerWiringRequest r;
    r.className = "SaveDialogController";
    r.viewName = "panel";
    r.viewType = "newui::SubView";
    r.delegateName = "onSizeChanged";
    r.senderType = "newui::View";  // Delegate<View, const Size&>
    r.arguments = {{"const newui::Size&", "size"}};
    return r;
}

ControllerWiringRequest onMouseDown() {
    ControllerWiringRequest r;
    r.className = "SaveDialogController";
    r.viewName = "panel";
    r.viewType = "newui::SubView";
    r.delegateName = "onMouseDown";
    r.senderType = "newui::View";  // Delegate<View, const Point&, uint32_t, uint32_t>
    r.arguments = {{"const newui::Point&", "pt"}, {"std::uint32_t", "buttons"}, {"std::uint32_t", "keyMask"}};
    return r;
}

std::string wire(const ControllerWiringRequest& request) {
    const std::string content = controllerSource();
    const ControllerWiringPlan plan = planControllerDelegateWiring(content, request);
    EXPECT_EQ(plan.status, ControllerWiringStatus::Ok);
    return applyPlan(content, plan.edits);
}

} // namespace

TEST(ControllerWiringRealNewui, TheUnwiredControllerItselfParsesClean) {
    // Baseline: if this fails the include paths are wrong and every test below means nothing.
    EXPECT_TRUE(errorsOf(controllerSource()).empty()) << joined(errorsOf(controllerSource()));
}

TEST(ControllerWiringRealNewui, ButtonOnClickCompilesAgainstTheRealDelegate) {
    const std::string generated = wire(onClick());
    EXPECT_TRUE(errorsOf(generated).empty()) << joined(errorsOf(generated)) << "\n---\n" << generated;
}

TEST(ControllerWiringRealNewui, ButtonOnClickWithTheSenderByPointerIsRejectedByTheRealDelegate) {
    expectMutationRejected(wire(onClick()), "newui::Control& sender", "newui::Control* sender");
}

TEST(ControllerWiringRealNewui, OnSizeChangedWithAConstReferenceArgumentCompiles) {
    const std::string generated = wire(onSizeChanged());
    EXPECT_TRUE(errorsOf(generated).empty()) << joined(errorsOf(generated)) << "\n---\n" << generated;
}

TEST(ControllerWiringRealNewui, OnSizeChangedWithTheWrongArgumentTypeIsRejected) {
    expectMutationRejected(wire(onSizeChanged()), "const newui::Size& size", "const newui::Point& size");
}

TEST(ControllerWiringRealNewui, OnMouseDownWithThreeArgumentsCompiles) {
    const std::string generated = wire(onMouseDown());
    EXPECT_TRUE(errorsOf(generated).empty()) << joined(errorsOf(generated)) << "\n---\n" << generated;
}

TEST(ControllerWiringRealNewui, OnMouseDownMissingAnArgumentIsRejected) {
    expectMutationRejected(wire(onMouseDown()), ", std::uint32_t keyMask", "");
}

TEST(ControllerWiringRealNewui, TwoWiringsOnDifferentControlsBothCompile) {
    std::string content = controllerSource();
    content = applyPlan(content, planControllerDelegateWiring(content, onClick()).edits);
    content = applyPlan(content, planControllerDelegateWiring(content, onSizeChanged()).edits);
    EXPECT_TRUE(errorsOf(content).empty()) << joined(errorsOf(content)) << "\n---\n" << content;
}
