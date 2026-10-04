#include <gtest/gtest.h>

#include "cpptools_codegen/classinsertion.h"

using namespace cpptools_codegen;

TEST(MergeAccessSectionsTest, GathersRepeatedPrivateSectionsIntoOne) {
    const std::string content =
        "class C {\n"
        "public:\n"
        "    void a();\n"
        "\n"
        "private:\n"
        "    int x_;\n"
        "\n"
        "protected:\n"
        "    void p();\n"
        "\n"
        "private:\n"
        "    int y_;\n"
        "};\n";

    const AccessMerge result = mergeAccessSections(content, "C");

    ASSERT_EQ(result.status, AccessMerge::Status::Merged);
    EXPECT_EQ(result.content,
        "class C {\n"
        "public:\n"
        "    void a();\n"
        "\n"
        "private:\n"
        "    int x_;\n"
        "\n"
        "    int y_;\n"
        "\n"
        "protected:\n"
        "    void p();\n"
        "};\n");
}

TEST(MergeAccessSectionsTest, ALaterSectionMatchingTheImplicitLeadingAccessJoinsIt) {
    const std::string content =
        "class C {\n"
        "    int a_;\n"
        "public:\n"
        "    void f();\n"
        "private:\n"
        "    int b_;\n"
        "};\n";

    const AccessMerge result = mergeAccessSections(content, "C");

    ASSERT_EQ(result.status, AccessMerge::Status::Merged);
    EXPECT_EQ(result.content,
        "class C {\n"
        "    int a_;\n"
        "\n"
        "    int b_;\n"
        "\n"
        "public:\n"
        "    void f();\n"
        "};\n");
}

TEST(MergeAccessSectionsTest, LeavesAClassWithEachAccessOnceAlone) {
    const std::string content = "class C {\npublic:\n    void a();\nprivate:\n    int x_;\n};\n";

    const AccessMerge result = mergeAccessSections(content, "C");

    EXPECT_EQ(result.status, AccessMerge::Status::Unchanged);
    EXPECT_EQ(result.content, content);
}

TEST(MergeAccessSectionsTest, KeepsCrlfLineEndings) {
    const std::string content = "class C {\r\nprivate:\r\n    int x_;\r\nprivate:\r\n    int y_;\r\n};\r\n";

    const AccessMerge result = mergeAccessSections(content, "C");

    ASSERT_EQ(result.status, AccessMerge::Status::Merged);
    EXPECT_EQ(result.content, "class C {\r\nprivate:\r\n    int x_;\r\n\r\n    int y_;\r\n};\r\n");
}

TEST(MergeAccessSectionsTest, RefusesWhenAPreprocessorLineSitsInASection) {
    const std::string content =
        "class C {\n"
        "private:\n"
        "    int x_;\n"
        "#if 1\n"
        "private:\n"
        "    int y_;\n"
        "#endif\n"
        "};\n";

    const AccessMerge result = mergeAccessSections(content, "C");

    EXPECT_EQ(result.status, AccessMerge::Status::Unsafe);
    EXPECT_EQ(result.content, content);
}

TEST(MergeAccessSectionsTest, ReportsAMissingClass) {
    EXPECT_EQ(mergeAccessSections("class Other {};\n", "C").status, AccessMerge::Status::ClassNotFound);
}

TEST(MergeAccessSectionsTest, MergedOutputParsesAndKeepsEveryMember) {
    const std::string content =
        "class C {\n"
        "public:\n"
        "    using B = int;\n"
        "private:\n"
        "    int onA() { return 1; }\n"
        "protected:\n"
        "    bool init() { return true; }\n"
        "private:\n"
        "    int onB() { return 2; }\n"
        "};\n";

    const AccessMerge once = mergeAccessSections(content, "C");
    ASSERT_EQ(once.status, AccessMerge::Status::Merged);
    EXPECT_NE(once.content.find("int onA()"), std::string::npos);
    EXPECT_NE(once.content.find("int onB()"), std::string::npos);
    EXPECT_NE(once.content.find("bool init()"), std::string::npos);
    // Merging is idempotent.
    EXPECT_EQ(mergeAccessSections(once.content, "C").status, AccessMerge::Status::Unchanged);
}
