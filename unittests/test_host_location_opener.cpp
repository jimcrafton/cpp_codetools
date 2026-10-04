// HostLocationOpener: the one-way "open this file at this line" request from a native editor to the
// VS host. Here the host is a plain function.

#include <gtest/gtest.h>

#include "../extension/NativeEditControls/HostLocationOpener.h"

using CodeToolsVsix::HostLocationOpener;

namespace {

struct Received {
    int calls = 0;
    std::wstring path;
    std::size_t pathLength = 0;
    std::uint64_t line = 0;
    std::uint64_t column = 0;
} g_received;

void __stdcall recordLocation(const wchar_t* path, std::size_t pathLength, std::uint64_t line, std::uint64_t column) {
    ++g_received.calls;
    g_received.path.assign(path, pathLength);   // a host copies the path before returning
    g_received.pathLength = pathLength;
    g_received.line = line;
    g_received.column = column;
}

}

TEST(HostLocationOpener, WithNoHostConnectedItSaysSoAndSendsNothing) {
    g_received = Received();
    HostLocationOpener opener;

    EXPECT_FALSE(opener.connected());
    EXPECT_FALSE(opener.open(L"C:\\proj\\S1Controller.h", 6, 10));
    EXPECT_EQ(g_received.calls, 0);
}

TEST(HostLocationOpener, ARequestReachesTheHostWithThePathLineAndColumn) {
    g_received = Received();
    HostLocationOpener opener;
    opener.setCallback(&recordLocation);

    EXPECT_TRUE(opener.connected());
    EXPECT_TRUE(opener.open(L"C:\\proj\\S1Controller.h", 47, 23));
    EXPECT_EQ(g_received.calls, 1);
    EXPECT_EQ(g_received.path, L"C:\\proj\\S1Controller.h");
    EXPECT_EQ(g_received.pathLength, std::wstring(L"C:\\proj\\S1Controller.h").size()) << "the length is explicit";
    EXPECT_EQ(g_received.line, 47u);
    EXPECT_EQ(g_received.column, 23u);
}

TEST(HostLocationOpener, DisconnectingStopsTheRequests) {
    g_received = Received();
    HostLocationOpener opener;
    opener.setCallback(&recordLocation);
    opener.setCallback(nullptr);

    EXPECT_FALSE(opener.connected());
    EXPECT_FALSE(opener.open(L"C:\\a.h", 1, 1));
    EXPECT_EQ(g_received.calls, 0);
}

TEST(HostLocationOpener, AnEmptyPathIsNotARequest) {
    g_received = Received();
    HostLocationOpener opener;
    opener.setCallback(&recordLocation);

    EXPECT_FALSE(opener.open(L"", 1, 1));
    EXPECT_EQ(g_received.calls, 0);
}

TEST(HostLocationOpener, TheSharedInstanceIsOneObject) {
    EXPECT_EQ(&HostLocationOpener::instance(), &HostLocationOpener::instance());
}
