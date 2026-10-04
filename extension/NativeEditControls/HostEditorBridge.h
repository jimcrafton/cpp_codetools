#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>

#include "DocumentEditService.h"

// The C ABI between DocumentEditService and the managed VS host (see HostServices::getText / applyEdits
// in HostServices.h). Requests go out as callbacks tagged with a requestId and are answered
// later through the *Reply exports - never synchronously, since VS's UI thread can be blocked
// waiting on the edit thread. All text is UTF-16 (the editor's, VS's and .NET's own), and offsets
// are UTF-16 code unit indices. Strings are never assumed null-terminated; every pointer is only
// valid for the duration of the call.
extern "C"
{
    struct HostTextEdit
    {
        std::uint64_t offset;      // UTF-16 index into the snapshot the plan was made against
        std::uint64_t length;
        const wchar_t* text;
        std::uint64_t textLength;  // in wchar_t units
    };

    typedef void(__stdcall* HostGetTextCallback)(std::uint64_t requestId, const wchar_t* path, std::size_t pathLength);
    typedef void(__stdcall* HostApplyEditsCallback)(std::uint64_t requestId, const wchar_t* path, std::size_t pathLength,
                                                    std::uint64_t expectedVersion, const HostTextEdit* edits,
                                                    std::size_t editCount);
}

namespace CodeToolsVsix
{
    // IHostDocumentEditor over those callbacks. All members except replyGetText/replyApply are
    // edit-thread only; the two replies may come from any thread and hop onto the edit thread via
    // `post` before touching anything.
    class HostEditorBridge : public IHostDocumentEditor
    {
    public:
        using Poster = std::function<void(std::function<void()>)>;  // runs the task on the edit thread

        explicit HostEditorBridge(Poster post) : post_(std::move(post)) {}

        // Both null disconnects: anything still waiting fails with Rejected.
        void setCallbacks(HostGetTextCallback getText, HostApplyEditsCallback applyEdits);
        bool connected() const { return getCallback_ != nullptr && applyCallback_ != nullptr; }

        void getText(const std::filesystem::path& path,
                     std::function<void(EditStatus, DocumentSnapshot)> done) override;
        void applyEdits(const std::filesystem::path& path, std::uint64_t expectedVersion,
                        std::vector<TextEdit> edits, std::function<void(EditStatus)> done) override;

        // Any thread. Unknown or already-answered ids are ignored.
        void replyGetText(std::uint64_t requestId, EditStatus status, std::wstring text, std::uint64_t version);
        void replyApply(std::uint64_t requestId, EditStatus status);

    private:
        void failAllPending(EditStatus status);

        Poster post_;
        HostGetTextCallback getCallback_ = nullptr;
        HostApplyEditsCallback applyCallback_ = nullptr;
        std::uint64_t nextId_ = 1;
        std::map<std::uint64_t, std::function<void(EditStatus, DocumentSnapshot)>> pendingGet_;
        std::map<std::uint64_t, std::function<void(EditStatus)>> pendingApply_;
    };
}
