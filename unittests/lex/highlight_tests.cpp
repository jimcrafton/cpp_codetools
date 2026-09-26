// Tests for the highlighting support classes (highlight.h, json5_language.h).

#include <lex/highlight.h>
#include <lex/json5_language.h>
#include <lex/json5_lexer.h>
#include "test_util.h"

#include <cstdint>
#include <cwchar>
#include <string>
#include <vector>

using namespace lex;

namespace {

struct Expect {
    std::uint32_t column;
    std::uint32_t length;
    StyleId style;
};

bool spansMatch(const std::vector<HighlightSpan>& got, const std::vector<Expect>& want) {
    if (got.size() != want.size()) return false;
    for (std::size_t i = 0; i < got.size(); ++i)
        if (got[i].column != want[i].column || got[i].length != want[i].length || got[i].style != want[i].style)
            return false;
    return true;
}

std::vector<HighlightSpan> line(const wchar_t* text) { return highlightLine(json5Language(), text); }

std::wstring escaped(const std::wstring& s) {
    std::wstring out;
    for (wchar_t c : s) {
        if (c >= 0x20 && c < 0x7f) out += c;
        else { wchar_t buf[8]; std::swprintf(buf, 8, L"\\x%X", static_cast<unsigned>(c)); out += buf; }
    }
    return out;
}

// The incremental highlighter must agree with a highlighter built from scratch.
bool sameAsFresh(SyntaxHighlighter& hl) {
    SyntaxHighlighter fresh(json5Language());
    fresh.setText(hl.text());
    if (hl.lineCount() != fresh.lineCount()) return false;
    for (std::size_t i = 0; i < hl.lineCount(); ++i) {
        if (hl.spans(i) != fresh.spans(i)) return false;
        if (hl.entryState(i) != fresh.entryState(i)) return false;
        if (hl.exitState(i) != fresh.exitState(i)) return false;
    }
    return true;
}

void touchAll(SyntaxHighlighter& hl) { hl.spans(hl.lineCount() - 1); }

}  // namespace

void runHighlightTests() {
    // --- one-shot lines ---------------------------------------------------------
    {
        auto s = line(L"  key: \"v\", // c");
        CHECK(spansMatch(s, {{2, 3, StyleId::PropertyName}, {5, 1, StyleId::Punctuation},
                             {7, 3, StyleId::String}, {10, 1, StyleId::Punctuation},
                             {12, 4, StyleId::Comment}}));
    }
    {
        // key detection
        CHECK(line(L"'a' : 1")[0].style == StyleId::PropertyName);
        CHECK(line(L"Infinity: 1")[0].style == StyleId::PropertyName);
        CHECK(line(L"true: 1")[0].style == StyleId::PropertyName);
        CHECK(line(L"a /*x*/ : 1")[0].style == StyleId::PropertyName);
        CHECK(line(L"\"a\"")[0].style == StyleId::String);       // no colon: plain string
        CHECK(line(L"a: b")[2].style == StyleId::Identifier);     // value side stays plain
        CHECK(line(L"a: \"b\"")[2].style == StyleId::String);
        CHECK(line(L"[a, 1]")[1].style == StyleId::Identifier);
        CHECK(line(L"1: 2")[0].style == StyleId::Number);         // numbers aren't names
        CHECK(line(L"null")[0].style == StyleId::Constant);
        CHECK(line(L"-Infinity")[0].style == StyleId::Number);
    }
    {
        // problems
        auto bad = line(L"01");
        CHECK(bad.size() == 1 && bad[0].style == StyleId::Number && bad[0].isProblem());
        auto err = line(L"@");
        CHECK(err.size() == 1 && err[0].style == StyleId::Error && err[0].isProblem());
        auto open = line(L"\"abc");
        CHECK(open.size() == 1 && (open[0].flags & TokenFlag_Unterminated) && open[0].isProblem());
        auto ok = line(L"1");
        CHECK(ok.size() == 1 && !ok[0].isProblem());
    }
    {
        // fragment starting inside a block comment, with an explicit entry state
        LexState exitState;
        LexState inComment{json5::InBlockComment, 0};
        auto s = highlightLine(json5Language(), L" y */ b", inComment, &exitState);
        CHECK(spansMatch(s, {{0, 5, StyleId::Comment}, {6, 1, StyleId::Identifier}}));
        CHECK(exitState == LexState{});

        auto s2 = highlightLine(json5Language(), L"still open", inComment, &exitState);
        CHECK(spansMatch(s2, {{0, 10, StyleId::Comment}}));
        CHECK(exitState == inComment);

        // multi-line text: only the first line is processed
        auto s3 = highlightLine(json5Language(), L"a\nb", {}, &exitState);
        CHECK(s3.size() == 1);
    }

    // --- document highlighter: state carries between lines ----------------------
    {
        SyntaxHighlighter hl(json5Language());
        hl.setText(L"a /* x\n y */ b\nc");
        CHECK(hl.lineCount() == 3);
        CHECK(hl.lineText(0) == L"a /* x" && hl.lineText(1) == L" y */ b" && hl.lineText(2) == L"c");
        CHECK(spansMatch(hl.spans(0), {{0, 1, StyleId::Identifier}, {2, 4, StyleId::Comment}}));
        CHECK(spansMatch(hl.spans(1), {{0, 5, StyleId::Comment}, {6, 1, StyleId::Identifier}}));
        CHECK(hl.entryState(1).mode == json5::InBlockComment);
        CHECK(hl.entryState(2) == LexState{});
        CHECK(hl.spans(99).empty());  // out of range is harmless
    }

    // --- incremental behaviour and cost -------------------------------------------
    {
        std::wstring doc;
        for (int i = 0; i < 100; ++i) doc += L"  a: 1,\n";
        SyntaxHighlighter hl(json5Language());
        hl.setText(doc);
        const std::size_t n = hl.lineCount();  // 101 (trailing empty line)
        CHECK(n == 101);

        touchAll(hl);
        std::size_t mark = hl.relexedLineCount();
        CHECK(mark == n);

        // Plain edit: exactly one line is re-lexed.
        hl.replace(hl.lineStart(50) + 5, 1, L"2");
        touchAll(hl);
        CHECK(hl.relexedLineCount() - mark == 1);
        CHECK(hl.lineText(50) == L"  a: 2,");
        mark = hl.relexedLineCount();

        // Laziness: nothing is lexed until a line is asked for.
        hl.replace(hl.lineStart(60) + 5, 1, L"3");
        CHECK(hl.relexedLineCount() == mark);
        hl.spans(10);  // an earlier, still-valid line
        CHECK(hl.relexedLineCount() == mark);
        touchAll(hl);
        CHECK(hl.relexedLineCount() - mark == 1);
        mark = hl.relexedLineCount();

        // Opening a comment re-lexes everything after it (states all change).
        hl.replace(hl.lineStart(10) + 2, 0, L"/*");
        touchAll(hl);
        CHECK(hl.relexedLineCount() - mark == n - 10);
        CHECK(hl.entryState(50).mode == json5::InBlockComment);
        mark = hl.relexedLineCount();

        // Closing it again on the next line.
        hl.replace(hl.lineStart(11) + 2, 0, L"*/");
        touchAll(hl);
        CHECK(hl.relexedLineCount() - mark == n - 11);
        CHECK(hl.entryState(50) == LexState{});
        CHECK(sameAsFresh(hl));
        mark = hl.relexedLineCount();

        // With the comment closed on line 11, an edit on line 10 stops there.
        hl.replace(hl.lineStart(10), 0, L" ");
        touchAll(hl);
        CHECK(hl.relexedLineCount() - mark == 1);
        mark = hl.relexedLineCount();

        // Removing the "/*" re-lexes lines 10 and 11, then converges at line 12.
        hl.replace(hl.lineStart(10) + 3, 2, L"");
        touchAll(hl);
        CHECK(hl.relexedLineCount() - mark == 2);
        CHECK(sameAsFresh(hl));
    }

    // --- edits that change the line structure --------------------------------------
    {
        SyntaxHighlighter hl(json5Language());
        hl.setText(L"a\r\nb\r\nc");
        touchAll(hl);
        hl.replace(1, 0, L"x\ny");           // insert lines
        CHECK(hl.lineCount() == 4 && sameAsFresh(hl));
        hl.replace(0, hl.text().size(), L"");  // delete everything
        CHECK(hl.lineCount() == 1 && sameAsFresh(hl));
        hl.replace(100, 100, L"q");           // out-of-range edit clamps to the end
        CHECK(hl.text() == L"q" && sameAsFresh(hl));

        hl.setText(L"a\rb");                  // lone CR is a break...
        touchAll(hl);
        CHECK(hl.lineCount() == 2);
        hl.replace(2, 0, L"\n");              // ...until an LF joins it into CRLF
        CHECK(hl.lineCount() == 2 && sameAsFresh(hl));
        hl.replace(2, 1, L"");                // and back
        CHECK(hl.lineCount() == 2 && sameAsFresh(hl));

        hl.setText(L"a b");              // JSON5 line separators
        CHECK(hl.lineCount() == 2 && sameAsFresh(hl));
    }

    // --- fuzz: random edits, incremental must equal from-scratch ----------------------
    {
        const wchar_t* pieces[] = {
            L"{", L"}", L"a", L": ", L"\"", L"'", L"\\", L"/*", L"*/", L"//", L"\n", L"\r\n", L"\r",
            L" ", L" ", L"1", L",", L"\\\n", L"x: 1,\n", L"[1, 2]", L"0x", L"'k'",
        };
        const std::size_t np = sizeof(pieces) / sizeof(pieces[0]);
        std::uint32_t rng = 20260919;
        auto rnd = [&rng]() { rng = rng * 1664525u + 1013904223u; return static_cast<std::size_t>(rng >> 8); };

        for (int doc = 0; doc < 60; ++doc) {
            std::wstring expected;
            const std::size_t initial = rnd() % 30;
            for (std::size_t i = 0; i < initial; ++i) expected += pieces[rnd() % np];

            SyntaxHighlighter hl(json5Language());
            hl.setText(expected);

            for (int step = 0; step < 60; ++step) {
                const std::size_t offset = rnd() % (expected.size() + 1);
                const std::size_t remove = rnd() % (std::min<std::size_t>(6, expected.size() - offset) + 1);
                std::wstring insert;
                const std::size_t pcs = rnd() % 4;
                for (std::size_t i = 0; i < pcs; ++i) insert += pieces[rnd() % np];

                expected.replace(offset, remove, insert);
                hl.replace(offset, remove, insert);
                CHECK(hl.text() == expected);

                // Sometimes look at random lines mid-way, so laziness gets exercised.
                if (rnd() % 3 == 0) hl.spans(rnd() % hl.lineCount());

                if (!sameAsFresh(hl)) {
                    CHECK(false);
                    std::wprintf(L"  ^ doc %d step %d, text: %ls\n", doc, step, escaped(expected).c_str());
                    doc = 60;  // stop after the first failing document
                    break;
                }
            }
        }
    }

    // --- LineIndex::update equals a full rebuild ------------------------------------------
    {
        const wchar_t* pieces[] = {L"a", L"bc", L"\n", L"\r\n", L"\r", L" ", L" ", L"\n\n", L"\r\r"};
        const std::size_t np = sizeof(pieces) / sizeof(pieces[0]);
        std::uint32_t rng = 777;
        auto rnd = [&rng]() { rng = rng * 1664525u + 1013904223u; return static_cast<std::size_t>(rng >> 8); };

        for (int mode = 0; mode < 2; ++mode) {
            const bool unicodeLs = mode == 1;
            for (int doc = 0; doc < 300; ++doc) {
                std::wstring text;
                for (std::size_t i = rnd() % 12; i > 0; --i) text += pieces[rnd() % np];
                LineIndex idx(text, unicodeLs);

                for (int step = 0; step < 40; ++step) {
                    const std::size_t offset = rnd() % (text.size() + 1);
                    const std::size_t remove = rnd() % (std::min<std::size_t>(4, text.size() - offset) + 1);
                    std::wstring insert;
                    for (std::size_t i = rnd() % 3; i > 0; --i) insert += pieces[rnd() % np];

                    text.replace(offset, remove, insert);
                    idx.update(text, offset, remove, insert.size());

                    const LineIndex want(text, unicodeLs);
                    bool same = idx.lineCount() == want.lineCount();
                    for (std::size_t l = 0; same && l < want.lineCount(); ++l)
                        same = idx.lineStart(l) == want.lineStart(l) && idx.lineEnd(l) == want.lineEnd(l);
                    if (!same) {
                        CHECK(false);
                        std::wprintf(L"  ^ LineIndex::update mismatch, text: %ls\n", escaped(text).c_str());
                        doc = 300;
                        break;
                    }
                }
            }
        }
    }

    // --- theme --------------------------------------------------------------------------
    {
        const Theme light = Theme::light();
        const Theme dark = Theme::dark();
        CHECK(light.background != dark.background);
        CHECK(light.style(StyleId::Comment).foreground != dark.style(StyleId::Comment).foreground);
        for (std::size_t i = 0; i < kStyleCount; ++i) {
            const StyleId id = static_cast<StyleId>(i);
            CHECK((light.style(id).foreground >> 24) == 0xFF);  // opaque
            CHECK((dark.style(id).foreground >> 24) == 0xFF);
        }
        CHECK(styleForClass(TokenClass::Comment) == StyleId::Comment);
        CHECK(styleForClass(TokenClass::Whitespace) == StyleId::Default);
    }
}
