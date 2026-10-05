// What the editor makes of a file that is not C++: a CMakeLists.txt is colored and folded as CMake and never
// parsed by libclang; anything else is plain text. (A CMakeLists.txt opened as C++ filled with bogus
// "unknown type name" problems.)

#include "../extension/NativeEditControls/CppEditor.h"
#include "../extension/NativeEditControls/CppHighlight.h"

#include <newui/rootview.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

using CodeToolsVsix::CppEditor;
namespace fs = std::filesystem;

TEST(EditorFileKinds, TheNameDecidesWhetherAFileIsCppCMakeOrPlain) {
    EXPECT_EQ(CppEditor::fileKindFor(L"C:\\proj\\src\\main.cpp"), CppEditor::FileKind::Cpp);
    EXPECT_EQ(CppEditor::fileKindFor(L"C:/proj/include/Parser.H"), CppEditor::FileKind::Cpp);
    EXPECT_EQ(CppEditor::fileKindFor(L"a.inl"), CppEditor::FileKind::Cpp);
    EXPECT_EQ(CppEditor::fileKindFor(L"C:\\proj\\CMakeLists.txt"), CppEditor::FileKind::CMake);
    EXPECT_EQ(CppEditor::fileKindFor(L"cmakelists.TXT"), CppEditor::FileKind::CMake);
    EXPECT_EQ(CppEditor::fileKindFor(L"cmake/FindThing.cmake"), CppEditor::FileKind::CMake);
    EXPECT_EQ(CppEditor::fileKindFor(L"notes.txt"), CppEditor::FileKind::Plain);
    EXPECT_EQ(CppEditor::fileKindFor(L"README.md"), CppEditor::FileKind::Plain);
    EXPECT_EQ(CppEditor::fileKindFor(L"Makefile"), CppEditor::FileKind::Plain);
    EXPECT_EQ(CppEditor::fileKindFor(L""), CppEditor::FileKind::Plain);
}

TEST(EditorFileKinds, CMakeTextGetsColorsAndPlainTextGetsNone) {
    const std::wstring text = L"add_library(core STATIC a.cpp)  # the core\n";

    const auto cmake = CodeToolsVsix::analyzeCMake(text);
    EXPECT_FALSE(cmake.ranges.empty());
    EXPECT_TRUE(cmake.foldsValid);

    const auto plain = CodeToolsVsix::analyzePlainText(text);
    EXPECT_TRUE(plain.ranges.empty());
    EXPECT_TRUE(plain.folds.empty());
}

TEST(EditorFileKinds, CMakeBlocksFoldWhatIsBetweenTheirCommands) {
    const std::wstring text =
        L"function(f)\n"          // line 0
        L"  if(A)\n"              // 1
        L"    set(x 1)\n"         // 2
        L"  elseif(B)\n"          // 3
        L"    set(x 2)\n"         // 4
        L"  else()\n"             // 5
        L"    set(x 3)\n"         // 6
        L"  endif()\n"            // 7
        L"endfunction()\n"        // 8
        L"set(one line)\n";
    auto folds = CodeToolsVsix::cmakeFoldsFor(text);
    std::sort(folds.begin(), folds.end(), [](const auto& a, const auto& b) { return a.start < b.start; });

    // the function body, and one fold per branch of the if
    ASSERT_EQ(folds.size(), 4u);
    std::size_t functionBody = std::wstring::npos;
    for (const auto& fold : folds) {
        if (fold.start == text.find(L"function(f)") + std::wstring(L"function(f)").size()) functionBody = fold.length;
    }
    EXPECT_NE(functionBody, std::wstring::npos) << "starts at the end of the function line";
    EXPECT_EQ(text.substr(folds[0].start + folds[0].length, 14), L"endfunction()\n");
}

TEST(EditorFileKinds, ABlockOnOneLineOrWithoutItsEndDoesNotFold) {
    EXPECT_TRUE(CodeToolsVsix::cmakeFoldsFor(L"if(A) set(x 1) endif()\n").empty());
    EXPECT_TRUE(CodeToolsVsix::cmakeFoldsFor(L"foreach(i a b)\n  set(x ${i})\n").empty()) << "never closed";
    EXPECT_TRUE(CodeToolsVsix::cmakeFoldsFor(L"").empty());
}

namespace {

struct TempFile
{
    fs::path path;
    TempFile(const std::string& name, const std::string& text) {
        static int counter = 0;
        path = fs::temp_directory_path() / ("editor_kind_" + std::to_string(++counter) + "_" +
               std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(path);
        path /= name;
        std::ofstream(path, std::ios::binary) << text;
    }
    ~TempFile() {
        std::error_code ec;
        fs::remove_all(path.parent_path(), ec);
    }
};

}  // namespace

TEST(EditorFileKinds, ACMakeFileLoadsWithoutBeingParsedAsCpp) {
    TempFile file("CMakeLists.txt",
        "add_executable(cppoutline main.cpp)\n\n"
        "target_link_libraries(cppoutline PRIVATE cpptools)\n"
        "cpptools_copy_libclang_dll(cppoutline)\n");
    auto* root = new newui::RootView(nullptr, newui::Rect(0, 0, 900, 700), "fileKindRoot");
    CppEditor editor(root);
    ASSERT_NE(editor.textControl(), nullptr);

    const std::wstring path = file.path.wstring();
    ASSERT_TRUE(editor.load(path.c_str(), path.size()));

    EXPECT_EQ(editor.fileKind(), CppEditor::FileKind::CMake);
    ASSERT_NE(editor.statusBar(), nullptr);
    EXPECT_TRUE(editor.statusBar()->problems().empty()) << "libclang must not have judged it";
    EXPECT_NE(editor.outlineControl()->text().find(L"No outline for CMake files"), std::wstring::npos);
}

TEST(EditorFileKinds, APlainTextFileHasNoProblemsAndNoOutline) {
    TempFile file("notes.txt", "int = ; this is not code\n");
    auto* root = new newui::RootView(nullptr, newui::Rect(0, 0, 900, 700), "plainKindRoot");
    CppEditor editor(root);

    const std::wstring path = file.path.wstring();
    ASSERT_TRUE(editor.load(path.c_str(), path.size()));

    EXPECT_EQ(editor.fileKind(), CppEditor::FileKind::Plain);
    EXPECT_TRUE(editor.statusBar()->problems().empty());
}

TEST(EditorFileKinds, ACppFileIsStillAnalyzedAsCpp) {
    TempFile file("a.cpp", "int main() { return 0; }\n");
    auto* root = new newui::RootView(nullptr, newui::Rect(0, 0, 900, 700), "cppKindRoot");
    CppEditor editor(root);

    const std::wstring path = file.path.wstring();
    ASSERT_TRUE(editor.load(path.c_str(), path.size()));

    EXPECT_EQ(editor.fileKind(), CppEditor::FileKind::Cpp);
}
