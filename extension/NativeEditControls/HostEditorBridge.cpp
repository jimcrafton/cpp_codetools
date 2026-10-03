#include "HostEditorBridge.h"

namespace CodeToolsVsix
{
    void HostEditorBridge::setCallbacks(HostGetTextCallback getText, HostApplyEditsCallback applyEdits)
    {
        getCallback_ = getText;
        applyCallback_ = applyEdits;
        if (!connected()) {
            failAllPending(EditStatus::Rejected);
        }
    }

    void HostEditorBridge::failAllPending(EditStatus status)
    {
        // Move out first: a completion may issue new requests.
        auto gets = std::move(pendingGet_);
        auto applies = std::move(pendingApply_);
        pendingGet_.clear();
        pendingApply_.clear();
        for (auto& entry : gets) {
            entry.second(status, DocumentSnapshot{});
        }
        for (auto& entry : applies) {
            entry.second(status);
        }
    }

    void HostEditorBridge::getText(const std::filesystem::path& path,
                                   std::function<void(EditStatus, DocumentSnapshot)> done)
    {
        if (!connected()) {
            done(EditStatus::NotOpen, DocumentSnapshot{});
            return;
        }
        const std::uint64_t id = nextId_++;
        pendingGet_[id] = std::move(done);
        const std::wstring wide = path.wstring();
        getCallback_(id, wide.data(), wide.size());
    }

    void HostEditorBridge::applyEdits(const std::filesystem::path& path, std::uint64_t expectedVersion,
                                      std::vector<TextEdit> edits, std::function<void(EditStatus)> done)
    {
        if (!connected()) {
            done(EditStatus::NotOpen);
            return;
        }
        const std::uint64_t id = nextId_++;
        pendingApply_[id] = std::move(done);
        const std::wstring wide = path.wstring();
        std::vector<HostTextEdit> raw;
        raw.reserve(edits.size());
        for (const TextEdit& edit : edits) {
            raw.push_back({edit.offset, edit.length, edit.text.data(), edit.text.size()});
        }
        applyCallback_(id, wide.data(), wide.size(), expectedVersion, raw.data(), raw.size());
    }

    void HostEditorBridge::replyGetText(std::uint64_t requestId, EditStatus status, std::wstring text,
                                        std::uint64_t version)
    {
        post_([this, requestId, status, text = std::move(text), version]() mutable {
            auto it = pendingGet_.find(requestId);
            if (it == pendingGet_.end()) {
                return;
            }
            auto done = std::move(it->second);
            pendingGet_.erase(it);
            done(status, DocumentSnapshot{std::move(text), version});
        });
    }

    void HostEditorBridge::replyApply(std::uint64_t requestId, EditStatus status)
    {
        post_([this, requestId, status]() {
            auto it = pendingApply_.find(requestId);
            if (it == pendingApply_.end()) {
                return;
            }
            auto done = std::move(it->second);
            pendingApply_.erase(it);
            done(status);
        });
    }
}
