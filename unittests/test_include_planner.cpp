#include "../extension/NativeEditControls/IncludePlanner.h"

#include <gtest/gtest.h>

using namespace CodeToolsVsix;

namespace {

// Applies the plan's edits to `content`, so tests assert on the resulting text.
std::wstring applied(std::wstring content, const IncludePlan& plan) {
    for (auto it = plan.edits.rbegin(); it != plan.edits.rend(); ++it) {
        content.replace(it->offset, it->length, it->text);
    }
    return content;
}

}  // namespace

TEST(IncludePlanner, NothingMissingMeansNoEdit) {
    const IncludePlan plan = planIncludes(L"#include <a/b.h>\n", {"<a/b.h>"});
    EXPECT_TRUE(plan.missing.empty());
    EXPECT_TRUE(plan.edits.empty());
}

TEST(IncludePlanner, AddsAfterTheLastInclude) {
    const std::wstring text = L"#pragma once\n#include <x.h>\n#include <y.h>\n\nclass A {};\n";
    const IncludePlan plan = planIncludes(text, {"<a/b.h>"});
    EXPECT_EQ(plan.missing, std::vector<std::string>{"<a/b.h>"});
    EXPECT_EQ(applied(text, plan), L"#pragma once\n#include <x.h>\n#include <y.h>\n#include <a/b.h>\n\nclass A {};\n");
}

TEST(IncludePlanner, AddsAfterPragmaOnceWithABlankLineWhenThereAreNoIncludes) {
    const std::wstring text = L"#pragma once\nclass A {};\n";
    EXPECT_EQ(applied(text, planIncludes(text, {"<a.h>"})), L"#pragma once\n\n#include <a.h>\nclass A {};\n");
}

TEST(IncludePlanner, AddsAtTheTopWhenThereIsNeither) {
    const std::wstring text = L"class A {};\n";
    EXPECT_EQ(applied(text, planIncludes(text, {"<a.h>"})), L"#include <a.h>\n\nclass A {};\n");
}

TEST(IncludePlanner, MatchesDelimiterSpacingSlashAndCaseInsensitively) {
    const std::wstring text = L"  #  include \"A\\B.H\"\n";
    EXPECT_TRUE(planIncludes(text, {"<a/b.h>"}).missing.empty());
}

TEST(IncludePlanner, ListsAMissingHeaderOnceInTheOrderGiven) {
    const IncludePlan plan = planIncludes(L"", {"<b.h>", "<a.h>", "<b.h>"});
    EXPECT_EQ(plan.missing, (std::vector<std::string>{"<b.h>", "<a.h>"}));
    ASSERT_EQ(plan.edits.size(), 1u);
    EXPECT_EQ(plan.edits[0].text, L"#include <b.h>\n#include <a.h>\n\n");
}

TEST(IncludePlanner, UsesTheFilesCrLfLineEnding) {
    const std::wstring text = L"#include <x.h>\r\nint a;\r\n";
    EXPECT_EQ(applied(text, planIncludes(text, {"<a.h>"})), L"#include <x.h>\r\n#include <a.h>\r\nint a;\r\n");
}

TEST(IncludePlanner, AnIncludeOnTheLastLineWithNoEndingGetsOneFirst) {
    const std::wstring text = L"#include <x.h>";
    EXPECT_EQ(applied(text, planIncludes(text, {"<a.h>"})), L"#include <x.h>\n#include <a.h>\n");
}

TEST(IncludePlanner, ACommentedOutIncludeDoesNotCount) {
    const std::wstring text = L"// #include <a.h>\n";
    EXPECT_EQ(planIncludes(text, {"<a.h>"}).missing, std::vector<std::string>{"<a.h>"});
}
