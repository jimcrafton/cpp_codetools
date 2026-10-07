// borrowHeaderSettings: a header no target lists is given the settings of the file that includes it. The index
// parses real files here (as the explorer's tests do); the CMake side is a hand-made model.

#include "../extension/NativeEditControls/HeaderFlags.h"

#include <cmakemodel/model.h>
#include <cpptools/projectindex.h>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

using namespace CodeToolsVsix;
namespace fs = std::filesystem;

namespace {

class HeaderFlagsTest : public ::testing::Test {
protected:
    void SetUp() override {
        static int counter = 0;
        dir_ = fs::temp_directory_path() / ("header_flags_" + std::to_string(++counter) + "_" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(dir_);
        root_ = dir_.generic_string();
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(dir_, ec);
    }

    void write(const std::string& relative, const std::string& text) {
        fs::create_directories((dir_ / relative).parent_path());
        std::ofstream(dir_ / relative, std::ios::binary | std::ios::trunc) << text;
    }

    // One executable "app" compiling `sources`, with `inc` on its include path.
    cmakemodel::Model model(const std::vector<std::string>& sources) {
        cmakemodel::Model result;
        result.sourceDir = root_;
        cmakemodel::Target target;
        target.name = "app";
        target.type = cmakemodel::TargetType::Executable;
        target.includeDirs = { root_ + "/inc" };
        for (const std::string& path : sources) {
            cmakemodel::SourceFile source;
            source.path = path;
            target.sources.push_back(source);
        }
        result.targets.push_back(target);
        return result;
    }

    std::unique_ptr<cpptools::ProjectIndex> index(const std::vector<std::string>& files) {
        auto result = std::make_unique<cpptools::ProjectIndex>();
        result->setRoots({ root_ });
        std::vector<std::string> paths;
        for (const std::string& file : files) paths.push_back(root_ + "/" + file);
        result->indexFiles(paths, {}, 1);
        return result;
    }

    fs::path dir_;
    std::string root_;
};

}  // namespace

TEST_F(HeaderFlagsTest, AHeaderTheTargetDoesNotListBorrowsFromTheFileThatIncludesIt) {
    write("inc/special.h", "#pragma once\nint special();\n");
    write("app/tool.h", "#pragma once\n#include <special.h>\n");   // only found through the target's include path
    write("app/main.cpp", "#include \"tool.h\"\nint main() { return special(); }\n");
    const cmakemodel::Model m = model({ "app/main.cpp" });
    cmakemodel::CompileSettingsIndex settings(m);
    auto indexed = index({ "app/main.cpp", "app/tool.h" });
    ASSERT_EQ(settings.find(root_ + "/app/tool.h"), nullptr) << "no target lists or reaches it";

    std::vector<std::string> borrowed;
    EXPECT_EQ(borrowHeaderSettings(*indexed, settings, &borrowed), 1u);

    ASSERT_EQ(borrowed.size(), 1u);
    const cmakemodel::CompileSettings* found = settings.find(root_ + "/app/tool.h");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->target, "app");
}

TEST_F(HeaderFlagsTest, AHeaderIncludedOnlyByAnotherHeaderBorrowsThroughIt) {
    write("app/deep.h", "#pragma once\n");
    write("app/tool.h", "#pragma once\n#include \"deep.h\"\n");
    write("app/main.cpp", "#include \"tool.h\"\nint main() { return 0; }\n");
    const cmakemodel::Model m = model({ "app/main.cpp" });
    cmakemodel::CompileSettingsIndex settings(m);
    auto indexed = index({ "app/main.cpp", "app/tool.h", "app/deep.h" });

    EXPECT_EQ(borrowHeaderSettings(*indexed, settings), 2u);

    EXPECT_NE(settings.find(root_ + "/app/deep.h"), nullptr);
}

TEST_F(HeaderFlagsTest, AHeaderNothingIncludesIsLeftAlone) {
    write("app/lonely.h", "#pragma once\n");
    write("app/main.cpp", "int main() { return 0; }\n");
    const cmakemodel::Model m = model({ "app/main.cpp" });
    cmakemodel::CompileSettingsIndex settings(m);
    auto indexed = index({ "app/main.cpp", "app/lonely.h" });

    EXPECT_EQ(borrowHeaderSettings(*indexed, settings), 0u);

    EXPECT_EQ(settings.find(root_ + "/app/lonely.h"), nullptr);
}

TEST_F(HeaderFlagsTest, AHeaderATargetAlreadyReachesKeepsItsOwnSettings) {
    write("inc/reached.h", "#pragma once\n");   // under the target's include folder
    write("app/main.cpp", "#include \"../inc/reached.h\"\nint main() { return 0; }\n");
    const cmakemodel::Model m = model({ "app/main.cpp" });
    cmakemodel::CompileSettingsIndex settings(m);
    auto indexed = index({ "app/main.cpp", "inc/reached.h" });

    EXPECT_EQ(borrowHeaderSettings(*indexed, settings), 0u);
}

TEST_F(HeaderFlagsTest, TheIncluderInTheSameFolderWinsOverOneFarAway) {
    write("app/shared.h", "#pragma once\n");
    write("app/near.cpp", "#include \"shared.h\"\n");
    write("far/away/other.cpp", "#include \"../../app/shared.h\"\n");
    cmakemodel::Model m = model({ "app/near.cpp" });
    cmakemodel::Target second;
    second.name = "other";
    second.type = cmakemodel::TargetType::Executable;
    cmakemodel::SourceFile source;
    source.path = "far/away/other.cpp";
    second.sources.push_back(source);
    m.targets.push_back(second);
    cmakemodel::CompileSettingsIndex settings(m);
    auto indexed = index({ "app/near.cpp", "far/away/other.cpp", "app/shared.h" });

    EXPECT_EQ(borrowHeaderSettings(*indexed, settings), 1u);

    ASSERT_NE(settings.find(root_ + "/app/shared.h"), nullptr);
    EXPECT_EQ(settings.find(root_ + "/app/shared.h")->target, "app") << "the one beside it, not the one three folders away";
}
