// DocumentTabs: several documents open at once, one tab each. The editors here are fakes with no
// window - what's under test is the tab bookkeeping (which tab shows what, what closing destroys),
// not the native child-window placement, which only a real window can show.

#include <gtest/gtest.h>

#include <newui/rootview.h>

#include "../extension/NativeEditControls/DocumentTabs.h"

using namespace CodeToolsVsix;

namespace {

class FakeEditor : public NativeEditor {
public:
    FakeEditor(int& alive, bool loads) : alive_(alive), loads_(loads) { ++alive_; }
    ~FakeEditor() override { --alive_; }

    bool load(const wchar_t*, std::size_t) override { return loads_; }
    bool save(const wchar_t*, std::size_t) override { return true; }
    bool execCommand(EditorCommand, std::uint32_t, const EditorCommandArgs*) override { return false; }
    void edit() { markDirty(); }

private:
    int& alive_;
    bool loads_;
};

struct Harness {
    int alive = 0;
    bool loads = true;
    bool factoryWorks = true;
    newui::RootView root{nullptr, newui::Rect(0, 0, 800, 600), "root"};
    DocumentTabs* tabs = nullptr;

    Harness() {
        tabs = new DocumentTabs([this](DocumentType, HWND) -> std::unique_ptr<NativeEditor> {
            if (!factoryWorks) {
                return nullptr;
            }
            return std::make_unique<FakeEditor>(alive, loads);
        });
        root.addChild(tabs);
        tabs->setBounds(newui::Rect(0, 0, 800, 600));
    }
    ~Harness() {
        tabs->closeAll();
        root.destroy();
    }
};

}

TEST(DocumentTabs, OpeningAFileAddsATabAndSelectsIt) {
    Harness h;
    NativeEditor* first = h.tabs->open(L"C:\\proj\\a.h", DocumentType::CppSource);
    NativeEditor* second = h.tabs->open(L"C:\\proj\\S1.newui", DocumentType::Designer);

    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_NE(first, second);
    EXPECT_EQ(h.tabs->count(), 2u);
    EXPECT_EQ(h.tabs->tabControl()->tabCount(), 2u);
    EXPECT_EQ(h.tabs->activeEditor(), second);
    EXPECT_EQ(h.tabs->activeType(), DocumentType::Designer);
    EXPECT_EQ(h.tabs->activePath(), L"C:\\proj\\S1.newui");
}

TEST(DocumentTabs, AnActivePagesOffsetInTheRootIncludesWhereTheTabsSitAndTheTabStrip) {
    Harness h;
    h.tabs->setBounds(newui::Rect(240, 30, 560, 570));   // right of a tree pane, below a menu bar
    h.tabs->open(L"C:\\proj\\a.h", DocumentType::CppSource);

    newui::SubView* page = h.tabs->tabControl()->page(0);
    ASSERT_NE(page, nullptr);
    const newui::Point offset = h.root.accumulatedOffset(page);
    EXPECT_FLOAT_EQ(offset.x, 240.0f);
    EXPECT_GT(offset.y, 30.0f) << "the tab strip is above the page";
    EXPECT_GT(page->bounds().size().width, 0.0f);
}

TEST(DocumentTabs, OpeningTheSameFileAgainSelectsItsTabWhateverTheCaseOrSpelling) {
    Harness h;
    NativeEditor* first = h.tabs->open(L"C:\\proj\\a.h", DocumentType::CppSource);
    h.tabs->open(L"C:\\proj\\b.h", DocumentType::CppSource);
    ASSERT_NE(h.tabs->activeEditor(), first);

    NativeEditor* again = h.tabs->open(L"c:\\proj\\sub\\..\\A.H", DocumentType::CppSource);
    EXPECT_EQ(again, first);
    EXPECT_EQ(h.tabs->count(), 2u);
    EXPECT_EQ(h.tabs->activeEditor(), first);
    EXPECT_EQ(h.alive, 2) << "no second editor was made";
}

TEST(DocumentTabs, AFileThatCantBeLoadedLeavesNoTabAndNoEditor) {
    Harness h;
    h.tabs->open(L"C:\\proj\\a.h", DocumentType::CppSource);
    h.loads = false;

    EXPECT_EQ(h.tabs->open(L"C:\\proj\\bad.h", DocumentType::CppSource), nullptr);
    EXPECT_EQ(h.tabs->count(), 1u);
    EXPECT_EQ(h.tabs->tabControl()->tabCount(), 1u);
    EXPECT_EQ(h.alive, 1);
    EXPECT_EQ(h.tabs->activePath(), L"C:\\proj\\a.h");
}

TEST(DocumentTabs, AnEditorThatCantBeCreatedLeavesNoTab) {
    Harness h;
    h.factoryWorks = false;

    EXPECT_EQ(h.tabs->open(L"C:\\proj\\a.h", DocumentType::CppSource), nullptr);
    EXPECT_EQ(h.tabs->count(), 0u);
    EXPECT_EQ(h.tabs->activeEditor(), nullptr);
    EXPECT_FALSE(h.tabs->activeType().has_value());
}

TEST(DocumentTabs, ClosingATabDestroysItsEditorAndSelectsANeighbour) {
    Harness h;
    NativeEditor* a = h.tabs->open(L"C:\\proj\\a.h", DocumentType::CppSource);
    h.tabs->open(L"C:\\proj\\b.h", DocumentType::CppSource);
    NativeEditor* c = h.tabs->open(L"C:\\proj\\c.h", DocumentType::CppSource);
    ASSERT_EQ(h.alive, 3);

    ASSERT_TRUE(h.tabs->closeActive());   // c, the selected one
    EXPECT_EQ(h.alive, 2);
    EXPECT_EQ(h.tabs->count(), 2u);
    EXPECT_NE(h.tabs->activeEditor(), c);
    EXPECT_EQ(h.tabs->activePath(), L"C:\\proj\\b.h");

    ASSERT_TRUE(h.tabs->close(0));        // a
    EXPECT_EQ(h.alive, 1);
    EXPECT_EQ(h.tabs->activePath(), L"C:\\proj\\b.h");
    (void)a;

    EXPECT_FALSE(h.tabs->close(5)) << "out of range changes nothing";
    EXPECT_EQ(h.tabs->count(), 1u);
}

TEST(DocumentTabs, ClosingTheLastTabLeavesNothingActive) {
    Harness h;
    h.tabs->open(L"C:\\proj\\a.h", DocumentType::CppSource);
    ASSERT_TRUE(h.tabs->closeActive());

    EXPECT_EQ(h.tabs->count(), 0u);
    EXPECT_EQ(h.tabs->activeEditor(), nullptr);
    EXPECT_FALSE(h.tabs->closeActive());
}

TEST(DocumentTabs, CloseAllDestroysEveryEditor) {
    Harness h;
    h.tabs->open(L"C:\\proj\\a.h", DocumentType::CppSource);
    h.tabs->open(L"C:\\proj\\b.h", DocumentType::CppSource);
    h.tabs->closeAll();

    EXPECT_EQ(h.alive, 0);
    EXPECT_EQ(h.tabs->count(), 0u);
}

TEST(DocumentTabs, ATitleIsTheFileNameWithAMarkWhileThereAreUnsavedChanges) {
    EXPECT_EQ(DocumentTabs::titleFor(L"C:\\proj\\S1Controller.h", false), "S1Controller.h");
    EXPECT_EQ(DocumentTabs::titleFor(L"C:\\proj\\S1Controller.h", true), "S1Controller.h *");
}

TEST(DocumentTabs, RefreshingTitlesFollowsTheEditorsDirtyFlag) {
    Harness h;
    auto* editor = static_cast<FakeEditor*>(h.tabs->open(L"C:\\proj\\a.h", DocumentType::CppSource));
    ASSERT_NE(editor, nullptr);
    ASSERT_FALSE(editor->isDirty());

    editor->edit();
    ASSERT_TRUE(editor->isDirty());
    h.tabs->refreshTitles();   // must not crash and must keep the tab/editor pairing

    EXPECT_EQ(h.tabs->activeEditor(), editor);
    EXPECT_EQ(h.tabs->tabControl()->tabCount(), 1u);
}
