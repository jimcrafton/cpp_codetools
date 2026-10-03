#include <gtest/gtest.h>

#include "cpptools/parser.h"
#include "cpptools_codegen/classbuilder.h"
#include "cpptools_codegen/fieldinsertion.h"

using namespace cpptools_codegen;

namespace {

std::string applyEdit(std::string content, const DelegateWiringEdit& edit) {
    content.insert(edit.offset, edit.text);
    return content;
}

const cpptools::Symbol* findChild(const cpptools::Symbol& parent, const std::string& name) {
    for (const cpptools::Symbol& child : parent.children) {
        if (child.name == name) {
            return &child;
        }
    }
    return nullptr;
}

} // namespace

TEST(FieldInsertionTest, DeclarationIsAReflectCommentPlusANullPointerField) {
    EXPECT_EQ(connectFieldDeclaration("ProgressBar", "progressBar_"),
              "    //@reflect connect=true\n    ProgressBar* progressBar_ = nullptr;\n");
}

TEST(FieldInsertionTest, ReturnsNulloptWhenTheClassDoesNotExist) {
    EXPECT_FALSE(planConnectField("class Widget {};\n", "Missing", "Button", "ok_").has_value());
}

TEST(FieldInsertionTest, InsertsAPrivateConnectFieldThatReparsesAsAField) {
    const std::string content = "struct Button {};\nclass Widget {\npublic:\n    int x = 0;\n};\n";

    const auto edit = planConnectField(content, "Widget", "Button", "okButton_");
    ASSERT_TRUE(edit.has_value());
    const std::string generated = applyEdit(content, *edit);

    EXPECT_NE(generated.find("//@reflect connect=true\n    Button* okButton_ = nullptr;"), std::string::npos);

    cpptools::Parser parser;
    const cpptools::ParseResult result = parser.parseBuffer("widget.h", generated);
    for (const cpptools::Diagnostic& diagnostic : result.diagnostics) {
        EXPECT_LT(static_cast<int>(diagnostic.severity), static_cast<int>(cpptools::Severity::Error))
            << diagnostic.message;
    }
    const cpptools::Symbol* widget = nullptr;
    for (const cpptools::Symbol& top : result.symbols) {
        if (top.name == "Widget") {
            widget = &top;
        }
    }
    ASSERT_NE(widget, nullptr);
    EXPECT_NE(findChild(*widget, "okButton_"), nullptr);
}

TEST(FieldInsertionTest, DoesNotDuplicateAnExistingField) {
    const std::string content = "struct Button {};\nclass Widget {\n    Button* okButton_ = nullptr;\n};\n";

    EXPECT_FALSE(planConnectField(content, "Widget", "Button", "okButton_").has_value());
}

TEST(FieldInsertionTest, ApplyingThePlanTwiceIsIdempotent) {
    const std::string content = "struct Button {};\nclass Widget {};\n";

    const auto first = planConnectField(content, "Widget", "Button", "okButton_");
    ASSERT_TRUE(first.has_value());
    const std::string once = applyEdit(content, *first);

    EXPECT_FALSE(planConnectField(once, "Widget", "Button", "okButton_").has_value());
}

TEST(FieldInsertionTest, ClassBuilderEmitsTheSameConnectFieldInThePrivateSection) {
    ClassBuilder builder("SaveDialogController");
    builder.addBaseClass("newui::RootController").addConnectField("newui::Button", "saveButton_");

    const std::string text = builder.toString();
    EXPECT_NE(text.find("private:\n    //@reflect connect=true\n    newui::Button* saveButton_ = nullptr;\n"),
              std::string::npos)
        << text;
}
