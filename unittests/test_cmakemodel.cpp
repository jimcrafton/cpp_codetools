// cmakemodel: the project as CMake's File API describes it - targets, their sources and link edges, and
// the CMakeLists line behind each. Most tests read a small hand-written reply (fixtures/cmake_fileapi);
// the last read this repo's own build tree, when it has one.

#include <cmakemodel/fileapi.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <string>

using namespace cmakemodel;

namespace {

const std::string kFixtureBuild = std::string(CMAKEMODEL_FIXTURE_DIR) + "/ninja";

Model loadFixture() {
    LoadResult result = loadFileApi(kFixtureBuild, "");
    EXPECT_TRUE(result.ok()) << result.error;
    return result.model;
}

bool hasName(const std::vector<const Target*>& targets, const std::string& name) {
    return std::any_of(targets.begin(), targets.end(), [&](const Target* t) { return t->name == name; });
}

}

TEST(CMakeModel, CompileSettingsComeFromTheTargetThatOwnsTheFile) {
    const Model model = loadFixture();
    const CompileSettingsIndex settings(model);

    const CompileSettings* app = settings.find("C:/proj/tools/main.cpp");
    ASSERT_NE(app, nullptr);
    EXPECT_EQ(app->target, "app");
    EXPECT_EQ(app->definitions, (std::vector<std::string>{ "WIN32", "UNICODE" }));
    EXPECT_EQ(app->toArgs(), (std::vector<std::string>{ "-IC:/proj/include", "-DWIN32", "-DUNICODE" }));

    const CompileSettings* core = settings.find("c:\\proj\\SRC\\a.cpp");
    ASSERT_NE(core, nullptr) << "any slash, any case";
    EXPECT_EQ(core->target, "core");

    const CompileSettings* header = settings.find("C:/proj/include/a.h");
    ASSERT_NE(header, nullptr);
    EXPECT_EQ(header->target, "core") << "a header a target lists";

    // a header nobody lists is reached through an include folder: the target that has it on its path
    const CompileSettings* unlisted = settings.find("C:/proj/include/deep/other.h");
    ASSERT_NE(unlisted, nullptr);
    EXPECT_FALSE(unlisted->includeDirs.empty());

    EXPECT_EQ(settings.find("C:/elsewhere/x.cpp"), nullptr) << "no target claims it";
    EXPECT_EQ(settings.find("C:/proj/include"), nullptr) << "the folder itself is not a file under it";
}

TEST(CMakeModel, ATargetFileMissingFromTheReplyIsSkippedNotFatal) {
    namespace fs = std::filesystem;
    const fs::path copy = fs::temp_directory_path() / "cmakemodel_missing_target";
    std::error_code ec;
    fs::remove_all(copy, ec);
    fs::create_directories(copy, ec);
    fs::copy(kFixtureBuild, copy, fs::copy_options::recursive, ec);
    ASSERT_FALSE(ec) << ec.message();
    fs::remove(copy / ".cmake/api/v1/reply/target-core-Debug.json");

    const LoadResult result = loadFileApi(copy.generic_string(), "");
    EXPECT_TRUE(result.ok()) << result.error;
    EXPECT_EQ(result.skipped, 1);
    EXPECT_EQ(result.model.targets.size(), 5u);
    EXPECT_EQ(result.model.find("core"), nullptr);
    EXPECT_NE(result.model.find("app"), nullptr);
    fs::remove_all(copy, ec);
}

TEST(CMakeModel, ReadsTheGeneratorAndTheOnlyConfigurationWithoutBeingTold) {
    const Model model = loadFixture();

    EXPECT_EQ(model.generator, "Ninja");
    EXPECT_EQ(model.cmakeVersion, "4.3.1");
    EXPECT_FALSE(model.multiConfig);
    EXPECT_EQ(model.configuration, "Debug");
    EXPECT_EQ(model.sourceDir, "C:/proj");
    EXPECT_EQ(model.targets.size(), 6u);
}

TEST(CMakeModel, TargetsAreSortedIntoProductsInputsAndGeneratedNoise) {
    const Model model = loadFixture();

    ASSERT_NE(model.find("app"), nullptr);
    EXPECT_EQ(model.find("app")->kind, ProductKind::Application);
    EXPECT_EQ(model.find("core")->kind, ProductKind::Library);
    EXPECT_EQ(model.find("core_tests")->kind, ProductKind::Test) << "it links gtest_main";
    EXPECT_EQ(model.find("objs")->kind, ProductKind::Input);
    EXPECT_EQ(model.find("gen")->kind, ProductKind::Tool);
    EXPECT_EQ(model.find("ALL_BUILD")->kind, ProductKind::Generated);
    EXPECT_EQ(model.find("nothing"), nullptr);

    EXPECT_TRUE(hasName(model.byKind(ProductKind::Library), "core"));
    EXPECT_EQ(model.byKind(ProductKind::Application).size(), 1u);
}

TEST(CMakeModel, ATargetKnowsItsArtifactAndTheCallThatMadeIt) {
    const Model model = loadFixture();
    const Target& app = *model.find("app");

    EXPECT_EQ(app.type, TargetType::Executable);
    EXPECT_EQ(app.nameOnDisk, "app.exe");
    ASSERT_EQ(app.artifacts.size(), 1u);
    EXPECT_EQ(app.artifacts[0], "tools/app.exe");
    EXPECT_EQ(app.directory, "tools");
    EXPECT_EQ(app.defined.file, "tools/CMakeLists.txt");
    EXPECT_EQ(app.defined.line, 2);
    EXPECT_EQ(app.defined.command, "add_executable");
    EXPECT_TRUE(app.defined.valid());
}

TEST(CMakeModel, ASourceNamesItsGroupAndTheCallThatListedIt) {
    const Model model = loadFixture();
    const Target& core = *model.find("core");

    ASSERT_EQ(core.sources.size(), 3u) << "the object library's .obj is not one of its sources";
    EXPECT_EQ(core.sources[0].path, "src/a.cpp");
    EXPECT_EQ(core.sources[0].group, "Source Files");
    EXPECT_EQ(core.sources[0].defined.command, "add_library");
    EXPECT_EQ(core.sources[0].defined.line, 10);
    EXPECT_EQ(core.sources[2].path, "include/a.h");
    EXPECT_EQ(core.sources[2].group, "Header Files");
}

TEST(CMakeModel, LinkEdgesAreTargetsImportedLibrariesOrFiles) {
    const Model model = loadFixture();
    const Target& app = *model.find("app");

    ASSERT_EQ(app.links.size(), 3u);
    EXPECT_EQ(app.links[0].kind, LinkKind::Target);
    EXPECT_EQ(app.links[0].name, "core");
    EXPECT_EQ(app.links[0].defined.command, "target_link_libraries");
    EXPECT_EQ(app.links[0].defined.line, 3);
    EXPECT_EQ(app.links[1].kind, LinkKind::Imported) << "CMake knows it only by name";
    EXPECT_EQ(app.links[1].name, "ext::lib");
    EXPECT_EQ(app.links[2].kind, LinkKind::File);
    EXPECT_EQ(app.links[2].name, "user32.lib");
}

TEST(CMakeModel, DependenciesDropTheGeneratorsOwnTargets) {
    const Model model = loadFixture();
    const Target& app = *model.find("app");

    ASSERT_EQ(app.dependencies.size(), 1u);
    EXPECT_EQ(app.dependencies[0], "core");
}

TEST(CMakeModel, DefinitionsAndIncludeDirsAreListedOnce) {
    const Model model = loadFixture();

    ASSERT_EQ(model.find("app")->definitions.size(), 2u);
    EXPECT_EQ(model.find("app")->definitions[0], "WIN32");
    ASSERT_EQ(model.find("core")->includeDirs.size(), 1u) << "the repeated directory once";
    EXPECT_EQ(model.find("core")->includeDirs[0], "C:/proj/include");
}

TEST(CMakeModel, AnObjectLibraryLinkedInContributesItsSourcesViaItsName) {
    const Model model = loadFixture();
    const std::vector<SourceFile> all = model.allSources(*model.find("core"));

    ASSERT_EQ(all.size(), 4u);
    EXPECT_TRUE(all[0].via.empty());
    EXPECT_EQ(all[3].path, "src/o1.cpp");
    EXPECT_EQ(all[3].via, "objs");
}

TEST(CMakeModel, UsedByListsTheTargetsThatLinkOne) {
    const Model model = loadFixture();
    const std::vector<const Target*> users = model.usedBy("core");

    ASSERT_EQ(users.size(), 2u);
    EXPECT_TRUE(hasName(users, "app"));
    EXPECT_TRUE(hasName(users, "core_tests"));
    EXPECT_TRUE(model.usedBy("app").empty());
    EXPECT_TRUE(model.usedBy("user32.lib").empty()) << "a file is not a target";
}

TEST(CMakeModel, ABuildDirectoryWithNoReplyExplainsWhatToDo) {
    const std::filesystem::path empty = std::filesystem::temp_directory_path() / "cmakemodel_empty_build";
    std::filesystem::create_directories(empty);

    const LoadResult result = loadFileApi(empty.string(), "");
    EXPECT_FALSE(result.ok());
    EXPECT_NE(result.error.find("requestQueries"), std::string::npos) << result.error;

    EXPECT_FALSE(loadFileApi((empty / "missing").string(), "").ok());
    std::filesystem::remove_all(empty);
}

TEST(CMakeModel, RequestingTheQueriesWritesTheFileCMakeLooksFor) {
    const std::filesystem::path build = std::filesystem::temp_directory_path() / "cmakemodel_query_build";
    std::filesystem::remove_all(build);
    std::filesystem::create_directories(build);

    std::string error;
    EXPECT_TRUE(requestQueries(build.string(), &error)) << error;
    EXPECT_TRUE(std::filesystem::exists(build / ".cmake" / "api" / "v1" / "query" / "codemodel-v2"));

    EXPECT_FALSE(requestQueries((build / "nope").string(), &error));
    EXPECT_FALSE(error.empty());
    std::filesystem::remove_all(build);
}

TEST(CMakeModel, ReadsThisRepositorysOwnBuildTree) {
    const std::string build = CMAKEMODEL_REPO_BUILD_DIR;
    if (!std::filesystem::exists(std::filesystem::path(build) / ".cmake" / "api" / "v1" / "reply")) {
        GTEST_SKIP() << "no File API reply in " << build << " (request it with requestQueries() and configure)";
    }

    const LoadResult result = loadFileApi(build, "Debug");
    ASSERT_TRUE(result.ok()) << result.error;
    if (result.skipped > 0) GTEST_SKIP() << "this build's reply is incomplete (" << result.skipped << " target files missing)";
    const Model& model = result.model;
    EXPECT_EQ(model.configuration, "Debug");

    const Target* tool = model.find("cppoutline");
    ASSERT_NE(tool, nullptr);
    EXPECT_EQ(tool->kind, ProductKind::Application);
    EXPECT_EQ(tool->defined.command, "add_executable");
    EXPECT_TRUE(tool->defined.valid());

    const Target* cpptools = model.find("cpptools");
    ASSERT_NE(cpptools, nullptr);
    EXPECT_EQ(cpptools->kind, ProductKind::Library);
    EXPECT_FALSE(cpptools->sources.empty());
    const bool linksLibclang = std::any_of(cpptools->links.begin(), cpptools->links.end(),
        [](const LinkItem& l) { return l.name == "libclang::libclang" && l.kind == LinkKind::Imported; });
    EXPECT_TRUE(linksLibclang);

    const Target* core = model.find("nativeeditcontrols_core");
    ASSERT_NE(core, nullptr);
    EXPECT_EQ(core->kind, ProductKind::Input) << "an OBJECT library";

    const Target* dll = model.find("NativeEditControls");
    ASSERT_NE(dll, nullptr);
    EXPECT_EQ(dll->kind, ProductKind::Library);
    const std::vector<SourceFile> all = model.allSources(*dll);
    EXPECT_GT(all.size(), dll->sources.size()) << "the object library's sources come along";
    EXPECT_TRUE(std::any_of(all.begin(), all.end(), [](const SourceFile& s) { return s.via == "nativeeditcontrols_core"; }));

    EXPECT_FALSE(loadFileApi(build, "NoSuchConfig").ok());
}

TEST(CMakeModel, ABorrowedHeaderBuildsWithTheSettingsOfTheFileItWasBorrowedFrom) {
    const Model model = loadFixture();
    CompileSettingsIndex settings(model);
    ASSERT_EQ(settings.find("C:/elsewhere/lonely.h"), nullptr);

    settings.borrow("C:/elsewhere/lonely.h", "C:/proj/tools/main.cpp");

    const CompileSettings* borrowed = settings.find("c:\\elsewhere\\LONELY.h");
    ASSERT_NE(borrowed, nullptr) << "any slash, any case";
    EXPECT_EQ(borrowed->target, "app");
}

TEST(CMakeModel, BorrowingFromAFileWithNoSettingsChangesNothing) {
    const Model model = loadFixture();
    CompileSettingsIndex settings(model);

    settings.borrow("C:/elsewhere/lonely.h", "C:/elsewhere/nobody.cpp");

    EXPECT_EQ(settings.find("C:/elsewhere/lonely.h"), nullptr);
}
