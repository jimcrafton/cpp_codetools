#include <gtest/gtest.h>

#include "cpptools/parser.h"
#include "cpptools_codegen/classbuilder.h"

using namespace cpptools_codegen;

TEST(ClassBuilderTest, GroupsMembersByAccessSpecifierPublicProtectedThenPrivate) {
    ClassBuilder builder("Widget");
    builder.addPrivateField("int value_ = 0;");
    builder.addProtectedField("int scratch_ = 0;");
    builder.addPublicField("static constexpr int kMax = 100;");

    EXPECT_EQ(builder.toString(),
              "class Widget {\n"
              "public:\n"
              "    static constexpr int kMax = 100;\n"
              "protected:\n"
              "    int scratch_ = 0;\n"
              "private:\n"
              "    int value_ = 0;\n"
              "};\n");
}

TEST(ClassBuilderTest, OmitsAccessBlocksWithNoMembers) {
    ClassBuilder builder("Empty");
    builder.addPublicField("int x;");

    EXPECT_EQ(builder.toString(), "class Empty {\npublic:\n    int x;\n};\n");
}

TEST(ClassBuilderTest, SupportsStructAndBaseClasses) {
    ClassBuilder builder("Derived");
    builder.makeStruct();
    builder.addBaseClass("Base");
    builder.addBaseClass("IInterface", "private");
    builder.addPublicField("int x;");

    EXPECT_EQ(builder.toString(),
              "struct Derived : public Base, private IInterface {\n"
              "public:\n"
              "    int x;\n"
              "};\n");
}

TEST(ClassBuilderTest, MethodsInTheSameAccessBlockFollowFieldsAndKeepAddedOrder) {
    ClassBuilder builder("Widget");
    builder.addPublicField("int x;");
    MethodBuilder first("first");
    MethodBuilder second("second");
    builder.addPublicMethod(first);
    builder.addPublicMethod(second);

    EXPECT_EQ(builder.toString(),
              "class Widget {\n"
              "public:\n"
              "    int x;\n"
              "\n"
              "    void first() {\n"
              "    }\n"
              "    void second() {\n"
              "    }\n"
              "};\n");
}

// Per bluesky/cpp-codegen-plan.md's own "Testing approach": verify by actually reparsing the
// generated result through the real libclang pipeline, not by trusting toString()'s own string-
// building logic - same discipline test_parser.cpp's Session tests already use for Rename.
TEST(ClassBuilderTest, GeneratedClassActuallyParsesAndHasTheRightMembers) {
    ClassBuilder builder("Widget");
    MethodBuilder getValue("getValue");
    getValue.returnType("int").makeConst();
    getValue.addBodyLine("return value_;");
    builder.addPublicMethod(getValue);
    builder.addPrivateField("int value_ = 0;");

    const std::string generated = builder.toString();

    cpptools::Parser parser;
    const cpptools::ParseResult result = parser.parseBuffer("widget.h", generated);

    for (const cpptools::Diagnostic& diagnostic : result.diagnostics) {
        EXPECT_LT(static_cast<int>(diagnostic.severity), static_cast<int>(cpptools::Severity::Error))
            << diagnostic.message;
    }

    ASSERT_EQ(result.symbols.size(), 1u);
    const cpptools::Symbol& widget = result.symbols[0];
    EXPECT_EQ(widget.kind, cpptools::SymbolKind::Class);
    EXPECT_EQ(widget.name, "Widget");

    bool foundMethod = false;
    bool foundField = false;
    for (const cpptools::Symbol& child : widget.children) {
        if (child.kind == cpptools::SymbolKind::Method && child.name == "getValue") {
            foundMethod = true;
        }
        if (child.kind == cpptools::SymbolKind::Field && child.name == "value_") {
            foundField = true;
        }
    }
    EXPECT_TRUE(foundMethod);
    EXPECT_TRUE(foundField);
}
