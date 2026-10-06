// The include graph around one file: columns of who includes it and what it includes, widest first.

#include "../extension/NativeEditControls/IncludeGraphLayout.h"

#include "cpptools/includeanalysis.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>

using namespace CodeToolsVsix;
namespace fs = std::filesystem;

namespace {

std::vector<std::string> plainFlags(const std::string&) { return { "-std=c++17", "-xc++" }; }

class IncludeGraphLayoutTest : public ::testing::Test {
protected:
    void SetUp() override {
        static int counter = 0;
        dir_ = fs::temp_directory_path() / ("cpptools_igl_" + std::to_string(++counter) + "_" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(dir_);
        write("types.h", "#pragma once\nstruct T { int x; };\n");
        write("util.h", "#pragma once\n#include \"types.h\"\ninline int util() { return 1; }\n");
        write("forward.h", "#pragma once\n#include \"types.h\"\n");
        write("main.cpp", "#include \"util.h\"\nint main() { return util(); }\n");
        write("other.cpp", "#include \"types.h\"\nint g() { T t; return t.x; }\n");
        write("third.cpp", "#include \"forward.h\"\nint h() { T t; return t.x; }\n");

        index_.setFlagsProvider(plainFlags);
        index_.setRoots({ dir_.string() });
        std::vector<std::string> files;
        for (const char* name : { "types.h", "util.h", "forward.h", "main.cpp", "other.cpp", "third.cpp" }) files.push_back(path(name));
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

    static std::vector<std::string> labelsIn(const IncludeGraphLayout& layout, int column) {
        std::vector<std::string> labels;
        for (const IncludeGraphNode& node : layout.nodes) {
            if (node.column == column) labels.push_back(node.label);
        }
        return labels;
    }

    static bool hasEdge(const IncludeGraphLayout& layout, const std::string& from, const std::string& to) {
        return std::any_of(layout.edges.begin(), layout.edges.end(), [&](const IncludeGraphEdge& e) {
            return layout.nodes[e.from].label == from && layout.nodes[e.to].label == to;
        });
    }

    fs::path dir_;
    cpptools::ProjectIndex index_;
};

}  // namespace

TEST_F(IncludeGraphLayoutTest, TheFocusIsInTheMiddleWithIncludersOnTheLeftAndIncludesOnTheRight) {
    const cpptools::IncludeGraph graph(index_);
    const IncludeGraphLayout layout = layoutIncludeGraph(graph, path("util.h"));

    ASSERT_FALSE(layout.empty());
    EXPECT_EQ(layout.nodes[0].label, "util.h");
    EXPECT_EQ(layout.nodes[0].column, 0);
    EXPECT_EQ(labelsIn(layout, -1), std::vector<std::string>({ "main.cpp" }));
    EXPECT_EQ(labelsIn(layout, 1), std::vector<std::string>({ "types.h" }));
    EXPECT_EQ(layout.firstColumn, -1);
    EXPECT_EQ(layout.lastColumn, 1);
    EXPECT_TRUE(hasEdge(layout, "main.cpp", "util.h"));
    EXPECT_TRUE(hasEdge(layout, "util.h", "types.h"));
}

TEST_F(IncludeGraphLayoutTest, IncludersOfIncludersFillTheSecondColumnWidestFirst) {
    const cpptools::IncludeGraph graph(index_);
    const IncludeGraphLayout layout = layoutIncludeGraph(graph, path("types.h"));

    const std::vector<std::string> near = labelsIn(layout, -1);
    ASSERT_EQ(near.size(), 3u) << "util.h, forward.h and other.cpp";
    EXPECT_EQ(labelsIn(layout, -2).size(), 2u) << "main.cpp and third.cpp, through them";
    EXPECT_TRUE(hasEdge(layout, "main.cpp", "util.h"));
    EXPECT_TRUE(hasEdge(layout, "third.cpp", "forward.h"));
    EXPECT_TRUE(hasEdge(layout, "other.cpp", "types.h"));
    EXPECT_EQ(layout.tallestColumn, 3u);
    for (const IncludeGraphNode& node : layout.nodes) {
        if (node.column == -1) EXPECT_LE(node.row, 2u);
    }
}

TEST_F(IncludeGraphLayoutTest, ATallColumnKeepsSomeBoxesAndASummaryOfTheRest) {
    const cpptools::IncludeGraph graph(index_);
    const IncludeGraphLayout layout = layoutIncludeGraph(graph, path("types.h"), 1, 2);

    const std::vector<std::string> near = labelsIn(layout, -1);
    ASSERT_EQ(near.size(), 3u) << "two boxes and a summary";
    EXPECT_EQ(near.back(), "+1 more");
    const IncludeGraphNode& more = layout.nodes.back();
    EXPECT_TRUE(more.path.empty());
    EXPECT_EQ(more.hidden, 1u);
}

TEST_F(IncludeGraphLayoutTest, AFileTheGraphDoesNotKnowHasNoLayout) {
    const cpptools::IncludeGraph graph(index_);
    EXPECT_TRUE(layoutIncludeGraph(graph, path("nowhere.h")).empty());
}
