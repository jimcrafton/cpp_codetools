#include <gtest/gtest.h>

#include "../extension/NativeEditControls/CodegenEditAdapter.h"

using namespace CodeToolsVsix;
using cpptools_codegen::DelegateWiringEdit;

TEST(CodegenEditAdapter, PlanningTextIsTheSnapshotAsUtf8) {
    EXPECT_EQ(planningText(L"caf\u00E9"), "caf\xC3\xA9");
}

TEST(CodegenEditAdapter, AsciiOffsetsMapOneToOne) {
    std::vector<TextEdit> out;
    ASSERT_TRUE(toTextEdits(L"class A {};", {{9, "int x;"}}, out));
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].offset, 9u);
    EXPECT_EQ(out[0].length, 0u);
    EXPECT_EQ(out[0].text, L"int x;");
}

TEST(CodegenEditAdapter, ByteOffsetsAfterMultiByteCharactersBecomeUtf16Indices) {
    // "é" = 2 bytes / 1 unit, the emoji = 4 bytes / 2 units, so "x" is at byte 8 but unit 5.
    const std::wstring text = L"a\u00E9\U0001F600x";
    ASSERT_EQ(planningText(text).find('x'), 7u);

    std::vector<TextEdit> out;
    ASSERT_TRUE(toTextEdits(text, {{7, "!"}, {0, "?"}}, out));
    ASSERT_EQ(out.size(), 2u);
    EXPECT_EQ(out[0].offset, 4u);
    EXPECT_EQ(out[1].offset, 0u);

    std::wstring applied = text;
    ASSERT_TRUE(applyTextEdits(applied, out));
    EXPECT_EQ(applied, L"?a\u00E9\U0001F600!x");
}

TEST(CodegenEditAdapter, AnOffsetInsideACharacterOrPastTheEndIsRefused) {
    const std::wstring text = L"\u00E9\U0001F600";  // bytes 0-1, 2-5
    std::vector<TextEdit> out = {{99, 99, L"kept"}};
    EXPECT_FALSE(toTextEdits(text, {{1, "x"}}, out));   // inside the e-acute
    EXPECT_FALSE(toTextEdits(text, {{3, "x"}}, out));   // inside the emoji
    EXPECT_FALSE(toTextEdits(text, {{7, "x"}}, out));   // past the end (6 bytes total)
    ASSERT_EQ(out.size(), 1u) << "out is untouched on failure";
    EXPECT_EQ(out[0].text, L"kept");
}

TEST(CodegenEditAdapter, InsertedTextIsWidened) {
    std::vector<TextEdit> out;
    ASSERT_TRUE(toTextEdits(L"", {{0, "// caf\xC3\xA9\n"}}, out));
    EXPECT_EQ(out[0].text, L"// caf\u00E9\n");
}
