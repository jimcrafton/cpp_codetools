// ProjectExplorer: the explorer's chrome (Resources/explorer.newui), its four views, the filter and what a
// double-click opens, over a folder on disk. No window: the controller's own API and the controls' delegates
// are exercised, not synthetic mouse or keyboard input. Indexing runs on the test's own thread
// (setBackground(false)) since there is no run loop to deliver a background result.

#include "../extension/NativeEditControls/ProjectExplorer.h"
#include "../extension/NativeEditControls/WorkspaceInfo.h"

#include <newui/layout.h>
#include <newui/rootview.h>

#include <gtest/gtest.h>

#include <any>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

using namespace CodeToolsVsix;
namespace fs = std::filesystem;

namespace {

struct Opened
{
    std::string path;
    std::size_t line = 0;
    int calls = 0;
};

class ProjectExplorerTest : public ::testing::Test {
protected:
    void SetUp() override {
        static int counter = 0;
        dir_ = fs::temp_directory_path() / ("project_explorer_" + std::to_string(++counter) + "_" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(dir_);
        write("a.h",
            "#pragma once\n"
            "namespace shapes {\n"
            "class Shape { public: virtual ~Shape(); virtual double area() const = 0; };\n"
            "class Circle : public Shape { public: double area() const override; };\n"
            "}\n");
        write("b.cpp", "#include \"a.h\"\nnamespace shapes { Shape::~Shape() {} double Circle::area() const { return 1.0; } }\n");
        write("docs/readme.md", "hello");

        root_ = std::make_unique<newui::RootView>(nullptr, newui::Rect(0, 0, 380, 700), "explorerTest");
        root_->setLayout(std::make_unique<newui::FlexLayout>(newui::Orientation::Vertical));
        explorer_ = std::make_unique<ProjectExplorer>(*root_);
        explorer_->setBackground(false);
        explorer_->setOpenHandler([this](const std::string& path, std::size_t line) {
            opened_.path = path;
            opened_.line = line;
            ++opened_.calls;
            return true;
        });
    }

    void TearDown() override {
        explorer_.reset();
        std::error_code ec;
        fs::remove_all(dir_, ec);
    }

    void write(const std::string& relative, const std::string& text) {
        fs::create_directories((dir_ / relative).parent_path());
        std::ofstream(dir_ / relative, std::ios::binary | std::ios::trunc) << text;
    }

    // What CMake would have written for a build under dir_/build: the small fixture reply.
    void addCMakeReply() {
        fs::create_directories(dir_ / "build");
        fs::copy(fs::path(EXPLORER_TEST_FIXTURES) / "ninja" / ".cmake", dir_ / "build" / ".cmake",
                 fs::copy_options::recursive);
    }

    std::vector<std::string> rows(const std::vector<std::size_t>& parent = {}) {
        ExplorerTreeModel* model = explorer_->model();
        std::vector<std::string> shown;
        for (std::size_t i = 0; i < model->childCount(parent); ++i) {
            std::vector<std::size_t> path = parent;
            path.push_back(i);
            shown.push_back(std::any_cast<std::string>(model->value(path)));
        }
        return shown;
    }

    bool hasRow(const std::vector<std::string>& shown, const std::string& prefix) {
        for (const std::string& row : shown) {
            if (row.compare(0, prefix.size(), prefix) == 0) return true;
        }
        return false;
    }

    fs::path dir_;
    std::unique_ptr<newui::RootView> root_;
    std::unique_ptr<ProjectExplorer> explorer_;
    Opened opened_;
};

}  // namespace

TEST_F(ProjectExplorerTest, LoadsItsChromeAndOffersTheFourViews) {
    ASSERT_TRUE(explorer_->loaded());
    ASSERT_NE(explorer_->modeControl(), nullptr);
    ASSERT_EQ(explorer_->modeControl()->segments().size(), 4u);
    EXPECT_EQ(explorer_->modeControl()->segments()[0], "Files");
    EXPECT_EQ(explorer_->modeControl()->segments()[2], "Products");
    EXPECT_NE(explorer_->filterField(), nullptr);
    EXPECT_NE(explorer_->treeView(), nullptr);
    EXPECT_EQ(explorer_->mode(), ExplorerMode::Symbols) << "the default view setting";
    EXPECT_EQ(explorer_->modeControl()->selectedIndex(), 1u);
}

TEST_F(ProjectExplorerTest, WithNothingOpenItSaysSo) {
    EXPECT_EQ(explorer_->statusText(), "No folder or solution is open.");
    EXPECT_EQ(explorer_->rootName(), "");
    const auto shown = rows();
    ASSERT_EQ(shown.size(), 1u);
    EXPECT_NE(shown[0].find("Open a folder"), std::string::npos);
}

TEST_F(ProjectExplorerTest, TheRootBarNamesTheFolderAndItsPath) {
    explorer_->setRoot(dir_.generic_string());

    EXPECT_EQ(explorer_->rootName(), dir_.filename().string());
    EXPECT_EQ(explorer_->rootPath(), dir_.string());
    EXPECT_EQ(explorer_->root(), dir_.generic_string());
}

TEST_F(ProjectExplorerTest, SymbolsComeFromTheIndexAndTheStatusSaysHowMany) {
    explorer_->setRoot(dir_.generic_string());

    const auto top = rows();
    ASSERT_EQ(top.size(), 1u);
    EXPECT_EQ(top[0], "namespace shapes");
    const auto inside = rows({ 0 });
    EXPECT_TRUE(hasRow(inside, "class Circle"));
    EXPECT_TRUE(hasRow(inside, "class Shape"));
    EXPECT_NE(explorer_->statusText().find("files,"), std::string::npos) << explorer_->statusText();
    EXPECT_NE(explorer_->statusText().find("symbols"), std::string::npos);
    EXPECT_NE(explorer_->statusText().find("No CMake build tree"), std::string::npos);
}

TEST_F(ProjectExplorerTest, TheFilesViewListsTheFolderWithoutBuildOutput) {
    write("build/junk.txt", "x");
    explorer_->setRoot(dir_.generic_string());
    explorer_->setMode(ExplorerMode::Files);

    const auto shown = rows();
    EXPECT_TRUE(hasRow(shown, "docs/"));
    EXPECT_TRUE(hasRow(shown, "a.h"));
    EXPECT_TRUE(hasRow(shown, "b.cpp"));
    EXPECT_FALSE(hasRow(shown, "build/"));
    EXPECT_FALSE(hasRow(shown, "b.cpp   not built")) << "without a CMake model nothing can be called not built";
}

TEST_F(ProjectExplorerTest, WithACMakeModelFilesNoProductBuildsAreFlagged) {
    addCMakeReply();
    explorer_->setRoot(dir_.generic_string());
    explorer_->setMode(ExplorerMode::Files);

    const auto shown = rows();
    EXPECT_TRUE(hasRow(shown, "b.cpp   not built"));
    EXPECT_FALSE(hasRow(shown, "a.h   not built")) << "a header is not judged";
    EXPECT_NE(explorer_->statusText().find("CMake: build"), std::string::npos) << explorer_->statusText();
}

TEST_F(ProjectExplorerTest, TheProductsViewShowsWhatCMakeDescribes) {
    addCMakeReply();
    explorer_->setRoot(dir_.generic_string());
    explorer_->setMode(ExplorerMode::Products);

    const auto groups = rows();
    ASSERT_EQ(groups.size(), 4u);
    EXPECT_EQ(groups[0], "Applications   (1)");
    EXPECT_EQ(groups[1], "Libraries   (1)") << "core; the 'objs' object library is an input, not a product";
    EXPECT_EQ(groups[2], "Tests   (1)");
    EXPECT_EQ(groups[3], "Tools   (1)");
}

TEST_F(ProjectExplorerTest, WithoutACMakeBuildTreeProductsExplainsWhy) {
    explorer_->setRoot(dir_.generic_string());
    explorer_->setMode(ExplorerMode::Products);

    const auto shown = rows();
    ASSERT_EQ(shown.size(), 1u);
    EXPECT_NE(shown[0].find("No CMake build folder"), std::string::npos);
}

TEST_F(ProjectExplorerTest, AConfiguredButUndescribedBuildGetsAQueryAndAnExplanation) {
    write("build/CMakeCache.txt", "# configured\n");
    explorer_->setRoot(dir_.generic_string());
    explorer_->setMode(ExplorerMode::Products);

    const auto shown = rows();
    ASSERT_EQ(shown.size(), 1u);
    EXPECT_NE(shown[0].find("configure the project once"), std::string::npos);
    EXPECT_TRUE(fs::exists(dir_ / "build" / ".cmake" / "api" / "v1" / "query" / "codemodel-v2"))
        << "so its next configure writes the reply";
}

TEST_F(ProjectExplorerTest, AnalysisSaysItIsNotAvailableYet) {
    explorer_->setRoot(dir_.generic_string());
    explorer_->setMode(ExplorerMode::Analysis);

    const auto shown = rows();
    ASSERT_EQ(shown.size(), 1u);
    EXPECT_NE(shown[0].find("not available yet"), std::string::npos);
}

TEST_F(ProjectExplorerTest, TheModeControlSwitchesTheView) {
    explorer_->setRoot(dir_.generic_string());
    ASSERT_EQ(rows()[0], "namespace shapes");

    explorer_->modeControl()->setSelectedIndex(0);   // Files
    EXPECT_EQ(explorer_->mode(), ExplorerMode::Files);
    EXPECT_TRUE(hasRow(rows(), "docs/"));

    explorer_->modeControl()->setSelectedIndex(1);
    EXPECT_EQ(explorer_->mode(), ExplorerMode::Symbols);
    EXPECT_EQ(rows()[0], "namespace shapes");
}

TEST_F(ProjectExplorerTest, TheFilterNarrowsTheCurrentView) {
    explorer_->setRoot(dir_.generic_string());

    explorer_->setFilter(L"circ");
    auto top = rows();
    ASSERT_EQ(top.size(), 1u);
    EXPECT_EQ(top[0], "namespace shapes");
    const auto inside = rows({ 0 });
    ASSERT_EQ(inside.size(), 1u);
    EXPECT_TRUE(hasRow(inside, "class Circle"));

    explorer_->setMode(ExplorerMode::Files);
    EXPECT_NE(rows()[0].find("No file name contains"), std::string::npos) << "a filter in Files searches by name";
    explorer_->setFilter(L"readme");
    const auto found = rows();
    ASSERT_EQ(found.size(), 1u);
    EXPECT_TRUE(hasRow(found, "readme.md"));

    explorer_->setFilter(L"");
    EXPECT_TRUE(hasRow(rows(), "docs/")) << "cleared: the tree again";
}

TEST_F(ProjectExplorerTest, ARowOpensItsFileAtItsLineThroughTheHandler) {
    explorer_->setRoot(dir_.generic_string());

    EXPECT_FALSE(explorer_->activate({ 0 })) << "a namespace opens nothing";
    // namespace shapes > class Circle (a.h line 4)
    ASSERT_TRUE(explorer_->activate({ 0, 0 }));
    EXPECT_EQ(opened_.calls, 1);
    EXPECT_EQ(fs::path(opened_.path).filename().string(), "a.h");
    EXPECT_EQ(opened_.line, 4u);
}

TEST_F(ProjectExplorerTest, AFolderRowOpensNothingAndAFileRowOpensTheFile) {
    explorer_->setRoot(dir_.generic_string());
    explorer_->setMode(ExplorerMode::Files);

    EXPECT_FALSE(explorer_->activate({ 0 })) << "docs/";
    EXPECT_EQ(opened_.calls, 0);
    ASSERT_TRUE(explorer_->activate({ 1 }));   // a.h
    EXPECT_EQ(fs::path(opened_.path).filename().string(), "a.h");
    EXPECT_EQ(opened_.line, 0u) << "a plain file opens without moving the caret";
}

TEST_F(ProjectExplorerTest, NoHandlerMeansNothingOpens) {
    explorer_->setOpenHandler(nullptr);
    explorer_->setRoot(dir_.generic_string());

    EXPECT_FALSE(explorer_->activate({ 0, 0 }));
}

TEST_F(ProjectExplorerTest, ChangingTheRootStartsOverAndClosingItEmptiesTheViews) {
    explorer_->setRoot(dir_.generic_string());
    ASSERT_EQ(rows()[0], "namespace shapes");

    const fs::path other = dir_ / "docs";
    explorer_->setRoot(other.generic_string());
    EXPECT_EQ(explorer_->rootName(), "docs");
    EXPECT_NE(rows()[0].find("Nothing is indexed"), std::string::npos) << "no C++ under docs";

    explorer_->setRoot("");
    EXPECT_NE(rows()[0].find("Open a folder"), std::string::npos);
    EXPECT_EQ(explorer_->rootName(), "");
}

TEST(WorkspaceInfoTest, AChangeIsAnnouncedOnceAndOnlyWhenSomethingChanged) {
    WorkspaceInfo info;
    int heard = 0;
    auto connection = info.onChanged.add([&heard](WorkspaceInfo&) {
        ++heard;
        return newui::SyncReturn::Ignored;
    });

    info.set({ "C:/proj" }, "Debug");
    info.set({ "C:/proj" }, "Debug");
    EXPECT_EQ(heard, 1);
    EXPECT_EQ(info.primaryRoot(), "C:/proj");
    EXPECT_EQ(info.configuration(), "Debug");

    info.set({ "C:/proj" }, "Release");
    EXPECT_EQ(heard, 2);
    info.set({}, "");
    EXPECT_EQ(heard, 3);
    EXPECT_TRUE(info.roots().empty());
    EXPECT_EQ(info.primaryRoot(), "");

    info.onChanged.remove(connection);
    info.set({ "D:/other" }, "");
    EXPECT_EQ(heard, 3) << "a listener that disconnected is not called";
}

TEST_F(ProjectExplorerTest, AMissingHeaderAndItsKnockOnErrorsAreOnlyAWarningOnTheFile) {
    write("broken.cpp", "#include \"does_not_exist.h\"\nMissingType value;\n");   // the second line fails because of the first
    write("really_broken.cpp", "int f() { return undeclared_name; }\n");           // an error of its own
    explorer_->setBackground(false);
    explorer_->setRoot(dir_.generic_string());
    explorer_->waitForIndexing();
    explorer_->setMode(ExplorerMode::Files);

    ExplorerTreeModel* model = explorer_->model();
    auto badgeOf = [&](const std::string& name) {
        for (std::size_t i = 0; i < model->childCount({}); ++i) {
            const ExplorerNode* node = model->nodeAt({ i });
            if (node != nullptr && node->text == name) return node->badge;
        }
        return ExplorerNode::Badge::None;
    };
    EXPECT_EQ(badgeOf("broken.cpp"), ExplorerNode::Badge::Warning) << "unresolved include: the flags, not the code";
    EXPECT_EQ(badgeOf("really_broken.cpp"), ExplorerNode::Badge::Error);
    EXPECT_EQ(badgeOf("b.cpp"), ExplorerNode::Badge::None);

    const ProjectStats& stats = explorer_->stats();
    EXPECT_EQ(stats.filesWithUnresolvedIncludes, 1u);
    EXPECT_EQ(stats.filesWithErrors, 1u) << "the knock-on error is counted with the include, not as an error";
}

TEST_F(ProjectExplorerTest, TheInfoButtonShowsHowLongThingsTookAndWhatIsWhere) {
    explorer_->setBackground(false);
    explorer_->setRoot(dir_.generic_string());
    explorer_->waitForIndexing();

    const ProjectStats& stats = explorer_->stats();
    EXPECT_TRUE(stats.complete);
    EXPECT_EQ(stats.filesToIndex, 2u);
    EXPECT_GE(stats.symbols, 3u);
    EXPECT_EQ(stats.scan.rootFiles, 2u);
    ASSERT_EQ(stats.scan.folders.size(), 1u);
    EXPECT_EQ(stats.scan.folders[0].name, "docs");
    EXPECT_GT(stats.indexThreads, 0u);

    explorer_->setMode(ExplorerMode::Files);
    EXPECT_FALSE(explorer_->infoShown());
    explorer_->setInfoShown(true);
    EXPECT_TRUE(explorer_->infoShown());
    const std::vector<std::string> groups = rows();
    ASSERT_EQ(groups.size(), 4u);
    EXPECT_TRUE(hasRow(groups, "Timing"));
    EXPECT_TRUE(hasRow(groups, "Top-level folders"));
    EXPECT_TRUE(hasRow(rows({ 1 }), "docs/"));

    explorer_->modeControl()->setSelectedIndex(1);   // picking a view leaves the info
    EXPECT_FALSE(explorer_->infoShown());
    EXPECT_FALSE(hasRow(rows(), "Timing"));
}
