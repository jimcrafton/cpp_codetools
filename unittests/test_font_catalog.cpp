// The Fonts surface's data: families, faces and sizes, split into installed and bundled.

#include "../extension/NativeEditControls/FontCatalog.h"

#include <newui/fontmanager.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>

using namespace CodeToolsVsix;
namespace fs = std::filesystem;

namespace {

FontFace face(const char* family, const char* style, bool bundled, std::uint64_t bytes, const char* path = nullptr) {
    FontFace f;
    f.family = family;
    f.style = style;
    f.fullName = std::string(family) + (*style ? std::string(" ") + style : std::string());
    f.path = path ? path : (std::string("C:/fonts/") + f.fullName + ".ttf");
    f.type = "TrueType";
    f.bundled = bundled;
    f.bytes = bytes;
    return f;
}

TEST(FontCatalogTest, GroupsFacesByFamilyAndSource) {
    const FontCatalog catalog = FontCatalog::fromFaces({
        face("Mono", "Italic", true, 300),
        face("Mono", "Regular", true, 200),
        face("Mono", "Bold", true, 250),
        face("Arial", "Regular", false, 100),
        face("Mono", "Regular", false, 50),
    });

    ASSERT_EQ(catalog.families().size(), 3u);
    EXPECT_EQ(catalog.installed().size(), 2u);
    ASSERT_EQ(catalog.bundled().size(), 1u);

    const FontFamily& mono = *catalog.bundled()[0];
    EXPECT_EQ(mono.name, "Mono");
    ASSERT_EQ(mono.faces.size(), 3u);
    EXPECT_EQ(mono.faces[0].style, "Regular");   // Regular, Bold, Italic
    EXPECT_EQ(mono.faces[1].style, "Bold");
    EXPECT_EQ(mono.faces[2].style, "Italic");
    EXPECT_EQ(mono.bytes(), 750u);
}

TEST(FontCatalogTest, SystemFamiliesComeFirstThenBundledEachSortedByName) {
    const FontCatalog catalog = FontCatalog::fromFaces({
        face("Zed", "Regular", false, 1),
        face("Alpha", "Regular", true, 1),
        face("beta", "Regular", false, 1),
    });
    ASSERT_EQ(catalog.families().size(), 3u);
    EXPECT_EQ(catalog.families()[0].name, "beta");
    EXPECT_EQ(catalog.families()[1].name, "Zed");
    EXPECT_EQ(catalog.families()[2].name, "Alpha");
}

TEST(FontCatalogTest, TheSameFileListedTwiceCountsOnce) {
    const FontCatalog catalog = FontCatalog::fromFaces({
        face("Arial", "Regular", false, 100, "C:/fonts/arial.ttf"),
        face("Arial", "Regular", false, 100, "C:/FONTS/ARIAL.TTF"),
    });
    EXPECT_EQ(catalog.faceCount(false), 1u);
    EXPECT_EQ(catalog.bytes(false), 100u);
}

TEST(FontCatalogTest, TotalsSplitBySource) {
    const FontCatalog catalog = FontCatalog::fromFaces({
        face("A", "Regular", true, 10),
        face("B", "Regular", true, 20),
        face("C", "Regular", false, 5),
    });
    EXPECT_EQ(catalog.faceCount(true), 2u);
    EXPECT_EQ(catalog.bytes(true), 30u);
    EXPECT_EQ(catalog.faceCount(false), 1u);
    EXPECT_EQ(catalog.bytes(false), 5u);
}

TEST(FontCatalogTest, MatchingIgnoresCaseAndLooksAtFaceNames) {
    const FontCatalog catalog = FontCatalog::fromFaces({
        face("Cascadia Mono", "Regular", true, 1),
        face("Cascadia Mono", "Italic", true, 1),
        face("Fira Code", "Regular", true, 1),
    });
    EXPECT_EQ(catalog.matching("", true).size(), 2u);
    EXPECT_EQ(catalog.matching("CASCADIA", true).size(), 1u);
    EXPECT_EQ(catalog.matching("mono italic", true).size(), 1u);
    EXPECT_TRUE(catalog.matching("fira", false).empty());   // wrong source
    EXPECT_TRUE(catalog.matching("nothing", true).empty());
}

TEST(FontCatalogTest, SizeText) {
    EXPECT_EQ(FontCatalog::sizeText(12), "12 B");
    EXPECT_EQ(FontCatalog::sizeText(340 * 1024), "340 KB");
    EXPECT_EQ(FontCatalog::sizeText(12ull * 1024 * 1024), "12.0 MB");
}

TEST(FontCatalogTest, ScanFindsARegisteredFontAsBundledWithItsFileSize) {
    const fs::path source = "C:/Windows/Fonts/consola.ttf";
    if (!fs::exists(source)) GTEST_SKIP() << "consola.ttf not installed";

    // Registered in place: the registry lasts for the process, so a copy in a deleted temp folder would break
    // every later test that builds the DirectWrite font collection.
    const fs::path copy = source;
    ASSERT_TRUE(newui::FontManager::addFontFile(copy.string()));
    const FontCatalog catalog = FontCatalog::scan();

    const std::string wanted = copy.string();
    bool found = false;
    for (const FontFamily* family : catalog.bundled()) {
        for (const FontFace& f : family->faces) {
            if (_stricmp(f.path.c_str(), wanted.c_str()) == 0) {
                found = true;
                EXPECT_TRUE(f.bundled);
                EXPECT_EQ(f.bytes, static_cast<std::uint64_t>(fs::file_size(copy)));
                EXPECT_EQ(f.type, "TrueType");
            }
        }
    }
    EXPECT_TRUE(found);
    EXPECT_FALSE(catalog.installed().empty());
}

}  // namespace
