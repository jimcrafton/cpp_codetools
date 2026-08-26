#include "cpptools/parser.h"

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
