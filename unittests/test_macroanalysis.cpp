// analyzeMacros: what the preprocessor did to a file. In-memory sources, no includes.

#include "cpptools_analysis/macroanalysis.h"

#include <gtest/gtest.h>

#include <algorithm>

using namespace cpptools_analysis;

namespace {

const char* kPath = "C:/analysis_test/macros.cpp";

const MacroUseInfo* useOf(const MacroAnalysis& result, const std::string& name, std::size_t line = 0) {
    for (const MacroUseInfo& use : result.uses) {
        if (use.name == name && (line == 0 || use.line == line)) return &use;
    }
    return nullptr;
}

}  // namespace

TEST(MacroAnalysisTest, ADefinitionKeepsItsParametersAndBody) {
    const MacroAnalysis result = analyzeMacros(
        "#define PI 3.14\n"
        "#define MAX(a, b) ((a) > (b) ? (a) : (b))\n"
        "int x = 0;\n", kPath);

    ASSERT_TRUE(result.ok);
    ASSERT_EQ(result.definitions.size(), 2u);
    EXPECT_EQ(result.definitions[0].name, "PI");
    EXPECT_FALSE(result.definitions[0].functionLike);
    EXPECT_EQ(result.definitions[0].body, "3.14");
    EXPECT_EQ(result.definitions[0].line, 1u);
    EXPECT_EQ(result.definitions[1].name, "MAX");
    EXPECT_TRUE(result.definitions[1].functionLike);
    ASSERT_EQ(result.definitions[1].params.size(), 2u);
    EXPECT_EQ(result.definitions[1].params[1], "b");
    EXPECT_EQ(result.definitions[1].line, 2u);
    EXPECT_EQ(result.definitions[1].repeatedParams, (std::vector<std::string>{ "a", "b" })) << "both appear twice in the body";
}

TEST(MacroAnalysisTest, AUseKnowsWhatItBecame) {
    const MacroAnalysis result = analyzeMacros(
        "#define PI 3.14\n"
        "#define MAX(a, b) ((a) > (b) ? (a) : (b))\n"
        "double r = PI;\n"
        "int m = MAX(1, 2);\n", kPath);

    const MacroUseInfo* pi = useOf(result, "PI");
    ASSERT_NE(pi, nullptr);
    EXPECT_EQ(pi->line, 3u);
    EXPECT_EQ(pi->column, 12u);
    EXPECT_EQ(pi->written, "PI");
    EXPECT_EQ(pi->expansion, "3.14");
    EXPECT_TRUE(pi->definedHere);
    EXPECT_EQ(pi->definedLine, 1u);

    const MacroUseInfo* max = useOf(result, "MAX");
    ASSERT_NE(max, nullptr);
    EXPECT_EQ(max->written, "MAX(1, 2)");
    EXPECT_EQ(max->expansion, "( ( 1 ) > ( 2 ) ? ( 1 ) : ( 2 ) )");
}

TEST(MacroAnalysisTest, AMacroInsideAnotherIsNestedNotAUseOfItsOwn) {
    const MacroAnalysis result = analyzeMacros(
        "#define TWICE(x) ((x) * 2)\n"
        "#define BASE 21\n"
        "#define ANSWER TWICE(BASE)\n"
        "int a = ANSWER;\n"
        "int b = TWICE(BASE);\n", kPath);

    const MacroUseInfo* answer = useOf(result, "ANSWER");
    ASSERT_NE(answer, nullptr);
    EXPECT_EQ(answer->expansion, "( ( 21 ) * 2 )");
    EXPECT_NE(std::find(answer->nested.begin(), answer->nested.end(), "TWICE"), answer->nested.end());
    EXPECT_NE(std::find(answer->nested.begin(), answer->nested.end(), "BASE"), answer->nested.end());
    EXPECT_EQ(useOf(result, "TWICE", 4), nullptr) << "written inside ANSWER's body, not in the file";

    const MacroUseInfo* twice = useOf(result, "TWICE", 5);
    ASSERT_NE(twice, nullptr);
    EXPECT_EQ(twice->expansion, "( ( 21 ) * 2 )");
    EXPECT_NE(std::find(twice->nested.begin(), twice->nested.end(), "BASE"), twice->nested.end()) << "BASE is in TWICE's argument";
}

TEST(MacroAnalysisTest, AnEmptyExpansionIsAUseWithNothingLeft) {
    const MacroAnalysis result = analyzeMacros("#define NOTHING\nNOTHING int x;\n", kPath);
    const MacroUseInfo* use = useOf(result, "NOTHING");
    ASSERT_NE(use, nullptr);
    EXPECT_EQ(use->expansion, "");
}

TEST(MacroAnalysisTest, AnArgumentWithASideEffectToAMacroThatRepeatsItIsWarnedAbout) {
    const MacroAnalysis result = analyzeMacros(
        "#define MAX(a, b) ((a) > (b) ? (a) : (b))\n"
        "#define ONCE(a) (a)\n"
        "int f(int);\n"
        "int i = 0, j = 1;\n"
        "int m1 = MAX(i++, j);\n"
        "int m2 = MAX(f(i), j);\n"
        "int m3 = ONCE(i++);\n"
        "int m4 = MAX(i, j);\n", kPath);

    ASSERT_EQ(result.warnings.size(), 2u);
    EXPECT_EQ(result.warnings[0].line, 5u);
    EXPECT_EQ(result.warnings[0].macro, "MAX");
    EXPECT_EQ(result.warnings[0].param, "a");
    EXPECT_EQ(result.warnings[0].argument, "i ++");
    EXPECT_EQ(result.warnings[0].reason, "contains ++");
    EXPECT_EQ(result.warnings[1].line, 6u);
    EXPECT_EQ(result.warnings[1].reason, "calls f");
}

TEST(MacroAnalysisTest, AParameterOnlyStringizedOrPastedIsNotEvaluatedTwice) {
    const MacroAnalysis result = analyzeMacros(
        "#define NAME(a) #a\n"
        "#define GLUE(a, b) a##b\n", kPath);
    ASSERT_EQ(result.definitions.size(), 2u);
    EXPECT_TRUE(result.definitions[0].repeatedParams.empty());
    EXPECT_TRUE(result.definitions[1].repeatedParams.empty());
}

TEST(MacroAnalysisTest, InactiveRegionsNameTheDirectiveThatSkippedThem) {
    const MacroAnalysis result = analyzeMacros(
        "#define FEATURE 0\n"
        "#if FEATURE\n"
        "int on = 1;\n"
        "int alsoOn = 2;\n"
        "#else\n"
        "int off = 3;\n"
        "#endif\n"
        "#ifdef NEVER_DEFINED\n"
        "int never = 4;\n"
        "#endif\n", kPath);

    ASSERT_EQ(result.inactive.size(), 2u);
    EXPECT_EQ(result.inactive[0].startLine, 3u);
    EXPECT_EQ(result.inactive[0].endLine, 4u);
    EXPECT_EQ(result.inactive[0].directive, "#if FEATURE");
    EXPECT_EQ(result.inactive[1].startLine, 9u);
    EXPECT_EQ(result.inactive[1].directive, "#ifdef NEVER_DEFINED");
}

TEST(MacroAnalysisTest, CompileDefinitionsDecideWhatIsInactive) {
    const std::string source = "#ifdef FEATURE\nint on;\n#endif\n";
    EXPECT_EQ(analyzeMacros(source, kPath).inactive.size(), 1u);
    EXPECT_TRUE(analyzeMacros(source, kPath, { "-DFEATURE" }).inactive.empty());
}

TEST(MacroAnalysisTest, AUseIsExpandedOneMacroAtATimeLeftmostFirst) {
    const MacroAnalysis result = analyzeMacros(
        "#define MIN(a, b) ((a) < (b) ? (a) : (b))\n"
        "#define MAX(a, b) ((a) > (b) ? (a) : (b))\n"
        "#define CLAMP(v, lo, hi) MAX((lo), MIN((v), (hi)))\n"
        "int w = CLAMP(width, 0, kMaxWidth);\n", kPath);

    const MacroUseInfo* use = useOf(result, "CLAMP");
    ASSERT_NE(use, nullptr);
    ASSERT_EQ(use->steps.size(), 4u);
    EXPECT_EQ(use->steps[0].macro, "");
    EXPECT_EQ(use->steps[0].text, "CLAMP(width, 0, kMaxWidth)");
    EXPECT_EQ(use->steps[1].macro, "CLAMP");
    EXPECT_EQ(use->steps[1].text, "MAX((0), MIN((width), (kMaxWidth)))");
    EXPECT_EQ(use->steps[2].macro, "MAX");
    EXPECT_EQ(use->steps[2].text, "(((0)) > (MIN((width), (kMaxWidth))) ? ((0)) : (MIN((width), (kMaxWidth))))");
    EXPECT_EQ(use->steps[3].macro, "MIN");
    EXPECT_EQ(use->steps[3].text.find("MIN"), std::string::npos) << "nothing left to expand";
}

TEST(MacroAnalysisTest, StepsHandleObjectLikeStringizingAndPasting) {
    const MacroAnalysis result = analyzeMacros(
        "#define PI 3.14\n"
        "#define NAME(a) #a\n"
        "#define GLUE(a, b) a##b\n"
        "double r = PI;\n"
        "const char* n = NAME(hello);\n"
        "int GLUE(foo, 1) = 0;\n", kPath);

    ASSERT_EQ(useOf(result, "PI")->steps.size(), 2u);
    EXPECT_EQ(useOf(result, "PI")->steps[1].text, "3.14");
    EXPECT_EQ(useOf(result, "NAME")->steps[1].text, "\"hello\"");
    EXPECT_EQ(useOf(result, "GLUE")->steps[1].text, "foo1");
}

TEST(MacroAnalysisTest, StepsFillInFileAndLineForTheBuiltIns) {
    const MacroAnalysis result = analyzeMacros(
        "#define LOG(msg) report(__FILE__, __LINE__, msg)\n"
        "\n"
        "void f() { LOG(\"hi\"); }\n", kPath);

    const MacroUseInfo* use = useOf(result, "LOG");
    ASSERT_NE(use, nullptr);
    ASSERT_EQ(use->steps.size(), 4u);
    EXPECT_EQ(use->steps[2].macro, "__FILE__");
    EXPECT_EQ(use->steps[2].text.find("__FILE__"), std::string::npos);
    EXPECT_NE(use->steps[2].text.find(".cpp\""), std::string::npos);
    EXPECT_EQ(use->steps[3].macro, "__LINE__");
    EXPECT_NE(use->steps[3].text.find(", 3, \"hi\""), std::string::npos) << use->steps[3].text;
}

TEST(MacroAnalysisTest, AnInactiveRegionNamesTheMacrosThatDecidedIt) {
    const MacroAnalysis result = analyzeMacros(
        "#define A 1\n"
        "#define F 1\n"
        "#if A == 0 && defined(B)\n"
        "int x;\n"
        "#endif\n"
        "#if F\n"
        "int on;\n"
        "#else\n"
        "int off;\n"
        "#endif\n"
        "#ifdef NEVER\n"
        "int z;\n"
        "#endif\n"
        "#if 0\n"
        "int zero;\n"
        "#endif\n", kPath);

    ASSERT_EQ(result.inactive.size(), 4u);
    EXPECT_EQ(result.inactive[0].macros, std::vector<std::string>({ "A", "B" }));
    EXPECT_EQ(result.inactive[1].macros, std::vector<std::string>({ "F" })) << "the #else is the other side of #if F";
    EXPECT_EQ(result.inactive[2].macros, std::vector<std::string>({ "NEVER" }));
    EXPECT_TRUE(result.inactive[3].macros.empty()) << "#if 0 names no macro";
}

TEST(MacroAnalysisTest, AUseHasATreeOfTheMacrosItBroughtIn) {
    const MacroAnalysis result = analyzeMacros(
        "#define MIN(a, b) ((a) < (b) ? (a) : (b))\n"
        "#define MAX(a, b) ((a) > (b) ? (a) : (b))\n"
        "#define LIMIT 800\n"
        "#define CLAMP(v, lo, hi) MAX((lo), MIN((v), (hi)))\n"
        "int w = CLAMP(width, 0, LIMIT);\n", kPath);

    const MacroUseInfo* use = useOf(result, "CLAMP");
    ASSERT_NE(use, nullptr);
    EXPECT_EQ(use->tree.name, "CLAMP");
    EXPECT_TRUE(use->tree.definedHere);
    EXPECT_EQ(use->tree.definedLine, 4u);
    ASSERT_EQ(use->tree.children.size(), 3u);   // MAX and MIN from its body, LIMIT from the argument
    std::vector<std::string> names;
    for (const MacroTreeNode& child : use->tree.children) names.push_back(child.name);
    EXPECT_NE(std::find(names.begin(), names.end(), "MAX"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "MIN"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "LIMIT"), names.end());
}

TEST(MacroAnalysisTest, TheFinalTextIsSplitByWhoWroteEachPart) {
    const MacroAnalysis result = analyzeMacros(
        "#define SQUARE(x) ((x) * (x))\n"
        "int n = SQUARE(side);\n", kPath);

    const MacroUseInfo* use = useOf(result, "SQUARE");
    ASSERT_NE(use, nullptr);
    std::string all;
    bool sawArgument = false;
    for (const MacroSpan& span : use->spans) {
        all += span.text;
        if (span.text == "side") { sawArgument = true; EXPECT_EQ(span.macro, ""); }
        if (span.text.find('*') != std::string::npos) EXPECT_EQ(span.macro, "SQUARE");
    }
    EXPECT_EQ(all, use->steps.back().text);
    EXPECT_TRUE(sawArgument);
}

TEST(MacroAnalysisTest, AMacroKnowsWhereItWasDefined) {
    const MacroAnalysis result = analyzeMacros(
        "#define LOCAL 1\n"
        "int a = LOCAL;\n"
        "int b = FROM_COMMAND_LINE;\n", kPath, { "-DFROM_COMMAND_LINE=2" });

    EXPECT_EQ(useOf(result, "LOCAL")->origin, MacroOrigin::File);
    const MacroUseInfo* flag = useOf(result, "FROM_COMMAND_LINE");
    ASSERT_NE(flag, nullptr);
    EXPECT_EQ(flag->origin, MacroOrigin::CommandLine);
    EXPECT_FALSE(flag->definedHere);
    EXPECT_EQ(flag->steps.back().text, "2");
}
