#include <gtest/gtest.h>

#include "cpptools_codegen/methodinsertion.h"

using namespace cpptools_codegen;

TEST(FunctionBodyInsertionPointTest, LandsJustBeforeTheClosingBraceWhenTheBodyHasNoTrailingReturn) {
    const std::string content =
        "class Widget {\n"
        "public:\n"
        "    void internal_init() {\n"
        "        int x = 0;\n"
        "    }\n"
        "};\n";

    const auto result = functionBodyInsertionPoint(content, "Widget", "internal_init");

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(content[*result], '}');
}

TEST(FunctionBodyInsertionPointTest, LandsBeforeATrailingReturnRatherThanAfterIt) {
    const std::string content =
        "class Widget {\n"
        "public:\n"
        "    bool internal_init() {\n"
        "        return true;\n"
        "    }\n"
        "};\n";

    const auto result = functionBodyInsertionPoint(content, "Widget", "internal_init");

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(content.substr(*result, 6), "return");
}

TEST(FunctionBodyInsertionPointTest, ReturnsNulloptWhenTheMethodDoesNotExist) {
    const std::string content = "class Widget {\npublic:\n    void other() {}\n};\n";

    EXPECT_FALSE(functionBodyInsertionPoint(content, "Widget", "internal_init").has_value());
}

TEST(FunctionBodyInsertionPointTest, ReturnsNulloptForADeclarationOnlyOverload) {
    const std::string content = "class Widget {\npublic:\n    bool internal_init();\n};\n";

    EXPECT_FALSE(functionBodyInsertionPoint(content, "Widget", "internal_init").has_value());
}

TEST(FunctionBodyInsertionPointTest, ReturnsNulloptWhenTheClassDoesNotExist) {
    const std::string content = "class Widget {\npublic:\n    bool internal_init() { return true; }\n};\n";

    EXPECT_FALSE(functionBodyInsertionPoint(content, "Other", "internal_init").has_value());
}
