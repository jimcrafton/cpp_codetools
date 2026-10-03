#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace CodeToolsVsix
{
    // One replacement in a document's text. Everything here is UTF-16, like the editor's own
    // model, VS's ITextBuffer and .NET strings: offset/length are UTF-16 code unit indices into
    // the snapshot the edit was planned against. length 0 = insert. (Only the codegen planner and
    // the file on disk are UTF-8; they convert at their own edge - see CodegenEditAdapter.h and the
    // disk route.)
    struct TextEdit
    {
        std::size_t offset = 0;
        std::size_t length = 0;
        std::wstring text;
    };

    // A document's text plus an opaque version that changes whenever the text does. An edit is
    // only applied if its expectedVersion still matches - a plan made against stale text fails
    // with VersionMismatch instead of splicing at the wrong offsets.
    struct DocumentSnapshot
    {
        std::wstring text;
        std::uint64_t version = 0;
    };

    enum class EditStatus
    {
        Ok,
        NotOpen,         // host only: the file isn't open in VS's native editor
        NotFound,         // not open anywhere and not on disk
        VersionMismatch,  // the text changed since it was read
        InvalidEdit,      // out of range or overlapping; nothing was changed
        IoError,
        Rejected,         // the editor/host refused the change
    };

    // An open editor that can be edited in place. Only ever called on the edit thread.
    class IEditableDocument
    {
    public:
        virtual ~IEditableDocument() = default;
        virtual DocumentSnapshot snapshot() const = 0;
        // All edits land as one undo step. Changes nothing on any non-Applied result.
        virtual EditStatus applyEdits(std::uint64_t expectedVersion, const std::vector<TextEdit>& edits) = 0;
    };

    // The VS side, for files open in VS's own text editor (the C# host owns those buffers). Async
    // on purpose: VS's UI thread can be blocked waiting on the edit thread, so the edit thread must
    // never block waiting on the UI thread. `done` may run later, on the edit thread; it must run
    // exactly once. Answer NotOpen for a file VS's native editor doesn't have open.
    class IHostDocumentEditor
    {
    public:
        virtual ~IHostDocumentEditor() = default;
        virtual void getText(const std::filesystem::path& path,
                             std::function<void(EditStatus, DocumentSnapshot)> done) = 0;
        virtual void applyEdits(const std::filesystem::path& path, std::uint64_t expectedVersion,
                                std::vector<TextEdit> edits, std::function<void(EditStatus)> done) = 0;
    };

    struct FileEdits
    {
        std::filesystem::path path;
        std::uint64_t expectedVersion = 0;
        std::vector<TextEdit> edits;
    };

    // Applies edits to text. False (text untouched) if any is out of range, overlaps another, or
    // starts/ends between the two halves of a surrogate pair.
    bool applyTextEdits(std::wstring& text, const std::vector<TextEdit>& edits);

    // Routes reads and edits of a file to wherever its live text is: one of our own open editors
    // (registered by path), else VS's native editor via the host, else the file on disk. Edit
    // thread only. Completions of the editor and disk routes run before the call returns; the host
    // route may complete later.
    class DocumentEditService
    {
    public:
        using SnapshotDone = std::function<void(EditStatus, DocumentSnapshot)>;
        using EditDone = std::function<void(EditStatus)>;

        // A read, plus whether the text came from the file on disk (as opposed to an open editor
        // or the host) - only disk files can be restored by a failed group.
        struct ResolvedText
        {
            EditStatus status = EditStatus::Ok;
            DocumentSnapshot snapshot;
            bool onDisk = false;
            std::string diskBytes;  // onDisk only: the file's exact bytes, for restoring it
        };
        void resolve(const std::filesystem::path& path, std::function<void(ResolvedText)> done);

        void registerEditor(const std::filesystem::path& path, IEditableDocument* editor);
        // Only removes the entry if it is still `editor` (a re-registered path is left alone).
        void unregisterEditor(const std::filesystem::path& path, IEditableDocument* editor);
        void setHost(IHostDocumentEditor* host) { host_ = host; }

        void getText(const std::filesystem::path& path, SnapshotDone done);
        void applyEdits(const std::filesystem::path& path, std::uint64_t expectedVersion,
                        std::vector<TextEdit> edits, EditDone done);

        // Several files as one unit: every file's version is checked and its edits validated
        // before any is changed. If a later file then fails, already-changed files that are on disk
        // are restored (open editors and host files are not - they are only ever left changed by
        // an I/O or host failure after the checks passed). `appliedCount` is how many files were
        // changed and kept.
        using GroupDone = std::function<void(EditStatus, std::size_t appliedCount)>;
        void applyGroup(std::vector<FileEdits> files, GroupDone done);

    private:
        static std::string keyFor(const std::filesystem::path& path);

        std::map<std::string, IEditableDocument*> editors_;
        IHostDocumentEditor* host_ = nullptr;
    };

    // The process-wide instance the editors register with.
    DocumentEditService& documentEditService();

    // The disk route on its own. The file is UTF-8 (an optional BOM is kept across an edit, and
    // line endings are untouched); a file that isn't valid UTF-8 is IoError, never rewritten. The
    // version is a hash of the file's bytes.
    void diskGetText(const std::filesystem::path& path, DocumentSnapshot& out, EditStatus& status);
    EditStatus diskApplyEdits(const std::filesystem::path& path, std::uint64_t expectedVersion,
                              const std::vector<TextEdit>& edits);
    std::uint64_t versionOfFileBytes(const std::string& bytes);
}
