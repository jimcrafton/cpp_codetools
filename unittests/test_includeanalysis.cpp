// IncludeGraph: which headers cost the project most, what includes what, and which includes look removable.
// A few small files in a temp folder, indexed with libclang.

#include "cpptools/includeanalysis.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

using namespace cpptools;
namespace fs = std::filesystem;

namespace {

std::vector<std::string> plainFlags(const std::string&) { return { "-std=c++17", "-xc++" }; }

class IncludeGraphTest : public ::testing::Test {
protected:
    void SetUp() override {
        static int counter = 0;
        dir_ = fs::temp_directory_path() / ("cpptools_includegraph_" + std::to_string(++counter) + "_" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(dir_);
        write("types.h", "#pragma once\nstruct T { int x; };\n");
        write("util.h", "#pragma once\n#include \"types.h\"\ninline int util() { return 1; }\n");
        write("unused.h", "#pragma once\ninline int unusedFn() { return 0; }\n");
        write("forward.h", "#pragma once\n#include \"types.h\"\n");
        write("main.cpp", "#include \"util.h\"\n#include \"unused.h\"\nint main() { return util(); }\n");
        write("other.cpp", "#include \"types.h\"\nint g() { T t; return t.x; }\n");
        write("third.cpp", "#include \"forward.h\"\nint h() { T t; return t.x; }\n");

        index_.setFlagsProvider(plainFlags);
        index_.setRoots({ dir_.string() });
        std::vector<std::string> files;
        for (const char* name : { "types.h", "util.h", "unused.h", "forward.h", "main.cpp", "other.cpp", "third.cpp" }) files.push_back(path(name));
        index_.indexFiles(files, {}, 2);
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(dir_, ec);
    }

    void write(const std::string& name, const std::string& text) {
        std::ofstream(dir_ / name, std::ios::binary | std::ios::trunc) << text;
    }

    std::string path(const std::string& name) const { return (dir_ / name).string(); }

    fs::path dir_;
    ProjectIndex index_;
};

bool named(const std::vector<std::string>& paths, const std::string& file) {
    return std::any_of(paths.begin(), paths.end(), [&](const std::string& p) { return fs::path(p).filename() == file; });
}

}  // namespace

TEST_F(IncludeGraphTest, ARankedHeaderCountsEveryFileItsChangeReaches) {
    const IncludeGraph graph(index_);
    const std::vector<HeaderImpact> ranked = graph.ranked();

    ASSERT_FALSE(ranked.empty());
    EXPECT_EQ(fs::path(ranked[0].path).filename(), "types.h") << "the most widely reached header comes first";
    EXPECT_EQ(ranked[0].directIncluders, 3u) << "util.h, forward.h and other.cpp";
    EXPECT_EQ(ranked[0].transitiveIncluders, 5u) << "those, plus main.cpp through util.h and third.cpp through forward.h";
    EXPECT_FALSE(ranked[0].external);

    const HeaderImpact unused = graph.impactOf(path("unused.h"));
    EXPECT_EQ(unused.directIncluders, 1u);
    EXPECT_EQ(unused.transitiveIncluders, 1u);
    EXPECT_EQ(graph.impactOf(path("util.h")).transitiveIncludes, 1u) << "types.h";
    EXPECT_EQ(graph.impactOf(path("main.cpp")).transitiveIncludes, 3u) << "util.h, unused.h and types.h";
}

TEST_F(IncludeGraphTest, ASourceFileIsNotRankedBecauseNothingIncludesIt) {
    const IncludeGraph graph(index_);
    for (const HeaderImpact& impact : graph.ranked()) EXPECT_NE(fs::path(impact.path).extension(), ".cpp");
    EXPECT_EQ(graph.impactOf(path("main.cpp")).directIncluders, 0u);
}

TEST_F(IncludeGraphTest, DirectIncludersAndIncludesComeBackInOrder) {
    const IncludeGraph graph(index_);
    const std::vector<std::string> includers = graph.includersOf(path("types.h"));
    ASSERT_EQ(includers.size(), 3u);
    EXPECT_TRUE(named(includers, "util.h"));
    EXPECT_TRUE(named(includers, "forward.h"));
    EXPECT_TRUE(named(includers, "other.cpp"));

    const std::vector<IncludeEdge> includes = graph.includesOf(path("main.cpp"));
    ASSERT_EQ(includes.size(), 2u);
    EXPECT_EQ(fs::path(includes[0].included).filename(), "util.h") << "directive order";
    EXPECT_EQ(includes[0].line, 1u);
    EXPECT_EQ(fs::path(includes[1].included).filename(), "unused.h");
    EXPECT_TRUE(graph.includersOf(path("nothing.h")).empty());
}

TEST_F(IncludeGraphTest, AnIncludeWhoseNamesNothingUsesIsHintedAndAForwardingHeaderIsNot) {
    const IncludeGraph graph(index_);
    const std::vector<PruneHint> hints = graph.pruneHints();

    ASSERT_EQ(hints.size(), 2u);
    auto hinted = [&](const std::string& includer, const std::string& header) {
        return std::any_of(hints.begin(), hints.end(), [&](const PruneHint& h) {
            return fs::path(h.includer).filename() == includer && fs::path(h.header).filename() == header;
        });
    };
    EXPECT_TRUE(hinted("main.cpp", "unused.h"));
    EXPECT_TRUE(hinted("util.h", "types.h")) << "util.h declares util() and never uses T";
    EXPECT_EQ(graph.pruneHintsFor(path("main.cpp")).at(0).line, 2u);
    EXPECT_TRUE(graph.pruneHintsFor(path("forward.h")).empty()) << "an umbrella header declares nothing: it exists to include";
    EXPECT_TRUE(graph.pruneHintsFor(path("third.cpp")).empty());
    EXPECT_TRUE(graph.pruneHintsFor(path("other.cpp")).empty());
}

TEST_F(IncludeGraphTest, AFileTheIndexNeverSawGivesNothing) {
    const IncludeGraph graph(index_);
    EXPECT_TRUE(graph.pruneHintsFor(path("missing.cpp")).empty());
    EXPECT_EQ(graph.impactOf(path("missing.h")).transitiveIncluders, 0u);
}
