#include <windows.h>

#include <gtest/gtest.h>

#include <atomic>
#include <fstream>
#include <iterator>

#include "../extension/NativeEditControls/DocumentEditService.h"

using namespace CodeToolsVsix;

namespace {

class TempDir {
public:
    TempDir() {
        static std::atomic<int> counter{0};
        path_ = std::filesystem::temp_directory_path() /
                ("docedit_test_" + std::to_string(::GetCurrentProcessId()) + "_" + std::to_string(counter++));
        std::filesystem::create_directories(path_);
    }
    ~TempDir() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    // Files are written and read as raw bytes (UTF-8), exactly as they sit on disk.
    std::filesystem::path write(const std::string& name, const std::string& bytes) const {
        std::filesystem::path p = path_ / name;
        std::ofstream(p, std::ios::binary) << bytes;
        return p;
    }
    static std::string read(const std::filesystem::path& p) {
        std::ifstream in(p, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

class FakeEditor : public IEditableDocument {
public:
    explicit FakeEditor(std::wstring text) : text_(std::move(text)) {}
    DocumentSnapshot snapshot() const override { return {text_, version_}; }
    EditStatus applyEdits(std::uint64_t expectedVersion, const std::vector<TextEdit>& edits) override {
        ++applyCalls;
        if (reject) {
            return EditStatus::Rejected;
        }
        if (expectedVersion != version_) {
            return EditStatus::VersionMismatch;
        }
        if (!applyTextEdits(text_, edits)) {
            return EditStatus::InvalidEdit;
        }
        ++version_;
        return EditStatus::Ok;
    }
    const std::wstring& text() const { return text_; }
    bool reject = false;
    int applyCalls = 0;

private:
    std::wstring text_;
    std::uint64_t version_ = 1;
};

// Completes on demand, so tests can prove the service copes with a host that answers later.
class FakeHost : public IHostDocumentEditor {
public:
    void getText(const std::filesystem::path&, std::function<void(EditStatus, DocumentSnapshot)> done) override {
        ++getCalls;
        pendingGet = [this, done] { done(getStatus, getSnapshot); };
        if (!deferred) {
            flush();
        }
    }
    void applyEdits(const std::filesystem::path&, std::uint64_t, std::vector<TextEdit>,
                    std::function<void(EditStatus)> done) override {
        ++applyCalls;
        pendingApply = [this, done] { done(applyStatus); };
        if (!deferred) {
            flush();
        }
    }
    void flush() {
        if (auto f = std::move(pendingGet)) { pendingGet = nullptr; f(); }
        if (auto f = std::move(pendingApply)) { pendingApply = nullptr; f(); }
    }
    EditStatus getStatus = EditStatus::NotOpen;
    DocumentSnapshot getSnapshot;
    EditStatus applyStatus = EditStatus::NotOpen;
    bool deferred = false;
    int getCalls = 0;
    int applyCalls = 0;
    std::function<void()> pendingGet, pendingApply;
};

struct Outcome {
    bool called = false;
    EditStatus status = EditStatus::Ok;
    std::size_t applied = 0;
};

std::uint64_t versionOf(const std::string& bytes) { return versionOfFileBytes(bytes); }

}  // namespace

// ---- applyTextEdits ----

TEST(ApplyTextEdits, InsertsAndReplacesAgainstOriginalOffsets) {
    std::wstring text = L"abcdef";
    ASSERT_TRUE(applyTextEdits(text, {{1, 0, L"X"}, {3, 2, L"YY"}}));
    EXPECT_EQ(text, L"aXbcYYf");
}

TEST(ApplyTextEdits, SameOffsetInsertionsKeepTheirGivenOrder) {
    std::wstring text = L"ab";
    ASSERT_TRUE(applyTextEdits(text, {{1, 0, L"1"}, {1, 0, L"2"}}));
    EXPECT_EQ(text, L"a12b");
}

TEST(ApplyTextEdits, RejectsOverlapAndOutOfRangeWithoutChangingTheText) {
    std::wstring text = L"abcdef";
    EXPECT_FALSE(applyTextEdits(text, {{1, 3, L"X"}, {2, 1, L"Y"}}));
    EXPECT_FALSE(applyTextEdits(text, {{7, 0, L"X"}}));
    EXPECT_FALSE(applyTextEdits(text, {{4, 5, L"X"}}));
    EXPECT_EQ(text, L"abcdef");
}

TEST(ApplyTextEdits, RefusesToSplitASurrogatePair) {
    std::wstring text = L"a\U0001F600b";  // a, a two-unit emoji, b
    ASSERT_EQ(text.size(), 4u);
    EXPECT_FALSE(applyTextEdits(text, {{2, 0, L"x"}}));   // between the halves
    EXPECT_FALSE(applyTextEdits(text, {{1, 1, L"x"}}));   // range ends between the halves
    EXPECT_TRUE(applyTextEdits(text, {{1, 2, L"E"}}));    // replacing the whole pair is fine
    EXPECT_EQ(text, L"aEb");
}

// ---- disk route ----

TEST(DocumentEditServiceDisk, MissingFileIsNotFound) {
    TempDir dir;
    DocumentEditService service;
    Outcome o;
    service.getText(dir.path() / "nope.h", [&](EditStatus s, DocumentSnapshot) { o.called = true; o.status = s; });
    EXPECT_TRUE(o.called);
    EXPECT_EQ(o.status, EditStatus::NotFound);
}

TEST(DocumentEditServiceDisk, ReadsUtf8AsUtf16WithoutTheBom) {
    TempDir dir;
    auto file = dir.write("a.h", "\xEF\xBB\xBF" "caf\xC3\xA9 \xF0\x9F\x98\x80 x");
    DocumentEditService service;

    DocumentSnapshot snap;
    service.getText(file, [&](EditStatus, DocumentSnapshot s) { snap = std::move(s); });
    EXPECT_EQ(snap.text, L"caf\u00E9 \U0001F600 x");
}

TEST(DocumentEditServiceDisk, AppliesUtf16EditsAndKeepsTheBomCrlfAndNonAscii) {
    TempDir dir;
    const std::string original = "\xEF\xBB\xBF" "caf\xC3\xA9 \xF0\x9F\x98\x80 x\r\nline2\r\n";
    auto file = dir.write("a.h", original);
    DocumentEditService service;

    DocumentSnapshot snap;
    service.getText(file, [&](EditStatus, DocumentSnapshot s) { snap = std::move(s); });
    const std::size_t x = snap.text.find(L'x');
    ASSERT_EQ(x, 8u);  // c a f e-acute space (emoji: 2 units) space x

    Outcome o;
    service.applyEdits(file, snap.version, {{x, 1, L"yy"}}, [&](EditStatus s) { o.called = true; o.status = s; });
    EXPECT_EQ(o.status, EditStatus::Ok);
    EXPECT_EQ(TempDir::read(file), "\xEF\xBB\xBF" "caf\xC3\xA9 \xF0\x9F\x98\x80 yy\r\nline2\r\n");
}

TEST(DocumentEditServiceDisk, AFileWithoutABomStaysWithoutOne) {
    TempDir dir;
    auto file = dir.write("a.h", "abc");
    DocumentEditService service;
    DocumentSnapshot snap;
    service.getText(file, [&](EditStatus, DocumentSnapshot s) { snap = std::move(s); });
    Outcome o;
    service.applyEdits(file, snap.version, {{0, 0, L"x"}}, [&](EditStatus s) { o.status = s; });
    EXPECT_EQ(TempDir::read(file), "xabc");
}

TEST(DocumentEditServiceDisk, StaleVersionAndInvalidEditsLeaveTheFileUntouched) {
    TempDir dir;
    auto file = dir.write("a.h", "hello");
    DocumentEditService service;

    Outcome stale;
    service.applyEdits(file, 12345, {{0, 0, L"x"}}, [&](EditStatus s) { stale.status = s; });
    EXPECT_EQ(stale.status, EditStatus::VersionMismatch);

    Outcome invalid;
    service.applyEdits(file, versionOf("hello"), {{99, 0, L"x"}}, [&](EditStatus s) { invalid.status = s; });
    EXPECT_EQ(invalid.status, EditStatus::InvalidEdit);

    EXPECT_EQ(TempDir::read(file), "hello");
}

TEST(DocumentEditServiceDisk, InvalidUtf8IsRefusedAndNeverRewritten) {
    TempDir dir;
    const std::string bytes = "ab\xFF\xFE" "cd";
    auto file = dir.write("latin.h", bytes);
    DocumentEditService service;

    Outcome read;
    service.getText(file, [&](EditStatus s, DocumentSnapshot) { read.status = s; });
    EXPECT_EQ(read.status, EditStatus::IoError);

    Outcome edit;
    service.applyEdits(file, versionOf(bytes), {{0, 0, L"x"}}, [&](EditStatus s) { edit.status = s; });
    EXPECT_EQ(edit.status, EditStatus::IoError);
    EXPECT_EQ(TempDir::read(file), bytes);
}

TEST(DocumentEditServiceDisk, ATextThatCannotBeEncodedIsRefusedNotWrittenLossily) {
    TempDir dir;
    auto file = dir.write("a.h", "ab");
    DocumentEditService service;
    DocumentSnapshot snap;
    service.getText(file, [&](EditStatus, DocumentSnapshot s) { snap = std::move(s); });

    Outcome o;
    service.applyEdits(file, snap.version, {{1, 0, std::wstring(1, static_cast<wchar_t>(0xD800))}},  // a lone surrogate
                       [&](EditStatus s) { o.status = s; });
    EXPECT_EQ(o.status, EditStatus::InvalidEdit);
    EXPECT_EQ(TempDir::read(file), "ab");
}

// ---- routing ----

TEST(DocumentEditServiceRouting, AnOpenEditorWinsOverTheDiskCopyAndDiskIsNotTouched) {
    TempDir dir;
    auto file = dir.write("a.h", "on disk");
    FakeEditor editor(L"in editor (unsaved)");
    DocumentEditService service;
    service.registerEditor(file, &editor);

    DocumentSnapshot snap;
    service.getText(file, [&](EditStatus, DocumentSnapshot s) { snap = std::move(s); });
    EXPECT_EQ(snap.text, L"in editor (unsaved)");

    Outcome o;
    service.applyEdits(file, snap.version, {{0, 0, L">"}}, [&](EditStatus s) { o.status = s; });
    EXPECT_EQ(o.status, EditStatus::Ok);
    EXPECT_EQ(editor.text(), L">in editor (unsaved)");
    EXPECT_EQ(TempDir::read(file), "on disk");
}

TEST(DocumentEditServiceRouting, PathLookupIgnoresCaseAndUnregisterOnlyRemovesTheSameEditor) {
    TempDir dir;
    auto file = dir.write("Widget.h", "x");
    FakeEditor first(L"first"), second(L"second");
    DocumentEditService service;
    service.registerEditor(file, &first);

    std::filesystem::path upper = dir.path() / "WIDGET.H";
    DocumentSnapshot snap;
    service.getText(upper, [&](EditStatus, DocumentSnapshot s) { snap = std::move(s); });
    EXPECT_EQ(snap.text, L"first");

    service.registerEditor(file, &second);
    service.unregisterEditor(file, &first);  // stale: second is registered now
    service.getText(file, [&](EditStatus, DocumentSnapshot s) { snap = std::move(s); });
    EXPECT_EQ(snap.text, L"second");

    service.unregisterEditor(file, &second);
    service.getText(file, [&](EditStatus, DocumentSnapshot s) { snap = std::move(s); });
    EXPECT_EQ(snap.text, L"x");  // back to disk
}

TEST(DocumentEditServiceRouting, HostNotOpenFallsThroughToDisk) {
    TempDir dir;
    auto file = dir.write("a.h", "disk");
    FakeHost host;  // answers NotOpen
    DocumentEditService service;
    service.setHost(&host);

    DocumentSnapshot snap;
    service.getText(file, [&](EditStatus, DocumentSnapshot s) { snap = std::move(s); });
    EXPECT_EQ(snap.text, L"disk");

    Outcome o;
    service.applyEdits(file, snap.version, {{0, 0, L"!"}}, [&](EditStatus s) { o.status = s; });
    EXPECT_EQ(o.status, EditStatus::Ok);
    EXPECT_EQ(TempDir::read(file), "!disk");
    EXPECT_EQ(host.applyCalls, 1);
}

TEST(DocumentEditServiceRouting, HostThatOwnsTheFileIsUsedAndAnAsyncAnswerIsAwaited) {
    TempDir dir;
    auto file = dir.write("a.h", "disk");
    FakeHost host;
    host.deferred = true;
    host.getStatus = EditStatus::Ok;
    host.getSnapshot = {L"vs buffer", 7};
    host.applyStatus = EditStatus::Ok;
    DocumentEditService service;
    service.setHost(&host);

    Outcome got;
    DocumentSnapshot snap;
    service.getText(file, [&](EditStatus s, DocumentSnapshot d) { got.called = true; got.status = s; snap = std::move(d); });
    EXPECT_FALSE(got.called);  // still waiting on the host
    host.flush();
    EXPECT_TRUE(got.called);
    EXPECT_EQ(snap.text, L"vs buffer");

    Outcome applied;
    service.applyEdits(file, 7, {{0, 0, L"x"}}, [&](EditStatus s) { applied.called = true; applied.status = s; });
    EXPECT_FALSE(applied.called);
    host.flush();
    EXPECT_EQ(applied.status, EditStatus::Ok);
    EXPECT_EQ(TempDir::read(file), "disk");  // the host's buffer was edited, never the file
}

// ---- groups ----

TEST(DocumentEditServiceGroup, AppliesEveryFile) {
    TempDir dir;
    auto h = dir.write("a.h", "class A {};");
    auto cpp = dir.write("a.cpp", "// a");
    DocumentEditService service;

    Outcome o;
    service.applyGroup({{h, versionOf("class A {};"), {{9, 0, L" int x; "}}},
                        {cpp, versionOf("// a"), {{4, 0, L"\n// b"}}}},
                       [&](EditStatus s, std::size_t n) { o.called = true; o.status = s; o.applied = n; });
    EXPECT_EQ(o.status, EditStatus::Ok);
    EXPECT_EQ(o.applied, 2u);
    EXPECT_EQ(TempDir::read(h), "class A { int x; };");
    EXPECT_EQ(TempDir::read(cpp), "// a\n// b");
}

TEST(DocumentEditServiceGroup, ASecondFilesStaleVersionChangesNothing) {
    TempDir dir;
    auto h = dir.write("a.h", "one");
    auto cpp = dir.write("a.cpp", "two");
    DocumentEditService service;

    Outcome o;
    service.applyGroup({{h, versionOf("one"), {{0, 0, L"X"}}}, {cpp, 999, {{0, 0, L"Y"}}}},
                       [&](EditStatus s, std::size_t n) { o.status = s; o.applied = n; });
    EXPECT_EQ(o.status, EditStatus::VersionMismatch);
    EXPECT_EQ(o.applied, 0u);
    EXPECT_EQ(TempDir::read(h), "one");
    EXPECT_EQ(TempDir::read(cpp), "two");
}

TEST(DocumentEditServiceGroup, ALaterFailureRestoresDiskFilesButNotOpenEditors) {
    TempDir dir;
    auto h = dir.write("a.h", "disk");
    FakeEditor ok(L"editor ok"), bad(L"editor bad");
    bad.reject = true;
    auto okPath = dir.write("ok.h", "x");
    auto badPath = dir.write("bad.h", "y");
    DocumentEditService service;
    service.registerEditor(okPath, &ok);
    service.registerEditor(badPath, &bad);

    Outcome o;
    // disk file, then an editor that accepts, then an editor that rejects.
    service.applyGroup({{h, versionOf("disk"), {{0, 0, L"!"}}},
                        {okPath, 1, {{0, 0, L">"}}},
                        {badPath, 1, {{0, 0, L"?"}}}},
                       [&](EditStatus s, std::size_t n) { o.called = true; o.status = s; o.applied = n; });
    EXPECT_EQ(o.status, EditStatus::Rejected);
    EXPECT_EQ(TempDir::read(h), "disk");          // restored
    EXPECT_EQ(ok.text(), L">editor ok");          // an open editor is left changed
    EXPECT_EQ(o.applied, 1u);                     // only the editor edit is still in place
}

TEST(DocumentEditServiceGroup, RollbackRestoresTheExactOriginalBytesIncludingBomAndCrlf) {
    TempDir dir;
    const std::string original = "\xEF\xBB\xBF" "caf\xC3\xA9 \xF0\x9F\x98\x80\r\nline2\r\n";
    auto h = dir.write("a.h", original);
    FakeEditor bad(L"editor");
    bad.reject = true;
    auto badPath = dir.write("bad.h", "y");
    DocumentEditService service;
    service.registerEditor(badPath, &bad);

    DocumentSnapshot snap;
    service.getText(h, [&](EditStatus, DocumentSnapshot s) { snap = std::move(s); });

    Outcome o;
    service.applyGroup({{h, snap.version, {{0, 0, L"// planned\n"}}}, {badPath, 1, {{0, 0, L"?"}}}},
                       [&](EditStatus s, std::size_t n) { o.status = s; o.applied = n; });
    EXPECT_EQ(o.status, EditStatus::Rejected);
    EXPECT_EQ(o.applied, 0u);
    EXPECT_EQ(TempDir::read(h), original) << "byte-for-byte, BOM included";
}

TEST(DocumentEditServiceGroup, AnEmptyGroupSucceeds) {
    DocumentEditService service;
    Outcome o;
    service.applyGroup({}, [&](EditStatus s, std::size_t n) { o.called = true; o.status = s; o.applied = n; });
    EXPECT_TRUE(o.called);
    EXPECT_EQ(o.status, EditStatus::Ok);
    EXPECT_EQ(o.applied, 0u);
}
