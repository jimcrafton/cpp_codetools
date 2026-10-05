// analyzeTemplates: the class and function templates a file instantiates. In-memory sources.

#include "cpptools_analysis/templateanalysis.h"

#include <gtest/gtest.h>

#include <algorithm>

using namespace cpptools_analysis;

namespace {

const char* kPath = "C:/analysis_test/templates.cpp";

const TemplateInfo* find(const TemplateAnalysis& result, const std::string& name) {
    for (const TemplateInfo& info : result.templates) {
        if (info.name == name) return &info;
    }
    return nullptr;
}

bool hasType(const TemplateInfo& info, const std::string& type) {
    return std::any_of(info.instantiations.begin(), info.instantiations.end(),
                       [&](const TemplateInstantiationInfo& i) { return i.type == type; });
}

}  // namespace

TEST(TemplateAnalysisTest, EachDistinctInstantiationOfAClassTemplateIsListedOnce) {
    const TemplateAnalysis result = analyzeTemplates(
        "namespace shapes {\n"
        "template <typename T> struct Box { T value; T get() const { return value; } };\n"
        "}\n"
        "shapes::Box<int> a;\n"
        "shapes::Box<int> b;\n"
        "shapes::Box<double> c;\n", kPath);

    ASSERT_TRUE(result.ok);
    const TemplateInfo* box = find(result, "shapes::Box");
    ASSERT_NE(box, nullptr);
    EXPECT_EQ(box->kind, TemplateInfo::Kind::Class);
    EXPECT_EQ(box->line, 2u);
    ASSERT_EQ(box->instantiations.size(), 2u);
    EXPECT_TRUE(hasType(*box, "shapes::Box<int>"));
    EXPECT_TRUE(hasType(*box, "shapes::Box<double>"));
    EXPECT_TRUE(box->instantiations[0].complete) << "a variable of the type needs its definition";
    EXPECT_EQ(box->instantiations[0].kind, TemplateInstantiationInfo::Kind::Implicit);
}

TEST(TemplateAnalysisTest, APointerToAnInstantiationIsNotComplete) {
    const TemplateAnalysis result = analyzeTemplates(
        "template <typename T> struct Box { T value; };\n"
        "Box<int>* p = nullptr;\n", kPath);
    const TemplateInfo* box = find(result, "Box");
    ASSERT_NE(box, nullptr);
    ASSERT_EQ(box->instantiations.size(), 1u);
    EXPECT_FALSE(box->instantiations[0].complete);
}

TEST(TemplateAnalysisTest, FunctionTemplatesListTheirArguments) {
    const TemplateAnalysis result = analyzeTemplates(
        "template <typename T> T twice(T x) { return x + x; }\n"
        "int a = twice(1);\n"
        "double b = twice(2.5);\n"
        "int c = twice<int>(3);\n", kPath);

    const TemplateInfo* twice = find(result, "twice");
    ASSERT_NE(twice, nullptr);
    EXPECT_EQ(twice->kind, TemplateInfo::Kind::Function);
    ASSERT_EQ(twice->instantiations.size(), 2u) << "twice<int> once, though written two ways";
    EXPECT_TRUE(hasType(*twice, "<int>"));
    EXPECT_TRUE(hasType(*twice, "<double>"));
}

TEST(TemplateAnalysisTest, ExplicitSpecializationsAndInstantiationsAreToldApart) {
    const TemplateAnalysis result = analyzeTemplates(
        "template <typename T> struct Traits { static const int size = 1; };\n"
        "template <> struct Traits<char> { static const int size = 2; };\n"
        "template struct Traits<long>;\n"
        "int n = Traits<int>::size;\n", kPath);

    const TemplateInfo* traits = find(result, "Traits");
    ASSERT_NE(traits, nullptr);
    auto kindOf = [&](const std::string& type) {
        for (const TemplateInstantiationInfo& i : traits->instantiations) {
            if (i.type == type) return i.kind;
        }
        return TemplateInstantiationInfo::Kind::Implicit;
    };
    EXPECT_EQ(kindOf("Traits<char>"), TemplateInstantiationInfo::Kind::ExplicitSpecialization);
    EXPECT_EQ(kindOf("Traits<long>"), TemplateInstantiationInfo::Kind::ExplicitInstantiation);
    EXPECT_EQ(kindOf("Traits<int>"), TemplateInstantiationInfo::Kind::Implicit);
}

TEST(TemplateAnalysisTest, OnlyTemplatesDeclaredUnderTheFolderAreKept) {
    const std::string source =
        "template <typename T> struct Mine { T v; };\n"
        "Mine<int> m;\n";
    EXPECT_NE(find(analyzeTemplates(source, kPath, {}, "C:/analysis_test"), "Mine"), nullptr);
    EXPECT_EQ(find(analyzeTemplates(source, kPath, {}, "C:/elsewhere"), "Mine"), nullptr);
}
