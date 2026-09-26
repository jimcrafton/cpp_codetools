// Tests for lex::Json5Lexer.

#include <lex/json5_lexer.h>
#include "test_util.h"

#include <cstdint>
#include <cwchar>
#include <string>
#include <vector>

using namespace lex;

namespace {

LexerOptions splitOpts() {
    LexerOptions o;
    o.splitAtNewlines = true;
    return o;
}

std::vector<Token> lexAll(const std::wstring& src, bool split = false) {
    Json5Lexer lx(src, split ? splitOpts() : LexerOptions{});
    return lx.tokenize();
}

std::wstring textOf(const std::wstring& src, const Token& t) { return src.substr(t.offset, t.length); }

// Tokens that aren't whitespace / newline / comment.
std::vector<Token> significant(const std::vector<Token>& toks) {
    std::vector<Token> out;
    for (const Token& t : toks)
        if (!isTrivia(t.cls) && t.cls != TokenClass::Comment) out.push_back(t);
    return out;
}

std::wstring joinTexts(const std::wstring& src, const std::vector<Token>& toks, TokenKind k) {
    std::wstring s;
    for (const Token& t : toks)
        if (t.kind == k) { s += textOf(src, t); s += L'|'; }
    return s;
}

bool lossless(const std::wstring& src, const std::vector<Token>& toks) {
    std::size_t at = 0;
    for (const Token& t : toks) {
        if (t.offset != at || t.length == 0) return false;
        at = t.end();
    }
    return at == src.size();
}

std::wstring escaped(const std::wstring& s) {
    std::wstring out;
    for (wchar_t c : s) {
        if (c >= 0x20 && c < 0x7f) out += c;
        else { wchar_t buf[8]; std::swprintf(buf, 8, L"\\x%X", static_cast<unsigned>(c)); out += buf; }
    }
    return out;
}

// Split-mode invariants for `src`: lossless, no token spans lines, and lexing
// any single line from its cached entry state reproduces the whole-file tokens.
void checkSplitInvariants(const std::wstring& src) {
    const int before = g_failures;
    Json5Lexer lx(src, splitOpts());
    std::vector<Token> toks;
    std::vector<LexState> entry;  // entry[i] = state just before toks[i]
    for (;;) {
        entry.push_back(lx.state());
        Token t = lx.next();
        if (t.isEof()) break;
        toks.push_back(t);
    }

    CHECK(lossless(src, toks));
    LineIndex idx(src, /*unicodeLineSeparators=*/true);
    for (const Token& t : toks) {
        CHECK(t.line == t.endLine);
        CHECK(idx.lineOf(t.offset) == t.line);
    }

    for (std::size_t i = 0; i < toks.size(); ++i) {
        if (i > 0 && toks[i - 1].line == toks[i].line) continue;  // not first on its line
        const std::uint32_t line = toks[i].line;
        Json5Lexer r(src, splitOpts());
        r.restore({idx.lineStart(line), line, idx.lineStart(line), entry[i]});
        for (std::size_t j = i; j < toks.size() && toks[j].line == line; ++j) {
            const Token u = r.next();
            CHECK(u.offset == toks[j].offset && u.length == toks[j].length &&
                  u.kind == toks[j].kind && u.flags == toks[j].flags);
        }
    }

    if (g_failures != before) std::wprintf(L"  ^ split-mode source: %ls\n", escaped(src).c_str());
}

void checkWholeInvariants(const std::wstring& src) {
    const int before = g_failures;
    auto toks = lexAll(src);
    CHECK(lossless(src, toks));
    if (g_failures != before) std::wprintf(L"  ^ source: %ls\n", escaped(src).c_str());
}

}  // namespace

void runJson5LexerTests() {
    // --- a representative document -------------------------------------------
    {
        const std::wstring src =
            L"{ a: 1, 'b': \"x\\\"y\", c: [+1, -Infinity, .5, 5., 0xFF, 1e-3, NaN, null, true, false] /* c */ } // d\n";
        auto toks = lexAll(src);
        CHECK(lossless(src, toks));
        auto sig = significant(toks);

        CHECK(joinTexts(src, sig, json5::Number) == L"1|+1|-Infinity|.5|5.|0xFF|1e-3|NaN|");
        CHECK(joinTexts(src, sig, json5::String) == L"'b'|\"x\\\"y\"|");
        CHECK(joinTexts(src, sig, json5::Identifier) == L"a|c|");
        CHECK(joinTexts(src, sig, json5::Null) == L"null|");
        CHECK(joinTexts(src, sig, json5::True) == L"true|");
        CHECK(joinTexts(src, sig, json5::False) == L"false|");
        for (const Token& t : toks) CHECK(!t.has(TokenFlag_Invalid) && !t.has(TokenFlag_Unterminated));

        int comments = 0;
        for (const Token& t : toks) if (t.cls == TokenClass::Comment) ++comments;
        CHECK(comments == 2);

        // Classes: literals are Constant, numeric words are Number.
        for (const Token& t : sig) {
            if (t.kind == json5::Null || t.kind == json5::True || t.kind == json5::False)
                CHECK(t.cls == TokenClass::Constant);
            if (t.kind == json5::Number) CHECK(t.cls == TokenClass::Number);
        }
    }

    // --- numbers ---------------------------------------------------------------
    for (const wchar_t* s : {L"0", L"0.5", L"0e5", L"1.e3", L"-0x1F", L"+.5", L"1E+10", L"Infinity", L"-NaN", L"0X0"}) {
        const std::wstring src = s;
        auto toks = lexAll(src);
        CHECK(toks.size() == 1 && toks[0].kind == json5::Number && !toks[0].has(TokenFlag_Invalid));
    }
    for (const wchar_t* s : {L"01", L"1e", L"1e+", L"0x", L"12abc", L"1.5x", L"-0xG"}) {
        const std::wstring src = s;
        auto toks = lexAll(src);
        CHECK(toks.size() == 1 && toks[0].kind == json5::Number && toks[0].has(TokenFlag_Invalid));
    }

    // --- signs and stray characters ----------------------------------------------
    {
        auto t1 = lexAll(L"-x");
        CHECK(t1.size() == 2 && t1[0].kind == json5::Sign && t1[1].kind == json5::Identifier);
        auto t2 = lexAll(L"+");
        CHECK(t2.size() == 1 && t2[0].kind == json5::Sign);
        auto t3 = lexAll(L"-Infinityx");  // not the word Infinity
        CHECK(t3.size() == 2 && t3[0].kind == json5::Sign && t3[1].kind == json5::Identifier);
        for (const wchar_t* s : {L".", L"/", L"@", L"\\", L"#"}) {
            auto t = lexAll(s);
            CHECK(t.size() == 1 && t[0].kind == kind::Error && t[0].cls == TokenClass::Error);
        }
        auto t4 = lexAll(L"\\x");
        CHECK(t4.size() == 2 && t4[0].kind == kind::Error && t4[1].kind == json5::Identifier);
    }

    // --- identifiers ---------------------------------------------------------------
    {
        const std::wstring src = L"$_x1 \\u0061bc \u00e9t\u00e9 \\u00 true\\u0021";
        auto sig = significant(lexAll(src));
        CHECK(sig.size() == 5);
        CHECK(textOf(src, sig[0]) == L"$_x1" && sig[0].kind == json5::Identifier);
        CHECK(textOf(src, sig[1]) == L"\\u0061bc" && !sig[1].has(TokenFlag_Invalid));
        CHECK(sig[2].kind == json5::Identifier && sig[2].length == 3);
        CHECK(sig[3].kind == json5::Identifier && sig[3].has(TokenFlag_Invalid));  // truncated \u escape
        CHECK(sig[4].kind == json5::Identifier);  // escaped => never a keyword

        const std::wstring text = L"a null true false Infinity NaN 1 \"s\" {";
        Json5Lexer real(text);
        auto rt = significant(real.tokenize());
        CHECK(rt.size() == 9);
        for (int i = 0; i < 6; ++i) CHECK(real.isIdentifierName(rt[i]));
        for (int i = 6; i < 9; ++i) CHECK(!real.isIdentifierName(rt[i]));
    }

    // --- strings (whole-token mode) ----------------------------------------------------
    {
        const std::wstring src = L"\"a\\\nb\" 'it\\'s' \"bad\nx";
        auto toks = lexAll(src);
        CHECK(lossless(src, toks));
        CHECK(toks[0].kind == json5::String && toks[0].line == 0 && toks[0].endLine == 1 &&
              !toks[0].has(TokenFlag_Unterminated));
        CHECK(textOf(src, toks[2]) == L"'it\\'s'");
        CHECK(toks[4].has(TokenFlag_Unterminated) && textOf(src, toks[4]) == L"\"bad");
        CHECK(toks[5].kind == kind::Newline);
    }
    {
        const std::wstring src = L"\"a\\\r\nb\"";  // CRLF continuation is one break
        auto toks = lexAll(src);
        CHECK(toks.size() == 1 && toks[0].endLine == 1 && !toks[0].has(TokenFlag_Unterminated));
    }
    {
        const std::wstring src = L"'a\"b' \"c'd\" \"e\\\\\"";
        auto sig = significant(lexAll(src));
        CHECK(sig.size() == 3);
        for (const Token& t : sig) CHECK(t.kind == json5::String && !t.has(TokenFlag_Unterminated));
    }
    {
        auto toks = lexAll(L"\"abc");  // EOF
        CHECK(toks.size() == 1 && toks[0].has(TokenFlag_Unterminated));
        auto t2 = lexAll(L"\"abc\\");  // lone backslash at EOF
        CHECK(t2.size() == 1 && t2[0].has(TokenFlag_Unterminated));
    }

    // --- U+2028 / U+2029 ------------------------------------------------------------------
    {
        const std::wstring src = L"a\u2028b // c\u2029d";
        auto toks = lexAll(src);
        CHECK(lossless(src, toks));
        CHECK(toks[0].line == 0 && toks[1].kind == kind::Newline && toks[2].line == 1);
        CHECK(toks.back().kind == json5::Identifier && toks.back().line == 2);
        const std::wstring str = L"\"a\u2028b\"";  // legal inside a string
        auto st = lexAll(str);
        CHECK(st.size() == 1 && !st[0].has(TokenFlag_Unterminated) && st[0].endLine == 1);
    }

    // --- comments -------------------------------------------------------------------------------
    {
        const std::wstring src = L"/*/ x */ /* open";
        auto toks = lexAll(src);
        CHECK(toks[0].kind == kind::BlockComment && textOf(src, toks[0]) == L"/*/ x */");
        CHECK(toks.back().has(TokenFlag_Unterminated));
    }

    // --- split mode: multi-line comment and string segments ---------------------------------------
    {
        const std::wstring src = L"/* a\n b */ \"ab\\\ncd\" x";
        auto toks = lexAll(src, true);
        CHECK(lossless(src, toks));
        // /* a | \n |  b */ | ws | "ab\ | \n | cd" | ws | x
        CHECK(toks.size() == 9);
        CHECK(toks[0].has(TokenFlag_Continued) && !toks[0].has(TokenFlag_Resumed));
        CHECK(toks[2].has(TokenFlag_Resumed) && !toks[2].has(TokenFlag_Continued));
        CHECK(toks[4].kind == json5::String && toks[4].has(TokenFlag_Continued));
        CHECK(toks[6].kind == json5::String && toks[6].has(TokenFlag_Resumed) &&
              textOf(src, toks[6]) == L"cd\"");

        Json5Lexer lx(src, splitOpts());
        lx.next();
        CHECK(lx.state().mode == json5::InBlockComment);

        // Unterminated at EOF keeps the exit state.
        const std::wstring open = L"/* a\n b";
        auto ot = lexAll(open, true);
        CHECK(ot.back().has(TokenFlag_Resumed) && ot.back().has(TokenFlag_Unterminated));
        Json5Lexer l2(open, splitOpts());
        l2.tokenize();
        CHECK(l2.state().mode == json5::InBlockComment);

        // Unescaped U+2028 inside a string splits, and the string stays open.
        const std::wstring u = L"\"a\u2028b\"";
        auto ut = lexAll(u, true);
        CHECK(ut.size() == 3 && ut[0].has(TokenFlag_Continued) && ut[2].has(TokenFlag_Resumed) &&
              !ut[2].has(TokenFlag_Unterminated));
    }

    // --- invariants over curated and fuzzed inputs ----------------------------------------------------
    {
        const wchar_t* curated[] = {
            L"", L"\n", L"\r\n", L"\r", L"{}", L"/*", L"*/", L"\"", L"'", L"\"\\", L"\"\\\n", L"\"\\\r\n\"",
            L"/* a\r\n b\r\n */ x", L"\"a\\\r\nb\\\nc\"", L"a\u2028\"b\\\u2029c\"", L"0x", L"-", L".",
            L"{ a: [1,2,{b:'c\\\n d'}], /* x\n y */ e: -Infinity } // z\n",
        };
        for (const wchar_t* s : curated) {
            checkWholeInvariants(s);
            checkSplitInvariants(s);
        }

        const wchar_t alphabet[] = L"{}[]:,\"'\\/*\n\r\u2028 \tabeE019.+-xI";
        const std::size_t n = sizeof(alphabet) / sizeof(alphabet[0]) - 1;
        std::uint32_t rng = 12345;
        auto rnd = [&rng]() { rng = rng * 1664525u + 1013904223u; return rng >> 8; };
        for (int iter = 0; iter < 4000; ++iter) {
            std::wstring s;
            const std::size_t len = rnd() % 40;
            for (std::size_t i = 0; i < len; ++i) s += alphabet[rnd() % n];
            checkWholeInvariants(s);
            checkSplitInvariants(s);
        }
    }
}
