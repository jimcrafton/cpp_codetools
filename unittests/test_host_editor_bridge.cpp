#include <gtest/gtest.h>

#include <deque>
#include <mutex>
#include <thread>

#include "../extension/NativeEditControls/HostEditorBridge.h"

using namespace CodeToolsVsix;

namespace {

// What the fake "C#" callbacks saw.
struct HostCalls {
    std::vector<std::uint64_t> getIds;
    std::vector<std::wstring> getPaths;
    std::vector<std::uint64_t> applyIds;
    std::uint64_t applyVersion = 0;
    std::vector<TextEdit> applyEdits;
};
HostCalls g_calls;

void __stdcall fakeGet(std::uint64_t id, const wchar_t* path, std::size_t length) {
    g_calls.getIds.push_back(id);
    g_calls.getPaths.emplace_back(path, length);
}

void __stdcall fakeApply(std::uint64_t id, const wchar_t*, std::size_t, std::uint64_t version,
                         const HostTextEdit* edits, std::size_t count) {
    g_calls.applyIds.push_back(id);
    g_calls.applyVersion = version;
    g_calls.applyEdits.clear();
    for (std::size_t i = 0; i < count; ++i) {
        g_calls.applyEdits.push_back(
            {static_cast<std::size_t>(edits[i].offset), static_cast<std::size_t>(edits[i].length),
             std::wstring(edits[i].text, static_cast<std::size_t>(edits[i].textLength))});
    }
}

// The edit thread's queue, drained by hand.
class Queue {
public:
    HostEditorBridge::Poster poster() {
        return [this](std::function<void()> task) {
            std::lock_guard<std::mutex> lock(mutex_);
            tasks_.push_back(std::move(task));
        };
    }
    std::size_t drain() {
        std::size_t ran = 0;
        for (;;) {
            std::function<void()> task;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (tasks_.empty()) {
                    return ran;
                }
                task = std::move(tasks_.front());
                tasks_.pop_front();
            }
            task();
            ++ran;
        }
    }

private:
    std::mutex mutex_;
    std::deque<std::function<void()>> tasks_;
};

class HostEditorBridgeTest : public ::testing::Test {
protected:
    void SetUp() override { g_calls = HostCalls{}; }
    Queue queue;
};

}  // namespace

TEST_F(HostEditorBridgeTest, NotConnectedSaysNotOpenSoTheServiceFallsBackToDisk) {
    HostEditorBridge bridge(queue.poster());
    EditStatus got = EditStatus::Ok;
    bridge.getText("a.h", [&](EditStatus s, DocumentSnapshot) { got = s; });
    EXPECT_EQ(got, EditStatus::NotOpen);
    EditStatus applied = EditStatus::Ok;
    bridge.applyEdits("a.h", 1, {}, [&](EditStatus s) { applied = s; });
    EXPECT_EQ(applied, EditStatus::NotOpen);
}

TEST_F(HostEditorBridgeTest, GetTextSendsTheRequestAndCompletesOnlyAfterTheReplyIsDrained) {
    HostEditorBridge bridge(queue.poster());
    bridge.setCallbacks(&fakeGet, &fakeApply);

    bool called = false;
    DocumentSnapshot snap;
    EditStatus status = EditStatus::Rejected;
    bridge.getText("C:\\src\\a.h", [&](EditStatus s, DocumentSnapshot d) { called = true; status = s; snap = std::move(d); });

    ASSERT_EQ(g_calls.getIds.size(), 1u);
    EXPECT_EQ(g_calls.getPaths[0], L"C:\\src\\a.h");
    EXPECT_FALSE(called) << "the host answers later";

    bridge.replyGetText(g_calls.getIds[0], EditStatus::Ok, L"text", 42);
    EXPECT_FALSE(called) << "the reply is only posted, not run, on the caller's thread";
    EXPECT_EQ(queue.drain(), 1u);

    EXPECT_TRUE(called);
    EXPECT_EQ(status, EditStatus::Ok);
    EXPECT_EQ(snap.text, L"text");
    EXPECT_EQ(snap.version, 42u);
}

TEST_F(HostEditorBridgeTest, ApplySendsEveryEditWithItsBytes) {
    HostEditorBridge bridge(queue.poster());
    bridge.setCallbacks(&fakeGet, &fakeApply);

    EditStatus status = EditStatus::Rejected;
    bridge.applyEdits("a.h", 9, {{3, 0, L"ins"}, {10, 2, L"\u00E9"}}, [&](EditStatus s) { status = s; });

    ASSERT_EQ(g_calls.applyIds.size(), 1u);
    EXPECT_EQ(g_calls.applyVersion, 9u);
    ASSERT_EQ(g_calls.applyEdits.size(), 2u);
    EXPECT_EQ(g_calls.applyEdits[0].text, L"ins");
    EXPECT_EQ(g_calls.applyEdits[1].offset, 10u);
    EXPECT_EQ(g_calls.applyEdits[1].text, L"\u00E9");

    bridge.replyApply(g_calls.applyIds[0], EditStatus::Ok);
    queue.drain();
    EXPECT_EQ(status, EditStatus::Ok);
}

TEST_F(HostEditorBridgeTest, RepliesMatchTheirRequestEvenWhenTheyArriveOutOfOrder) {
    HostEditorBridge bridge(queue.poster());
    bridge.setCallbacks(&fakeGet, &fakeApply);

    std::wstring first, second;
    bridge.getText("a.h", [&](EditStatus, DocumentSnapshot d) { first = d.text; });
    bridge.getText("b.h", [&](EditStatus, DocumentSnapshot d) { second = d.text; });
    ASSERT_EQ(g_calls.getIds.size(), 2u);
    EXPECT_NE(g_calls.getIds[0], g_calls.getIds[1]);

    bridge.replyGetText(g_calls.getIds[1], EditStatus::Ok, L"B", 1);
    bridge.replyGetText(g_calls.getIds[0], EditStatus::Ok, L"A", 1);
    queue.drain();
    EXPECT_EQ(first, L"A");
    EXPECT_EQ(second, L"B");
}

TEST_F(HostEditorBridgeTest, ADuplicateOrUnknownReplyIsIgnored) {
    HostEditorBridge bridge(queue.poster());
    bridge.setCallbacks(&fakeGet, &fakeApply);

    int calls = 0;
    bridge.getText("a.h", [&](EditStatus, DocumentSnapshot) { ++calls; });
    bridge.replyGetText(g_calls.getIds[0], EditStatus::Ok, L"x", 1);
    bridge.replyGetText(g_calls.getIds[0], EditStatus::Ok, L"x", 1);  // answered twice
    bridge.replyGetText(9999, EditStatus::Ok, L"x", 1);               // never asked
    bridge.replyApply(9999, EditStatus::Ok);
    queue.drain();
    EXPECT_EQ(calls, 1);
}

TEST_F(HostEditorBridgeTest, ARepliesFromAnotherThreadAreCompletedOnTheDrainingThread) {
    HostEditorBridge bridge(queue.poster());
    bridge.setCallbacks(&fakeGet, &fakeApply);

    std::thread::id completedOn;
    bridge.getText("a.h", [&](EditStatus, DocumentSnapshot) { completedOn = std::this_thread::get_id(); });
    const std::uint64_t id = g_calls.getIds[0];

    std::thread vsUiThread([&] { bridge.replyGetText(id, EditStatus::Ok, L"x", 1); });
    vsUiThread.join();
    queue.drain();
    EXPECT_EQ(completedOn, std::this_thread::get_id());
}

TEST_F(HostEditorBridgeTest, DisconnectingFailsEverythingStillWaiting) {
    HostEditorBridge bridge(queue.poster());
    bridge.setCallbacks(&fakeGet, &fakeApply);

    EditStatus get = EditStatus::Ok, apply = EditStatus::Ok;
    bridge.getText("a.h", [&](EditStatus s, DocumentSnapshot) { get = s; });
    bridge.applyEdits("a.h", 1, {}, [&](EditStatus s) { apply = s; });

    bridge.setCallbacks(nullptr, nullptr);
    EXPECT_EQ(get, EditStatus::Rejected);
    EXPECT_EQ(apply, EditStatus::Rejected);
    EXPECT_FALSE(bridge.connected());
}

// The whole chain: the service asks the host, which answers later, and the group completes.
TEST_F(HostEditorBridgeTest, DocumentEditServiceUsesTheBridgeAsItsHost) {
    HostEditorBridge bridge(queue.poster());
    bridge.setCallbacks(&fakeGet, &fakeApply);
    DocumentEditService service;
    service.setHost(&bridge);

    EditStatus result = EditStatus::Rejected;
    std::size_t applied = 99;
    service.applyGroup({{"vs_buffer.h", 5, {{0, 0, L"x"}}}}, [&](EditStatus s, std::size_t n) { result = s; applied = n; });

    ASSERT_EQ(g_calls.getIds.size(), 1u);                 // version check asks the host first
    bridge.replyGetText(g_calls.getIds[0], EditStatus::Ok, L"abc", 5);
    queue.drain();
    ASSERT_EQ(g_calls.applyIds.size(), 1u);               // then the apply
    EXPECT_EQ(g_calls.applyVersion, 5u);
    bridge.replyApply(g_calls.applyIds[0], EditStatus::Ok);
    queue.drain();

    EXPECT_EQ(result, EditStatus::Ok);
    EXPECT_EQ(applied, 1u);
}
