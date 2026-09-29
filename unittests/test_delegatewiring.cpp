#include <algorithm>

#include <gtest/gtest.h>

#include "cpptools/parser.h"
#include "cpptools_codegen/delegatewiring.h"
#include "cpptools_codegen/methodbuilder.h"

using namespace cpptools_codegen;

namespace {

// Applies edits (each offset against the original content) in descending offset order, so an
// earlier edit's own offset is never shifted by a later one already applied - same discipline
// the plan doc calls for (matching Rename's own multi-occurrence apply).
std::string applyEdits(const std::string& content, std::vector<DelegateWiringEdit> edits) {
    std::sort(edits.begin(), edits.end(),
              [](const DelegateWiringEdit& a, const DelegateWiringEdit& b) { return a.offset > b.offset; });
    std::string result = content;
    for (const DelegateWiringEdit& edit : edits) {
        result.insert(edit.offset, edit.text);
    }
    return result;
}

// A fake Component base (matching newui's own: Component::internal_init() virtual, base body
// `return true;`) plus a minimal fake control/delegate pair, so a generated wiring call
// referencing a real member (saveButton_->onClick.add(...)) actually resolves when reparsed,
// instead of erroring on an undeclared identifier that has nothing to do with the logic under
// test.
const char* kComponentPreamble =
    "class Component {\n"
    "protected:\n"
    "    virtual bool internal_init() { return true; }\n"
    "};\n\n"
    "struct FakeDelegate {\n"
    "    template<typename T>\n"
    "    void add(T* instance, void (T::*method)(int)) {}\n"
    "};\n\n"
    "struct FakeButton {\n"
    "    FakeDelegate onClick;\n"
    "};\n\n";

bool hasMethodChild(const cpptools::Symbol& symbol, const std::string& name) {
    for (const cpptools::Symbol& child : symbol.children) {
        if (child.kind == cpptools::SymbolKind::Method && child.name == name) {
            return true;
        }
    }
    return false;
}

std::size_t countMethodChildren(const cpptools::Symbol& symbol, const std::string& name) {
    std::size_t count = 0;
    for (const cpptools::Symbol& child : symbol.children) {
        if (child.kind == cpptools::SymbolKind::Method && child.name == name) {
            ++count;
        }
    }
    return count;
}

} // namespace

TEST(DelegateWiringTest, ReturnsNulloptWhenTheClassDoesNotExist) {
    const std::string content = "class Widget {};\n";

    EXPECT_FALSE(planDelegateWiring(content, "Missing", "void h() {}\n", "x.add(this, &Widget::h);").has_value());
}

TEST(DelegateWiringTest, CreatesANewInternalInitOverrideWhenNoneExists) {
    const std::string content = std::string(kComponentPreamble) +
        "class Widget : public Component {\n"
        "public:\n"
        "    Widget() = default;\n"
        "    FakeButton* saveButton_ = nullptr;\n"
        "};\n";

    MethodBuilder handler("onSaveButtonClicked");
    handler.addArgument("int", "x").addBodyLine("// handle click");
    const std::string wiringCallLine = "saveButton_->onClick.add(this, &Widget::onSaveButtonClicked);";

    const auto edits = planDelegateWiring(content, "Widget", handler.toString(1), wiringCallLine);
    ASSERT_TRUE(edits.has_value());
    ASSERT_EQ(edits->size(), 1u);

    const std::string generated = applyEdits(content, *edits);

    cpptools::Parser parser;
    const cpptools::ParseResult result = parser.parseBuffer("widget.h", generated);
    for (const cpptools::Diagnostic& diagnostic : result.diagnostics) {
        EXPECT_LT(static_cast<int>(diagnostic.severity), static_cast<int>(cpptools::Severity::Error))
            << diagnostic.message;
    }

    const cpptools::Symbol* widget = nullptr;
    for (const cpptools::Symbol& top : result.symbols) {
        if (top.kind == cpptools::SymbolKind::Class && top.name == "Widget") {
            widget = &top;
        }
    }
    ASSERT_NE(widget, nullptr);
    EXPECT_TRUE(hasMethodChild(*widget, "onSaveButtonClicked"));
    EXPECT_EQ(countMethodChildren(*widget, "internal_init"), 1u);
}

TEST(DelegateWiringTest, AppendsIntoAnExistingInternalInitOverrideBeforeItsFinalReturn) {
    const std::string content = std::string(kComponentPreamble) +
        "class Widget : public Component {\n"
        "protected:\n"
        "    bool internal_init() override {\n"
        "        return Component::internal_init();\n"
        "    }\n"
        "public:\n"
        "    Widget() = default;\n"
        "    FakeButton* saveButton_ = nullptr;\n"
        "};\n";

    MethodBuilder handler("onSaveButtonClicked");
    handler.addArgument("int", "x").addBodyLine("// handle click");
    const std::string wiringCallLine = "saveButton_->onClick.add(this, &Widget::onSaveButtonClicked);";

    const auto edits = planDelegateWiring(content, "Widget", handler.toString(1), wiringCallLine);
    ASSERT_TRUE(edits.has_value());
    ASSERT_EQ(edits->size(), 2u);

    const std::string generated = applyEdits(content, *edits);

    // The wiring call must land BEFORE the existing internal_init()'s own final return - after it
    // would be dead, unreachable code.
    const std::size_t wiringPos = generated.find(wiringCallLine);
    const std::size_t returnPos = generated.find("return Component::internal_init();");
    ASSERT_NE(wiringPos, std::string::npos);
    ASSERT_NE(returnPos, std::string::npos);
    EXPECT_LT(wiringPos, returnPos);

    cpptools::Parser parser;
    const cpptools::ParseResult result = parser.parseBuffer("widget.h", generated);
    for (const cpptools::Diagnostic& diagnostic : result.diagnostics) {
        EXPECT_LT(static_cast<int>(diagnostic.severity), static_cast<int>(cpptools::Severity::Error))
            << diagnostic.message;
    }

    const cpptools::Symbol* widget = nullptr;
    for (const cpptools::Symbol& top : result.symbols) {
        if (top.kind == cpptools::SymbolKind::Class && top.name == "Widget") {
            widget = &top;
        }
    }
    ASSERT_NE(widget, nullptr);
    EXPECT_TRUE(hasMethodChild(*widget, "onSaveButtonClicked"));
    // internal_init() must not have been duplicated - exactly one, the pre-existing override.
    EXPECT_EQ(countMethodChildren(*widget, "internal_init"), 1u);
}
