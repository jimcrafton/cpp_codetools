#include "cpptools/parser.h"

#include <cstdio>
#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

namespace {

using cpptools::Parser;
using cpptools::Severity;
using cpptools::Symbol;
using cpptools::SymbolKind;

const Symbol* findChild(const std::vector<Symbol>& symbols, const std::string& name) {
    for (const Symbol& symbol : symbols) {
        if (symbol.name == name) {
            return &symbol;
        }
    }
    return nullptr;
}

} // namespace

TEST(ParserTest, DetectsNamespace) {
    Parser parser;
    auto result = parser.parseBuffer("test.cpp", "namespace outer { }\n");

    const Symbol* outer = findChild(result.symbols, "outer");
    ASSERT_NE(outer, nullptr);
    EXPECT_EQ(outer->kind, SymbolKind::Namespace);
    EXPECT_EQ(outer->location.line, 1u);
}

TEST(ParserTest, DetectsClassAndStruct) {
    Parser parser;
    auto result = parser.parseBuffer("test.cpp", "class MyClass {};\nstruct MyStruct {};\n");

    const Symbol* cls = findChild(result.symbols, "MyClass");
    ASSERT_NE(cls, nullptr);
    EXPECT_EQ(cls->kind, SymbolKind::Class);

    const Symbol* strct = findChild(result.symbols, "MyStruct");
    ASSERT_NE(strct, nullptr);
    EXPECT_EQ(strct->kind, SymbolKind::Struct);
}

TEST(ParserTest, DetectsEnum) {
    Parser parser;
    auto result = parser.parseBuffer("test.cpp", "enum Color { Red, Green, Blue };\n");

    const Symbol* color = findChild(result.symbols, "Color");
    ASSERT_NE(color, nullptr);
    EXPECT_EQ(color->kind, SymbolKind::Enum);
}

TEST(ParserTest, DetectsFreeFunction) {
    Parser parser;
    auto result = parser.parseBuffer("test.cpp", "int add(int a, int b) { return a + b; }\n");

    const Symbol* fn = findChild(result.symbols, "add");
    ASSERT_NE(fn, nullptr);
    EXPECT_EQ(fn->kind, SymbolKind::Function);
}

TEST(ParserTest, DetectsTypedef) {
    Parser parser;
    auto result = parser.parseBuffer("test.cpp", "typedef int MyInt;\n");

    const Symbol* alias = findChild(result.symbols, "MyInt");
    ASSERT_NE(alias, nullptr);
    EXPECT_EQ(alias->kind, SymbolKind::Typedef);
}

TEST(ParserTest, NestingProducesTree) {
    Parser parser;
    auto result = parser.parseBuffer("test.cpp",
        "namespace outer {\n"
        "class Widget {\n"
        "public:\n"
        "    void draw();\n"
        "    int width;\n"
        "};\n"
        "}\n");

    const Symbol* outer = findChild(result.symbols, "outer");
    ASSERT_NE(outer, nullptr);
    EXPECT_EQ(outer->kind, SymbolKind::Namespace);

    const Symbol* widget = findChild(outer->children, "Widget");
    ASSERT_NE(widget, nullptr);
    EXPECT_EQ(widget->kind, SymbolKind::Class);

    const Symbol* draw = findChild(widget->children, "draw");
    ASSERT_NE(draw, nullptr);
    EXPECT_EQ(draw->kind, SymbolKind::Method);

    const Symbol* width = findChild(widget->children, "width");
    ASSERT_NE(width, nullptr);
    EXPECT_EQ(width->kind, SymbolKind::Field);
}

TEST(ParserTest, SymbolsFromIncludeAreExcluded) {
    std::filesystem::path tempDir = std::filesystem::temp_directory_path();
    std::filesystem::path headerPath = tempDir / "cpptools_test_include.h";

    {
        std::ofstream header(headerPath);
        header << "struct FromInclude { int value; };\n";
    }

    std::string headerPathForInclude = headerPath.string();
    // #include wants forward slashes even on Windows.
    for (char& c : headerPathForInclude) {
        if (c == '\\') {
            c = '/';
        }
    }

    std::string content = "#include \"" + headerPathForInclude + "\"\n"
                           "struct FromMain { int value; };\n";

    Parser parser;
    auto result = parser.parseBuffer("test.cpp", content);

    std::filesystem::remove(headerPath);

    EXPECT_EQ(findChild(result.symbols, "FromInclude"), nullptr);
    EXPECT_NE(findChild(result.symbols, "FromMain"), nullptr);
}

TEST(ParserTest, InvalidCodeProducesErrorDiagnostic) {
    Parser parser;
    auto result = parser.parseBuffer("test.cpp", "this is not valid c++ +++ ;;;\n");

    bool hasError = false;
    for (const cpptools::Diagnostic& diagnostic : result.diagnostics) {
        if (diagnostic.severity == Severity::Error || diagnostic.severity == Severity::Fatal) {
            hasError = true;
            break;
        }
    }
    EXPECT_TRUE(hasError);
}

TEST(ParserTest, ParseFileReadsFromDisk) {
    std::filesystem::path tempDir = std::filesystem::temp_directory_path();
    std::filesystem::path filePath = tempDir / "cpptools_test_parsefile.cpp";

    {
        std::ofstream file(filePath);
        file << "void fromDisk() {}\n";
    }

    Parser parser;
    auto result = parser.parseFile(filePath.string());

    std::filesystem::remove(filePath);

    EXPECT_NE(findChild(result.symbols, "fromDisk"), nullptr);
}

const cpptools::Diagnostic* findDiagnostic(const cpptools::ParseResult& result, const std::string& messagePart) {
    for (const cpptools::Diagnostic& diagnostic : result.diagnostics) {
        if (diagnostic.message.find(messagePart) != std::string::npos) {
            return &diagnostic;
        }
    }
    return nullptr;
}

TEST(ParserTest, DiagnosticsCarryByteOffsetCategoryAndWhetherTheyAreInTheMainFile) {
    Parser parser;
    const std::string code = "int ok = 1;\nint x = ;\n";
    auto result = parser.parseBuffer("test.cpp", code);

    const cpptools::Diagnostic* expression = findDiagnostic(result, "expected expression");
    ASSERT_NE(expression, nullptr);
    EXPECT_EQ(expression->category, "Parse Issue");
    EXPECT_TRUE(expression->fromMainFile);
    EXPECT_EQ(expression->location.offset, code.find(';', 12));   // the ';' where an expression was wanted
    EXPECT_EQ(expression->location.line, 2u);
    EXPECT_EQ(expression->location.column, 9u);
}

TEST(ParserTest, SemanticAndPreprocessorProblemsHaveTheirOwnCategories) {
    Parser parser;
    auto result = parser.parseBuffer("test.cpp", "#include <nope_missing_header.h>\nint main() { foo(); }\n");

    const cpptools::Diagnostic* include = findDiagnostic(result, "file not found");
    ASSERT_NE(include, nullptr);
    EXPECT_EQ(include->category, "Lexical or Preprocessor Issue");
    EXPECT_GT(include->rangeEndOffset, include->location.offset) << "the diagnostic covers the header name";
    const cpptools::Diagnostic* undeclared = findDiagnostic(result, "undeclared identifier 'foo'");
    ASSERT_NE(undeclared, nullptr);
    EXPECT_EQ(undeclared->category, "Semantic Issue");
}

TEST(ParserTest, AMissingIncludeDoesNotHideTheSyntaxErrorsAfterIt) {
    // Without KeepGoing a missing header is fatal and everything after it goes unreported.
    Parser parser;
    auto result = parser.parseBuffer("test.cpp", "#include <nope_missing_header.h>\nint x = ;\n");
    const cpptools::Diagnostic* expression = findDiagnostic(result, "expected expression");
    ASSERT_NE(expression, nullptr);
    EXPECT_EQ(expression->category, "Parse Issue");
}

TEST(ParserTest, SymbolsCarryTheirByteOffset) {
    Parser parser;
    const std::string code = "int a;\nvoid f();\n";
    auto result = parser.parseBuffer("test.cpp", code);
    const Symbol* f = findChild(result.symbols, "f");
    ASSERT_NE(f, nullptr);
    EXPECT_EQ(f->location.offset, code.find("f()"));
}

TEST(ParserTest, ExtraTokensAfterAnIncludeAreAPreprocessorWarning) {
    Parser parser;
    const std::string code = "#include <nope.h> zsd;klfjsl;dl\nint a;\n";
    auto result = parser.parseBuffer("test.cpp", code);
    const cpptools::Diagnostic* extra = findDiagnostic(result, "extra tokens at end of #include");
    ASSERT_NE(extra, nullptr);
    EXPECT_EQ(extra->severity, Severity::Warning);
    EXPECT_EQ(extra->category, "Lexical or Preprocessor Issue");
    EXPECT_EQ(extra->location.offset, code.find("zsd"));   // reported at the first extra token
}

TEST(ParserTest, DISABLED_ProbeMsvcHeaders) {
    struct Case { const char* name; std::vector<std::string> args; };
    const Case cases[] = {
        { "default (-std=c++17 -xc++)", {"-std=c++17", "-xc++"} },
        { "cl driver mode", {"--driver-mode=cl", "/std:c++17", "/EHsc", "/TP"} },
        { "cl driver mode, no /TP", {"--driver-mode=cl", "/std:c++17", "/EHsc"} },
    };
    for (const Case& c : cases) {
        Parser parser;
        auto result = parser.parseBuffer("probe.cpp", "#include <vector>\n#include <string>\nstd::vector<int> v; std::string s; int x = ;\n", c.args);
        std::printf("---- %s\n", c.name);
        for (const cpptools::Diagnostic& d : result.diagnostics) {
            std::printf("  [%d] %s | cat='%s' main=%d\n", static_cast<int>(d.severity), d.message.c_str(), d.category.c_str(), d.fromMainFile ? 1 : 0);
        }
    }
}
