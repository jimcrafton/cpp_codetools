#include "DocumentEditService.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <memory>
#include <system_error>

#include <windows.h>

namespace CodeToolsVsix
{
    namespace
    {
        bool readFileBytes(const std::filesystem::path& path, std::string& out)
        {
            std::ifstream in(path, std::ios::binary);
            if (!in) {
                return false;
            }
            out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            return !in.bad();
        }

        // Temp file + rename, so a failed write never leaves a half-written target.
        bool writeFileBytes(const std::filesystem::path& path, const std::string& bytes)
        {
            std::filesystem::path temp = path;
            temp += ".codetools.tmp";
            {
                std::ofstream out(temp, std::ios::binary | std::ios::trunc);
                if (!out) {
                    return false;
                }
                out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
                out.flush();
                if (!out) {
                    out.close();
                    std::error_code ignored;
                    std::filesystem::remove(temp, ignored);
                    return false;
                }
            }
            std::error_code ec;
            std::filesystem::rename(temp, path, ec);
            if (ec) {
                std::filesystem::remove(temp, ec);
                return false;
            }
            return true;
        }

        struct GroupState
        {
            DocumentEditService* service = nullptr;
            std::vector<FileEdits> files;
            std::vector<DocumentEditService::ResolvedText> resolved;
            DocumentEditService::GroupDone done;
        };
    }

    std::uint64_t versionOfFileBytes(const std::string& bytes)
    {
        std::uint64_t hash = 1469598103934665603ull;  // FNV-1a 64
        for (unsigned char c : bytes) {
            hash ^= c;
            hash *= 1099511628211ull;
        }
        return hash;
    }

    namespace
    {
        // Between the two halves of a surrogate pair?
        bool splitsSurrogatePair(const std::wstring& text, std::size_t pos)
        {
            return pos > 0 && pos < text.size() && text[pos - 1] >= 0xD800 && text[pos - 1] <= 0xDBFF &&
                   text[pos] >= 0xDC00 && text[pos] <= 0xDFFF;
        }
    }

    bool applyTextEdits(std::wstring& text, const std::vector<TextEdit>& edits)
    {
        std::vector<const TextEdit*> ordered;
        ordered.reserve(edits.size());
        for (const TextEdit& e : edits) {
            ordered.push_back(&e);
        }
        std::stable_sort(ordered.begin(), ordered.end(),
                         [](const TextEdit* a, const TextEdit* b) { return a->offset < b->offset; });

        std::size_t previousEnd = 0;
        for (const TextEdit* e : ordered) {
            if (e->offset > text.size() || e->length > text.size() - e->offset || e->offset < previousEnd ||
                splitsSurrogatePair(text, e->offset) || splitsSurrogatePair(text, e->offset + e->length)) {
                return false;
            }
            previousEnd = e->offset + e->length;
        }
        // Descending, so an earlier edit's offset is never shifted by a later one already applied.
        for (auto it = ordered.rbegin(); it != ordered.rend(); ++it) {
            text.replace((*it)->offset, (*it)->length, (*it)->text);
        }
        return true;
    }

    namespace
    {
        const std::string kUtf8Bom("\xEF\xBB\xBF", 3);

        // Strict: false on invalid UTF-8 rather than substituting characters and later writing
        // those substitutions back over the user's file.
        bool decodeUtf8(const std::string& bytes, std::wstring& out)
        {
            out.clear();
            if (bytes.empty()) {
                return true;
            }
            const int chars = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(),
                                                  static_cast<int>(bytes.size()), nullptr, 0);
            if (chars <= 0) {
                return false;
            }
            out.assign(static_cast<std::size_t>(chars), L'\0');
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()),
                                out.data(), chars);
            return true;
        }

        bool encodeUtf8(const std::wstring& text, std::string& out)
        {
            out.clear();
            if (text.empty()) {
                return true;
            }
            const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                                                  static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
            if (bytes <= 0) {
                return false;  // e.g. a lone surrogate
            }
            out.assign(static_cast<std::size_t>(bytes), '\0');
            WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                                out.data(), bytes, nullptr, nullptr);
            return true;
        }

        bool hasBom(const std::string& bytes)
        {
            return bytes.compare(0, 3, kUtf8Bom) == 0 && bytes.size() >= 3;
        }
    }

    namespace
    {
        // The file as a snapshot, plus its exact bytes (for restoring it after a failed group).
        void diskRead(const std::filesystem::path& path, DocumentSnapshot& out, std::string& bytes, EditStatus& status)
        {
            std::error_code ec;
            if (!std::filesystem::is_regular_file(path, ec)) {
                status = EditStatus::NotFound;
                return;
            }
            if (!readFileBytes(path, bytes)) {
                status = EditStatus::IoError;
                return;
            }
            if (!decodeUtf8(bytes.substr(hasBom(bytes) ? 3 : 0), out.text)) {
                status = EditStatus::IoError;
                return;
            }
            out.version = versionOfFileBytes(bytes);
            status = EditStatus::Ok;
        }
    }

    void diskGetText(const std::filesystem::path& path, DocumentSnapshot& out, EditStatus& status)
    {
        std::string bytes;
        diskRead(path, out, bytes, status);
    }

    EditStatus diskApplyEdits(const std::filesystem::path& path, std::uint64_t expectedVersion,
                              const std::vector<TextEdit>& edits)
    {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(path, ec)) {
            return EditStatus::NotFound;
        }
        std::string bytes;
        if (!readFileBytes(path, bytes)) {
            return EditStatus::IoError;
        }
        if (versionOfFileBytes(bytes) != expectedVersion) {
            return EditStatus::VersionMismatch;
        }
        const bool bom = hasBom(bytes);
        std::wstring text;
        if (!decodeUtf8(bytes.substr(bom ? 3 : 0), text)) {
            return EditStatus::IoError;
        }
        if (!applyTextEdits(text, edits)) {
            return EditStatus::InvalidEdit;
        }
        std::string encoded;
        if (!encodeUtf8(text, encoded)) {
            return EditStatus::InvalidEdit;
        }
        return writeFileBytes(path, (bom ? kUtf8Bom : std::string()) + encoded) ? EditStatus::Ok : EditStatus::IoError;
    }

    std::string DocumentEditService::keyFor(const std::filesystem::path& path)
    {
        std::error_code ec;
        std::filesystem::path canonical = std::filesystem::weakly_canonical(path, ec);
        std::string key = (ec ? path : canonical).generic_u8string();
        // Windows paths are case-insensitive.
        std::transform(key.begin(), key.end(), key.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return key;
    }

    void DocumentEditService::registerEditor(const std::filesystem::path& path, IEditableDocument* editor)
    {
        editors_[keyFor(path)] = editor;
    }

    void DocumentEditService::unregisterEditor(const std::filesystem::path& path, IEditableDocument* editor)
    {
        auto it = editors_.find(keyFor(path));
        if (it != editors_.end() && it->second == editor) {
            editors_.erase(it);
        }
    }

    namespace
    {
        void applyNext(std::shared_ptr<GroupState> state, std::size_t index);

        void rollbackAndFinish(const std::shared_ptr<GroupState>& state, std::size_t failedIndex, EditStatus status)
        {
            std::size_t kept = failedIndex;
            for (std::size_t i = failedIndex; i-- > 0;) {
                if (state->resolved[i].onDisk &&
                    writeFileBytes(state->files[i].path, state->resolved[i].diskBytes)) {
                    --kept;
                }
            }
            state->done(status, kept);
        }

        void applyNext(std::shared_ptr<GroupState> state, std::size_t index)
        {
            if (index == state->files.size()) {
                state->done(EditStatus::Ok, state->files.size());
                return;
            }
            FileEdits& file = state->files[index];
            state->service->applyEdits(file.path, file.expectedVersion, file.edits,
                                       [state, index](EditStatus status) {
                                           if (status != EditStatus::Ok) {
                                               rollbackAndFinish(state, index, status);
                                               return;
                                           }
                                           applyNext(state, index + 1);
                                       });
        }

        void checkNext(std::shared_ptr<GroupState> state, std::size_t index)
        {
            if (index == state->files.size()) {
                applyNext(state, 0);
                return;
            }
            state->service->resolve(state->files[index].path, [state, index](DocumentEditService::ResolvedText r) {
                if (r.status != EditStatus::Ok) {
                    state->done(r.status, 0);
                    return;
                }
                const FileEdits& file = state->files[index];
                if (r.snapshot.version != file.expectedVersion) {
                    state->done(EditStatus::VersionMismatch, 0);
                    return;
                }
                std::wstring dryRun = r.snapshot.text;
                if (!applyTextEdits(dryRun, file.edits)) {
                    state->done(EditStatus::InvalidEdit, 0);
                    return;
                }
                state->resolved[index] = std::move(r);
                checkNext(state, index + 1);
            });
        }
    }

    void DocumentEditService::resolve(const std::filesystem::path& path, std::function<void(ResolvedText)> done)
    {
        auto it = editors_.find(keyFor(path));
        if (it != editors_.end()) {
            ResolvedText r;
            r.snapshot = it->second->snapshot();
            done(std::move(r));
            return;
        }
        auto fromDisk = [path, done]() {
            ResolvedText r;
            r.onDisk = true;
            diskRead(path, r.snapshot, r.diskBytes, r.status);
            done(std::move(r));
        };
        if (host_ == nullptr) {
            fromDisk();
            return;
        }
        host_->getText(path, [done, fromDisk](EditStatus status, DocumentSnapshot snapshot) {
            if (status == EditStatus::NotOpen) {
                fromDisk();
                return;
            }
            ResolvedText r;
            r.status = status;
            r.snapshot = std::move(snapshot);
            done(std::move(r));
        });
    }

    void DocumentEditService::getText(const std::filesystem::path& path, SnapshotDone done)
    {
        resolve(path, [done](ResolvedText r) { done(r.status, std::move(r.snapshot)); });
    }

    void DocumentEditService::applyEdits(const std::filesystem::path& path, std::uint64_t expectedVersion,
                                         std::vector<TextEdit> edits, EditDone done)
    {
        auto it = editors_.find(keyFor(path));
        if (it != editors_.end()) {
            done(it->second->applyEdits(expectedVersion, edits));
            return;
        }
        if (host_ == nullptr) {
            done(diskApplyEdits(path, expectedVersion, edits));
            return;
        }
        host_->applyEdits(path, expectedVersion, edits,
                          [path, expectedVersion, edits, done](EditStatus status) {
                              done(status == EditStatus::NotOpen ? diskApplyEdits(path, expectedVersion, edits)
                                                                 : status);
                          });
    }

    void DocumentEditService::applyGroup(std::vector<FileEdits> files, GroupDone done)
    {
        auto state = std::make_shared<GroupState>();
        state->service = this;
        state->files = std::move(files);
        state->resolved.resize(state->files.size());
        state->done = std::move(done);
        if (state->files.empty()) {
            state->done(EditStatus::Ok, 0);
            return;
        }
        checkNext(state, 0);
    }

    DocumentEditService& documentEditService()
    {
        static DocumentEditService instance;
        return instance;
    }
}
