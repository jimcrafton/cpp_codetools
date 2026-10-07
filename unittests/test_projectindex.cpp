// ProjectIndex: what a project's files declare, include and refer to, found by parsing each one with
// libclang. The tests write a few small, self-contained files to a temp folder.

#include "cpptools/projectindex.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

using namespace cpptools;
namespace fs = std::filesystem;

namespace {

std::vector<std::string> plainFlags(const std::string&) { return { "-std=c++17", "-xc++" }; }

bool has(const std::vector<IndexedSymbol>& symbols, const std::string& qualified) {
    return std::any_of(symbols.begin(), symbols.end(), [&](const IndexedSymbol& s) { return s.qualifiedName == qualified; });
}

const IndexedSymbol* find(const std::vector<IndexedSymbol>& symbols, const std::string& qualified, SymbolKind kind) {
    for (const IndexedSymbol& s : symbols) {
        if (s.qualifiedName == qualified && s.kind == kind) return &s;
    }
    return nullptr;
}

class ProjectIndexTest : public ::testing::Test {
protected:
    void SetUp() override {
        static int counter = 0;
        dir_ = fs::temp_directory_path() / ("cpptools_projectindex_" + std::to_string(++counter) + "_" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(dir_);

        write("a.h",
            "#pragma once\n"
            "namespace shapes {\n"
            "class Shape {\n"
            "public:\n"
            "    virtual ~Shape();\n"
            "    virtual double area() const = 0;\n"
            "protected:\n"
            "    int id_ = 0;\n"
            "};\n"
            "class Circle : public Shape {\n"
            "public:\n"
            "    explicit Circle(double r);\n"
            "    double area() const override;\n"
            "private:\n"
            "    double radius_;\n"
            "};\n"
            "struct Point;\n"
            "}\n");
        write("b.cpp",
            "#include \"a.h\"\n"
            "namespace shapes {\n"
            "Shape::~Shape() {}\n"
            "Circle::Circle(double r) : radius_(r) {}\n"
            "double Circle::area() const { return 3.14 * radius_ * radius_; }\n"
            "}\n"
            "double total(const shapes::Shape& s) { return s.area(); }\n");
        write("c.h", "class Orphan { public: int value; };\n");
        write("b2.h", "#include \"a.h\"\ninline int twice(int x) { return x * 2; }\n");
        write("d.cpp", "#include \"b2.h\"\nint main() { return twice(2); }\n");

        index_.setFlagsProvider(plainFlags);
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(dir_, ec);
    }

    void write(const std::string& name, const std::string& text) {
        std::ofstream(dir_ / name, std::ios::binary | std::ios::trunc) << text;
    }

    std::string path(const std::string& name) const { return (dir_ / name).string(); }

    std::vector<std::string> all() const { return { path("a.h"), path("b.cpp"), path("c.h"), path("b2.h"), path("d.cpp") }; }

    IndexProgress indexAll(unsigned threads = 2) { return index_.indexFiles(all(), {}, threads); }

    fs::path dir_;
    ProjectIndex index_;
};

}  // namespace

TEST_F(ProjectIndexTest, IndexesEveryFileAndReportsWhatItDid) {
    const IndexProgress summary = indexAll();

    EXPECT_EQ(summary.total, 5u);
    EXPECT_EQ(summary.done, 5u);
    EXPECT_EQ(summary.parsed, 5u);
    EXPECT_EQ(summary.failed, 0u);
    EXPECT_EQ(index_.fileCount(), 5u);
    EXPECT_TRUE(index_.hasFile(path("a.h")));
}

TEST_F(ProjectIndexTest, FindsEachClassOnceByItsQualifiedName) {
    indexAll();
    const std::vector<IndexedSymbol> classes = index_.findClasses();

    ASSERT_EQ(classes.size(), 3u);
    EXPECT_TRUE(has(classes, "shapes::Shape"));
    EXPECT_TRUE(has(classes, "shapes::Circle"));
    EXPECT_TRUE(has(classes, "Orphan"));
    EXPECT_FALSE(has(classes, "shapes::Point")) << "only forward-declared";
    for (const IndexedSymbol& c : classes) {
        EXPECT_TRUE(c.isDefinition);
        EXPECT_FALSE(c.usr.empty());
    }
}

TEST_F(ProjectIndexTest, AFilterMatchesAnyCaseAndPutsPrefixMatchesFirst) {
    indexAll();

    const auto circle = index_.findClasses("CIRC");
    ASSERT_EQ(circle.size(), 1u);
    EXPECT_EQ(circle[0].name, "Circle");

    const auto sh = index_.findClasses("sh");   // shapes::Shape, shapes::Circle (inside the namespace)
    ASSERT_GE(sh.size(), 2u);
    EXPECT_EQ(sh[0].name, "Shape") << "its name starts with it; Circle only has it in its namespace";
    EXPECT_TRUE(index_.findClasses("nothing like it").empty());
}

TEST_F(ProjectIndexTest, AClassKnowsItsBasesAndWhereItIsWritten) {
    indexAll();
    const auto classes = index_.findClasses("Circle");

    ASSERT_EQ(classes.size(), 1u);
    ASSERT_EQ(classes[0].bases.size(), 1u);
    EXPECT_EQ(classes[0].bases[0], "shapes::Shape");
    EXPECT_EQ(classes[0].location.file, ProjectIndex::normalizePath(path("a.h")));
    EXPECT_EQ(classes[0].location.line, 10u);
    EXPECT_EQ(classes[0].kind, SymbolKind::Class);
}

TEST_F(ProjectIndexTest, MembersListTheirKindsAndTheirEnclosingClass) {
    indexAll();
    const auto symbols = index_.symbolsIn(path("a.h"));
    const auto circle = index_.findClasses("Circle");
    ASSERT_EQ(circle.size(), 1u);

    EXPECT_NE(find(symbols, "shapes", SymbolKind::Namespace), nullptr);
    EXPECT_NE(find(symbols, "shapes::Shape::id_", SymbolKind::Field), nullptr);
    EXPECT_NE(find(symbols, "shapes::Shape::~Shape", SymbolKind::Destructor), nullptr);
    EXPECT_NE(find(symbols, "shapes::Circle::Circle", SymbolKind::Constructor), nullptr);
    const IndexedSymbol* area = find(symbols, "shapes::Circle::area", SymbolKind::Method);
    ASSERT_NE(area, nullptr);
    EXPECT_EQ(area->parentUsr, circle[0].usr);
    EXPECT_FALSE(area->isDefinition) << "declared here, defined in b.cpp";
}

TEST_F(ProjectIndexTest, ADeclarationInAHeaderAndItsDefinitionInASourceAreOneSymbol) {
    indexAll();
    const auto symbols = index_.symbolsIn(path("a.h"));
    const IndexedSymbol* area = find(symbols, "shapes::Circle::area", SymbolKind::Method);
    ASSERT_NE(area, nullptr);

    const auto both = index_.declarationsOf(area->usr);
    ASSERT_EQ(both.size(), 2u);
    EXPECT_EQ(std::count_if(both.begin(), both.end(), [](const IndexedSymbol& s) { return s.isDefinition; }), 1);
    EXPECT_TRUE(find(index_.symbolsIn(path("b.cpp")), "shapes::Circle::area", SymbolKind::Method)->isDefinition);
}

TEST_F(ProjectIndexTest, RecordsWhatEachFileIncludesAndWhoIncludesIt) {
    indexAll();
    const auto edges = index_.includesOf(path("b.cpp"));

    ASSERT_EQ(edges.size(), 1u);
    EXPECT_EQ(edges[0].included, ProjectIndex::normalizePath(path("a.h")));
    EXPECT_EQ(edges[0].line, 1u);
    EXPECT_FALSE(edges[0].external);

    const auto direct = index_.includers(path("a.h"));
    ASSERT_EQ(direct.size(), 2u);   // b.cpp and b2.h, not d.cpp
    const auto everyone = index_.includers(path("a.h"), true);
    EXPECT_EQ(everyone.size(), 3u) << "d.cpp through b2.h";
    EXPECT_TRUE(index_.includers(path("d.cpp")).empty());
}

TEST_F(ProjectIndexTest, CountsHowOftenAndFromHowManyFilesASymbolIsUsed) {
    indexAll();
    const auto shape = index_.findClasses("Shape");
    const auto orphan = index_.findClasses("Orphan");
    ASSERT_FALSE(shape.empty());
    ASSERT_EQ(orphan.size(), 1u);

    EXPECT_GE(index_.referenceCount(shape[0].usr), 2u);
    EXPECT_EQ(index_.referencingFileCount(shape[0].usr), 2u) << "a.h (as a base) and b.cpp";
    EXPECT_EQ(index_.referenceCount(orphan[0].usr), 0u);
    EXPECT_EQ(index_.referencingFileCount(orphan[0].usr), 0u);
}

TEST_F(ProjectIndexTest, WithRootsOnlyTheProjectsOwnDeclarationsAreCounted) {
    fs::path elsewhere = dir_ / "elsewhere";
    fs::create_directories(elsewhere);
    index_.setRoots({ elsewhere.string() });
    indexAll();

    const auto shape = index_.findClasses("Shape");
    ASSERT_FALSE(shape.empty());
    EXPECT_EQ(index_.referenceCount(shape[0].usr), 0u) << "a.h is not under the roots";
    ASSERT_EQ(index_.includesOf(path("b.cpp")).size(), 1u);
    EXPECT_TRUE(index_.includesOf(path("b.cpp"))[0].external);

    ProjectIndex inside;
    inside.setFlagsProvider(plainFlags);
    inside.setRoots({ dir_.string() });
    inside.indexFiles(all(), {}, 2);
    EXPECT_GE(inside.referenceCount(inside.findClasses("Shape")[0].usr), 2u);
    EXPECT_FALSE(inside.includesOf(path("b.cpp"))[0].external);
}

TEST_F(ProjectIndexTest, AFileThatHasNotChangedIsNotParsedAgain) {
    indexAll();
    const IndexProgress again = indexAll();

    EXPECT_EQ(again.parsed, 0u);
    EXPECT_EQ(again.skipped, 5u);
}

TEST_F(ProjectIndexTest, AChangedFileIsParsedAgainAndTheRestAreNot) {
    indexAll();
    write("c.h", "class Orphan { public: int value; };\nclass Newcomer {};\n");

    const IndexProgress again = indexAll();
    EXPECT_EQ(again.parsed, 1u);
    EXPECT_EQ(again.skipped, 4u);
    EXPECT_TRUE(has(index_.findClasses(), "Newcomer"));
}

TEST_F(ProjectIndexTest, ATouchedFileWithTheSameTextIsNotParsedAgain) {
    indexAll();
    const fs::path file = dir_ / "c.h";
    fs::last_write_time(file, fs::last_write_time(file) + std::chrono::hours(1));   // newer, same bytes

    const IndexProgress again = indexAll();
    EXPECT_EQ(again.parsed, 0u);
    EXPECT_EQ(again.skipped, 5u);
    EXPECT_EQ(indexAll().skipped, 5u);   // and the new time was remembered
}

TEST_F(ProjectIndexTest, ATouchedFileWithOtherTextOfTheSameSizeIsParsedAgain) {
    indexAll();
    write("c.h", "class Orphan { public: int valux; };\n");   // one letter different, same length
    fs::last_write_time(dir_ / "c.h", fs::last_write_time(dir_ / "c.h") + std::chrono::hours(1));

    EXPECT_EQ(indexAll().parsed, 1u);
}

TEST_F(ProjectIndexTest, TheContentHashSurvivesTheCache) {
    indexAll();
    const std::string cache = path("index.bin");
    ASSERT_TRUE(index_.save(cache));
    ASSERT_FALSE(fs::exists(cache + ".tmp"));   // written aside and moved over

    ProjectIndex loaded;
    loaded.setFlagsProvider(plainFlags);
    ASSERT_TRUE(loaded.load(cache));
    fs::last_write_time(dir_ / "c.h", fs::last_write_time(dir_ / "c.h") + std::chrono::hours(1));
    EXPECT_EQ(loaded.indexFiles(all(), {}, 2).parsed, 0u);
}

TEST_F(ProjectIndexTest, ChangedCompileFlagsMakeEveryFileStale) {
    indexAll();
    index_.setFlagsProvider([](const std::string&) { return std::vector<std::string>{ "-std=c++17", "-xc++", "-DEXTRA=1" }; });

    EXPECT_EQ(indexAll().parsed, 5u);
}

TEST_F(ProjectIndexTest, AMissingFileIsCountedAsFailedAndDropsOut) {
    indexAll();
    fs::remove(dir_ / "c.h");

    const IndexProgress again = indexAll();
    EXPECT_EQ(again.failed, 1u);
    EXPECT_FALSE(index_.hasFile(path("c.h")));
    EXPECT_FALSE(has(index_.findClasses(), "Orphan"));
}

TEST_F(ProjectIndexTest, AnUnsavedBufferIsIndexedUntilTheFileIsIndexedAgain) {
    indexAll();
    const std::string buffer = "class Buffered { public: int x; };\n";

    ASSERT_TRUE(index_.updateFile(path("c.h"), &buffer));
    EXPECT_TRUE(has(index_.findClasses(), "Buffered"));
    EXPECT_FALSE(has(index_.findClasses(), "Orphan"));

    EXPECT_EQ(indexAll().parsed, 1u) << "the buffer was not what is on disk";
    EXPECT_TRUE(has(index_.findClasses(), "Orphan"));
    EXPECT_FALSE(has(index_.findClasses(), "Buffered"));
}

TEST_F(ProjectIndexTest, RemovingAFileForgetsIt) {
    indexAll();
    index_.removeFile(path("c.h"));

    EXPECT_FALSE(index_.hasFile(path("c.h")));
    EXPECT_FALSE(has(index_.findClasses(), "Orphan"));
}

TEST_F(ProjectIndexTest, ProgressIsReportedAndCanStopTheRun) {
    std::size_t calls = 0;
    const IndexProgress summary = index_.indexFiles(all(), [&](const IndexProgress& p) {
        ++calls;
        return p.done < 2;   // stop after the second file
    }, 1);

    EXPECT_EQ(calls, 2u);
    EXPECT_EQ(summary.done, 2u);
    EXPECT_LT(index_.fileCount(), 5u);
}

TEST_F(ProjectIndexTest, ManyThreadsGiveTheSameResultAsOne) {
    ProjectIndex single;
    single.setFlagsProvider(plainFlags);
    single.indexFiles(all(), {}, 1);
    indexAll(4);

    const auto a = single.findClasses();
    const auto b = index_.findClasses();
    ASSERT_EQ(a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        EXPECT_EQ(a[i].usr, b[i].usr);
        EXPECT_EQ(single.referenceCount(a[i].usr), index_.referenceCount(b[i].usr));
    }
}

TEST_F(ProjectIndexTest, SavedIndexLoadsBackAsItWas) {
    indexAll();
    const std::string cache = path("index.bin");
    ASSERT_TRUE(index_.save(cache));

    ProjectIndex loaded;
    loaded.setFlagsProvider(plainFlags);
    ASSERT_TRUE(loaded.load(cache));
    EXPECT_EQ(loaded.fileCount(), 5u);
    const auto before = index_.findClasses();
    const auto after = loaded.findClasses();
    ASSERT_EQ(before.size(), after.size());
    for (std::size_t i = 0; i < before.size(); ++i) {
        EXPECT_EQ(before[i].usr, after[i].usr);
        EXPECT_EQ(before[i].bases, after[i].bases);
        EXPECT_EQ(index_.referenceCount(before[i].usr), loaded.referenceCount(after[i].usr));
    }
    EXPECT_EQ(loaded.includers(path("a.h"), true).size(), 3u);

    // What it loaded is up to date: nothing is parsed again.
    EXPECT_EQ(loaded.indexFiles(all(), {}, 2).parsed, 0u);
}

TEST_F(ProjectIndexTest, ACacheThatIsNotOursIsRefusedAndChangesNothing) {
    indexAll();
    write("junk.bin", "this is not an index");
    write("short.bin", "CPTOOLIX");

    EXPECT_FALSE(index_.load(path("junk.bin")));
    EXPECT_FALSE(index_.load(path("short.bin")));
    EXPECT_FALSE(index_.load(path("missing.bin")));
    EXPECT_EQ(index_.fileCount(), 5u);
}

TEST(ProjectIndexPaths, ANormalizedPathIsAbsoluteWithForwardSlashesAndNoDots) {
    const std::string normalized = ProjectIndex::normalizePath("a/b/../c.h");

    EXPECT_NE(normalized.find("a/c.h"), std::string::npos);
    EXPECT_EQ(normalized.find('\\'), std::string::npos);
    EXPECT_TRUE(fs::path(normalized).is_absolute());
}

TEST(ProjectIndexRepository, FindsThisRepositorysOwnParserClasses) {
    ProjectIndex index;
    const std::string header = std::string(CPPTOOLS_TEST_REPO_ROOT) + "/include/cpptools/parser.h";
    ASSERT_TRUE(fs::exists(header));

    index.indexFiles({ header }, {}, 1);

    EXPECT_TRUE(has(index.findClasses(), "cpptools::Session"));
    EXPECT_TRUE(has(index.findClasses(), "cpptools::Parser"));
    EXPECT_TRUE(has(index.findClasses("sess"), "cpptools::Session"));
}

// Not run by default (several minutes of parsing in a Debug build): prints how long indexing this whole
// repository takes, cold and then warm. Run with --gtest_also_run_disabled_tests.
TEST(ProjectIndexRepository, DISABLED_TimesIndexingTheWholeProject) {
    const fs::path root = CPPTOOLS_TEST_REPO_ROOT;
    std::vector<std::string> files;
    for (const char* folder : { "include", "src", "extension/NativeEditControls", "unittests" }) {
        for (const auto& entry : fs::recursive_directory_iterator(root / folder)) {
            const std::string ext = entry.path().extension().string();
            if (entry.is_regular_file() && (ext == ".cpp" || ext == ".h")) files.push_back(entry.path().string());
        }
    }

    ProjectIndex index;
    index.setRoots({ root.string() });
    using Clock = std::chrono::steady_clock;
    auto start = Clock::now();
    IndexProgress cold = index.indexFiles(files, {}, 0);
    const auto coldMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
    start = Clock::now();
    IndexProgress warm = index.indexFiles(files, {}, 0);
    const auto warmMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();

    std::size_t symbols = 0;
    for (const std::string& f : index.files()) symbols += index.symbolsIn(f).size();
    std::printf("INDEX files=%zu parsed=%zu failed=%zu cold=%lldms warm=%lldms (skipped %zu) symbols=%zu classes=%zu\n",
        files.size(), cold.parsed, cold.failed, static_cast<long long>(coldMs), static_cast<long long>(warmMs), warm.skipped,
        symbols, index.findClasses().size());
    EXPECT_EQ(warm.parsed, 0u);
}

// Run by name (--gtest_also_run_disabled_tests): what libclang says about this repo's own test files, with the same flags
// the explorer uses. Prints to stdout.
TEST(ProjectIndexRepo, DISABLED_PrintsWhyTheRepoTestsReportErrors) {
    const std::string root = CPPTOOLS_TEST_REPO_ROOT;
    ProjectIndex index;
    index.setRoots({ root });
    std::vector<std::string> files;
    for (const char* name : { "test_explorer_model.cpp", "test_cmakemodel.cpp", "test_projectindex.cpp", "lex/cmake_lexer_tests.cpp",
                              "lex/cmake_parser_tests.cpp", "test_controllerwiring_newui.cpp", "test_delegatebindings.cpp",
                              "test_editor_file_kinds.cpp", "test_project_explorer.cpp" }) {
        files.push_back(root + "/unittests/" + name);
    }
    index.indexFiles(files, {}, 1);
    for (const std::string& file : files) {
        const ProjectIndex::Problems problems = index.problemsIn(file);
        std::printf("%s: %u errors, %u warnings, %u unresolved\n", file.c_str(), problems.errors, problems.warnings, problems.unresolvedIncludes);
        for (const IndexedDiagnostic& sample : problems.samples) {
            std::printf("    %s:%u  %s\n", sample.file.c_str(), sample.line, sample.message.c_str());
        }
    }
}

TEST_F(ProjectIndexTest, CountsTheErrorsAndWarningsEachFileReportsInItself) {
    write("bad.cpp", "int broken() { return missing_name; }\n");                       // an error in this file
    write("warn.cpp", "int fine() { int unused; return 0; }\n");                       // -Wall: an unused variable
    write("hdr_user.cpp", "#include \"bad_header.h\"\nint ok() { return 1; }\n");      // the problem is in the header
    write("bad_header.h", "int header_broken() { return also_missing; }\n");
    write("noinc.cpp", "#include \"does_not_exist.h\"\nint fine2() { return 2; }\n");   // only a missing include
    write("mid.h", "#include \"gone_for_good.h\"\n");                                    // the missing header is in a header...
    write("outer.cpp", "#include \"mid.h\"\nGoneType value;\n");                         // ...and this file's errors follow from it
    ProjectIndex local;
    local.setFlagsProvider([](const std::string&) { return std::vector<std::string>{ "-x", "c++", "-std=c++17", "-Wall" }; });
    local.indexFiles({ path("bad.cpp"), path("warn.cpp"), path("hdr_user.cpp"), path("bad_header.h"), path("a.h"), path("noinc.cpp"),
                       path("mid.h"), path("outer.cpp") }, {}, 1);

    // a missing header inside a header counts for every file that includes it, not just the header
    EXPECT_GE(local.problemsIn(path("outer.cpp")).unresolvedIncludes, 1u);
    EXPECT_GE(local.problemsIn(path("mid.h")).unresolvedIncludes, 1u);
    EXPECT_EQ(local.problemsIn(path("hdr_user.cpp")).unresolvedIncludes, 0u) << "an ordinary error in a header is not a missing one";

    // a header that cannot be found is the flags' problem, not an error in the code
    EXPECT_EQ(local.problemsIn(path("noinc.cpp")).errors, 0u);
    EXPECT_GE(local.problemsIn(path("noinc.cpp")).unresolvedIncludes, 1u);
    EXPECT_EQ(local.problemsIn(path("bad.cpp")).unresolvedIncludes, 0u);

    EXPECT_GE(local.problemsIn(path("bad.cpp")).errors, 1u);
    EXPECT_EQ(local.problemsIn(path("bad.cpp")).warnings, 0u);
    EXPECT_EQ(local.problemsIn(path("warn.cpp")).errors, 0u);
    EXPECT_GE(local.problemsIn(path("warn.cpp")).warnings, 1u);
    EXPECT_EQ(local.problemsIn(path("hdr_user.cpp")).errors, 0u) << "a header's problem is the header's own";
    EXPECT_GE(local.problemsIn(path("bad_header.h")).errors, 1u) << "indexed as a file of its own";
    EXPECT_EQ(local.problemsIn(path("a.h")).errors, 0u);
    EXPECT_EQ(local.problemsIn(path("not_indexed.cpp")).errors, 0u);

    // an example of what the compiler said is kept, with its line
    const ProjectIndex::Problems bad = local.problemsIn(path("bad.cpp"));
    ASSERT_FALSE(bad.samples.empty());
    EXPECT_EQ(bad.samples[0].line, 1u);
    EXPECT_NE(bad.samples[0].message.find("missing_name"), std::string::npos) << bad.samples[0].message;
    EXPECT_LE(bad.samples.size(), 3u) << "only the first few";
    const ProjectIndex::Problems outer = local.problemsIn(path("outer.cpp"));
    ASSERT_FALSE(outer.samples.empty());
    EXPECT_NE(outer.samples[0].message.find("file not found"), std::string::npos) << "the include that is missing, reported in mid.h";
    EXPECT_NE(outer.samples[0].file.find("mid.h"), std::string::npos);

    // and they survive the cache
    const std::string cache = path("problems.bin");
    ASSERT_TRUE(local.save(cache));
    ProjectIndex loaded;
    ASSERT_TRUE(loaded.load(cache));
    EXPECT_EQ(loaded.problemsIn(path("bad.cpp")).errors, local.problemsIn(path("bad.cpp")).errors);
    EXPECT_EQ(loaded.problemsIn(path("warn.cpp")).warnings, local.problemsIn(path("warn.cpp")).warnings);
    EXPECT_EQ(loaded.problemsIn(path("noinc.cpp")).unresolvedIncludes, local.problemsIn(path("noinc.cpp")).unresolvedIncludes);
    ASSERT_EQ(loaded.problemsIn(path("bad.cpp")).samples.size(), bad.samples.size());
    EXPECT_EQ(loaded.problemsIn(path("bad.cpp")).samples[0].message, bad.samples[0].message);
    EXPECT_EQ(loaded.problemsIn(path("bad.cpp")).samples[0].line, bad.samples[0].line);
}
