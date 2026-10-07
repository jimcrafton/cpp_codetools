// ProjectExplorer: the explorer's chrome (Resources/explorer.newui), its four views, the filter and what a
// double-click opens, over a folder on disk. No window: the controller's own API and the controls' delegates
// are exercised, not synthetic mouse or keyboard input. Indexing runs on the test's own thread
// (setBackground(false)) since there is no run loop to deliver a background result.

#include "../extension/NativeEditControls/CppDiagnostics.h"
#include "../extension/NativeEditControls/ProjectExplorer.h"
#include "../extension/NativeEditControls/WorkspaceInfo.h"

#include <cmakemodel/fileapi.h>
#include <cmakemodel/model.h>
#include <cpptools/compileflags.h>
#include <cpptools/projectindex.h>

#include <newui/layout.h>
#include <newui/rootview.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <any>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <string>

using namespace CodeToolsVsix;
namespace fs = std::filesystem;

namespace {

// Every explorer in these tests would otherwise save its index under the real %LOCALAPPDATA%, one file per temp folder.
class PrivateCacheFolder : public ::testing::Environment {
public:
    void SetUp() override {
        folder_ = fs::temp_directory_path() / ("explorer_test_cache_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ProjectExplorer::setCacheFolder(folder_.string());
    }
    void TearDown() override {
        ProjectExplorer::setCacheFolder(std::string());
        std::error_code ec;
        fs::remove_all(folder_, ec);
    }

private:
    fs::path folder_;
};

const ::testing::Environment* const privateCacheFolder = ::testing::AddGlobalTestEnvironment(new PrivateCacheFolder);

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

TEST_F(ProjectExplorerTest, AnalysisIncludesRanksHeadersByWhatTheyRebuildAndHintsAtUnneededIncludes) {
    write("unused.h", "#pragma once\ninline int unusedFn() { return 0; }\n");
    write("user.cpp", "#include \"a.h\"\n#include \"unused.h\"\nint main() { shapes::Circle c; (void)c; return 0; }\n");
    explorer_->setBackground(false);
    explorer_->setRoot(dir_.generic_string());
    explorer_->waitForIndexing();
    explorer_->setMode(ExplorerMode::Analysis);

    const auto groups = rows();
    ASSERT_EQ(groups.size(), 2u);
    EXPECT_TRUE(hasRow(groups, "Most expensive headers"));
    EXPECT_TRUE(hasRow(groups, "Includes that may be unneeded"));
    const auto headers = rows({ 0 });
    ASSERT_GE(headers.size(), 2u);
    EXPECT_TRUE(hasRow({ headers[0] }, "a.h")) << "included by b.cpp and user.cpp: first";
    EXPECT_EQ(headers[0], "a.h   2") << "the translation units it reaches: b.cpp and user.cpp";
    const auto includers = rows({ 0, 0 });
    EXPECT_EQ(includers.size(), 2u);
    const auto hints = rows({ 1 });
    ASSERT_EQ(hints.size(), 1u);
    EXPECT_NE(hints[0].find("user.cpp"), std::string::npos);
    EXPECT_NE(hints[0].find("unused.h"), std::string::npos);
}

TEST_F(ProjectExplorerTest, AnalysisIncludesPerFileOpensOntoWhatAFileIncludes) {
    explorer_->setBackground(false);
    explorer_->setRoot(dir_.generic_string());
    explorer_->waitForIndexing();
    explorer_->setMode(ExplorerMode::Analysis);
    explorer_->setIncludeView(IncludeView::PerFile);

    const auto files = rows();
    ASSERT_TRUE(hasRow(files, "b.cpp"));
    std::size_t at = 0;
    while (files[at].compare(0, 5, "b.cpp") != 0) ++at;
    const auto includes = rows({ at });
    ASSERT_EQ(includes.size(), 1u);
    EXPECT_EQ(includes[0].compare(0, 3, "a.h"), 0) << includes[0];

    explorer_->setFilter(L"a.h");
    EXPECT_EQ(rows().size(), 1u) << "only the file whose path matches";
}

TEST_F(ProjectExplorerTest, AnalysisMacrosListsTheMacrosOfTheActiveFileFirst) {
    write("macros.cpp",
        "#define MIN(a, b) ((a) < (b) ? (a) : (b))\n"
        "#define MAX(a, b) ((a) > (b) ? (a) : (b))\n"
        "#define CLAMP(v, lo, hi) MAX((lo), MIN((v), (hi)))\n"
        "#define FEATURE 0\n"
        "int i = 0, j = 1;\n"
        "int w = CLAMP(i, 0, 9);\n"
        "int m = MAX(i++, j);\n"
        "#if FEATURE\n"
        "int off;\n"
        "#endif\n");
    explorer_->setBackground(false);
    explorer_->setRoot(dir_.generic_string());
    explorer_->waitForIndexing();
    explorer_->setMode(ExplorerMode::Analysis);
    explorer_->setAnalysisTab(AnalysisTab::Macros);
    explorer_->setActiveFile((dir_ / "macros.cpp").generic_string());

    const auto groups = rows();
    ASSERT_EQ(groups.size(), 3u) << "the macros, the inactive regions, the arguments evaluated twice";
    EXPECT_TRUE(hasRow(groups, "Macros used in macros.cpp"));
    EXPECT_TRUE(hasRow(groups, "Inactive regions"));
    EXPECT_TRUE(hasRow(groups, "Evaluated more than once"));

    const auto macros = rows({ 0 });
    ASSERT_GE(macros.size(), 2u);
    EXPECT_NE(macros[0].find("CLAMP"), std::string::npos) << "first met: " << macros[0];
    EXPECT_NE(macros[0].find("1 use · 3 levels"), std::string::npos) << macros[0];
    EXPECT_NE(macros[1].find("MAX"), std::string::npos) << macros[1];
    const auto dependencies = rows({ 0, 0, 0 });
    ASSERT_EQ(dependencies.size(), 2u) << "under the CLAMP use: the macros its body brought in";
    EXPECT_EQ(dependencies[0].compare(0, 3, "MAX"), 0) << dependencies[0];
    EXPECT_EQ(dependencies[1].compare(0, 3, "MIN"), 0) << dependencies[1];
    EXPECT_EQ(explorer_->model()->nodeAt({ 0, 0, 0, 1 })->line, 1u) << "MIN opens its #define";
    EXPECT_TRUE(explorer_->model()->nodeAt({ 0, 0, 0, 1 })->openOnSelect);
    const auto uses = rows({ 0, 1 });
    ASSERT_EQ(uses.size(), 1u);
    EXPECT_NE(uses[0].find("MAX(i++, j)"), std::string::npos) << uses[0];

    ExplorerTreeModel* model = explorer_->model();
    EXPECT_EQ(model->nodeAt({ 0, 1 })->line, 2u) << "a macro opens its #define";
    EXPECT_TRUE(model->nodeAt({ 0, 1 })->openOnSelect);
    EXPECT_EQ(explorerIconFor(*model->nodeAt({ 0, 1 })).name, "symbols/macro");
    EXPECT_EQ(model->nodeAt({ 0, 1, 0 })->line, 7u) << "a use opens its file at its line";
    EXPECT_TRUE(model->nodeAt({ 0, 1, 0 })->openOnSelect) << "one click, like the rest";
    EXPECT_TRUE(model->nodeAt({ 1, 0 })->openOnSelect) << "an inactive region";
    EXPECT_EQ(model->nodeAt({ 0, 1, 0 })->path, (dir_ / "macros.cpp").generic_string());
    EXPECT_NE(rows({ 1 })[0].find("#if FEATURE"), std::string::npos);
}

TEST_F(ProjectExplorerTest, SelectingAUseShowsWhatItBecameTintedByMacroAndOpensItsMacros) {
    write("macros.cpp",
        "#define MIN(a, b) ((a) < (b) ? (a) : (b))\n"
        "#define MAX(a, b) ((a) > (b) ? (a) : (b))\n"
        "#define CLAMP(v, lo, hi) MAX((lo), MIN((v), (hi)))\n"
        "int w = CLAMP(width, 0, 9);\n");
    explorer_->setBackground(false);
    explorer_->setRoot(dir_.generic_string());
    explorer_->waitForIndexing();
    explorer_->setMode(ExplorerMode::Analysis);
    explorer_->setAnalysisTab(AnalysisTab::Macros);
    explorer_->setActiveFile((dir_ / "macros.cpp").generic_string());

    EXPECT_FALSE(explorer_->cardHost()->isVisible()) << "nothing selected";
    explorer_->treeView()->setSelectedPath(std::vector<std::size_t>{ 0, 0, 0 });   // the CLAMP use
    EXPECT_TRUE(explorer_->cardHost()->isVisible());
    EXPECT_FALSE(explorer_->cardStepsControl()->isVisible());
    const std::wstring text = explorer_->cardText()->model().text();
    EXPECT_NE(text.find(L"width"), std::wstring::npos);
    EXPECT_EQ(text.find(L"MIN"), std::wstring::npos) << "fully expanded";
    EXPECT_FALSE(explorer_->cardText()->styledRanges().empty()) << "parts tinted by the macro that wrote them";
    EXPECT_TRUE(explorer_->treeView()->controller().isExpanded({ 0, 0, 0 })) << "its macros open";

    explorer_->treeView()->clearSelection();
    EXPECT_FALSE(explorer_->cardHost()->isVisible());
}

TEST_F(ProjectExplorerTest, AnalysisMacrosCanCoverTheWholeFolder) {
    write("one.cpp", "#define ONE 1\nint a = ONE;\n");
    write("two.cpp", "#define TWO 2\nint b = TWO; int c = TWO;\n");
    write("docs/other.cpp", "#define ELSEWHERE 3\nint d = ELSEWHERE;\n");
    explorer_->setBackground(false);
    explorer_->setRoot(dir_.generic_string());
    explorer_->waitForIndexing();
    explorer_->setMode(ExplorerMode::Analysis);
    explorer_->setAnalysisTab(AnalysisTab::Macros);
    explorer_->setActiveFile((dir_ / "one.cpp").generic_string());
    EXPECT_TRUE(hasRow(rows(), "Macros used in one.cpp"));

    explorer_->setMacroScope(MacroScope::Folder);
    const auto macros = rows({ 0 });
    EXPECT_TRUE(hasRow(rows(), "Macros used in this folder"));
    ASSERT_EQ(macros.size(), 2u) << "ONE and TWO; ELSEWHERE is in another folder";
    EXPECT_EQ(macros[0].compare(0, 3, "TWO"), 0) << "two uses first";
    EXPECT_NE(rows({ 0, 0 })[0].find("two.cpp:"), std::string::npos) << "uses name their file";
}

TEST_F(ProjectExplorerTest, AnalysisMacrosWithNoActiveFileAsksForOne) {
    explorer_->setBackground(false);
    explorer_->setRoot(dir_.generic_string());
    explorer_->waitForIndexing();
    explorer_->setMode(ExplorerMode::Analysis);
    explorer_->setAnalysisTab(AnalysisTab::Macros);
    ASSERT_EQ(rows().size(), 1u);
    EXPECT_NE(rows()[0].find("Open a C++ file"), std::string::npos);

    explorer_->setActiveFile((dir_ / "b.cpp").generic_string());
    EXPECT_NE(rows({ 0 })[0].find("No macros"), std::string::npos);
}

TEST_F(ProjectExplorerTest, AnalysisTemplatesSaysWhatItNeeds) {
    explorer_->setRoot(dir_.generic_string());
    explorer_->setMode(ExplorerMode::Analysis);
    explorer_->setAnalysisTab(AnalysisTab::Templates);
    ASSERT_EQ(rows().size(), 1u);
    EXPECT_NE(rows()[0].find("Clang AST library"), std::string::npos);
}

TEST_F(ProjectExplorerTest, TheAnalysisSubTabsShowOnlyWhileAnalysisIsTheMode) {
    EXPECT_FALSE(explorer_->analysisControl()->parent()->isVisible());
    explorer_->setMode(ExplorerMode::Analysis);
    EXPECT_TRUE(explorer_->analysisControl()->parent()->isVisible());
    EXPECT_TRUE(explorer_->includeViewControl()->parent()->isVisible()) << "Includes is the first sub-tab";
    explorer_->setAnalysisTab(AnalysisTab::Macros);
    EXPECT_FALSE(explorer_->includeViewControl()->parent()->isVisible());
    explorer_->setMode(ExplorerMode::Symbols);
    EXPECT_FALSE(explorer_->analysisControl()->parent()->isVisible());
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

namespace {

bool hasArg(const cpptools::CompileFlags& flags, const std::string& arg) {
    return std::find(flags.args.begin(), flags.args.end(), arg) != flags.args.end();
}

std::shared_ptr<const cmakemodel::CompileSettingsIndex> fixtureSettings() {
    static const cmakemodel::LoadResult loaded = cmakemodel::loadFileApi(std::string(EXPLORER_TEST_FIXTURES) + "/ninja", "");
    return loaded.ok() ? std::make_shared<const cmakemodel::CompileSettingsIndex>(loaded.model) : nullptr;
}

}  // namespace

TEST(WorkspaceInfoTest, TheBuildsOwnIncludesAndDefinitionsAreAddedToAFilesFlags) {
    auto settings = fixtureSettings();
    ASSERT_NE(settings, nullptr);

    WorkspaceInfo info;
    const std::string file = "C:/proj/tools/main.cpp";   // the fixture's `app`: -IC:/proj/include, WIN32, UNICODE
    EXPECT_EQ(info.compileSettingsVersion(), 0u);
    EXPECT_FALSE(hasArg(info.compileFlagsFor(file), "-DWIN32")) << "nothing known yet";

    info.setCompileSettings(settings);
    EXPECT_EQ(info.compileSettingsVersion(), 1u);
    const cpptools::CompileFlags flags = info.compileFlagsFor(file);
    EXPECT_TRUE(hasArg(flags, "-DWIN32"));
    EXPECT_TRUE(hasArg(flags, "-DUNICODE"));
    EXPECT_TRUE(hasArg(flags, "-IC:/proj/include"));
    EXPECT_NE(flags.origin.find("CMake target app"), std::string::npos) << flags.origin;
    EXPECT_FALSE(hasArg(info.compileFlagsFor("C:/elsewhere/x.cpp"), "-DWIN32")) << "a file no target claims is left as it was";

    info.setCompileSettings(nullptr);
    EXPECT_EQ(info.compileSettingsVersion(), 2u);
    EXPECT_FALSE(hasArg(info.compileFlagsFor(file), "-DWIN32"));
}

TEST(WorkspaceInfoTest, AnEditorsCachedFlagsAreLookedUpAgainWhenTheBuildSettingsChange) {
    auto settings = fixtureSettings();
    ASSERT_NE(settings, nullptr);

    auto document = std::make_shared<CppDocument>();
    document->setPath("C:/proj/tools/main.cpp");
    EXPECT_FALSE(hasArg(document->flags(), "-DWIN32"));

    WorkspaceInfo::instance().setCompileSettings(settings);   // the explorer finished reading CMake
    EXPECT_TRUE(hasArg(document->flags(), "-DWIN32")) << "the cached flags noticed";
    EXPECT_NE(document->flags().origin.find("CMake target app"), std::string::npos);

    WorkspaceInfo::instance().setCompileSettings(nullptr);   // leave the shared instance as found
    EXPECT_FALSE(hasArg(document->flags(), "-DWIN32"));
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

TEST_F(ProjectExplorerTest, AHeaderNothingIncludesIsMarkedUnreferenced) {
    write("orphan.h", "int orphan();\n");
    write("used.h", "int used();\n");
    write("user_of_used.cpp", "#include \"used.h\"\nint g() { return used(); }\n");
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
    EXPECT_EQ(badgeOf("orphan.h"), ExplorerNode::Badge::Unreferenced);
    EXPECT_EQ(badgeOf("used.h"), ExplorerNode::Badge::None);
}

namespace {

// A CMake reply for a build under dir/build whose source dir is dir itself: core (b.cpp, in the root list) and
// app (tools/main.cpp plus the extra source, in tools/CMakeLists.txt).
void writeReplyFor(const fs::path& dir, const std::string& appExtraSource) {
    const fs::path reply = dir / "build" / ".cmake" / "api" / "v1" / "reply";
    fs::create_directories(reply);
    const std::string root = dir.generic_string();
    auto put = [&](const std::string& name, const std::string& text) { std::ofstream(reply / name, std::ios::binary) << text; };
    put("index-2026-10-04T00-00-00-0000.json",
        R"({"cmake":{"version":{"string":"4.3.1"},"generator":{"multiConfig":false,"name":"Ninja"}},)"
        R"("objects":[{"kind":"codemodel","version":{"major":2,"minor":10},"jsonFile":"codemodel-v2-1.json"}],"reply":{}})");
    put("codemodel-v2-1.json",
        R"({"kind":"codemodel","version":{"major":2,"minor":10},"paths":{"source":")" + root + R"(","build":")" + root + R"(/build"},)"
        R"("configurations":[{"name":"Debug","directories":[{"source":".","build":"."},{"source":"tools","build":"tools"}],)"
        R"("projects":[{"name":"proj","directoryIndexes":[0,1]}],"targets":[)"
        R"({"name":"app","id":"app::@1","directoryIndex":1,"projectIndex":0,"jsonFile":"target-app-Debug.json"},)"
        R"({"name":"core","id":"core::@1","directoryIndex":0,"projectIndex":0,"jsonFile":"target-core-Debug.json"}]}]})");
    put("target-app-Debug.json",
        R"({"name":"app","id":"app::@1","type":"EXECUTABLE","nameOnDisk":"app.exe","artifacts":[{"path":"tools/app.exe"}],"backtrace":1,)"
        R"("backtraceGraph":{"files":["tools/CMakeLists.txt"],"commands":["add_executable"],"nodes":[{"file":0},{"command":0,"file":0,"line":2,"parent":0}]},)"
        R"("sources":[{"path":"tools/main.cpp","compileGroupIndex":0,"sourceGroupIndex":0,"backtrace":1},)"
        R"({"path":"tools/)" + appExtraSource + R"(","compileGroupIndex":0,"sourceGroupIndex":0,"backtrace":1}],)"
        R"("sourceGroups":[{"name":"Source Files","sourceIndexes":[0,1]}],)"
        R"("compileGroups":[{"language":"CXX","sourceIndexes":[0,1]}]})");
    put("target-core-Debug.json",
        R"({"name":"core","id":"core::@1","type":"STATIC_LIBRARY","nameOnDisk":"core.lib","artifacts":[{"path":"core.lib"}],"backtrace":1,)"
        R"("backtraceGraph":{"files":["CMakeLists.txt"],"commands":["add_library"],"nodes":[{"file":0},{"command":0,"file":0,"line":2,"parent":0}]},)"
        R"("sources":[{"path":"b.cpp","compileGroupIndex":0,"sourceGroupIndex":0,"backtrace":1}],)"
        R"("sourceGroups":[{"name":"Source Files","sourceIndexes":[0]}],)"
        R"("compileGroups":[{"language":"CXX","sourceIndexes":[0]}]})");
}

}  // namespace

TEST_F(ProjectExplorerTest, ACMakeListsWhoseTargetNamesAMissingSourceIsMarkedAndSoIsItsFolder) {
    write("CMakeLists.txt", "add_library(core b.cpp)\nadd_subdirectory(tools)\n");
    write("tools/CMakeLists.txt", "add_executable(app main.cpp ghost.cpp)\n");
    write("tools/main.cpp", "int main() { return 0; }\n");   // ghost.cpp is never written
    writeReplyFor(dir_, "ghost.cpp");
    explorer_->setBackground(false);
    explorer_->setRoot(dir_.generic_string());
    explorer_->waitForIndexing();
    explorer_->setMode(ExplorerMode::Files);

    ExplorerTreeModel* model = explorer_->model();
    auto badgeIn = [&](const std::vector<std::size_t>& parent, const std::string& prefix) {
        for (std::size_t i = 0; i < model->childCount(parent); ++i) {
            std::vector<std::size_t> path = parent;
            path.push_back(i);
            const ExplorerNode* node = model->nodeAt(path);
            if (node != nullptr && node->text.compare(0, prefix.size(), prefix) == 0) return std::make_pair(true, node->badge);
        }
        return std::make_pair(false, ExplorerNode::Badge::None);
    };
    auto indexOf = [&](const std::string& prefix) -> std::size_t {
        for (std::size_t i = 0; i < model->childCount({}); ++i) {
            const ExplorerNode* node = model->nodeAt({ i });
            if (node != nullptr && node->text.compare(0, prefix.size(), prefix) == 0) return i;
        }
        return std::size_t(-1);
    };

    const std::size_t tools = indexOf("tools");
    ASSERT_NE(tools, std::size_t(-1));
    EXPECT_EQ(model->nodeAt({ tools })->badge, ExplorerNode::Badge::Error) << "the folder holding the broken list";
    const auto toolsList = badgeIn({ tools }, "CMakeLists.txt");
    ASSERT_TRUE(toolsList.first);
    EXPECT_EQ(toolsList.second, ExplorerNode::Badge::Error);
    EXPECT_EQ(badgeIn({ tools }, "main.cpp").second, ExplorerNode::Badge::None) << "the source that exists is fine";
    EXPECT_EQ(badgeIn({}, "CMakeLists.txt").second, ExplorerNode::Badge::None) << "core's sources are all there";
}

TEST_F(ProjectExplorerTest, ACMakeListsWhoseSourcesAllExistIsNotMarked) {
    write("CMakeLists.txt", "add_library(core b.cpp)\nadd_subdirectory(tools)\n");
    write("tools/CMakeLists.txt", "add_executable(app main.cpp extra.cpp)\n");
    write("tools/main.cpp", "int main() { return 0; }\n");
    write("tools/extra.cpp", "int extra() { return 0; }\n");
    writeReplyFor(dir_, "extra.cpp");
    explorer_->setBackground(false);
    explorer_->setRoot(dir_.generic_string());
    explorer_->waitForIndexing();
    explorer_->setMode(ExplorerMode::Files);

    ExplorerTreeModel* model = explorer_->model();
    for (std::size_t i = 0; i < model->childCount({}); ++i) {
        const ExplorerNode* node = model->nodeAt({ i });
        ASSERT_NE(node, nullptr);
        EXPECT_NE(node->badge, ExplorerNode::Badge::Error) << node->text;
    }
}

// Run by name (--gtest_also_run_disabled_tests): this repo's own files that reported errors, indexed with the flags the
// explorer now builds from the real build/ tree. Prints to stdout.
TEST(ProjectExplorerRepo, DISABLED_TheRepoFilesThatNeededTheBuildsFlags) {
    const fs::path repo = fs::path(EXPLORER_TEST_FIXTURES).parent_path().parent_path().parent_path();
    const cmakemodel::LoadResult loaded = cmakemodel::loadFileApi((repo / "build").generic_string(), "Debug");
    if (!loaded.ok()) GTEST_SKIP() << loaded.error;
    auto settings = std::make_shared<cmakemodel::CompileSettingsIndex>(loaded.model);

    auto provider = [settings](const std::string& file) {
        std::vector<std::string> args = cpptools::compileFlagsFor(file).args;
        if (const cmakemodel::CompileSettings* found = settings->find(file)) {
            for (std::string& extra : found->toArgs()) args.push_back(std::move(extra));
        }
        return args;
    };
    const std::vector<std::string> names = {
        "unittests/test_explorer_model.cpp", "unittests/test_cmakemodel.cpp", "unittests/test_projectindex.cpp",
        "unittests/lex/cmake_lexer_tests.cpp", "unittests/test_delegatebindings.cpp", "unittests/test_editor_file_kinds.cpp",
        "src/codegen/methodinsertion.cpp", "src/codegen/accessmerge.cpp", "extension/NativeEditControls/NativeEditor.cpp",
        "include/cpptools/diagnostic.h", "include/cpptools_codegen/classbuilder.h", "src/cmakemodel/model.cpp" };

    for (const bool withBuildFlags : { false, true }) {
        cpptools::ProjectIndex index;
        index.setRoots({ repo.generic_string() });
        if (withBuildFlags) index.setFlagsProvider(provider);
        std::vector<std::string> files;
        for (const std::string& name : names) files.push_back((repo / name).generic_string());
        index.indexFiles(files, {}, 4);
        std::printf("--- %s\n", withBuildFlags ? "with the build's flags added" : "compile_commands.json only");
        for (const std::string& name : names) {
            const cpptools::ProjectIndex::Problems p = index.problemsIn((repo / name).generic_string());
            std::printf("  %-50s errors %u  warnings %u  unresolved %u%s\n", name.c_str(), p.errors, p.warnings, p.unresolvedIncludes,
                        p.samples.empty() ? "" : ("   first: " + p.samples[0].message).c_str());
        }
    }
}

TEST_F(ProjectExplorerTest, ACountInTheInfoPanelOpensOntoTheFilesBehindItAndTheyOpen) {
    write("really_broken.cpp", "int f() { return undeclared_name; }\n");
    explorer_->setBackground(false);
    explorer_->setRoot(dir_.generic_string());
    explorer_->waitForIndexing();
    explorer_->setInfoShown(true);

    ExplorerTreeModel* model = explorer_->model();
    ASSERT_GE(model->childCount({}), 3u);   // Timing, Top-level folders, Index, CMake
    std::size_t errorRow = model->childCount({ 2 });
    for (std::size_t i = 0; i < model->childCount({ 2 }); ++i) {
        const ExplorerNode* row = model->nodeAt({ 2, i });
        if (row != nullptr && row->text == "Files with errors") errorRow = i;
    }
    ASSERT_LT(errorRow, model->childCount({ 2 }));
    ASSERT_EQ(model->childCount({ 2, errorRow }), 1u);
    const ExplorerNode* file = model->nodeAt({ 2, errorRow, 0 });
    ASSERT_NE(file, nullptr);
    EXPECT_EQ(file->text, "really_broken.cpp");

    EXPECT_TRUE(explorer_->activate({ 2, errorRow, 0 })) << "a double-click on it opens the file";
    EXPECT_NE(opened_.path.find("really_broken.cpp"), std::string::npos);
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

namespace {
    newui::FileWatcher::Change change(newui::FileWatcher::Action action, const std::string& path) {
        newui::FileWatcher::Change result;
        result.action = action;
        result.path = path;
        return result;
    }
}

TEST(ProjectExplorerDiskChanges, EditingASourceOrHeaderIsAModification) {
    using Action = newui::FileWatcher::Action;
    const ProjectExplorer::DiskChanges summary = ProjectExplorer::summarizeChanges(
        { change(Action::Modified, "C:/p/a.cpp"), change(Action::Modified, "C:/p/inc/b.H") });

    EXPECT_FALSE(summary.full);
    EXPECT_EQ(summary.modified, (std::set<std::string>{ "C:/p/a.cpp", "C:/p/inc/b.H" }));
}

TEST(ProjectExplorerDiskChanges, AFileComingGoingOrMovingNeedsAFullRefresh) {
    using Action = newui::FileWatcher::Action;
    EXPECT_TRUE(ProjectExplorer::summarizeChanges({ change(Action::Added, "C:/p/new.cpp") }).full);
    EXPECT_TRUE(ProjectExplorer::summarizeChanges({ change(Action::Removed, "C:/p/readme.md") }).full);
    EXPECT_TRUE(ProjectExplorer::summarizeChanges({ change(Action::Renamed, "C:/p/b.cpp") }).full);
    EXPECT_TRUE(ProjectExplorer::summarizeChanges({ change(Action::Overflow, "C:/p") }).full);
}

TEST(ProjectExplorerDiskChanges, EditingCMakeInputsNeedsAFullRefresh) {
    using Action = newui::FileWatcher::Action;
    const ProjectExplorer::DiskChanges lists = ProjectExplorer::summarizeChanges({ change(Action::Modified, "C:/p/app/CMakeLists.txt") });
    EXPECT_TRUE(lists.full);
    EXPECT_TRUE(lists.modified.empty());
    EXPECT_TRUE(ProjectExplorer::summarizeChanges({ change(Action::Modified, "C:/p/cmake/Tools.cmake") }).full);
}

TEST(ProjectExplorerDiskChanges, EditingAFileTheExplorerDoesNotShowIsNothing) {
    using Action = newui::FileWatcher::Action;
    EXPECT_FALSE(ProjectExplorer::summarizeChanges({ change(Action::Modified, "C:/p/docs/readme.md"),
                                                     change(Action::Modified, "C:/p/docs") }).any());
}

TEST(ProjectExplorerWatchIgnores, DotBuildAndDependencyFoldersAreIgnored) {
    EXPECT_TRUE(ProjectExplorer::watchIgnores("C:/p", "C:/p/.git/index"));
    EXPECT_TRUE(ProjectExplorer::watchIgnores("C:/p", "C:/p/build/CMakeCache.txt"));
    EXPECT_TRUE(ProjectExplorer::watchIgnores("C:/p", "C:/p/build-ninja/x/y.obj"));
    EXPECT_TRUE(ProjectExplorer::watchIgnores("C:/p", "C:/p/3rdparty/newui/a.h"));
    EXPECT_TRUE(ProjectExplorer::watchIgnores("C:/p", "C:/p/src/out"));   // a skipped folder itself
}

TEST(ProjectExplorerWatchIgnores, BuildOutputAndTempFilesAreIgnored) {
    EXPECT_TRUE(ProjectExplorer::watchIgnores("C:/p", "C:/p/src/a.obj"));
    EXPECT_TRUE(ProjectExplorer::watchIgnores("C:/p", "C:/p/src/a.cpp.tmp"));
}

TEST(ProjectExplorerWatchIgnores, ProjectFilesAreNot) {
    EXPECT_FALSE(ProjectExplorer::watchIgnores("C:/p", "C:/p/src/a.cpp"));
    EXPECT_FALSE(ProjectExplorer::watchIgnores("C:\\p\\", "C:/p/CMakeLists.txt"));   // a root spelled with backslashes
    EXPECT_FALSE(ProjectExplorer::watchIgnores("C:/p", "C:/p/docs/readme.md"));
}

TEST_F(ProjectExplorerTest, ARebuildKeepsTheOpenFoldersAndTheSelectionEvenWhenRowsShift) {
    write("docs/guide.md", "g");
    explorer_->setRoot(dir_.generic_string());
    explorer_->setMode(ExplorerMode::Files);
    ASSERT_TRUE(hasRow(rows(), "docs/"));
    newui::TreeController& controller = explorer_->treeView()->controller();
    controller.setExpanded({ 0 }, true);   // docs, the only folder
    ASSERT_FALSE(rows({ 0 }).empty());
    explorer_->treeView()->setSelectedPath(std::vector<std::size_t>{ 0, 0 });

    write("adir/x.txt", "x");   // a new folder sorts before docs, so docs moves down a row
    explorer_->setMode(ExplorerMode::Files);   // the same view again: a rebuild

    EXPECT_TRUE(hasRow(rows(), "adir/"));
    EXPECT_FALSE(controller.isExpanded({ 0 })) << "adir was never opened";
    EXPECT_TRUE(controller.isExpanded({ 1 })) << "docs is still open, one row lower";
    ASSERT_TRUE(explorer_->treeView()->selectedPath().has_value());
    EXPECT_EQ(*explorer_->treeView()->selectedPath(), (std::vector<std::size_t>{ 1, 0 }));
}

TEST_F(ProjectExplorerTest, SwitchingViewsStartsWithEverythingClosed) {
    write("docs/guide.md", "g");
    explorer_->setRoot(dir_.generic_string());
    explorer_->setMode(ExplorerMode::Files);
    explorer_->treeView()->controller().setExpanded({ 0 }, true);

    explorer_->setMode(ExplorerMode::Symbols);
    explorer_->setMode(ExplorerMode::Files);

    EXPECT_FALSE(explorer_->treeView()->controller().isExpanded({ 0 }));
}

TEST(ProjectExplorerReply, TheNewestIndexIsFoundByItsStampedName) {
    const fs::path dir = fs::temp_directory_path() / ("reply_" + std::to_string(::GetCurrentProcessId()));
    fs::create_directories(dir / "reply");
    for (const char* name : { "index-2026-10-01T10-00-00-0000.json", "index-2026-10-06T09-30-00-0000.json",
                              "codemodel-v2-abc.json", "index-2026-10-03T08-00-00-0000.json" }) {
        std::ofstream(dir / "reply" / name) << "{}";
    }

    EXPECT_EQ(ProjectExplorer::latestReplyIndex(dir.generic_string()), "index-2026-10-06T09-30-00-0000.json");
    EXPECT_EQ(ProjectExplorer::latestReplyIndex((dir / "nope").generic_string()), "");

    std::error_code ec;
    fs::remove_all(dir, ec);
}

// A developer's profile of a real folder, skipped unless EXPLORER_PROFILE_ROOT names one: loads it the way the
// explorer does (inline, so every UI-thread step shows up in the "took N ms on the UI thread" log lines) and
// then times a rebuild of each view.
TEST(ProjectExplorerProfile, ARealFolder) {
    const char* root = std::getenv("EXPLORER_PROFILE_ROOT");
    if (root == nullptr) GTEST_SKIP() << "set EXPLORER_PROFILE_ROOT to a folder to profile";
    newui::RootView view(nullptr, newui::Rect(0, 0, 380, 700), "profile");
    view.setLayout(std::make_unique<newui::FlexLayout>(newui::Orientation::Vertical));
    ProjectExplorer explorer(view);
    explorer.setBackground(false);
    using Clock = std::chrono::steady_clock;
    auto ms = [](Clock::time_point since) { return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - since).count(); };
    explorer.setMode(ExplorerMode::Files);
    const auto start = Clock::now();
    explorer.setRoot(root);
    std::printf("PROFILE total load (job + UI steps, inline): %lld ms\n", static_cast<long long>(ms(start)));
    const ProjectStats& stats = explorer.stats();
    std::printf("PROFILE files %zu, parsed %zu, up to date %zu, failed %zu; cache load %.0f ms, index %.0f ms\n",
                stats.filesToIndex, stats.parsed, stats.upToDate, stats.failed, stats.cacheLoadMs, stats.indexMs);
    for (ExplorerMode mode : { ExplorerMode::Files, ExplorerMode::Symbols, ExplorerMode::Products, ExplorerMode::Analysis,
                               ExplorerMode::Files, ExplorerMode::Symbols, ExplorerMode::Products, ExplorerMode::Analysis }) {
        const auto began = Clock::now();
        explorer.setMode(mode);
        std::printf("PROFILE rebuild mode %d: %lld ms\n", static_cast<int>(mode), static_cast<long long>(ms(began)));
    }
}

TEST_F(ProjectExplorerTest, ClickingAModeRebuildsTheTreeOnce) {
    explorer_->setRoot(dir_.generic_string());
    explorer_->setMode(ExplorerMode::Files);
    int rebuilds = 0;
    explorer_->model()->onChanged.add([&rebuilds](newui::Model&) {
        ++rebuilds;
        return newui::SyncReturn::Ignored;
    });

    explorer_->modeControl()->setSelectedIndex(1);   // what a click does

    EXPECT_EQ(rebuilds, 1);
}

TEST_F(ProjectExplorerTest, ARevisitedViewShowsTheSameTreeAndANewIndexShowsWhatChanged) {
    explorer_->setRoot(dir_.generic_string());
    explorer_->setMode(ExplorerMode::Symbols);
    const std::vector<std::string> first = rows();
    ASSERT_FALSE(first.empty());

    explorer_->setMode(ExplorerMode::Files);
    explorer_->setMode(ExplorerMode::Symbols);   // from the kept tree
    EXPECT_EQ(rows(), first);

    write("c.h", "#pragma once\nclass Brandnew {};\n");
    explorer_->setRoot(dir_.generic_string());   // a new index: the kept tree must not be reused
    explorer_->setMode(ExplorerMode::Symbols);
    EXPECT_TRUE(hasRow(rows(), "class Brandnew"));
}
