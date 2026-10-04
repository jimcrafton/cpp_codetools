// HostServices / copyHostServices: the one versioned table the managed host hands the DLL. What matters is
// that an older host (a smaller table) and a newer one (a bigger one) both work, and that "no table"
// disconnects everything.

#include <gtest/gtest.h>

#include <cstddef>

#include "../extension/NativeEditControls/HostServices.h"

using CodeToolsVsix::copyHostServices;

namespace {

void __stdcall logLine(cpptools::Severity, const wchar_t*, std::size_t) {}
void __stdcall getText(std::uint64_t, const wchar_t*, std::size_t) {}
void __stdcall applyEdits(std::uint64_t, const wchar_t*, std::size_t, std::uint64_t, const HostTextEdit*, std::size_t) {}
void __stdcall openLocation(const wchar_t*, std::size_t, std::uint64_t, std::uint64_t) {}

HostServices fullTable() {
    HostServices table{};
    table.size = sizeof(HostServices);
    table.logSink = &logLine;
    table.getText = &getText;
    table.applyEdits = &applyEdits;
    table.openLocation = &openLocation;
    return table;
}

}

TEST(HostServices, NoTableMeansEverythingIsDisconnected) {
    const HostServices copy = copyHostServices(nullptr);

    EXPECT_EQ(copy.size, 0u);
    EXPECT_EQ(copy.logSink, nullptr);
    EXPECT_EQ(copy.getText, nullptr);
    EXPECT_EQ(copy.applyEdits, nullptr);
    EXPECT_EQ(copy.openLocation, nullptr);
}

TEST(HostServices, ATableWithASizeOfZeroIsAlsoEmpty) {
    HostServices table = fullTable();
    table.size = 0;

    const HostServices copy = copyHostServices(&table);
    EXPECT_EQ(copy.logSink, nullptr);
    EXPECT_EQ(copy.openLocation, nullptr);
}

TEST(HostServices, AFullTableIsCopiedWhole) {
    const HostServices table = fullTable();
    const HostServices copy = copyHostServices(&table);

    EXPECT_EQ(copy.size, sizeof(HostServices));
    EXPECT_EQ(copy.logSink, &logLine);
    EXPECT_EQ(copy.getText, &getText);
    EXPECT_EQ(copy.applyEdits, &applyEdits);
    EXPECT_EQ(copy.openLocation, &openLocation);
}

TEST(HostServices, AnOlderHostsSmallerTableLeavesTheNewerMembersNull) {
    // A host built before openLocation existed: its table ends right before it.
    HostServices table = fullTable();
    table.size = static_cast<std::uint32_t>(offsetof(HostServices, openLocation));

    const HostServices copy = copyHostServices(&table);
    EXPECT_EQ(copy.logSink, &logLine);
    EXPECT_EQ(copy.getText, &getText);
    EXPECT_EQ(copy.applyEdits, &applyEdits);
    EXPECT_EQ(copy.openLocation, nullptr) << "not in the host's table: unavailable, not garbage";
    EXPECT_EQ(copy.size, offsetof(HostServices, openLocation));
}

TEST(HostServices, AnEvenOlderHostWithOnlyTheLogSinkWorksToo) {
    HostServices table = fullTable();
    table.size = static_cast<std::uint32_t>(offsetof(HostServices, getText));

    const HostServices copy = copyHostServices(&table);
    EXPECT_EQ(copy.logSink, &logLine);
    EXPECT_EQ(copy.getText, nullptr);
    EXPECT_EQ(copy.applyEdits, nullptr);
}

TEST(HostServices, ANewerHostsExtraMembersAreIgnoredNotRead) {
    // A host built after this DLL, with a member we have never heard of at the end.
    struct NewerTable {
        HostServices known;
        void* futureCapability;
    } newer{};
    newer.known = fullTable();
    newer.known.size = sizeof(NewerTable);
    newer.futureCapability = &newer;

    const HostServices copy = copyHostServices(&newer.known);
    EXPECT_EQ(copy.size, sizeof(HostServices)) << "only what this DLL knows about";
    EXPECT_EQ(copy.logSink, &logLine);
    EXPECT_EQ(copy.openLocation, &openLocation);
}

TEST(HostServices, TheTableIsLaidOutTheWayTheManagedStructIs) {
    // NativeMethods.HostServices (C#): a uint, then four pointers, sequential. Appended members only.
    EXPECT_EQ(offsetof(HostServices, size), 0u);
    EXPECT_EQ(offsetof(HostServices, logSink), sizeof(void*)) << "after the uint, padded to a pointer";
    EXPECT_EQ(offsetof(HostServices, getText), 2 * sizeof(void*));
    EXPECT_EQ(offsetof(HostServices, applyEdits), 3 * sizeof(void*));
    EXPECT_EQ(offsetof(HostServices, openLocation), 4 * sizeof(void*));
    EXPECT_EQ(sizeof(HostServices), 5 * sizeof(void*));
}
