#include "cpptools/parser.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

namespace {

namespace fs = std::filesystem;
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

// ---- Session: a kept translation unit, reparsed ------------------------------------------------

namespace {
long errorCount(const cpptools::ParseResult& result) {
    return std::count_if(result.diagnostics.begin(), result.diagnostics.end(), [](const cpptools::Diagnostic& d) {
        return d.severity == Severity::Error || d.severity == Severity::Fatal;
    });
}
} // namespace

TEST(SessionTest, TheFirstUpdateParsesAndTheNextOnesReparseWithTheNewContent) {
    cpptools::Session session;
    auto first = session.update("session.cpp", "int x = ;\n");
    EXPECT_GT(errorCount(first), 0);
    EXPECT_EQ(session.parseCount(), 1u);
    EXPECT_EQ(session.reparseCount(), 0u);

    auto second = session.update("session.cpp", "int x = 1;\nvoid later();\n");
    EXPECT_EQ(errorCount(second), 0) << "the new content, not the old";
    EXPECT_NE(findChild(second.symbols, "later"), nullptr);
    EXPECT_EQ(session.parseCount(), 1u);
    EXPECT_EQ(session.reparseCount(), 1u);

    auto third = session.update("session.cpp", "void changed();\n");
    EXPECT_EQ(findChild(third.symbols, "later"), nullptr);
    EXPECT_NE(findChild(third.symbols, "changed"), nullptr);
    EXPECT_EQ(session.reparseCount(), 2u);
}

TEST(SessionTest, ADifferentFileOrDifferentFlagsStartOver) {
    cpptools::Session session;
    session.update("a.cpp", "int a;\n");
    session.update("a.cpp", "int a2;\n");
    EXPECT_EQ(session.parseCount(), 1u);

    session.update("b.cpp", "int b;\n");
    EXPECT_EQ(session.parseCount(), 2u) << "another file";

    session.update("b.cpp", "int b;\n", { "-std=c++20", "-xc++" });
    EXPECT_EQ(session.parseCount(), 3u) << "other flags";
    session.update("b.cpp", "int b2;\n", { "-std=c++20", "-xc++" });
    EXPECT_EQ(session.parseCount(), 3u);
    EXPECT_EQ(session.reparseCount(), 2u);
}

TEST(SessionTest, AnEditInThePreambleIsStillReflected) {
    cpptools::Session session;
    session.update("preamble.cpp", "#include <vector>\nstd::vector<int> v;\n");
    // The includes are the part libclang caches - change them and the answer must change too.
    auto broken = session.update("preamble.cpp", "#include <no_such_header_at_all.h>\nint x;\n");
    EXPECT_NE(findDiagnostic(broken, "file not found"), nullptr);
    auto fixed = session.update("preamble.cpp", "#include <vector>\nstd::vector<int> v;\n");
    EXPECT_EQ(findDiagnostic(fixed, "file not found"), nullptr);
    EXPECT_EQ(errorCount(fixed), 0);
}

TEST(SessionTest, ReparsingAFileWithHeavyIncludesIsMuchFasterThanTheFirstParse) {
    using clock = std::chrono::steady_clock;
    const std::string includes =
        "#include <vector>\n#include <string>\n#include <map>\n#include <unordered_map>\n#include <algorithm>\n"
        "#include <functional>\n#include <memory>\n#include <sstream>\n#include <regex>\n#include <filesystem>\n";
    // libclang reuses a preamble only for a main file that exists on disk (it validates it against the file).
    const fs::path file = fs::temp_directory_path() / "cpptools_session_heavy.cpp";
    {
        std::ofstream out(file);
        out << includes;
    }
    cpptools::Session session;
    auto milliseconds = [](clock::time_point from) {
        return std::chrono::duration<double, std::milli>(clock::now() - from).count();
    };

    auto start = clock::now();
    auto first = session.update(file.string(), includes + "int f() { return 1; }\n");
    const double firstMs = milliseconds(start);
    if (errorCount(first) > 0 || firstMs < 40.0) {
        std::error_code ignored;
        fs::remove(file, ignored);
        GTEST_SKIP() << "the standard headers weren't found or parsed too fast to compare (" << firstMs << " ms)";
    }

    double bestReparse = 1e9;
    std::string sequence;
    for (int i = 0; i < 4; ++i) {
        start = clock::now();
        auto again = session.update(file.string(), includes + "int f() { return " + std::to_string(i) + "; }\nint g" + std::to_string(i) + ";\n");
        const double ms = milliseconds(start);
        bestReparse = std::min(bestReparse, ms);
        sequence += " " + std::to_string(static_cast<int>(ms));
        EXPECT_EQ(errorCount(again), 0);
    }
    std::error_code ignored;
    fs::remove(file, ignored);
    std::printf("heavy includes: first parse %.0f ms, reparses (ms):%s\n", firstMs, sequence.c_str());
    EXPECT_LT(bestReparse, firstMs * 0.5) << "the preamble (the includes) should be reused";
}

TEST(SessionTest, UpdatesFromSeveralThreadsAreSerializedNotCorrupting) {
    cpptools::Session session;
    std::atomic<int> withoutErrors{ 0 };
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < 5; ++i) {
                auto result = session.update("threads.cpp", "int v" + std::to_string(t) + "_" + std::to_string(i) + ";\n");
                if (errorCount(result) == 0) {
                    ++withoutErrors;
                }
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    EXPECT_EQ(withoutErrors.load(), 20);
    EXPECT_EQ(session.parseCount() + session.reparseCount(), 20u);
}
