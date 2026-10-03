#include "../extension/NativeEditControls/IncludeManager.h"

#include <newui/reflection.h>

#include <gtest/gtest.h>

using namespace CodeToolsVsix;

namespace {

struct IncMgrBase {};
struct IncMgrMiddle : IncMgrBase {};
struct IncMgrLeaf : IncMgrMiddle {};
struct IncMgrSharesHeader : IncMgrBase {};
struct IncMgrNoHeader {};

using std::string;
using std::vector;
using newui::reflection::Class;

// Registered once for the whole binary; a registered Class lives for the process.
const Class* registered(const char* name) {
    static bool done = false;
    if (!done) {
        done = true;
        using namespace newui::reflection;
        ReflectionRegistry::registerClass(ClassBuilder<IncMgrBase>().header("<inc/base.h>").build());
        ReflectionRegistry::registerClass(ClassBuilder<IncMgrMiddle>().base<IncMgrBase>().header("\"middle.h\"").build());
        ReflectionRegistry::registerClass(ClassBuilder<IncMgrLeaf>().base<IncMgrMiddle>().header("\"leaf.h\"").build());
        ReflectionRegistry::registerClass(
            ClassBuilder<IncMgrSharesHeader>().base<IncMgrBase>().header("<inc/base.h>").build());
        ReflectionRegistry::registerClass(ClassBuilder<IncMgrNoHeader>().build());
    }
    return newui::reflection::classinfo(string(name));
}

const Class* leaf() { return registered("IncMgrLeaf"); }

}  // namespace

TEST(IncludeManager, GetHeadersListsEveryAncestorBaseFirstThenTheClassItself) {
    IncludeManager::clearCache();
    EXPECT_EQ(IncludeManager::getHeaders(leaf()), (vector<string>{"<inc/base.h>", "\"middle.h\"", "\"leaf.h\""}));
}

TEST(IncludeManager, GetHeadersDropsDuplicatesKeepingTheFirst) {
    IncludeManager::clearCache();
    EXPECT_EQ(IncludeManager::getHeaders(registered("IncMgrSharesHeader")), (vector<string>{"<inc/base.h>"}));
}

TEST(IncludeManager, GetHeadersSkipsClassesWithNoRecordedHeader) {
    EXPECT_TRUE(IncludeManager::getHeaders(registered("IncMgrNoHeader")).empty());
}

TEST(IncludeManager, ANullClassHasNoHeaders) {
    EXPECT_TRUE(IncludeManager::getHeaders(static_cast<const Class*>(nullptr)).empty());
    EXPECT_EQ(IncludeManager::headerOf(nullptr), "");
}

TEST(IncludeManager, GetHeadersOverSeveralClassesMergesWithoutDuplicates) {
    const vector<const Class*> classes = {leaf(), registered("IncMgrSharesHeader")};
    EXPECT_EQ(IncludeManager::getHeaders(classes), (vector<string>{"<inc/base.h>", "\"middle.h\"", "\"leaf.h\""}));
}

TEST(IncludeManager, HeaderOfIsOnlyTheDefiningHeaderNotTheBases) {
    EXPECT_EQ(IncludeManager::headerOf(leaf()), "\"leaf.h\"");
    EXPECT_EQ(IncludeManager::headerOf(registered("IncMgrNoHeader")), "");
}

TEST(IncludeManager, HeadersOfDedupesAndSkipsEmptyAndNull) {
    const vector<const Class*> classes = {registered("IncMgrBase"), registered("IncMgrSharesHeader"), nullptr,
                                          registered("IncMgrNoHeader"), leaf()};
    EXPECT_EQ(IncludeManager::headersOf(classes), (vector<string>{"<inc/base.h>", "\"leaf.h\""}));
}

TEST(IncludeManager, ResultsSurviveClearingTheCache) {
    const auto before = IncludeManager::getHeaders(leaf());
    IncludeManager::clearCache();
    EXPECT_EQ(IncludeManager::getHeaders(leaf()), before);
}
