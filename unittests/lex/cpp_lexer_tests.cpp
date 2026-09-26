// Tests for lex::CppLexer and lex::cppLanguage().

#include <lex/cpp_language.h>
#include <lex/cpp_lexer.h>

#include <gtest/gtest.h>

#include <cstdlib>
#include <random>
#include <string>
#include <vector>

using namespace lex;

namespace {

LexerOptions splitOptions() {
    LexerOptions o;
    o.splitAtNewlines = true;
    return o;
}

std::vector<Token> lexAll(const std::wstring& src, bool split = false) {
    CppLexer lexer(src, split ? splitOptions() : LexerOptions{});
    return lexer.tokenize();
}

struct Tok {
    TokenKind kind;
    std::wstring text;
    std::uint8_t flags;
};

// The tokens that aren't whitespace / newlines (comments included).
std::vector<Tok> significant(const std::wstring& src, bool split = false) {
    std::vector<Tok> out;
    for (const Token& t : lexAll(src, split)) {
        if (isTrivia(t.cls)) continue;
        out.push_back({t.kind, src.substr(t.offset, t.length), t.flags});
    }
    return out;
}

std::vector<std::wstring> texts(const std::wstring& src) {
    std::vector<std::wstring> out;
    for (const Tok& t : significant(src)) out.push_back(t.text);
    return out;
}

std::vector<TokenKind> kinds(const std::wstring& src) {
    std::vector<TokenKind> out;
    for (const Tok& t : significant(src)) out.push_back(t.kind);
    return out;
}

bool lossless(const std::wstring& src, const std::vector<Token>& tokens) {
    std::size_t at = 0;
    for (const Token& t : tokens) {
        if (t.offset != at || t.length == 0) return false;
        at = t.end();
    }
    return at == src.size();
}

// A printable rendering of a source string for failure messages.
std::string shown(const std::wstring& src) {
    std::string out;
    for (wchar_t c : src) {
        if (c == L'\n') out += "\\n";
        else if (c == L'\r') out += "\\r";
        else if (c < 0x20 || c > 0x7E) out += '?';
        else out += static_cast<char>(c);
    }
    return out;
}

// One number-shaped token, flagged or not.
Tok onlyToken(const std::wstring& src) {
    const std::vector<Tok> toks = significant(src);
    EXPECT_EQ(toks.size(), 1u) << std::string(src.begin(), src.end());
    return toks.empty() ? Tok{kind::Error, L"", 0} : toks[0];
}

}  // namespace

// ---- basics ------------------------------------------------------------------------------------

TEST(CppLexer, KeywordsConstantsAndIdentifiers) {
    const std::wstring src = L"int x = nullptr; if (true) return _y1;";
    const std::vector<TokenKind> expected = {
        cpp::Keyword, cpp::Identifier, cpp::Operator, cpp::Constant, cpp::Semicolon,
        cpp::Keyword, cpp::LParen, cpp::Constant, cpp::RParen, cpp::Keyword, cpp::Identifier, cpp::Semicolon};
    EXPECT_EQ(kinds(src), expected);
    for (const Token& t : lexAll(L"int")) EXPECT_EQ(t.cls, TokenClass::Keyword);
    for (const Token& t : lexAll(L"false")) EXPECT_EQ(t.cls, TokenClass::Constant);
    EXPECT_EQ(kinds(L"override final"), (std::vector<TokenKind>{cpp::Keyword, cpp::Keyword}));
    EXPECT_EQ(kinds(L"integer intx Int"), (std::vector<TokenKind>{cpp::Identifier, cpp::Identifier, cpp::Identifier}));
}

TEST(CppLexer, IdentifiersMayHoldUcnsDollarsAndNonAscii) {
    EXPECT_EQ(onlyToken(L"caf\\u00e9").kind, cpp::Identifier);
    EXPECT_EQ(onlyToken(L"caf\\u00e9").flags, TokenFlag_None);
    EXPECT_EQ(onlyToken(L"a\\u00e").flags & TokenFlag_Invalid, TokenFlag_Invalid) << "a short UCN";
    EXPECT_EQ(onlyToken(L"$x").kind, cpp::Identifier);
    EXPECT_EQ(onlyToken(L"na\u00efve").kind, cpp::Identifier);
}

TEST(CppLexer, BracketsAndSeparatorsHaveTheirOwnKinds) {
    EXPECT_EQ(kinds(L"{}()[];,"), (std::vector<TokenKind>{cpp::LBrace, cpp::RBrace, cpp::LParen, cpp::RParen,
        cpp::LBracket, cpp::RBracket, cpp::Semicolon, cpp::Comma}));
}

// ---- numbers -----------------------------------------------------------------------------------

TEST(CppLexer, WellFormedNumbersAreOneUnflaggedToken) {
    for (const wchar_t* n : {L"0", L"42", L"0x1F", L"0XdeadBEEF", L"0b1010", L"1'000'000", L"3.14f", L".5", L"5.",
             L"1e10", L"1E-9", L"1e+5", L"0x1.8p3", L"0x1p-2", L"12ULL", L"1_km", L"0'7", L"100u", L"08.5", L"0xE"}) {
        const Tok t = onlyToken(n);
        EXPECT_EQ(t.kind, cpp::Number) << std::string(n, n + wcslen(n));
        EXPECT_EQ(t.text, n);
        EXPECT_EQ(t.flags & TokenFlag_Invalid, 0) << std::string(n, n + wcslen(n));
    }
}

TEST(CppLexer, MalformedNumbersAreOneFlaggedToken) {
    for (const wchar_t* n : {L"0x", L"0b", L"0b12", L"1e", L"1e+", L"08", L"09", L"0xg"}) {
        const Tok t = onlyToken(n);
        EXPECT_EQ(t.kind, cpp::Number) << std::string(n, n + wcslen(n));
        EXPECT_EQ(t.flags & TokenFlag_Invalid, TokenFlag_Invalid) << std::string(n, n + wcslen(n));
    }
}

TEST(CppLexer, ASignOnlyJoinsANumberAfterAnExponentLetter) {
    EXPECT_EQ(texts(L"a+1"), (std::vector<std::wstring>{L"a", L"+", L"1"}));
    EXPECT_EQ(texts(L"0xE+1"), (std::vector<std::wstring>{L"0xE", L"+", L"1"}));
    EXPECT_EQ(texts(L"1e+2"), (std::vector<std::wstring>{L"1e+2"}));
    EXPECT_EQ(texts(L"x-.5"), (std::vector<std::wstring>{L"x", L"-", L".5"}));
    EXPECT_EQ(texts(L"1.f"), (std::vector<std::wstring>{L"1.f"}));
    EXPECT_EQ(texts(L"a.b"), (std::vector<std::wstring>{L"a", L".", L"b"}));
}

TEST(CppLexer, ADigitSeparatorNeedsAnAlphanumericAfterIt) {
    EXPECT_EQ(texts(L"1'000"), (std::vector<std::wstring>{L"1'000"}));
    EXPECT_EQ(texts(L"0xFF'FF"), (std::vector<std::wstring>{L"0xFF'FF"}));
    // "1' " is a number and the start of a character literal, not a separator.
    EXPECT_EQ(significant(L"1' ").front().text, L"1");
}

// ---- strings and character literals --------------------------------------------------------------

TEST(CppLexer, StringsAndCharacterLiteralsWithPrefixesEscapesAndSuffixes) {
    for (const wchar_t* s : {L"\"abc\"", L"L\"w\"", L"u8\"x\"", L"U\"x\"", L"u\"x\"", L"\"a\\\"b\"", L"\"abc\"s", L"\"\"",
             L"\"a\\\\\"", L"\"tab\\t\\x41\\u00e9\""}) {
        const Tok t = onlyToken(s);
        EXPECT_EQ(t.kind, cpp::String) << std::string(s, s + wcslen(s));
        EXPECT_EQ(t.text, s);
        EXPECT_EQ(t.flags, TokenFlag_None) << std::string(s, s + wcslen(s));
    }
    for (const wchar_t* s : {L"'a'", L"'\\''", L"L'x'", L"u8'c'", L"'ab'", L"'\\n'", L"'a'_c"}) {
        const Tok t = onlyToken(s);
        EXPECT_EQ(t.kind, cpp::CharLiteral) << std::string(s, s + wcslen(s));
        EXPECT_EQ(t.text, s);
        EXPECT_EQ(t.flags, TokenFlag_None);
    }
    EXPECT_EQ(onlyToken(L"''").flags & TokenFlag_Invalid, TokenFlag_Invalid) << "an empty character literal";
    // Prefix letters on their own are identifiers.
    EXPECT_EQ(kinds(L"L u8 R"), (std::vector<TokenKind>{cpp::Identifier, cpp::Identifier, cpp::Identifier}));
}

TEST(CppLexer, AStringEndsAtABareLineBreakUnterminated) {
    const std::vector<Tok> toks = significant(L"\"abc\nint");
    ASSERT_EQ(toks.size(), 2u);
    EXPECT_EQ(toks[0].kind, cpp::String);
    EXPECT_EQ(toks[0].text, L"\"abc");
    EXPECT_EQ(toks[0].flags & TokenFlag_Unterminated, TokenFlag_Unterminated);
    EXPECT_EQ(toks[1].kind, cpp::Keyword);
    EXPECT_EQ(significant(L"'x").front().flags & TokenFlag_Unterminated, TokenFlag_Unterminated);
    EXPECT_EQ(significant(L"\"eof").front().flags & TokenFlag_Unterminated, TokenFlag_Unterminated);
}

TEST(CppLexer, ABackslashBeforeTheBreakContinuesAString) {
    const std::wstring src = L"\"abc\\\ndef\" int";
    const std::vector<Tok> whole = significant(src);
    ASSERT_EQ(whole.size(), 2u);
    EXPECT_EQ(whole[0].text, L"\"abc\\\ndef\"");
    EXPECT_EQ(whole[0].flags, TokenFlag_None);

    // Split: the pieces carry the state.
    LexState exit;
    const std::vector<HighlightSpan> first = highlightLine(cppLanguage(), L"\"abc\\\n", {}, &exit);
    ASSERT_EQ(first.size(), 1u);
    EXPECT_EQ(first[0].flags & TokenFlag_Continued, TokenFlag_Continued);
    EXPECT_EQ(exit.mode, static_cast<std::uint32_t>(cpp::InString));
    EXPECT_EQ(exit.aux, static_cast<std::uint32_t>(L'"'));

    LexState after;
    const std::vector<HighlightSpan> second = highlightLine(cppLanguage(), L"def\" int", exit, &after);
    ASSERT_EQ(second.size(), 2u);
    EXPECT_EQ(second[0].flags & TokenFlag_Resumed, TokenFlag_Resumed);
    EXPECT_EQ(second[0].length, 4u);
    EXPECT_EQ(second[1].kind, cpp::Keyword);
    EXPECT_EQ(after, LexState{});
}

// ---- raw strings -------------------------------------------------------------------------------

TEST(CppLexer, RawStringsAreOneTokenWhateverIsInsideThem) {
    for (const wchar_t* s : {L"R\"(a)\"", L"R\"xy(a)\"b)xy\"", L"u8R\"(x)\"", L"LR\"d(\\n\"quoted\" // not a comment)d\"",
             L"R\"()\"", L"R\"(a\nb\n)\"", L"R\"(x)\"_udl"}) {
        const Tok t = onlyToken(s);
        EXPECT_EQ(t.kind, cpp::RawString) << std::string(s, s + wcslen(s));
        EXPECT_EQ(t.text, s);
        EXPECT_EQ(t.flags, TokenFlag_None);
    }
}

TEST(CppLexer, ARawStringSpanningLinesCarriesItsDelimiterInTheState) {
    LexState state;
    highlightLine(cppLanguage(), L"auto s = R\"abc(line one\n", {}, &state);
    EXPECT_EQ(state.mode, static_cast<std::uint32_t>(cpp::InRawString));
    EXPECT_EQ(cpp::rawStringDelimiter(state.aux), L"abc");

    // A ")" that isn't the delimiter's closer doesn't end it.
    LexState middle;
    const std::vector<HighlightSpan> spans = highlightLine(cppLanguage(), L"  ) )ab\" )abc\n", state, &middle);
    ASSERT_EQ(spans.size(), 1u);
    EXPECT_EQ(spans[0].kind, cpp::RawString);
    EXPECT_EQ(spans[0].flags & (TokenFlag_Resumed | TokenFlag_Continued), TokenFlag_Resumed | TokenFlag_Continued);
    EXPECT_EQ(middle, state);

    LexState after;
    const std::vector<HighlightSpan> last = highlightLine(cppLanguage(), L"end)abc\"; int y;", middle, &after);
    ASSERT_EQ(last.size(), 5u);   // the tail of the string, ';', int, y, ';' - trivia omitted
    EXPECT_EQ(last[0].kind, cpp::RawString);
    EXPECT_EQ(last[0].length, 8u);
    EXPECT_EQ(after, LexState{});
}

TEST(CppLexer, ABadRawStringDelimiterDegradesToAFlaggedOrdinaryString) {
    const std::wstring tooLong = L"R\"12345678901234567(x)12345678901234567\"";
    const std::vector<Token> toks = lexAll(tooLong);
    EXPECT_TRUE(lossless(tooLong, toks));
    bool flagged = false;
    for (const Token& t : toks) flagged = flagged || (t.kind == cpp::String && t.has(TokenFlag_Invalid));
    EXPECT_TRUE(flagged);

    EXPECT_EQ(significant(L"R\"(open").front().flags & TokenFlag_Unterminated, TokenFlag_Unterminated);
    // A space in the delimiter is never valid.
    const std::vector<Tok> spaced = significant(L"R\"a b(x)a b\"");
    ASSERT_FALSE(spaced.empty());
    EXPECT_EQ(spaced[0].kind, cpp::String);
    EXPECT_EQ(spaced[0].flags & TokenFlag_Invalid, TokenFlag_Invalid);
}

// ---- comments ----------------------------------------------------------------------------------

TEST(CppLexer, LineAndBlockComments) {
    EXPECT_EQ(onlyToken(L"// a comment").kind, kind::LineComment);
    EXPECT_EQ(onlyToken(L"/* a */").kind, kind::BlockComment);
    EXPECT_EQ(onlyToken(L"/**/").text, L"/**/");
    EXPECT_EQ(onlyToken(L"/*/ x */").text, L"/*/ x */") << "\"/*/\" opens a comment, it doesn't close one";
    EXPECT_EQ(onlyToken(L"/* a\n b\n */").text, L"/* a\n b\n */") << "one token when not splitting";
    EXPECT_EQ(onlyToken(L"/* open").flags & TokenFlag_Unterminated, TokenFlag_Unterminated);
    EXPECT_EQ(texts(L"a / b /c"), (std::vector<std::wstring>{L"a", L"/", L"b", L"/", L"c"}));
    EXPECT_EQ(texts(L"a// x\nb"), (std::vector<std::wstring>{L"a", L"// x", L"b"}));
}

TEST(CppLexer, ABackslashBeforeTheBreakContinuesALineComment) {
    const std::vector<Tok> toks = significant(L"// a \\\n b\nint");
    ASSERT_EQ(toks.size(), 2u);
    EXPECT_EQ(toks[0].text, L"// a \\\n b");
    EXPECT_EQ(toks[1].kind, cpp::Keyword);

    LexState exit;
    highlightLine(cppLanguage(), L"// a \\\n", {}, &exit);
    EXPECT_EQ(exit.mode, static_cast<std::uint32_t>(cpp::InLineComment));
    LexState after;
    const std::vector<HighlightSpan> next = highlightLine(cppLanguage(), L" b", exit, &after);
    ASSERT_EQ(next.size(), 1u);
    EXPECT_EQ(next[0].kind, kind::LineComment);
    EXPECT_EQ(after, LexState{});
    // A comment that just ends in a backslash at the very end of the file has nothing to continue.
    EXPECT_EQ(significant(L"// x \\").size(), 1u);
}

TEST(CppLexer, ABlankLineEndsASplicedCommentStringOrDirective) {
    // A backslash splices the next line in, and only that line: a blank one after it ends the
    // comment / string / directive, so what follows lexes as ordinary code.
    struct Case { const wchar_t* src; std::vector<TokenKind> expected; };
    const Case cases[] = {
        {L"// a \\\n\nint x;", {kind::LineComment, cpp::Keyword, cpp::Identifier, cpp::Semicolon}},
        {L"\"a\\\n\nint x;", {cpp::String, cpp::Keyword, cpp::Identifier, cpp::Semicolon}},
        {L"#define A \\\n\n#include <v>", {cpp::Directive, cpp::Identifier, cpp::Directive, cpp::HeaderName}},
        {L"// a \\\r\n\r\nint x;", {kind::LineComment, cpp::Keyword, cpp::Identifier, cpp::Semicolon}},
    };
    for (const Case& c : cases) {
        for (bool split : {false, true}) {
            std::vector<TokenKind> got;
            for (const Tok& t : significant(c.src, split)) got.push_back(t.kind);
            EXPECT_EQ(got, c.expected) << shown(c.src) << " split=" << split;
        }
        SyntaxHighlighter h(cppLanguage());
        h.setText(c.src);
        EXPECT_EQ(h.exitState(2), LexState{}) << shown(c.src) << " (the line after the blank one)";
    }

    // ...whereas a line that does end in a backslash carries on (an empty line can't, it has none).
    EXPECT_EQ(significant(L"// a \\\n b\nint", true).size(), 3u);   // comment, comment, int
}

TEST(CppLexer, ABlockCommentSpanningLinesCarriesItsStateWhenSplit) {
    LexState exit;
    const std::vector<HighlightSpan> first = highlightLine(cppLanguage(), L"int a; /* one\n", {}, &exit);
    ASSERT_EQ(first.size(), 4u);
    EXPECT_EQ(first[3].kind, kind::BlockComment);
    EXPECT_EQ(first[3].flags & TokenFlag_Continued, TokenFlag_Continued);
    EXPECT_EQ(exit.mode, static_cast<std::uint32_t>(cpp::InBlockComment));

    LexState after;
    const std::vector<HighlightSpan> second = highlightLine(cppLanguage(), L"   two */ int b;", exit, &after);
    ASSERT_GE(second.size(), 3u);
    EXPECT_EQ(second[0].kind, kind::BlockComment);
    EXPECT_EQ(second[0].flags & TokenFlag_Resumed, TokenFlag_Resumed);
    EXPECT_EQ(second[0].column, 0u) << "the continuation line's leading spaces belong to the comment";
    EXPECT_EQ(second[1].kind, cpp::Keyword);
    EXPECT_EQ(after, LexState{});
}

// ---- operators ---------------------------------------------------------------------------------

TEST(CppLexer, OperatorsAreTakenLongestFirst) {
    struct Case { const wchar_t* src; std::vector<std::wstring> expected; };
    const Case cases[] = {
        {L"a<<=b", {L"a", L"<<=", L"b"}},
        {L"a>>=b", {L"a", L">>=", L"b"}},
        {L"x->*y", {L"x", L"->*", L"y"}},
        {L"a<=>b", {L"a", L"<=>", L"b"}},
        {L"s::t", {L"s", L"::", L"t"}},
        {L"f(...)", {L"f", L"(", L"...", L")"}},
        {L"a>>b", {L"a", L">>", L"b"}},
        {L"a<b>c", {L"a", L"<", L"b", L">", L"c"}},
        {L"x++ + ++y", {L"x", L"++", L"+", L"++", L"y"}},
        {L"a&&b&c", {L"a", L"&&", L"b", L"&", L"c"}},
        {L"p->q.r", {L"p", L"->", L"q", L".", L"r"}},
        {L"a.*b", {L"a", L".*", L"b"}},
        {L"a!=b==c", {L"a", L"!=", L"b", L"==", L"c"}},
        {L"a?b:c", {L"a", L"?", L"b", L":", L"c"}},
        {L"~a", {L"~", L"a"}},
    };
    for (const Case& c : cases) EXPECT_EQ(texts(c.src), c.expected) << std::string(c.src, c.src + wcslen(c.src));
    for (const Token& t : lexAll(L"<<=")) EXPECT_EQ(t.cls, TokenClass::Operator);
}

TEST(CppLexer, StrayCharactersAreErrorTokensAndLexingCarriesOn) {
    const std::vector<Tok> toks = significant(L"a @ b ` c \\ d");
    ASSERT_EQ(toks.size(), 7u);
    EXPECT_EQ(toks[1].kind, kind::Error);
    EXPECT_EQ(toks[3].kind, kind::Error);
    EXPECT_EQ(toks[5].kind, kind::Error);
    EXPECT_EQ(toks[6].kind, cpp::Identifier);
}

// ---- the preprocessor --------------------------------------------------------------------------

TEST(CppLexer, IncludeTakesAHeaderNameOrAString) {
    std::vector<Tok> toks = significant(L"#include <vector>");
    ASSERT_EQ(toks.size(), 2u);
    EXPECT_EQ(toks[0].kind, cpp::Directive);
    EXPECT_EQ(toks[0].text, L"#include");
    EXPECT_EQ(toks[1].kind, cpp::HeaderName);
    EXPECT_EQ(toks[1].text, L"<vector>");

    toks = significant(L"#include \"x.h\"");
    ASSERT_EQ(toks.size(), 2u);
    EXPECT_EQ(toks[1].kind, cpp::String);

    toks = significant(L"#  include<map>");
    ASSERT_EQ(toks.size(), 2u);
    EXPECT_EQ(toks[0].text, L"#  include");
    EXPECT_EQ(toks[1].text, L"<map>");

    toks = significant(L"#import <a/b.h> // c");
    ASSERT_EQ(toks.size(), 3u);
    EXPECT_EQ(toks[1].kind, cpp::HeaderName);

    toks = significant(L"#include <vector");
    ASSERT_EQ(toks.size(), 2u);
    EXPECT_EQ(toks[1].kind, cpp::HeaderName);
    EXPECT_EQ(toks[1].flags & TokenFlag_Unterminated, TokenFlag_Unterminated);

    // Only the first '<' after the directive is a header name; and other directives have none.
    EXPECT_EQ(kinds(L"#include <a> <b"), (std::vector<TokenKind>{cpp::Directive, cpp::HeaderName, cpp::Operator, cpp::Identifier}));
    EXPECT_EQ(kinds(L"#if a < b"), (std::vector<TokenKind>{cpp::Directive, cpp::Identifier, cpp::Operator, cpp::Identifier}));
    for (const Token& t : lexAll(L"#include <x>")) if (t.kind == cpp::Directive) EXPECT_EQ(t.cls, TokenClass::Preprocessor);
}

TEST(CppLexer, AHashIsADirectiveOnlyFirstOnItsLine) {
    EXPECT_EQ(kinds(L"# define X(a) a##b"),
        (std::vector<TokenKind>{cpp::Directive, cpp::Identifier, cpp::LParen, cpp::Identifier, cpp::RParen,
                                cpp::Identifier, cpp::Hash, cpp::Identifier}));
    EXPECT_EQ(texts(L"# define X")[0], L"# define");
    EXPECT_EQ(kinds(L"#pragma once"), (std::vector<TokenKind>{cpp::Directive, cpp::Identifier}));
    EXPECT_EQ(kinds(L"#"), (std::vector<TokenKind>{cpp::Directive})) << "the null directive";

    // Code before it on the line: it's the stringize operator.
    EXPECT_EQ(kinds(L"int a; #define X"), (std::vector<TokenKind>{cpp::Keyword, cpp::Identifier, cpp::Semicolon,
        cpp::Hash, cpp::Identifier, cpp::Identifier}));
    // A comment before it isn't code.
    const std::vector<Tok> commented = significant(L"/* c */ #define X");
    ASSERT_EQ(commented.size(), 3u);
    EXPECT_EQ(commented[1].kind, cpp::Directive);
    EXPECT_EQ(commented[1].text, L"#define");
    // Indented is still first.
    EXPECT_EQ(significant(L"   \t#if 1").front().kind, cpp::Directive);
    EXPECT_EQ(texts(L"a # b ## c"), (std::vector<std::wstring>{L"a", L"#", L"b", L"##", L"c"}));
}

TEST(CppLexer, ADirectiveContinuesOverBackslashNewline) {
    const std::wstring src = L"#define A \\\n  1 + 2\nint x;";
    const std::vector<Token> toks = lexAll(src);
    EXPECT_TRUE(lossless(src, toks));
    std::size_t splices = 0;
    for (const Token& t : toks) {
        if (t.kind == kind::Whitespace && t.length == 1 && src[t.offset] == L'\\') {
            ++splices;
            EXPECT_TRUE(isTrivia(t.cls));
        }
    }
    EXPECT_EQ(splices, 1u);

    // On the spliced line a '#' is the stringize operator, not a directive; the next real line's is.
    EXPECT_EQ(kinds(L"#define A \\\n#B\n#undef A"),
        (std::vector<TokenKind>{cpp::Directive, cpp::Identifier, cpp::Hash, cpp::Identifier, cpp::Directive, cpp::Identifier}));

    // Split mode: the state says a directive is still open, and closes with the last line.
    LexState open;
    highlightLine(cppLanguage(), L"#define A \\\n", {}, &open);
    EXPECT_EQ(open.mode & cpp::InDirective, static_cast<std::uint32_t>(cpp::InDirective));
    LexState done;
    highlightLine(cppLanguage(), L"  1 + 2", open, &done);
    EXPECT_EQ(done, LexState{});
    LexState plain;
    highlightLine(cppLanguage(), L"#define A 1", {}, &plain);
    EXPECT_EQ(plain, LexState{}) << "a directive that doesn't continue leaves no state behind";
    highlightLine(cppLanguage(), L"#include <vector>", {}, &plain);
    EXPECT_EQ(plain, LexState{});
}

TEST(CppLexer, ABlockCommentInsideADirectiveKeepsTheDirectiveGoingWhenItCloses) {
    LexState open;
    highlightLine(cppLanguage(), L"#define A /* one\n", {}, &open);
    EXPECT_EQ(open.mode & cpp::MultiLineMask, static_cast<std::uint32_t>(cpp::InBlockComment));
    EXPECT_EQ(open.mode & cpp::InDirective, static_cast<std::uint32_t>(cpp::InDirective));

    LexState mid;
    const std::vector<HighlightSpan> spans = highlightLine(cppLanguage(), L"two */ #x", open, &mid);
    ASSERT_EQ(spans.size(), 3u);   // the rest of the comment, '#', x
    EXPECT_EQ(spans[1].kind, cpp::Hash) << "still inside the directive, so not a new one";
    EXPECT_EQ(mid, LexState{});
}

TEST(CppLexer, ALineSpliceOutsideDirectivesAndCommentsIsJustTrivia) {
    const std::wstring src = L"int x = 1 + \\\n 2;";
    EXPECT_TRUE(lossless(src, lexAll(src)));
    EXPECT_EQ(texts(src), (std::vector<std::wstring>{L"int", L"x", L"=", L"1", L"+", L"2", L";"}));
}

// ---- line breaks -------------------------------------------------------------------------------

TEST(CppLexer, LineBreaksAreLfCrLfAndALoneCr) {
    const std::wstring src = L"a\nb\r\nc\rd";
    const std::vector<Token> toks = lexAll(src);
    ASSERT_EQ(toks.size(), 7u);   // a NL b CRLF c CR d
    EXPECT_EQ(toks[4].line, 2u);
    EXPECT_EQ(toks[6].line, 3u) << "a lone CR starts a line";
    EXPECT_EQ(toks[6].column, 0u);

    // U+2028 isn't a line break in C++ (it is in JSON5).
    const std::wstring separator = L"a\u2028b";
    for (const Token& t : lexAll(separator)) EXPECT_NE(t.cls, TokenClass::Newline);
}

namespace {
// Set CPPLEXER_SOAK=N to run N times as many random inputs in the property tests.
int rounds(int base) {
    const char* soak = std::getenv("CPPLEXER_SOAK");
    const int factor = soak != nullptr ? std::atoi(soak) : 1;
    return base * (factor > 0 ? factor : 1);
}

}  // namespace

// ---- properties --------------------------------------------------------------------------------

namespace {

std::wstring randomSource(std::mt19937& rng) {
    static const wchar_t* pieces[] = {
        L"int", L" ", L"  ", L"\n", L"\r\n", L"\r", L"x", L"foo", L"1", L"0x", L"1'0", L"1e+", L".5", L"3.14f",
        L"\"", L"'", L"\\", L"\\\n", L"\"str\"", L"'c'", L"u8\"", L"L'", L"R\"", L"R\"d(", L")d\"", L")\"", L"(", L")",
        L"/*", L"*/", L"//", L"/", L"*", L"#", L"##", L"#include", L"#define", L" <", L">", L"<", L"<<=", L"->", L"...",
        L"{", L"}", L";", L",", L"@", L"`", L"u", L"_", L"$", L"\\u00e9", L"\\u", L"nullptr", L"true", L"::", L"?",
    };
    std::wstring s;
    const std::size_t count = rng() % 40;
    for (std::size_t i = 0; i < count; ++i) s += pieces[rng() % (sizeof(pieces) / sizeof(pieces[0]))];
    return s;
}

}  // namespace

TEST(CppLexerProperties, LosslessAndTerminatingInBothModesOnRandomFragments) {
    std::mt19937 rng(42);
    for (int round = 0; round < rounds(3000); ++round) {
        const std::wstring src = randomSource(rng);
        for (bool split : {false, true}) {
            const std::vector<Token> toks = lexAll(src, split);
            ASSERT_TRUE(lossless(src, toks)) << "round " << round << " split " << split;
            if (split) {
                for (const Token& t : toks) {
                    if (t.cls == TokenClass::Newline) continue;
                    for (std::size_t i = t.offset; i < t.end(); ++i) {
                        ASSERT_TRUE(src[i] != L'\n' && src[i] != L'\r') << "a split token crosses a line: round " << round;
                    }
                }
            }
        }
    }
}

// Split mode cuts a multi-line token into per-line segments; glued back together they are what the
// whole-token lexer finds.
TEST(CppLexerProperties, SplitSegmentsGlueBackIntoTheWholeTokens) {
    std::mt19937 rng(7);
    for (int round = 0; round < rounds(3000); ++round) {
        const std::wstring src = randomSource(rng);
        const std::vector<Token> whole = lexAll(src, false);
        const std::vector<Token> split = lexAll(src, true);

        struct Glued { TokenKind kind; TokenClass cls; std::size_t offset; std::size_t end; std::uint8_t flags; };
        std::vector<Glued> glued;
        for (std::size_t i = 0; i < split.size(); ++i) {
            Glued g{split[i].kind, split[i].cls, split[i].offset, split[i].end(), split[i].flags};
            // An open block comment or raw string runs over blank lines (each its own Newline token);
            // a spliced string, // comment or directive reaches one line further and no more.
            const bool spansBlankLines = g.kind == kind::BlockComment || g.kind == cpp::RawString;
            while ((split[i].flags & TokenFlag_Continued) != 0) {
                // The line break, then (unless the file ends) the next segment.
                if (i + 1 < split.size() && split[i + 1].cls == TokenClass::Newline) {
                    g.end = split[i + 1].end();
                    ++i;
                    while (spansBlankLines && i + 1 < split.size() && split[i + 1].cls == TokenClass::Newline) {
                        g.end = split[i + 1].end();
                        ++i;
                    }
                } else {
                    break;
                }
                if (i + 1 < split.size() && (split[i + 1].flags & TokenFlag_Resumed) != 0) {
                    ++i;
                    g.end = split[i].end();
                    g.flags |= split[i].flags;
                } else {
                    g.flags |= TokenFlag_Unterminated;   // the file ended on the break
                    break;
                }
            }
            g.flags &= static_cast<std::uint8_t>(~(TokenFlag_Continued | TokenFlag_Resumed));
            glued.push_back(g);
        }

        ASSERT_EQ(glued.size(), whole.size()) << "round " << round << ": " << shown(src);
        for (std::size_t i = 0; i < glued.size(); ++i) {
            ASSERT_EQ(glued[i].kind, whole[i].kind) << "round " << round << " token " << i;
            ASSERT_EQ(glued[i].offset, whole[i].offset) << "round " << round << " token " << i;
            ASSERT_EQ(glued[i].end, whole[i].end()) << "round " << round << " token " << i;
            // (A trailing continued comment / string has no closing segment to carry its flag.)
            const std::uint8_t mask = static_cast<std::uint8_t>(~TokenFlag_Unterminated);
            ASSERT_EQ(glued[i].flags & mask, whole[i].flags & mask) << "round " << round << " token " << i;
        }
    }
}

TEST(CppLexerProperties, PeekChangesNothing) {
    std::mt19937 rng(11);
    for (int round = 0; round < rounds(1500); ++round) {
        const std::wstring src = randomSource(rng);
        for (bool split : {false, true}) {
            CppLexer plain(src, split ? splitOptions() : LexerOptions{});
            CppLexer peeking(src, split ? splitOptions() : LexerOptions{});
            for (;;) {
                const Token a = plain.next();
                const Token peeked = peeking.peek();
                const Token b = peeking.next();
                ASSERT_EQ(peeked.kind, b.kind) << "round " << round;
                ASSERT_EQ(peeked.length, b.length) << "round " << round;
                ASSERT_EQ(a.kind, b.kind) << "round " << round;
                ASSERT_EQ(a.offset, b.offset) << "round " << round;
                ASSERT_EQ(a.length, b.length) << "round " << round;
                ASSERT_EQ(a.flags, b.flags) << "round " << round;
                if (a.isEof()) break;
            }
        }
    }
}

// ---- highlighting ------------------------------------------------------------------------------

TEST(CppLanguage, StylesFollowTheTokenClasses) {
    SyntaxHighlighter h(cppLanguage());
    h.setText(L"#include <vector>\nint x = 42; // note\nauto s = \"hi\"; bool b = nullptr;\n/* a\n b */ char c = 'c';\n");
    auto styleAt = [&](std::size_t line, std::uint32_t column) {
        for (const HighlightSpan& s : h.spans(line))
            if (s.column <= column && column < s.column + s.length) return s.style;
        return StyleId::Default;
    };
    EXPECT_EQ(styleAt(0, 0), StyleId::Preprocessor);      // #include
    EXPECT_EQ(styleAt(0, 10), StyleId::String);           // <vector>
    EXPECT_EQ(styleAt(1, 0), StyleId::Keyword);           // int
    EXPECT_EQ(styleAt(1, 4), StyleId::Identifier);        // x
    EXPECT_EQ(styleAt(1, 8), StyleId::Number);            // 42
    EXPECT_EQ(styleAt(1, 12), StyleId::Comment);          // // note
    EXPECT_EQ(styleAt(2, 9), StyleId::String);            // "hi"
    EXPECT_EQ(styleAt(2, 24), StyleId::Constant);         // nullptr
    EXPECT_EQ(styleAt(3, 0), StyleId::Comment);           // /* a
    EXPECT_EQ(styleAt(4, 1), StyleId::Comment);           //  b */   (the state carried in)
    EXPECT_EQ(styleAt(4, 8), StyleId::Keyword);           // char
    EXPECT_EQ(styleAt(4, 16), StyleId::String);           // 'c'
    EXPECT_STREQ(cppLanguage().name(), L"C++");
}

TEST(CppLanguage, ProblemsAreFlaggedForSquiggles) {
    SyntaxHighlighter h(cppLanguage());
    h.setText(L"int a = 08; char c = ''; auto s = \"open\nint b = 0x;\n");
    bool sawInvalidNumber = false, sawEmptyChar = false, sawUnterminated = false;
    for (std::size_t line = 0; line < h.lineCount(); ++line) {
        for (const HighlightSpan& s : h.spans(line)) {
            if (!s.isProblem()) continue;
            if (s.kind == cpp::Number) sawInvalidNumber = true;
            if (s.kind == cpp::CharLiteral) sawEmptyChar = true;
            if (s.kind == cpp::String && (s.flags & TokenFlag_Unterminated) != 0) sawUnterminated = true;
        }
    }
    EXPECT_TRUE(sawInvalidNumber);
    EXPECT_TRUE(sawEmptyChar);
    EXPECT_TRUE(sawUnterminated);
}

TEST(CppLanguage, IncrementalHighlightingMatchesAFreshPassAfterEdits) {
    std::mt19937 rng(5);
    for (int round = 0; round < rounds(300); ++round) {
        std::wstring text;
        for (int i = 0; i < 6; ++i) text += randomSource(rng) + L"\n";
        SyntaxHighlighter incremental(cppLanguage());
        incremental.setText(text);
        for (std::size_t line = 0; line < incremental.lineCount(); ++line) incremental.spans(line);   // warm it up

        for (int edit = 0; edit < 6; ++edit) {
            const std::size_t at = rng() % (text.size() + 1);
            const std::size_t removed = rng() % 6;
            const std::wstring inserted = randomSource(rng).substr(0, rng() % 12);
            incremental.replace(at, removed, inserted);
            text = incremental.text();

            SyntaxHighlighter fresh(cppLanguage());
            fresh.setText(text);
            ASSERT_EQ(incremental.lineCount(), fresh.lineCount()) << "round " << round;
            for (std::size_t line = 0; line < fresh.lineCount(); ++line) {
                ASSERT_EQ(incremental.spans(line), fresh.spans(line)) << "round " << round << " edit " << edit << " line " << line;
                ASSERT_EQ(incremental.exitState(line), fresh.exitState(line)) << "round " << round << " line " << line;
            }
        }
    }
}
