#include <gtest/gtest.h>

#include "cpptools_codegen/classinsertion.h"

using namespace cpptools_codegen;

TEST(ClassInsertionPointTest, FindsTheBraceRangeOfAClassDefinition) {
    const std::string content =
        "class Foo {\n"
        "public:\n"
        "    void bar();\n"
        "};\n";

    const auto result = classInsertionPoint(content, "Foo");

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(content[result->openOffset], '{');
    EXPECT_EQ(content[result->closeOffset], '}');
}

TEST(ClassInsertionPointTest, FindsAStructDefinitionToo) {
    const std::string content = "struct Bar {\n    int x;\n};\n";

    const auto result = classInsertionPoint(content, "Bar");

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(content[result->openOffset], '{');
    EXPECT_EQ(content[result->closeOffset], '}');
}

TEST(ClassInsertionPointTest, ReturnsNulloptForANameThatDoesNotExist) {
    const std::string content = "class Foo {};\n";

    EXPECT_FALSE(classInsertionPoint(content, "Bar").has_value());
}

TEST(ClassInsertionPointTest, ReturnsNulloptForAForwardDeclarationOnly) {
    const std::string content = "class Foo;\n";

    EXPECT_FALSE(classInsertionPoint(content, "Foo").has_value());
}

TEST(ClassInsertionPointTest, PicksTheDefinitionWhenAForwardDeclarationAlsoExists) {
    const std::string content =
        "class Foo;\n"
        "class Foo {\n"
        "    int x;\n"
        "};\n";

    const auto result = classInsertionPoint(content, "Foo");

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(content[result->openOffset], '{');
    EXPECT_EQ(content[result->closeOffset], '}');
}
