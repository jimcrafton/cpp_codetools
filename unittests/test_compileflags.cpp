// Tests for cpptools::compileFlagsFor() / cleanCommandLine(): finding the flags a build would use.

#include "cpptools/compileflags.h"
#include "cpptools/parser.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using cpptools::CompileFlags;
using cpptools::compileFlagsFor;
using cpptools::cleanCommandLine;

namespace {

bool contains(const std::vector<std::string>& args, const std::string& arg) {
    return std::find(args.begin(), args.end(), arg) != args.end();
}

// Any one of args starts with prefix.
bool hasPrefix(const std::vector<std::string>& args, const std::string& prefix) {
    return std::any_of(args.begin(), args.end(), [&](const std::string& arg) { return arg.compare(0, prefix.size(), prefix) == 0; });
}

// A scratch directory tree, removed afterwards.
struct Tree {
    Tree() {
        root = fs::temp_directory_path() / ("cpptools_flags_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        fs::remove_all(root);
        fs::create_directories(root);
    }
    ~Tree() {
        std::error_code ignored;
        fs::remove_all(root, ignored);
    }
    fs::path write(const std::string& relative, const std::string& contents) const {
        const fs::path file = root / relative;
        fs::create_directories(file.parent_path());
        std::ofstream out(file, std::ios::binary);
        out << contents;
        return file;
    }
    fs::path root;
};

// A JSON string literal for a path (backslashes doubled).
std::string json(const fs::path& path) {
    std::string out = "\"";
    for (char c : path.generic_string()) {
        out += c;   // forward slashes need no escaping
    }
    return out + "\"";
}

} // namespace

TEST(CleanCommandLine, DropsTheCompilerTheSourceAndTheOutputOptions) {
    const fs::path dir = fs::temp_directory_path() / "proj";
    const std::string source = (dir / "src" / "a.cpp").string();
    const std::vector<std::string> args = cleanCommandLine(
        { "clang++", "-std=c++20", "-DFOO=1", "-MD", "-MT", "a.o", "-MF", "a.d", "-c", source, "-o", "a.o", "-Wall" }, dir.string(), source);
    EXPECT_TRUE(contains(args, "-std=c++20"));
    EXPECT_TRUE(contains(args, "-DFOO=1"));
    EXPECT_TRUE(contains(args, "-Wall"));
    for (const char* dropped : { "clang++", "-c", "-o", "a.o", "-MD", "-MT", "-MF", "a.d" }) {
        EXPECT_FALSE(contains(args, dropped)) << dropped;
    }
    EXPECT_FALSE(contains(args, source));
}

TEST(CleanCommandLine, TheEndOfOptionsMarkerGoesWithTheSourceItIntroduced) {
    // What libclang's own loader hands back for a file that isn't in the database (a header): the
    // options, "--", the file. Anything appended after "--" would be taken for an input file.
    const fs::path dir = fs::temp_directory_path() / "proj";
    const std::string header = (dir / "a.h").string();
    const std::vector<std::string> args = cleanCommandLine({ "clang++", "--driver-mode=g++", "-DX", "-x", "c++-header", "--", header }, dir.string(), header);
    EXPECT_FALSE(contains(args, "--"));
    EXPECT_FALSE(contains(args, header));
    EXPECT_TRUE(contains(args, "-DX"));
}

TEST(CleanCommandLine, ADriverModeIsNeverRepeatedAndIsAddedForClOnlyWhenMissing) {
    const fs::path dir = fs::temp_directory_path() / "proj";
    const std::string source = (dir / "a.cpp").string();
    auto count = [](const std::vector<std::string>& args, const std::string& arg) { return std::count(args.begin(), args.end(), arg); };
    // libclang's own loader already includes one.
    EXPECT_EQ(count(cleanCommandLine({ "cl.exe", "--driver-mode=cl", "/DX", "/c", source }, dir.string(), source), "--driver-mode=cl"), 1);
    // Plain cl.exe: added once.
    const std::vector<std::string> plain = cleanCommandLine({ "cl.exe", "/DX", "/c", source }, dir.string(), source);
    EXPECT_EQ(count(plain, "--driver-mode=cl"), 1);
    EXPECT_EQ(plain[0], "--driver-mode=cl");
    // A gcc-style one keeps what it has and gets no cl mode.
    const std::vector<std::string> gnu = cleanCommandLine({ "clang++", "--driver-mode=g++", "-DX", source }, dir.string(), source);
    EXPECT_EQ(count(gnu, "--driver-mode=g++"), 1);
    EXPECT_EQ(count(gnu, "--driver-mode=cl"), 0);
}

TEST(CompileFlagsFor, HeadersAreNotWarnedAboutPragmaOnce) {
    EXPECT_TRUE(contains(cpptools::defaultCompileArgs(), "-Wno-pragma-once-outside-header"));
    EXPECT_TRUE(contains(cleanCommandLine({ "clang++", "-c", "a.cpp" }, "/", "a.cpp"), "-Wno-pragma-once-outside-header"));

    cpptools::Parser parser;
    auto result = parser.parseBuffer("header.h", "#pragma once\nstruct A { int x; };\n");
    for (const cpptools::Diagnostic& d : result.diagnostics) {
        EXPECT_EQ(d.message.find("pragma once"), std::string::npos) << d.message;
    }
}

TEST(CleanCommandLine, TheSourceIsRecognizedHoweverItIsSpelled) {
    const fs::path dir = fs::temp_directory_path() / "proj";
    // Relative to the build's directory, and with a different case and slashes.
    EXPECT_FALSE(contains(cleanCommandLine({ "clang++", "-c", "src/a.cpp" }, dir.string(), (dir / "src" / "a.cpp").string()), "src/a.cpp"));
    std::string shouty = (dir / "SRC" / "A.CPP").generic_string();
    EXPECT_FALSE(contains(cleanCommandLine({ "clang++", "-c", shouty }, dir.string(), (dir / "src" / "a.cpp").string()), shouty));
}

TEST(CleanCommandLine, RelativeIncludePathsBecomeAbsoluteAgainstTheBuildDirectory) {
    const fs::path dir = fs::temp_directory_path() / "proj" / "build";
    const std::string source = (dir / "a.cpp").string();
    const std::vector<std::string> args = cleanCommandLine(
        { "g++", "-I../inc", "-I", "../other", "-isystem", "sys", "-iquote../q", "-include", "pre.h", "-I", (dir / "abs").string(), "-c", source }, dir.string(), source);
    auto abs = [&](const std::string& relative) { return (dir / relative).lexically_normal().string(); };
    EXPECT_TRUE(contains(args, "-I" + abs("../inc")));
    EXPECT_TRUE(contains(args, abs("../other")));
    EXPECT_TRUE(contains(args, abs("sys")));
    EXPECT_TRUE(contains(args, "-iquote" + abs("../q")));
    EXPECT_TRUE(contains(args, abs("pre.h")));
    EXPECT_TRUE(contains(args, (dir / "abs").lexically_normal().string())) << "already absolute: left alone";
}

TEST(CleanCommandLine, AnMsvcCommandLineGetsClDriverModeAndLosesItsOutputOptions) {
    const fs::path dir = fs::temp_directory_path() / "proj" / "build";
    const std::string source = (dir / "a.cpp").string();
    const std::vector<std::string> args = cleanCommandLine(
        { "C:/VS/bin/cl.exe", "/nologo", "/TP", "/DFOO", "/I..\\inc", "/std:c++17", "/EHsc", "/MDd", "/Zi", "/FS", "/Fobuild\\a.obj",
          "/Fdbuild\\vc.pdb", "/showIncludes", "/c", source }, dir.string(), source);
    ASSERT_FALSE(args.empty());
    EXPECT_EQ(args[0], "--driver-mode=cl");
    EXPECT_TRUE(contains(args, "/std:c++17"));
    EXPECT_TRUE(contains(args, "/DFOO"));
    EXPECT_TRUE(contains(args, "/EHsc"));
    EXPECT_TRUE(contains(args, "/MDd"));
    EXPECT_TRUE(hasPrefix(args, "/I")) << "the include path is kept (made absolute)";
    for (const char* dropped : { "/nologo", "/Zi", "/FS", "/showIncludes", "/c" }) {
        EXPECT_FALSE(contains(args, dropped)) << dropped;
    }
    EXPECT_FALSE(hasPrefix(args, "/Fo"));
    EXPECT_FALSE(hasPrefix(args, "/Fd"));
    EXPECT_FALSE(contains(args, source));
}

TEST(CompileFlagsFor, NothingFoundMeansTheDefaults) {
    Tree tree;
    const fs::path file = tree.write("lonely/a.cpp", "int a;\n");
    const CompileFlags flags = compileFlagsFor(file.string());
    EXPECT_FALSE(flags.fromProject());
    EXPECT_EQ(flags.args, cpptools::defaultCompileArgs());
}

TEST(CompileFlagsFor, TheFilesOwnEntryInTheNearestDatabaseIsUsed) {
    Tree tree;
    const fs::path source = tree.write("proj/src/a.cpp", "#include \"foo.h\"\nFoo f;\n");
    tree.write("proj/inc/foo.h", "struct Foo { int x; };\n");
    const fs::path other = tree.write("proj/src/b.cpp", "int b;\n");
    tree.write("proj/build/compile_commands.json",
        "[\n"
        " {\"directory\": " + json(tree.root / "proj" / "build") + ", \"file\": " + json(source) +
        ", \"arguments\": [\"clang++\", \"-I../inc\", \"-DA_ONLY\", \"-std=c++17\", \"-c\", " + json(source) + ", \"-o\", \"a.o\"]},\n"
        " {\"directory\": " + json(tree.root / "proj" / "build") + ", \"file\": " + json(other) +
        ", \"arguments\": [\"clang++\", \"-DB_ONLY\", \"-c\", " + json(other) + "]}\n"
        "]\n");

    const CompileFlags flags = compileFlagsFor(source.string());
    ASSERT_TRUE(flags.fromProject());
    EXPECT_NE(flags.origin.find("compile_commands.json"), std::string::npos);
    EXPECT_TRUE(contains(flags.args, "-DA_ONLY"));
    EXPECT_FALSE(contains(flags.args, "-DB_ONLY")) << "its own entry, not its neighbour's";
    EXPECT_TRUE(contains(flags.args, "-I" + (tree.root / "proj" / "inc").lexically_normal().string()));

    // The flags really do find the include.
    cpptools::Parser parser;
    const std::string code = "#include \"foo.h\"\nFoo f;\n";
    auto withFlags = parser.parseBuffer(source.string(), code, flags.args);
    auto withDefaults = parser.parseBuffer(source.string(), code);
    auto errors = [](const cpptools::ParseResult& result) {
        return std::count_if(result.diagnostics.begin(), result.diagnostics.end(), [](const cpptools::Diagnostic& d) {
            return d.severity == cpptools::Severity::Error || d.severity == cpptools::Severity::Fatal;
        });
    };
    EXPECT_EQ(errors(withFlags), 0);
    EXPECT_GT(errors(withDefaults), 0) << "without the include path foo.h isn't found";
}

TEST(CompileFlagsFor, AHeaderBorrowsTheClosestSourceFilesFlagsAndIsParsedAsCpp) {
    Tree tree;
    const fs::path near = tree.write("proj/src/lex/a.cpp", "int a;\n");
    const fs::path far = tree.write("proj/other/z.cpp", "int z;\n");
    const fs::path header = tree.write("proj/src/lex/a.h", "struct A {};\n");
    tree.write("proj/build/compile_commands.json",
        "[\n"
        " {\"directory\": " + json(tree.root / "proj" / "build") + ", \"file\": " + json(far) + ", \"arguments\": [\"clang++\", \"-DFAR\", " + json(far) + "]},\n"
        " {\"directory\": " + json(tree.root / "proj" / "build") + ", \"file\": " + json(near) + ", \"arguments\": [\"clang++\", \"-DNEAR\", " + json(near) + "]}\n"
        "]\n");

    const CompileFlags flags = compileFlagsFor(header.string());
    ASSERT_TRUE(flags.fromProject());
    EXPECT_TRUE(contains(flags.args, "-DNEAR"));
    EXPECT_FALSE(contains(flags.args, "-DFAR"));
    EXPECT_TRUE(contains(flags.args, "-xc++")) << "a header is C++, not what the C++ source's language was inferred from";

    // A file nobody built that's beside neither: still the closest one.
    const fs::path unbuilt = tree.write("proj/other/new.h", "struct N {};\n");
    EXPECT_TRUE(contains(compileFlagsFor(unbuilt.string()).args, "-DFAR"));
}

TEST(CompileFlagsFor, MsvcEntriesGiveClDriverModeAndAHeaderGetsTP) {
    Tree tree;
    const fs::path source = tree.write("proj/src/a.cpp", "int a;\n");
    const fs::path header = tree.write("proj/src/a.h", "struct A {};\n");
    tree.write("proj/build/compile_commands.json",
        "[{\"directory\": " + json(tree.root / "proj" / "build") + ", \"file\": " + json(source) +
        ", \"command\": \"cl.exe /nologo /TP /DFOO /I../inc /std:c++17 /EHsc /Fobuild/a.obj /c " + source.generic_string() + "\"}]\n");

    CompileFlags flags = compileFlagsFor(source.string());
    ASSERT_TRUE(flags.fromProject());
    ASSERT_FALSE(flags.args.empty());
    EXPECT_EQ(flags.args[0], "--driver-mode=cl");
    EXPECT_TRUE(contains(flags.args, "/DFOO"));
    EXPECT_FALSE(hasPrefix(flags.args, "/Fo"));

    flags = compileFlagsFor(header.string());
    EXPECT_TRUE(contains(flags.args, "/TP"));
}

TEST(CompileFlagsFor, TheNewestBuildDirectoryWinsAndACloserDatabaseBeatsAFartherOne) {
    Tree tree;
    const fs::path source = tree.write("proj/src/a.cpp", "int a;\n");
    auto entry = [&](const std::string& define) {
        return "[{\"directory\": " + json(tree.root / "proj") + ", \"file\": " + json(source) +
               ", \"arguments\": [\"clang++\", \"-D" + define + "\", " + json(source) + "]}]\n";
    };
    tree.write("proj/build-old/compile_commands.json", entry("OLD"));
    tree.write("proj/build-new/compile_commands.json", entry("NEW"));
    fs::last_write_time(tree.root / "proj" / "build-old" / "compile_commands.json",
        fs::last_write_time(tree.root / "proj" / "build-new" / "compile_commands.json") - std::chrono::hours(1));
    EXPECT_TRUE(contains(compileFlagsFor(source.string()).args, "-DNEW"));

    // A database in a nearer directory (src/) is found before the one at proj/.
    tree.write("proj/src/compile_commands.json", entry("NEAR"));
    EXPECT_TRUE(contains(compileFlagsFor(source.string()).args, "-DNEAR"));
}

TEST(CompileFlagsFor, CompileFlagsTxtIsTheFallback) {
    Tree tree;
    const fs::path file = tree.write("proj/src/a.cpp", "int a;\n");
    tree.write("proj/compile_flags.txt", "-std=c++20\n\n-DFROM_TXT\n-Iinc\n");
    const CompileFlags flags = compileFlagsFor(file.string());
    ASSERT_TRUE(flags.fromProject());
    EXPECT_NE(flags.origin.find("compile_flags.txt"), std::string::npos);
    EXPECT_TRUE(contains(flags.args, "-std=c++20"));
    EXPECT_TRUE(contains(flags.args, "-DFROM_TXT"));
    EXPECT_TRUE(contains(flags.args, "-I" + (tree.root / "proj" / "inc").lexically_normal().string())) << "relative to the file's own directory";
}

TEST(CompileFlagsFor, ACompileFlagsTxtBesideADatabaseWinsBecauseThatIsLibclangsOrderAndSaysSo) {
    Tree tree;
    const fs::path source = tree.write("proj/a.cpp", "int a;\n");
    tree.write("proj/compile_flags.txt", "-DFROM_TXT\n");
    tree.write("proj/compile_commands.json",
        "[{\"directory\": " + json(tree.root / "proj") + ", \"file\": " + json(source) + ", \"arguments\": [\"clang++\", \"-DFROM_DB\", " + json(source) + "]}]\n");
    const CompileFlags flags = compileFlagsFor(source.string());
    EXPECT_TRUE(contains(flags.args, "-DFROM_TXT"));
    EXPECT_FALSE(contains(flags.args, "-DFROM_DB"));
    EXPECT_NE(flags.origin.find("compile_flags.txt"), std::string::npos) << "and the origin doesn't claim the JSON";
}

TEST(CompileFlagsFor, ABrokenDatabaseIsSkippedNotFatal) {
    Tree tree;
    const fs::path source = tree.write("proj/a.cpp", "int a;\n");
    tree.write("proj/compile_commands.json", "this is not json {{{");
    const CompileFlags flags = compileFlagsFor(source.string());
    EXPECT_FALSE(flags.fromProject());
    EXPECT_EQ(flags.args, cpptools::defaultCompileArgs());
}

#ifdef _WIN32
TEST(CompileFlagsFor, TheStandardLibraryParsesWithAnMsvcCommandLine) {
    // What a real MSVC project's flags do to <vector>: the standard headers are found (libclang
    // locates the installed toolchain) and there is nothing wrong with them.
    Tree tree;
    const fs::path source = tree.write("proj/a.cpp", "#include <vector>\n#include <string>\nstd::vector<std::string> v;\n");
    tree.write("proj/build/compile_commands.json",
        "[{\"directory\": " + json(tree.root / "proj" / "build") + ", \"file\": " + json(source) +
        ", \"command\": \"cl.exe /nologo /TP /std:c++17 /EHsc /c " + source.generic_string() + "\"}]\n");
    const CompileFlags flags = compileFlagsFor(source.string());
    cpptools::Parser parser;
    auto result = parser.parseBuffer(source.string(), "#include <vector>\n#include <string>\nstd::vector<std::string> v;\n", flags.args);
    for (const cpptools::Diagnostic& d : result.diagnostics) {
        if (d.message.find("file not found") != std::string::npos) {
            GTEST_SKIP() << "no MSVC toolchain found by libclang here";
        }
    }
    for (const cpptools::Diagnostic& d : result.diagnostics) {
        EXPECT_LT(static_cast<int>(d.severity), static_cast<int>(cpptools::Severity::Error)) << d.message;
    }
}
#endif
