// The Fonts surface: its data model, the surface list, and the Workspace switching between surfaces.

#include "../extension/NativeEditControls/DesignerSurface.h"
#include "../extension/NativeEditControls/FontsModel.h"
#include "../extension/NativeEditControls/Workspace.h"

#include <gtest/gtest.h>

using namespace CodeToolsVsix;

namespace {

FontFace face(const char* family, const char* style, bool bundled, std::uint64_t bytes) {
    FontFace f;
    f.family = family;
    f.style = style;
    f.fullName = std::string(family) + " " + style;
    f.path = "C:/fonts/" + f.fullName + ".ttf";
    f.type = "TrueType";
    f.bundled = bundled;
    f.bytes = bytes;
    return f;
}

FontsModel sampleModel() {
    FontsModel model;
    model.setCatalog(FontCatalog::fromFaces({
        face("Segoe UI", "Regular", false, 1000),
        face("Cascadia Mono", "Regular", true, 2 * 1024 * 1024),
        face("Cascadia Mono", "Italic", true, 2 * 1024 * 1024),
        face("Fira Code", "Regular", true, 1024 * 1024),
    }));
    model.setLiveFonts({ { "Message", "Segoe UI" }, { "Caption", "Segoe UI" } });
    return model;
}

TEST(DesignerSurfaceList, NamesTheSurfacesInMenuOrderAndMarksTheBuiltOnes) {
    const auto& all = designerSurfaces();
    ASSERT_EQ(all.size(), 8u);
    EXPECT_STREQ(all[0].name, "View Designer");
    EXPECT_STREQ(all[3].name, "Fonts");
    for (const DesignerSurfaceInfo& info : all) {
        EXPECT_EQ(info.available, info.id == DesignerSurface::Designer || info.id == DesignerSurface::Fonts) << info.name;
        EXPECT_EQ(&designerSurfaceInfo(info.id), &info);
    }
    EXPECT_EQ(designerSurfaceTitle(DesignerSurface::Fonts), "codetools++ - Fonts");
}

TEST(FontsModel, SourceRowsAreHeadingsOverFamilies) {
    const FontsModel model = sampleModel();
    const auto rows = model.sourceRows();
    // SYSTEM, Segoe UI, BUNDLED, Cascadia Mono, Fira Code, LIVE SYSTEM UI, Message, Caption
    ASSERT_EQ(rows.size(), 8u);
    EXPECT_EQ(rows[0].kind, FontsModel::Row::Kind::Heading);
    EXPECT_EQ(rows[0].text, "SYSTEM");
    EXPECT_EQ(rows[1].text, "Segoe UI");
    EXPECT_EQ(rows[2].text, "BUNDLED");
    EXPECT_EQ(rows[3].text, "Cascadia Mono");
    EXPECT_EQ(rows[3].detail, "4.0 MB");
    EXPECT_EQ(rows[4].text, "Fira Code");
    EXPECT_EQ(rows[5].text, "LIVE SYSTEM UI");
    EXPECT_EQ(rows[6].source, FontsModel::Source::Live);
    EXPECT_EQ(rows[6].detail, "Segoe UI");
}

TEST(FontsModel, TheFilterNarrowsEveryList) {
    FontsModel model = sampleModel();
    model.setFilter("fira");
    EXPECT_EQ(model.cards().size(), 1u);
    int families = 0;
    for (const auto& row : model.sourceRows()) {
        if (row.kind == FontsModel::Row::Kind::Family && row.source != FontsModel::Source::Live) ++families;
    }
    EXPECT_EQ(families, 1);
}

TEST(FontsModel, TheFirstBundledFamilyStartsSelected) {
    const FontsModel model = sampleModel();
    EXPECT_EQ(model.selectedSource(), FontsModel::Source::Bundled);
    EXPECT_EQ(model.selectedName(), "Cascadia Mono");
    ASSERT_NE(model.selectedFamily(), nullptr);
    EXPECT_EQ(model.selectedFamily()->faces.size(), 2u);
}

TEST(FontsModel, CascadiaMonoAndFiraCodeStartKeptAndTheRestDoNot) {
    FontsModel model;
    model.setCatalog(FontCatalog::fromFaces({
        face("Cascadia Mono", "Regular", true, 2 * 1024 * 1024),
        face("Fira Code", "Regular", true, 1024 * 1024),
        face("Monaspace Neon", "Regular", true, 4 * 1024 * 1024),
    }));
    EXPECT_TRUE(model.kept("Cascadia Mono"));
    EXPECT_TRUE(model.kept("Fira Code"));
    EXPECT_FALSE(model.kept("Monaspace Neon"));
    EXPECT_EQ(model.keptCount(), 2u);
    EXPECT_EQ(model.keptText(), "2 of 3 families kept");

    model.setKept("Monaspace Neon", true);
    model.setKept("Fira Code", false);
    EXPECT_TRUE(model.kept("Monaspace Neon"));
    EXPECT_FALSE(model.kept("Fira Code"));
    EXPECT_EQ(model.keptBytes(), 6u * 1024u * 1024u);

    model.setKept("Fira Code", true);
    EXPECT_TRUE(model.kept("Fira Code"));
}

TEST(FontsModel, TotalsSaySizeKeptAndSaved) {
    FontsModel model = sampleModel();
    EXPECT_EQ(model.bundledBytes(), 5u * 1024u * 1024u);
    EXPECT_EQ(model.keptBytes(), 5u * 1024u * 1024u);   // both families in the sample are kept
    EXPECT_EQ(model.sizeSummary(), "5.0 MB of 5.0 MB - trimming saves 0 B");
    EXPECT_EQ(model.totalText(), "3 bundled faces");
}

TEST(FontsModel, PropertiesFollowTheSelection) {
    FontsModel model = sampleModel();
    auto props = model.properties();
    ASSERT_GE(props.size(), 5u);
    EXPECT_EQ(props[0], std::make_pair(std::string("Source"), std::string("bundled")));
    EXPECT_EQ(props[1], std::make_pair(std::string("Files"), std::string("2")));
    EXPECT_EQ(props[3].second, FontsModel::kUsedByCascadiaMono);
    EXPECT_EQ(model.propertiesTitle(), "PROPERTIES - Cascadia Mono");

    model.select(FontsModel::Source::Live, "Message");
    props = model.properties();
    ASSERT_EQ(props.size(), 2u);
    EXPECT_EQ(props[1], std::make_pair(std::string("Resolves to"), std::string("Segoe UI")));

    model.select(FontsModel::Source::System, "Segoe UI");
    EXPECT_EQ(model.properties()[0].second, "installed in Windows");
}

TEST(FontsModel, ThePreviewFollowsTheSelectedFontWhateverItsSource) {
    FontsModel model = sampleModel();
    EXPECT_EQ(model.previewFamily(), "Cascadia Mono");
    const auto faces = model.previewFaces();
    ASSERT_EQ(faces.size(), 2u);
    EXPECT_EQ(faces[0], "Cascadia Mono Regular");
    EXPECT_EQ(faces[1], "Cascadia Mono Italic");

    model.select(FontsModel::Source::System, "Segoe UI");
    EXPECT_EQ(model.previewFamily(), "Segoe UI");

    model.select(FontsModel::Source::Live, "Caption");
    EXPECT_EQ(model.previewFamily(), "Segoe UI");   // what the role resolves to
    EXPECT_TRUE(model.previewFaces().empty());

    model.select(FontsModel::Source::System, "Nothing Installed");
    EXPECT_TRUE(model.previewFamily().empty());
}

TEST(WorkspaceSurfaces, StartsOnTheDesignerAndLabelsTheSwitcher) {
    auto* workspace = new Workspace();
    EXPECT_EQ(workspace->surface(), DesignerSurface::Designer);
    ASSERT_NE(workspace->surfaceButton(), nullptr);
    EXPECT_NE(workspace->surfaceButton()->text().find("codetools++ - View Designer"), std::string::npos);
    EXPECT_FALSE(workspace->surfaceButton()->icon().empty());
    EXPECT_EQ(workspace->topBar()->childViews().front(), workspace->surfaceButton());   // far left
    delete workspace;
}

TEST(WorkspaceSurfaces, ASurfaceNotBuiltYetCannotBeShown) {
    auto* workspace = new Workspace();
    int changes = 0;
    workspace->onSurfaceChanged.add([&changes](Workspace&) { ++changes; return newui::SyncReturn::Handled; });
    EXPECT_FALSE(workspace->showSurface(DesignerSurface::FrameMap));
    EXPECT_EQ(workspace->surface(), DesignerSurface::Designer);
    EXPECT_EQ(changes, 0);
    delete workspace;
}

TEST(WorkspaceSurfaces, FontsReplacesTheDesignerAndTheDesignerComesBack) {
    auto* workspace = new Workspace();
    int changes = 0;
    workspace->onSurfaceChanged.add([&changes](Workspace&) { ++changes; return newui::SyncReturn::Handled; });
    ASSERT_NE(workspace->fontsSurface(), nullptr);
    EXPECT_FALSE(workspace->fontsSurface()->isVisible());
    EXPECT_FALSE(workspace->fontsSurface()->loaded());   // not read until first shown

    EXPECT_TRUE(workspace->showSurface(DesignerSurface::Fonts));
    EXPECT_EQ(workspace->surface(), DesignerSurface::Fonts);
    EXPECT_TRUE(workspace->fontsSurface()->isVisible());
    EXPECT_TRUE(workspace->fontsSurface()->loaded());
    EXPECT_NE(workspace->surfaceButton()->text().find("codetools++ - Fonts"), std::string::npos);

    EXPECT_TRUE(workspace->showSurface(DesignerSurface::Designer));
    EXPECT_FALSE(workspace->fontsSurface()->isVisible());
    EXPECT_NE(workspace->surfaceButton()->text().find("codetools++ - View Designer"), std::string::npos);
    EXPECT_EQ(changes, 2);
    delete workspace;
}

}  // namespace
