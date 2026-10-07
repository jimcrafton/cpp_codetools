// DocumentTabs: several documents open at once, one tab each. The editors here are fakes with no
// window - what's under test is the tab bookkeeping (which tab shows what, what closing destroys),
// not the native child-window placement, which only a real window can show.

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>

#include <newui/rootview.h>
#include <newui/utils.h>

#include "../extension/NativeEditControls/DocumentTabs.h"

using namespace CodeToolsVsix;

namespace {

class FakeEditor : public NativeEditor {
public:
    FakeEditor(int& alive, bool loads) : alive_(alive), loads_(loads) { ++alive_; }
    ~FakeEditor() override { --alive_; }

    bool load(const wchar_t*, std::size_t) override {
        if (loads_) {
            ++loadCount;
            clearDirty();
        }
        return loads_;
    }
    bool save(const wchar_t*, std::size_t) override { return true; }
    bool execCommand(EditorCommand command, std::uint32_t, const EditorCommandArgs* args) override {
        if (command == EditorCommand::GotoLine && args != nullptr && args->text1 != nullptr) {
            wentTo = std::wstring(args->text1, args->text1Length);
            return true;
        }
        return false;
    }
    void edit() { markDirty(); }   // quiet, as CppEditor's is: nothing tells the tabs
    void editNotifying() { markDirty(); notifyStateChanged(); }   // as the Designer's modified flag does
    void saved() { clearDirty(); notifyStateChanged(); }   // what a save does: the file is written, then the editor reports it
    int loadCount = 0;
    void saveAs(const std::wstring& path) { path_ = path; notifyStateChanged(); }
    std::wstring currentPath() const override { return path_; }

    std::wstring path_;
    std::wstring wentTo;   // the "line:column" of the last GotoLine

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

TEST(DocumentTabs, OpeningAtAPositionOpensTheTabAndMovesTheCaretThere) {
    Harness h;
    auto* editor = static_cast<FakeEditor*>(h.tabs->openAt(L"C:\\proj\\S1Controller.h", DocumentType::CppSource, 6, 10));

    ASSERT_NE(editor, nullptr);
    EXPECT_EQ(editor->wentTo, L"6:10");
    EXPECT_EQ(h.tabs->count(), 1u);
    EXPECT_EQ(h.tabs->activeEditor(), editor);
}

TEST(DocumentTabs, OpeningAtAPositionInAnOpenFileJustSelectsItAndMoves) {
    Harness h;
    auto* first = static_cast<FakeEditor*>(h.tabs->open(L"C:\\proj\\a.h", DocumentType::CppSource));
    h.tabs->open(L"C:\\proj\\b.h", DocumentType::CppSource);
    ASSERT_NE(h.tabs->activeEditor(), first);

    auto* again = static_cast<FakeEditor*>(h.tabs->openAt(L"C:\\proj\\a.h", DocumentType::CppSource, 47, 23));
    EXPECT_EQ(again, first);
    EXPECT_EQ(first->wentTo, L"47:23");
    EXPECT_EQ(h.tabs->count(), 2u);
    EXPECT_EQ(h.tabs->activeEditor(), first);
}

TEST(DocumentTabs, AColumnOfZeroMeansTheStartOfTheLine) {
    Harness h;
    auto* editor = static_cast<FakeEditor*>(h.tabs->openAt(L"C:\\proj\\a.h", DocumentType::CppSource, 3, 0));
    ASSERT_NE(editor, nullptr);
    EXPECT_EQ(editor->wentTo, L"3:1");
}

TEST(DocumentTabs, OpeningAtAPositionInAFileThatCantBeLoadedGivesNothing) {
    Harness h;
    h.loads = false;
    EXPECT_EQ(h.tabs->openAt(L"C:\\proj\\bad.h", DocumentType::CppSource, 1, 1), nullptr);
    EXPECT_EQ(h.tabs->count(), 0u);
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

TEST(DocumentTabs, ASaveAsInsideTheEditorMovesTheTabToTheNewFile) {
    Harness h;
    auto* editor = static_cast<FakeEditor*>(h.tabs->open(L"C:\\proj\\s1.newui", DocumentType::Designer));
    ASSERT_NE(editor, nullptr);

    editor->saveAs(L"C:\\proj\\new1.newui");

    EXPECT_EQ(h.tabs->activePath(), L"C:\\proj\\new1.newui");
    EXPECT_EQ(h.tabs->count(), 1u);
}

namespace {

namespace fs = std::filesystem;

// A real file for the tabs to compare against, and a way to change it so that its stamp differs.
class DiskFile {
public:
    DiskFile() {
        static int counter = 0;
        dir_ = fs::temp_directory_path() / ("doctabs_" + std::to_string(::GetCurrentProcessId()) + "_" + std::to_string(++counter));
        fs::create_directories(dir_);
        path_ = (dir_ / "a.cpp").wstring();
        write("one");
    }
    ~DiskFile() {
        std::error_code ec;
        fs::remove_all(dir_, ec);
    }
    void write(const std::string& text) {
        std::ofstream(path_, std::ios::binary | std::ios::trunc) << text;
        fs::last_write_time(path_, fs::last_write_time(path_) + std::chrono::seconds(++bumps_ * 5));   // a stamp that always differs
    }
    void remove() { fs::remove(path_); }
    const std::wstring& path() const { return path_; }
    // The path as the watcher reports it: UTF-8 with forward slashes.
    std::string reported() const { return newui::wideToUtf8(fs::path(path_).generic_wstring()); }

private:
    fs::path dir_;
    std::wstring path_;
    int bumps_ = 0;
};

newui::FileWatcher::Changes modified(const std::string& path) {
    newui::FileWatcher::Change change;
    change.action = newui::FileWatcher::Action::Modified;
    change.path = path;
    return { change };
}

}

TEST(DocumentTabsDiskChanges, ACleanTabIsReloadedWhenItsFileChanges) {
    Harness h;
    DiskFile file;
    auto* editor = static_cast<FakeEditor*>(h.tabs->open(file.path(), DocumentType::CppSource));
    ASSERT_NE(editor, nullptr);
    file.write("two, longer");

    h.tabs->applyDiskChanges(modified(file.reported()));

    EXPECT_EQ(editor->loadCount, 2);
    EXPECT_EQ(h.tabs->diskStateAt(0), DocumentTabs::DiskState::InSync);
}

TEST(DocumentTabsDiskChanges, ATabWithEditsKeepsThemAndSaysTheFileChanged) {
    Harness h;
    DiskFile file;
    auto* editor = static_cast<FakeEditor*>(h.tabs->open(file.path(), DocumentType::CppSource));
    editor->edit();
    file.write("two, longer");

    h.tabs->applyDiskChanges(modified(file.reported()));

    EXPECT_EQ(editor->loadCount, 1);
    EXPECT_TRUE(editor->isDirty());
    EXPECT_EQ(h.tabs->diskStateAt(0), DocumentTabs::DiskState::Changed);
    EXPECT_EQ(DocumentTabs::titleFor(file.path(), true, DocumentTabs::DiskState::Changed), "a.cpp * (changed on disk)");
}

TEST(DocumentTabsDiskChanges, TheHandlerCanChooseToReloadATabWithEdits) {
    Harness h;
    DiskFile file;
    std::wstring asked;
    h.tabs->setChangedOnDiskHandler([&asked](const std::wstring& path) {
        asked = path;
        return true;
    });
    auto* editor = static_cast<FakeEditor*>(h.tabs->open(file.path(), DocumentType::CppSource));
    editor->edit();
    file.write("two, longer");

    h.tabs->applyDiskChanges(modified(file.reported()));

    EXPECT_EQ(asked, file.path());
    EXPECT_EQ(editor->loadCount, 2);
    EXPECT_FALSE(editor->isDirty());
    EXPECT_EQ(h.tabs->diskStateAt(0), DocumentTabs::DiskState::InSync);
}

TEST(DocumentTabsDiskChanges, TheHandlerCanChooseToKeepTheEdits) {
    Harness h;
    DiskFile file;
    h.tabs->setChangedOnDiskHandler([](const std::wstring&) { return false; });
    auto* editor = static_cast<FakeEditor*>(h.tabs->open(file.path(), DocumentType::CppSource));
    editor->edit();
    file.write("two, longer");

    h.tabs->applyDiskChanges(modified(file.reported()));

    EXPECT_EQ(editor->loadCount, 1);
    EXPECT_EQ(h.tabs->diskStateAt(0), DocumentTabs::DiskState::Changed);
}

TEST(DocumentTabsDiskChanges, TheEditorsOwnSaveIsNotAnExternalChange) {
    Harness h;
    DiskFile file;
    h.tabs->setChangedOnDiskHandler([](const std::wstring&) { return true; });
    auto* editor = static_cast<FakeEditor*>(h.tabs->open(file.path(), DocumentType::CppSource));
    editor->edit();
    file.write("what the editor saved");
    editor->saved();
    h.tabs->editorSaved(editor);

    h.tabs->applyDiskChanges(modified(file.reported()));

    EXPECT_EQ(editor->loadCount, 1) << "the file is what the editor holds: nothing to reload";
    EXPECT_EQ(h.tabs->diskStateAt(0), DocumentTabs::DiskState::InSync);
}

TEST(DocumentTabsDiskChanges, ADeletedFileIsMarkedAndItsTextKept) {
    Harness h;
    DiskFile file;
    auto* editor = static_cast<FakeEditor*>(h.tabs->open(file.path(), DocumentType::CppSource));
    file.remove();

    h.tabs->applyDiskChanges(modified(file.reported()));

    EXPECT_EQ(editor->loadCount, 1);
    EXPECT_EQ(h.tabs->diskStateAt(0), DocumentTabs::DiskState::Deleted);
    EXPECT_EQ(DocumentTabs::titleFor(file.path(), false, DocumentTabs::DiskState::Deleted), "a.cpp (deleted)");
}

TEST(DocumentTabsDiskChanges, AnotherFilesChangeLeavesTheTabAlone) {
    Harness h;
    DiskFile file;
    auto* editor = static_cast<FakeEditor*>(h.tabs->open(file.path(), DocumentType::CppSource));
    file.write("two, longer");

    h.tabs->applyDiskChanges(modified("C:/somewhere/else.cpp"));

    EXPECT_EQ(editor->loadCount, 1);
}

TEST(DocumentTabsDiskChanges, ReloadsTheRightTabOfSeveral) {
    Harness h;
    DiskFile first;
    DiskFile second;
    auto* a = static_cast<FakeEditor*>(h.tabs->open(first.path(), DocumentType::CppSource));
    auto* b = static_cast<FakeEditor*>(h.tabs->open(second.path(), DocumentType::CppSource));
    second.write("changed");

    h.tabs->applyDiskChanges(modified(second.reported()));

    EXPECT_EQ(a->loadCount, 1);
    EXPECT_EQ(b->loadCount, 2);
}

TEST(DocumentTabsDiskChanges, AnEditorThatReportsItsOwnDirtyChangesNeedsNoEditorSavedCall) {
    Harness h;
    DiskFile file;
    h.tabs->setChangedOnDiskHandler([](const std::wstring&) { return true; });
    auto* editor = static_cast<FakeEditor*>(h.tabs->open(file.path(), DocumentType::Designer));
    editor->editNotifying();
    file.write("what the editor saved");
    editor->saved();   // dirty -> clean, reported

    h.tabs->applyDiskChanges(modified(file.reported()));

    EXPECT_EQ(editor->loadCount, 1);
}
