#include <gtest/gtest.h>

#include <lex/json5_parser.h>

#include "../extension/NativeEditControls/ControllerRef.h"

using namespace CodeToolsVsix;

namespace {

// A .newui the way the real ones look: leading comments, unquoted keys, a meta block.
const wchar_t* kFrame =
    L"// A dialog.\n"
    L"// Second comment line.\n"
    L"{\n"
    L"  meta: { author: \"\", version: \"0.1.0\" },\n"
    L"  type: \"Frame\",\n"
    L"  title: \"Save\",  // keep me\n"
    L"  rootView: { type: \"RootView\" },\n"
    L"}\n";

std::wstring withEdits(std::wstring text, const std::vector<TextEdit>& edits) {
    EXPECT_TRUE(applyTextEdits(text, edits));
    return text;
}

ControllerRef save() { return {"SaveDialogController", "SaveDialogController.h"}; }

}  // namespace

TEST(ControllerRefRead, AbsentKeyOrNonObjectIsFalse) {
    ControllerRef ref;
    EXPECT_FALSE(readControllerRef(kFrame, ref));
    EXPECT_FALSE(readControllerRef(L"", ref));
    EXPECT_FALSE(readControllerRef(L"[1, 2]", ref));
    EXPECT_FALSE(readControllerRef(L"{ controller: \"NotAnObject\" }", ref));
    EXPECT_FALSE(readControllerRef(L"{ controller: { class: \"\", header: \"a.h\" } }", ref)) << "empty class";
    EXPECT_FALSE(readControllerRef(L"{ controller: { header: \"a.h\" } }", ref)) << "no class";
}

TEST(ControllerRefRead, ReadsClassAndHeaderIncludingSingleQuotesAndComments) {
    ControllerRef ref;
    ASSERT_TRUE(readControllerRef(
        L"{ title: 'x', controller: { /* c */ class: 'My::Ctl', header: \"sub/My.h\" } }", ref));
    EXPECT_EQ(ref.className, "My::Ctl");
    EXPECT_EQ(ref.header, "sub/My.h");
}

TEST(ControllerRefSet, InsertsFirstInsideTheBracesAndLeavesEverythingElseUntouched) {
    const std::wstring original = kFrame;
    std::vector<TextEdit> edits;
    ASSERT_TRUE(planSetControllerRef(original, save(), edits));
    ASSERT_EQ(edits.size(), 1u);
    EXPECT_EQ(edits[0].length, 0u) << "an insertion, nothing replaced";

    const std::wstring result = withEdits(original, edits);
    EXPECT_TRUE(lex::json5::parse(result).ok());

    ControllerRef read;
    ASSERT_TRUE(readControllerRef(result, read));
    EXPECT_EQ(read, save());

    // Byte for byte otherwise: taking the inserted text back out gives the original.
    std::wstring without = result;
    without.erase(edits[0].offset, edits[0].text.size());
    EXPECT_EQ(without, original);

    // The leading comments still sit before the brace.
    EXPECT_EQ(result.rfind(L"// A dialog.\n// Second comment line.\n{", 0), 0u);
    EXPECT_NE(result.find(L"title: \"Save\",  // keep me"), std::wstring::npos);
}

TEST(ControllerRefSet, ReplacesAnExistingKeyInPlace) {
    const std::wstring original =
        L"{\n  type: \"Frame\",\n  controller: { class: \"Old\", header: \"Old.h\" },  // note\n  title: \"T\",\n}\n";
    std::vector<TextEdit> edits;
    ASSERT_TRUE(planSetControllerRef(original, save(), edits));
    ASSERT_EQ(edits.size(), 1u);
    EXPECT_GT(edits[0].length, 0u) << "replaces the old property";

    const std::wstring result = withEdits(original, edits);
    ControllerRef read;
    ASSERT_TRUE(readControllerRef(result, read));
    EXPECT_EQ(read, save());
    EXPECT_EQ(result.find(L"Old"), std::wstring::npos);
    // Only the property changed: the comment after it, and the other keys, are as they were.
    EXPECT_NE(result.find(L",  // note\n  title: \"T\","), std::wstring::npos);
    EXPECT_EQ(result.rfind(L"{\n  type: \"Frame\",\n  controller: ", 0), 0u);
}

TEST(ControllerRefSet, SettingItTwiceLeavesExactlyOneKey) {
    std::vector<TextEdit> edits;
    std::wstring text = kFrame;
    ASSERT_TRUE(planSetControllerRef(text, save(), edits));
    text = withEdits(text, edits);
    ASSERT_TRUE(planSetControllerRef(text, {"Other", "Other.h"}, edits));
    text = withEdits(text, edits);

    size_t count = 0;
    for (size_t at = text.find(L"controller:"); at != std::wstring::npos; at = text.find(L"controller:", at + 1)) {
        ++count;
    }
    EXPECT_EQ(count, 1u);
    ControllerRef read;
    ASSERT_TRUE(readControllerRef(text, read));
    EXPECT_EQ(read.className, "Other");
}

TEST(ControllerRefSet, WorksOnAnEmptyObject) {
    std::vector<TextEdit> edits;
    ASSERT_TRUE(planSetControllerRef(L"{}", save(), edits));
    const std::wstring result = withEdits(L"{}", edits);
    EXPECT_TRUE(lex::json5::parse(result).ok());
    ControllerRef read;
    EXPECT_TRUE(readControllerRef(result, read));
}

TEST(ControllerRefSet, RefusesATextThatIsNotAnObject) {
    std::vector<TextEdit> edits = {{1, 1, L"kept"}};
    EXPECT_FALSE(planSetControllerRef(L"", save(), edits));
    EXPECT_FALSE(planSetControllerRef(L"[1]", save(), edits));
    EXPECT_FALSE(planSetControllerRef(L"not json at all {", save(), edits));
    ASSERT_EQ(edits.size(), 1u) << "untouched on failure";
}

TEST(ControllerRefSet, QuotesAndBackslashesInNamesAreEscaped) {
    std::vector<TextEdit> edits;
    ASSERT_TRUE(planSetControllerRef(L"{}", {"A", "dir\\sub\"x.h"}, edits));
    const std::wstring result = withEdits(L"{}", edits);
    ControllerRef read;
    ASSERT_TRUE(readControllerRef(result, read));
    EXPECT_EQ(read.header, "dir\\sub\"x.h");
}
