// ExplorerModel: the rows the project explorer shows for its Files, Symbols and Products views, built
// from a folder on disk, a ProjectIndex and a cmakemodel::Model. No UI.

#include "../extension/NativeEditControls/ExplorerModel.h"

#include <cmakemodel/model.h>
#include <newui/svgimage.h>
#include <cpptools/projectindex.h>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

using namespace CodeToolsVsix;
namespace fs = std::filesystem;

namespace {

class ExplorerModelTest : public ::testing::Test {
protected:
    void SetUp() override {
        static int counter = 0;
        dir_ = fs::temp_directory_path() / ("explorer_model_" + std::to_string(++counter) + "_" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(dir_);
    }
    void TearDown() override {
        std::error_code ec;
        fs::remove_all(dir_, ec);
    }
    void write(const std::string& relative, const std::string& text) {
        fs::create_directories((dir_ / relative).parent_path());
        std::ofstream(dir_ / relative, std::ios::binary | std::ios::trunc) << text;
    }

    // The rows of one level, as the tree would show them.
    std::vector<std::string> rows(ExplorerTreeModel& model, const std::vector<std::size_t>& parent = {}) {
        std::vector<std::string> shown;
        for (std::size_t i = 0; i < model.childCount(parent); ++i) {
            std::vector<std::size_t> path = parent;
            path.push_back(i);
            shown.push_back(std::any_cast<std::string>(model.value(path)));
        }
        return shown;
    }

    fs::path dir_;
};

bool startsWithText(const std::string& text, const std::string& prefix) { return text.compare(0, prefix.size(), prefix) == 0; }

}  // namespace

TEST(ExplorerNode, ARowShowsItsKindItsNameAndItsDetail) {
    ExplorerNode type;
    type.kind = ExplorerNode::Kind::Class;
    type.text = "Session";
    type.detail = "parser.h";
    EXPECT_EQ(type.display(), "class Session   parser.h");

    ExplorerNode folder;
    folder.kind = ExplorerNode::Kind::Folder;
    folder.text = "src";
    EXPECT_EQ(folder.display(), "src/");

    ExplorerNode plain;
    plain.kind = ExplorerNode::Kind::Function;
    plain.text = "update()";
    EXPECT_EQ(plain.display(), "update()");
}

TEST_F(ExplorerModelTest, TheFilesViewListsFoldersFirstAndSkipsWhatNobodyBrowses) {
    write("src/a.cpp", "int a;\n");
    write("src/b.h", "int b;\n");
    write("docs/readme.md", "hello\n");
    write("build/out.txt", "x");
    write("build-ninja/out.txt", "x");
    write("3rdparty/dep.h", "x");
    write(".git/config", "x");
    write("top.txt", "12345");
    write("junk.obj", "x");

    ExplorerTreeModel model;
    model.setRoot(buildFilesTree(dir_.string()));
    const auto top = rows(model);

    ASSERT_EQ(top.size(), 3u) << "docs/, src/, top.txt";
    EXPECT_EQ(top[0], "docs/");
    EXPECT_EQ(top[1], "src/");
    EXPECT_TRUE(startsWithText(top[2], "top.txt"));
    EXPECT_NE(top[2].find("5 B"), std::string::npos) << "a file shows its size";

    const auto inSrc = rows(model, { 1 });
    ASSERT_EQ(inSrc.size(), 2u);
    EXPECT_TRUE(startsWithText(inSrc[0], "a.cpp"));
    EXPECT_TRUE(startsWithText(inSrc[1], "b.h"));
}

TEST_F(ExplorerModelTest, AFolderIsReadFromDiskOnlyWhenItIsFirstOpened) {
    write("src/a.cpp", "x");
    ExplorerTreeModel model;
    ExplorerNode root = buildFilesTree(dir_.string());
    EXPECT_FALSE(root.loaded);
    model.setRoot(std::move(root));

    ASSERT_EQ(model.childCount({}), 1u);
    const ExplorerNode* src = model.nodeAt({ 0 });
    ASSERT_NE(src, nullptr);
    EXPECT_FALSE(src->loaded) << "src is listed but not read";

    write("src/later.cpp", "x");   // appears because src has not been read yet
    EXPECT_EQ(model.childCount({ 0 }), 2u);
    EXPECT_TRUE(model.nodeAt({ 0 })->loaded);
}

TEST_F(ExplorerModelTest, AFileRowOpensItsPathAndAFolderOpensNothing) {
    write("src/a.cpp", "x");
    ExplorerTreeModel model;
    model.setRoot(buildFilesTree(dir_.string()));

    EXPECT_TRUE(model.nodeAt({ 0 })->path.empty());
    const ExplorerNode* file = model.nodeAt({ 0, 0 });
    ASSERT_NE(file, nullptr);
    EXPECT_EQ(file->kind, ExplorerNode::Kind::File);
    EXPECT_EQ(fs::path(file->path).filename().string(), "a.cpp");
    EXPECT_EQ(file->line, 0u);
    EXPECT_EQ(model.nodeAt({ 5 }), nullptr);
    EXPECT_EQ(model.childCount({ 5 }), 0u);
}

TEST_F(ExplorerModelTest, TheFileDetailProviderCanAddWhatEachRowSays) {
    write("a.cpp", "x");
    ExplorerTreeModel model;
    model.setRoot(buildFilesTree(dir_.string(), [](const std::string& path, bool isDirectory) {
        return isDirectory ? std::string() : std::string("not built");
    }));

    const auto top = rows(model);
    ASSERT_EQ(top.size(), 1u);
    EXPECT_EQ(top[0], "a.cpp   not built   1 line   1 B") << "the label, then the line and size columns";
}

TEST_F(ExplorerModelTest, SearchingFilesListsMatchesAnywhereBelowTheRoot) {
    write("src/parser.cpp", "x");
    write("src/deep/Parser_test.cpp", "x");
    write("src/other.cpp", "x");
    write("build/parser.cpp", "x");

    ExplorerTreeModel model;
    model.setRoot(buildFileSearch(dir_.string(), "PARSER"));
    const auto found = rows(model);

    ASSERT_EQ(found.size(), 2u) << "build/ is skipped";
    EXPECT_NE(found[0].find("parser.cpp"), std::string::npos);
    EXPECT_NE(found[1].find("Parser_test.cpp"), std::string::npos);
    EXPECT_NE(found[1].find("src/deep"), std::string::npos) << "the detail is the folder it is in";

    model.setRoot(buildFileSearch(dir_.string(), "zzz"));
    EXPECT_EQ(model.nodeAt({ 0 })->kind, ExplorerNode::Kind::Note);
    model.setRoot(buildFileSearch(dir_.string(), ""));
    EXPECT_EQ(model.nodeAt({ 0 })->kind, ExplorerNode::Kind::Note);
}

TEST_F(ExplorerModelTest, SearchingFilesStopsAtTheLimitAndSaysSo) {
    for (int i = 0; i < 5; ++i) write("f" + std::to_string(i) + ".cpp", "x");

    ExplorerTreeModel model;
    model.setRoot(buildFileSearch(dir_.string(), "f", 3));
    ASSERT_EQ(model.childCount({}), 4u);
    EXPECT_EQ(model.nodeAt({ 3 })->kind, ExplorerNode::Kind::Note);
}

namespace {

class SymbolsTest : public ExplorerModelTest {
protected:
    void SetUp() override {
        ExplorerModelTest::SetUp();
        write("a.h",
            "#pragma once\n"
            "namespace shapes {\n"
            "class Shape {\n"
            "public:\n"
            "    virtual ~Shape();\n"
            "    virtual double area() const = 0;\n"
            "    int id_ = 0;\n"
            "};\n"
            "class Circle : public Shape {\n"
            "public:\n"
            "    double area() const override;\n"
            "};\n"
            "enum Color { Red };\n"
            "double helper(int x);\n"
            "}\n");
        write("b.cpp",
            "#include \"a.h\"\n"
            "namespace shapes {\n"
            "Shape::~Shape() {}\n"
            "double Circle::area() const { return 1.0; }\n"
            "double helper(int x) { return x; }\n"
            "}\n");
        index_.setFlagsProvider([](const std::string&) { return std::vector<std::string>{ "-std=c++17", "-xc++" }; });
        index_.indexFiles({ (dir_ / "a.h").string(), (dir_ / "b.cpp").string() }, {}, 2);
    }

    cpptools::ProjectIndex index_;
};

}  // namespace

TEST_F(SymbolsTest, NamespacesHoldTheirTypesAndTypesHoldTheirMembers) {
    ExplorerTreeModel model;
    model.setRoot(buildSymbolsTree(index_));

    const auto top = rows(model);
    ASSERT_EQ(top.size(), 1u);
    EXPECT_EQ(top[0], "namespace shapes");

    const auto inside = rows(model, { 0 });
    // types first (by name, any case), then functions
    ASSERT_EQ(inside.size(), 4u);
    EXPECT_TRUE(startsWithText(inside[0], "class Circle"));
    EXPECT_TRUE(startsWithText(inside[1], "enum Color"));
    EXPECT_TRUE(startsWithText(inside[2], "class Shape"));
    EXPECT_EQ(inside[3], "helper()");
    EXPECT_NE(inside[0].find("a.h"), std::string::npos) << "a type shows the file it is in";

    const auto shapeMembers = rows(model, { 0, 2 });
    ASSERT_EQ(shapeMembers.size(), 3u);   // ~Shape(), area(), id_  (functions before data)
    EXPECT_EQ(shapeMembers[0], "area()");
    EXPECT_EQ(shapeMembers[1], "~Shape()");
    EXPECT_EQ(shapeMembers[2], "id_");
}

TEST_F(SymbolsTest, AMethodDeclaredInAHeaderAndDefinedInASourceIsOneRowThatOpensTheHeader) {
    ExplorerTreeModel model;
    model.setRoot(buildSymbolsTree(index_));

    const auto circleMembers = rows(model, { 0, 0 });
    ASSERT_EQ(circleMembers.size(), 1u);
    const ExplorerNode* area = model.nodeAt({ 0, 0, 0 });
    ASSERT_NE(area, nullptr);
    EXPECT_EQ(fs::path(area->path).filename().string(), "a.h") << "its declaration, not the .cpp";
    EXPECT_EQ(area->line, 11u);
}

TEST_F(SymbolsTest, ATypeOpensItsDefinition) {
    ExplorerTreeModel model;
    model.setRoot(buildSymbolsTree(index_));

    const ExplorerNode* circle = model.nodeAt({ 0, 0 });
    ASSERT_NE(circle, nullptr);
    EXPECT_EQ(circle->kind, ExplorerNode::Kind::Class);
    EXPECT_EQ(fs::path(circle->path).filename().string(), "a.h");
    EXPECT_EQ(circle->line, 9u);
}

TEST_F(SymbolsTest, AFilterKeepsMatchesWithTheRowsAroundThem) {
    ExplorerTreeModel model;
    model.setRoot(buildSymbolsTree(index_, "CIRC"));

    const auto top = rows(model);
    ASSERT_EQ(top.size(), 1u);
    EXPECT_EQ(top[0], "namespace shapes");
    const auto inside = rows(model, { 0 });
    ASSERT_EQ(inside.size(), 1u);
    EXPECT_TRUE(startsWithText(inside[0], "class Circle"));
    EXPECT_EQ(rows(model, { 0, 0 }).size(), 1u) << "a matching type keeps its members";
}

TEST_F(SymbolsTest, AFilterThatMatchesNothingSaysSo) {
    ExplorerTreeModel model;
    model.setRoot(buildSymbolsTree(index_, "zzzz"));

    const ExplorerNode* note = model.nodeAt({ 0 });
    ASSERT_NE(note, nullptr);
    EXPECT_EQ(note->kind, ExplorerNode::Kind::Note);
    EXPECT_NE(note->text.find("zzzz"), std::string::npos);

    cpptools::ProjectIndex empty;
    model.setRoot(buildSymbolsTree(empty));
    EXPECT_EQ(model.nodeAt({ 0 })->kind, ExplorerNode::Kind::Note);
}

namespace {

cmakemodel::Target target(const std::string& name, cmakemodel::TargetType type, cmakemodel::ProductKind kind) {
    cmakemodel::Target t;
    t.name = name;
    t.type = type;
    t.kind = kind;
    t.defined = { "CMakeLists.txt", 10, "add_library" };
    return t;
}

cmakemodel::Model sampleModel() {
    cmakemodel::Model model;
    model.sourceDir = "C:/proj";
    cmakemodel::Target app = target("app", cmakemodel::TargetType::Executable, cmakemodel::ProductKind::Application);
    app.nameOnDisk = "app.exe";
    app.defined = { "tools/CMakeLists.txt", 2, "add_executable" };
    app.sources.push_back({ "tools/main.cpp", false, "Source Files", "", app.defined });
    app.links.push_back({ cmakemodel::LinkKind::Target, "core", { "tools/CMakeLists.txt", 3, "target_link_libraries" } });
    app.links.push_back({ cmakemodel::LinkKind::Imported, "ext::lib", { "tools/CMakeLists.txt", 3, "target_link_libraries" } });
    app.links.push_back({ cmakemodel::LinkKind::Target, "plugin", { "tools/CMakeLists.txt", 3, "target_link_libraries" } });

    cmakemodel::Target core = target("core", cmakemodel::TargetType::StaticLibrary, cmakemodel::ProductKind::Library);
    core.sources.push_back({ "src/b.cpp", false, "Source Files", "", core.defined });
    core.sources.push_back({ "src/a.cpp", false, "Source Files", "", core.defined });
    core.sources.push_back({ "include/a.h", false, "Header Files", "", core.defined });

    cmakemodel::Target plugin = target("plugin", cmakemodel::TargetType::SharedLibrary, cmakemodel::ProductKind::Library);
    plugin.nameOnDisk = "plugin.dll";

    cmakemodel::Target test = target("core_tests", cmakemodel::TargetType::Executable, cmakemodel::ProductKind::Test);
    cmakemodel::Target gen = target("gen", cmakemodel::TargetType::Utility, cmakemodel::ProductKind::Tool);
    cmakemodel::Target vendored = target("vendored", cmakemodel::TargetType::StaticLibrary, cmakemodel::ProductKind::Library);
    vendored.directory = "3rdparty/vendored";
    cmakemodel::Target llvm = target("clang-tablegen-targets", cmakemodel::TargetType::Utility, cmakemodel::ProductKind::Tool);
    llvm.defined = { "D:/code/LLVM/lib/cmake/clang/ClangConfig.cmake", 5, "add_custom_target" };

    model.targets = { app, core, plugin, test, gen, vendored, llvm };
    return model;
}

}  // namespace

TEST(ExplorerProducts, ProductsAreGroupedAndThirdPartyTargetsAreLeftOut) {
    ExplorerTreeModel model;
    model.setRoot(buildProductsTree(sampleModel()));

    const auto groups = [&] {
        std::vector<std::string> shown;
        for (std::size_t i = 0; i < model.childCount({}); ++i) shown.push_back(std::any_cast<std::string>(model.value(std::vector<std::size_t>{ i })));
        return shown;
    }();
    ASSERT_EQ(groups.size(), 4u);
    EXPECT_EQ(groups[0], "Applications   (1)");
    EXPECT_EQ(groups[1], "Libraries   (2)") << "vendored (3rdparty) is not counted";
    EXPECT_EQ(groups[2], "Tests   (1)");
    EXPECT_EQ(groups[3], "Tools   (1)") << "the LLVM target is outside the project";
}

TEST(ExplorerProducts, AProductSplitsIntoBuildTimeAndRuntime) {
    ExplorerTreeModel model;
    model.setRoot(buildProductsTree(sampleModel()));

    const ExplorerNode* app = model.nodeAt({ 0, 0 });
    ASSERT_NE(app, nullptr);
    EXPECT_EQ(app->display(), "app   executable");
    ASSERT_EQ(app->children.size(), 2u);
    EXPECT_EQ(app->children[0].text, "Build Time");
    EXPECT_EQ(app->children[1].text, "Runtime");
    EXPECT_EQ(app->path, "C:/proj/tools/CMakeLists.txt") << "a product opens the CMake call that made it";
    EXPECT_EQ(app->line, 2u);
}

TEST(ExplorerProducts, SourcesAreFilesAndLinksOpenTheCMakeLineThatMadeThem) {
    ExplorerTreeModel model;
    model.setRoot(buildProductsTree(sampleModel()));

    const ExplorerNode& buildTime = model.nodeAt({ 0, 0 })->children[0];
    ASSERT_EQ(buildTime.children.size(), 2u);
    const ExplorerNode& sources = buildTime.children[0];
    EXPECT_EQ(sources.display(), "Sources   (1)");
    EXPECT_EQ(sources.children[0].path, "C:/proj/tools/main.cpp");
    EXPECT_EQ(sources.children[0].line, 0u);

    const ExplorerNode& links = buildTime.children[1];
    ASSERT_EQ(links.children.size(), 3u);
    EXPECT_EQ(links.children[0].display(), "core   target");
    EXPECT_EQ(links.children[0].path, "C:/proj/tools/CMakeLists.txt");
    EXPECT_EQ(links.children[0].line, 3u);
    EXPECT_EQ(links.children[1].display(), "ext::lib   imported");
}

TEST(ExplorerProducts, RuntimeNamesTheArtifactAndTheSharedLibrariesItNeeds) {
    ExplorerTreeModel model;
    model.setRoot(buildProductsTree(sampleModel()));

    const ExplorerNode& runtime = model.nodeAt({ 0, 0 })->children[1];
    ASSERT_EQ(runtime.children.size(), 3u);
    EXPECT_EQ(runtime.children[0].text, "Builds app.exe");
    EXPECT_EQ(runtime.children[1].display(), "plugin.dll   shared library");
    EXPECT_EQ(runtime.children[2].kind, ExplorerNode::Kind::Note) << "what is not read yet is said, not hidden";
}

TEST(ExplorerProducts, SourcesAreListedByNameIncludingHeaders) {
    ExplorerTreeModel model;
    model.setRoot(buildProductsTree(sampleModel()));

    const ExplorerNode& sources = model.nodeAt({ 1, 0 })->children[0].children[0];   // Libraries > core > Build Time > Sources
    ASSERT_EQ(sources.children.size(), 3u);
    EXPECT_EQ(sources.children[0].text, "a.cpp");
    EXPECT_EQ(sources.children[1].text, "a.h");
    EXPECT_EQ(sources.children[2].text, "b.cpp");
}

TEST(ExplorerProducts, AFilterFindsAProductOrOneOfItsFiles) {
    ExplorerTreeModel model;
    model.setRoot(buildProductsTree(sampleModel(), "b.cpp"));

    ASSERT_EQ(model.childCount({}), 1u);
    EXPECT_EQ(std::any_cast<std::string>(model.value(std::vector<std::size_t>{ 0 })), "Libraries   (1)");
    EXPECT_EQ(model.nodeAt({ 0, 0 })->text, "core");

    model.setRoot(buildProductsTree(sampleModel(), "nothing"));
    EXPECT_EQ(model.nodeAt({ 0 })->kind, ExplorerNode::Kind::Note);
}

TEST_F(ExplorerModelTest, ScanningCountsEachTopLevelFolderAndLeavesOutWhatTheFilesViewHides) {
    write("src/a.cpp", std::string(100, 'a'));
    write("src/deep/b.h", std::string(50, 'b'));
    write("docs/readme.md", "hello");
    write("build/out.txt", "x");
    write(".git/config", "x");
    write("top.txt", "12345");
    write("src/skip.obj", "zzzz");

    std::vector<std::string> reported;
    const FolderScan scan = scanProjectFolders(dir_.generic_string(),
        [&](std::size_t done, std::size_t total, const std::string& folder) {
            EXPECT_LE(done, total);
            reported.push_back(folder);
        });

    ASSERT_EQ(scan.folders.size(), 3u);   // build, docs, src - not .git
    EXPECT_EQ(scan.folders[0].name, "build");
    EXPECT_TRUE(scan.folders[0].skipped);
    EXPECT_EQ(scan.folders[0].files, 0u) << "named, not counted";
    EXPECT_EQ(scan.folders[2].name, "src");
    EXPECT_EQ(scan.folders[2].files, 2u);
    EXPECT_EQ(scan.folders[2].folders, 1u);
    EXPECT_EQ(scan.folders[2].bytes, 150u);
    EXPECT_EQ(scan.rootFiles, 1u);
    EXPECT_EQ(scan.rootBytes, 5u);
    EXPECT_EQ(reported.size(), 3u) << "one report per folder";
}

TEST_F(ExplorerModelTest, ScanningStopsWhenCancelled) {
    write("a/x.txt", "x");
    write("b/y.txt", "y");
    std::atomic<bool> cancel{ true };
    const FolderScan scan = scanProjectFolders(dir_.generic_string(), {}, &cancel, 1);
    for (const FolderStat& folder : scan.folders) EXPECT_EQ(folder.files, 0u);
}

TEST_F(ExplorerModelTest, FileRowsKeepTheirSizeAndShowLinesForCppAndCMakeWhateverElseTheyShow) {
    write("a.cpp", "int a;\nint b;\nint c;\n");                 // 3 lines, newline-terminated
    write("b.h", "one\ntwo");                                    // 2 lines, no final newline
    write("empty.cpp", "");
    write("notes.md", "x\ny\nz\n");                              // not a line-counted kind
    write("CMakeLists.txt", "project(x)\n");
    ExplorerNode top = buildFilesTree(dir_.generic_string(), [](const std::string& path, bool isDirectory) {
        return !isDirectory && path.find("a.cpp") != std::string::npos ? std::string("product") : std::string();
    });
    ExplorerTreeModel model;
    model.setRoot(std::move(top));
    auto find = [&](const std::string& name) -> const ExplorerNode* {   // the top-level rows are the root folder's entries
        for (std::size_t i = 0; i < model.childCount({}); ++i) {
            const ExplorerNode* node = model.nodeAt({ i });
            if (node != nullptr && node->text == name) return node;
        }
        return nullptr;
    };

    const ExplorerNode* a = find("a.cpp");
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->detail, "product") << "the product label stays beside the name";
    ASSERT_EQ(a->cells.size(), 2u);
    EXPECT_EQ(a->cells[0], "3 lines");
    EXPECT_EQ(a->cells[1], "20 B") << "and no longer replaces the size";
    EXPECT_EQ(find("b.h")->cells[0], "2 lines") << "a last line with no newline still counts";
    EXPECT_EQ(find("empty.cpp")->cells[0], "") << "nothing to count";
    EXPECT_EQ(find("CMakeLists.txt")->cells[0], "1 line");
    EXPECT_EQ(find("notes.md")->cells[0], "") << "only code and CMake get a line count";
    EXPECT_EQ(find("notes.md")->cells[1], "6 B");
}

TEST(ExplorerStats, TheInfoTreeListsFoldersLargestFirstWithTheirCounts) {
    ProjectStats stats;
    stats.scan.folders = { { "small", 2, 0, 100, false }, { "big", 1204, 30, 40 * 1024 * 1024, false }, { "build", 0, 0, 0, true } };
    stats.scan.rootFiles = 3;
    stats.scan.rootBytes = 2048;
    stats.scanMs = 120;
    stats.indexMs = 2500;
    stats.indexThreads = 7;
    stats.filesToIndex = 253;
    stats.complete = true;

    const ExplorerNode tree = buildStatsTree(stats);
    ASSERT_EQ(tree.children.size(), 4u);
    EXPECT_EQ(tree.children[0].text, "Timing");
    ASSERT_EQ(tree.children[0].children[0].cells.size(), 1u);
    EXPECT_EQ(tree.children[0].children[0].cells[0], "120 ms");
    EXPECT_EQ(tree.children[0].children[0].cellTones[0], ExplorerNode::Tone::Good) << "under a second";
    EXPECT_EQ(tree.children[0].children[3].cells[0], "2.5 s");
    EXPECT_EQ(tree.children[0].children[3].cellTones[0], ExplorerNode::Tone::Normal);
    EXPECT_EQ(tree.children[0].children[3].detail, "on 7 threads");

    const ExplorerNode& folders = tree.children[1];
    ASSERT_EQ(folders.children.size(), 4u);   // big, small, build (skipped, last), and the files in the root
    EXPECT_EQ(folders.children[0].text, "big");
    ASSERT_EQ(folders.children[0].cells.size(), 3u);
    EXPECT_EQ(folders.children[0].cells[0], "1,204 files");
    EXPECT_EQ(folders.children[0].cells[1], "40.0 MB");
    EXPECT_EQ(folders.children[0].cells[2], "30 folders");
    EXPECT_EQ(folders.children[0].cellTones[1], ExplorerNode::Tone::Bad) << "nearly all of the project's bytes";
    EXPECT_EQ(folders.children[1].cellTones[1], ExplorerNode::Tone::Normal);
    EXPECT_EQ(folders.children[1].text, "small");
    EXPECT_EQ(folders.children[2].text, "build");
    EXPECT_NE(folders.children[2].detail.find("not shown"), std::string::npos);
    EXPECT_EQ(tree.children[2].text, "Index");
    EXPECT_EQ(tree.children[3].text, "CMake");

    // unresolved includes get a row of their own, in a warning tone, with a pointer to the likely cause
    ProjectStats flags;
    flags.filesWithUnresolvedIncludes = 5;
    flags.unresolvedIncludes = 12;
    const ExplorerNode flagged = buildStatsTree(flags);
    const ExplorerNode* row = nullptr;
    for (const ExplorerNode& r : flagged.children[2].children) {
        if (r.text == "Files with unresolved includes") row = &r;
    }
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(row->cells[0], "5");
    EXPECT_EQ(row->cellTones[0], ExplorerNode::Tone::Warn);
    EXPECT_EQ(row->detail, "12 includes");
    bool hint = false;
    for (const ExplorerNode& r : flagged.children[2].children) {
        if (r.kind == ExplorerNode::Kind::Note && r.text.find("compile_commands.json") != std::string::npos) hint = true;
    }
    EXPECT_TRUE(hint) << "a note row points at the likely cause";
}

TEST(ExplorerIcons, EachKindOfRowGetsAnIconThatExistsOnDisk) {
    auto node = [](ExplorerNode::Kind kind, const std::string& text, const std::string& detail = std::string()) {
        ExplorerNode n;
        n.kind = kind;
        n.text = text;
        n.detail = detail;
        return n;
    };
    using Kind = ExplorerNode::Kind;
    EXPECT_EQ(explorerIconFor(node(Kind::File, "Parser.H")).name, "files-actions/header");
    EXPECT_EQ(explorerIconFor(node(Kind::File, "main.cpp")).name, "files-actions/source");
    EXPECT_EQ(explorerIconFor(node(Kind::File, "CMakeLists.txt")).name, "files/cmake");
    EXPECT_EQ(explorerIconFor(node(Kind::File, "notes.md")).name, "files/markdown");
    EXPECT_EQ(explorerIconFor(node(Kind::File, "layout.newui")).name, "files/newui");
    EXPECT_EQ(explorerIconFor(node(Kind::File, "other.xyz")).name, "files/document");
    EXPECT_EQ(explorerIconFor(node(Kind::Product, "app", "executable")).name, "products/executable");
    EXPECT_EQ(explorerIconFor(node(Kind::Product, "core", "static library")).name, "products/static-library");
    EXPECT_EQ(explorerIconFor(node(Kind::Product, "dll", "shared library")).name, "products/shared-library");
    ExplorerNode test = node(Kind::Product, "core_tests", "executable");
    test.iconHint = "test";
    EXPECT_EQ(explorerIconFor(test).name, "products/test") << "the hint wins over the type";
    EXPECT_TRUE(explorerIconFor(node(Kind::Group, "Timing")).empty());
    EXPECT_TRUE(explorerIconFor(node(Kind::Note, "x")).empty());

    EXPECT_EQ(explorerIconPath(explorerIconFor(node(Kind::Class, "X")), false), "Images/icons/cpp-symbol-icons-32/light/symbols/class.svg");
    EXPECT_EQ(explorerIconPath(explorerIconFor(node(Kind::Class, "X")), true), "Images/icons/cpp-symbol-icons-32/dark/symbols/class.svg");

    // every icon it can name is a real file under Resources, in both themes
    const fs::path repo = fs::path(EXPLORER_TEST_FIXTURES).parent_path().parent_path().parent_path();   // .../unittests/fixtures/cmake_fileapi
    const fs::path resources = repo / "extension" / "NativeEditControls" / "Resources";
    std::vector<ExplorerNode> nodes;
    for (Kind kind : { Kind::Folder, Kind::Namespace, Kind::Class, Kind::Struct, Kind::Enum, Kind::Function, Kind::Field,
                       Kind::Variable, Kind::Link }) {
        nodes.push_back(node(kind, "x"));
    }
    for (const char* file : { "a.h", "a.cpp", "CMakeLists.txt", "a.md", "a.json", "a.newui", "a.png", "a.txt" }) nodes.push_back(node(Kind::File, file));
    for (const char* type : { "executable", "static library", "shared library", "module library", "object library", "interface library", "utility" }) {
        nodes.push_back(node(Kind::Product, "p", type));
    }
    for (const char* hint : { "test", "custom-command" }) {
        ExplorerNode n = node(Kind::Product, "p", "executable");
        n.iconHint = hint;
        nodes.push_back(n);
    }
    for (const ExplorerNode& n : nodes) {
        const ExplorerIcon icon = explorerIconFor(n);
        ASSERT_FALSE(icon.empty()) << n.text << " " << n.detail;
        for (bool dark : { false, true }) {
            EXPECT_TRUE(fs::exists(resources / explorerIconPath(icon, dark))) << explorerIconPath(icon, dark);
        }
    }
}

namespace {

// The painted area of a rendered icon: the right and bottom edge of the pixels with any alpha.
struct Painted { int right = -1; int bottom = -1; };

Painted paintedExtent(const BLImage& image) {
    BLImageData data{};
    const_cast<BLImage&>(image).get_data(&data);
    Painted painted;
    for (int y = 0; y < data.size.h; ++y) {
        const auto* row = static_cast<const std::uint8_t*>(data.pixel_data) + std::size_t(y) * data.stride;
        for (int x = 0; x < data.size.w; ++x) {
            if (row[x * 4 + 3] != 0) {
                painted.right = std::max(painted.right, x);
                painted.bottom = std::max(painted.bottom, y);
            }
        }
    }
    return painted;
}

}  // namespace

// newui scales an SVG's viewBox to the size asked for, unless the file declares its own width and height: then it
// renders at that size and crops to the one asked for (a 32 px icon drawn into 16 showed its top-left quarter).
TEST(ExplorerIcons, TheColoredSetDeclaresNoFixedSizeAndRastersWholeAtAnySize) {
    const fs::path repo = fs::path(EXPLORER_TEST_FIXTURES).parent_path().parent_path().parent_path();
    const fs::path set = repo / "extension/NativeEditControls/Resources/Images/icons/cpp-symbol-icons-32";
    int files = 0;
    for (const auto& entry : fs::recursive_directory_iterator(set)) {
        if (entry.path().extension() != ".svg") continue;
        ++files;
        std::ifstream in(entry.path(), std::ios::binary);
        std::string head(200, ' ');
        in.read(head.data(), 200);
        head.resize(static_cast<std::size_t>(in.gcount()));
        const std::string tag = head.substr(0, head.find('>'));
        EXPECT_EQ(tag.find(" width="), std::string::npos) << entry.path().string();
    }
    EXPECT_GT(files, 300);

    const std::string icon = (set / "light/symbols/class.svg").string();   // the shape spans 2.25 to 13.75 of 16
    BLImage at16, at32;
    ASSERT_TRUE(newui::renderSvgFile(icon, 16, 16, at16));
    ASSERT_TRUE(newui::renderSvgFile(icon, 32, 32, at32));
    const Painted p16 = paintedExtent(at16);
    const Painted p32 = paintedExtent(at32);
    EXPECT_GE(p16.right, 12);
    EXPECT_LE(p16.right, 14) << "whole, not cropped to the cell";
    EXPECT_GE(p32.right, 26);
    EXPECT_LE(p32.right, 29);
}

TEST(ExplorerIcons, ABadgeSelectsTheComposedIconWhereOneExistsAndLeavesTheBaseWhereNot) {
    auto node = [](ExplorerNode::Kind kind, const std::string& text, ExplorerNode::Badge badge) {
        ExplorerNode n;
        n.kind = kind;
        n.text = text;
        n.badge = badge;
        return n;
    };
    using Kind = ExplorerNode::Kind;
    using Badge = ExplorerNode::Badge;
    EXPECT_EQ(explorerIconFor(node(Kind::File, "a.cpp", Badge::Error)).name, "badged/source-error");
    EXPECT_EQ(explorerIconFor(node(Kind::File, "a.cpp", Badge::NotBuilt)).name, "badged/source-notbuilt");
    EXPECT_EQ(explorerIconFor(node(Kind::File, "a.h", Badge::Warning)).name, "badged/header-warning");
    EXPECT_EQ(explorerIconFor(node(Kind::File, "CMakeLists.txt", Badge::Error)).name, "badged/cmake-error");
    EXPECT_EQ(explorerIconFor(node(Kind::Folder, "src", Badge::Error)).name, "badged/folder-error");
    EXPECT_EQ(explorerIconFor(node(Kind::File, "a.h", Badge::NotBuilt)).name, "files-actions/header") << "no such composed icon: the base";
    EXPECT_EQ(explorerIconFor(node(Kind::Folder, "src", Badge::Unreferenced)).name, "folders/folder");
    EXPECT_EQ(explorerIconFor(node(Kind::Class, "X", Badge::Error)).name, "symbols/class") << "symbols have no badged status icons";

    // every composed icon it can name exists, in both themes
    const fs::path repo = fs::path(EXPLORER_TEST_FIXTURES).parent_path().parent_path().parent_path();
    const fs::path resources = repo / "extension" / "NativeEditControls" / "Resources";
    for (Kind kind : { Kind::File, Kind::Folder }) {
        for (const char* file : { "a.cpp", "a.h", "CMakeLists.txt", "x" }) {
            for (Badge badge : { Badge::Error, Badge::Warning, Badge::NotBuilt, Badge::Unreferenced }) {
                const ExplorerIcon icon = explorerIconFor(node(kind, kind == Kind::Folder ? "src" : file, badge));
                for (bool dark : { false, true }) {
                    EXPECT_TRUE(fs::exists(resources / explorerIconPath(icon, dark))) << explorerIconPath(icon, dark);
                }
            }
        }
    }
}
