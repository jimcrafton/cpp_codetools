// Tests for lex::CMakeLexer.

#include <lex/cmake_lexer.h>

#include <gtest/gtest.h>

#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

using namespace lex;

namespace {

std::vector<Token> lexAll(const std::wstring& src, bool split = false) {
    LexerOptions options;
    options.splitAtNewlines = split;
    CMakeLexer lexer(src, options);
    return lexer.tokenize();
}

std::wstring textOf(const std::wstring& src, const Token& t) { return src.substr(t.offset, t.length); }

// The tokens that carry meaning: not whitespace, newlines or comments.
std::vector<Token> significant(const std::vector<Token>& tokens) {
    std::vector<Token> out;
    for (const Token& t : tokens) {
        if (!isTrivia(t.cls) && t.cls != TokenClass::Comment) out.push_back(t);
    }
    return out;
}

// Every source character is in exactly one token, in order.
void expectLossless(const std::wstring& src, bool split = false) {
    std::wstring rebuilt;
    for (const Token& t : lexAll(src, split)) rebuilt += textOf(src, t);
    EXPECT_EQ(rebuilt, src);
}

std::wstring readFile(const char* path) {
    std::ifstream in(path, std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return std::wstring(bytes.begin(), bytes.end());   // CMake files here are ASCII
}

}  // namespace

TEST(CMakeLexer, ACommandCallIsANameParenthesesAndArguments) {
    const std::wstring src = L"add_library(core STATIC src/a.cpp)";
    const auto tokens = significant(lexAll(src));

    ASSERT_EQ(tokens.size(), 6u);
    EXPECT_EQ(tokens[0].kind, cmake::CommandName);
    EXPECT_EQ(tokens[0].cls, TokenClass::Keyword);
    EXPECT_EQ(textOf(src, tokens[0]), L"add_library");
    EXPECT_EQ(tokens[1].kind, cmake::LParen);
    EXPECT_EQ(tokens[2].kind, cmake::Unquoted);
    EXPECT_EQ(textOf(src, tokens[4]), L"src/a.cpp");
    EXPECT_EQ(tokens[5].kind, cmake::RParen);
}

TEST(CMakeLexer, InsideParenthesesAWordIsAnArgumentEvenIfItLooksLikeACommand) {
    const std::wstring src = L"if(NOT add_library)\nendif()";
    const auto tokens = significant(lexAll(src));

    EXPECT_EQ(tokens[0].kind, cmake::CommandName);
    EXPECT_EQ(tokens[2].kind, cmake::Unquoted);   // NOT
    EXPECT_EQ(tokens[3].kind, cmake::Unquoted);   // add_library
    EXPECT_EQ(tokens[4].kind, cmake::RParen);
    EXPECT_EQ(tokens[5].kind, cmake::CommandName) << "back at the top level after the ')'";
}

TEST(CMakeLexer, NestedParenthesesKeepTheCommandOpenUntilTheMatchingOne) {
    const std::wstring src = L"if((A OR B) AND C)\nset(x 1)";
    const auto tokens = significant(lexAll(src));

    int commands = 0;
    for (const Token& t : tokens) commands += t.kind == cmake::CommandName;
    EXPECT_EQ(commands, 2) << "if and set, not A or B";
}

TEST(CMakeLexer, ALineCommentRunsToTheEndOfTheLine) {
    const std::wstring src = L"# a comment (with parens)\nset(x 1) # trailing\n";
    const auto tokens = lexAll(src);

    EXPECT_EQ(tokens[0].kind, kind::LineComment);
    EXPECT_EQ(textOf(src, tokens[0]), L"# a comment (with parens)");
    EXPECT_EQ(tokens[1].kind, kind::Newline);
    bool trailing = false;
    for (const Token& t : tokens) trailing = trailing || textOf(src, t) == L"# trailing";
    EXPECT_TRUE(trailing);
}

TEST(CMakeLexer, ABracketCommentClosesOnlyAtItsOwnLevel) {
    const std::wstring src = L"#[==[ not over ]] nor ]=] yet ]==] set(x)";
    const auto tokens = lexAll(src);

    EXPECT_EQ(tokens[0].kind, kind::BlockComment);
    EXPECT_EQ(textOf(src, tokens[0]), L"#[==[ not over ]] nor ]=] yet ]==]");
    EXPECT_FALSE(tokens[0].has(TokenFlag_Unterminated));
}

TEST(CMakeLexer, AHashThatIsNotABracketOpenerIsAnOrdinaryComment) {
    const std::wstring src = L"#[ not a bracket\nset(x)";
    const auto tokens = lexAll(src);

    EXPECT_EQ(tokens[0].kind, kind::LineComment);
    EXPECT_EQ(textOf(src, tokens[0]), L"#[ not a bracket");
}

TEST(CMakeLexer, AQuotedArgumentKeepsItsEscapesAndMaySpanLines) {
    const std::wstring src = L"message(\"say \\\"hi\\\"\nsecond line\")";
    const auto tokens = significant(lexAll(src));

    ASSERT_EQ(tokens.size(), 4u);
    EXPECT_EQ(tokens[2].kind, cmake::Quoted);
    EXPECT_EQ(textOf(src, tokens[2]), L"\"say \\\"hi\\\"\nsecond line\"");
    EXPECT_EQ(tokens[2].line, 0u);
    EXPECT_EQ(tokens[2].endLine, 1u);
}

TEST(CMakeLexer, ABracketArgumentIsOneTokenWhateverIsInsideIt) {
    const std::wstring src = L"set(x [=[ ]] ) \" # ]=])";
    const auto tokens = significant(lexAll(src));

    ASSERT_EQ(tokens.size(), 5u);
    EXPECT_EQ(tokens[3].kind, cmake::Bracket);
    EXPECT_EQ(textOf(src, tokens[3]), L"[=[ ]] ) \" # ]=]");
}

TEST(CMakeLexer, VariableReferencesAndGeneratorExpressionsStayInsideTheirArgument) {
    const std::wstring src = L"target_link_libraries(t $<$<CONFIG:Debug>:a;b> ${DIR}/x ${a${b}} $ENV{PATH})";
    const auto tokens = significant(lexAll(src));

    std::vector<std::wstring> args;
    for (const Token& t : tokens) {
        if (t.kind == cmake::Unquoted) args.push_back(textOf(src, t));
    }
    ASSERT_EQ(args.size(), 5u);
    EXPECT_EQ(args[1], L"$<$<CONFIG:Debug>:a;b>");
    EXPECT_EQ(args[2], L"${DIR}/x");
    EXPECT_EQ(args[3], L"${a${b}}");
    EXPECT_EQ(args[4], L"$ENV{PATH}");
}

TEST(CMakeLexer, AQuotedPartInsideAnUnquotedArgumentIsPartOfIt) {
    const std::wstring src = L"add_definitions(-DNAME=\"a b\" -DX)";
    const auto tokens = significant(lexAll(src));

    ASSERT_EQ(tokens.size(), 5u);
    EXPECT_EQ(textOf(src, tokens[2]), L"-DNAME=\"a b\"");
    EXPECT_EQ(textOf(src, tokens[3]), L"-DX");
}

TEST(CMakeLexer, ABackslashEscapesTheNextCharacterOfAnUnquotedArgument) {
    const std::wstring src = L"set(x a\\ b\\)c)";
    const auto tokens = significant(lexAll(src));

    ASSERT_EQ(tokens.size(), 5u);
    EXPECT_EQ(textOf(src, tokens[3]), L"a\\ b\\)c");
}

TEST(CMakeLexer, AStrayWordAtTheTopLevelIsFlaggedNotLost) {
    const std::wstring src = L"}\nset(x 1)";
    const auto tokens = significant(lexAll(src));

    EXPECT_EQ(tokens[0].kind, cmake::Unquoted);
    EXPECT_TRUE(tokens[0].has(TokenFlag_Invalid));
    EXPECT_EQ(tokens[1].kind, cmake::CommandName);
}

TEST(CMakeLexer, UnterminatedTokensAreFlaggedAndTheEndIsStillReached) {
    for (const wchar_t* src : { L"message(\"never closed", L"set(x [[ never", L"#[[ never" }) {
        const auto tokens = lexAll(src);
        bool flagged = false;
        for (const Token& t : tokens) flagged = flagged || t.has(TokenFlag_Unterminated);
        EXPECT_TRUE(flagged) << src;
        expectLossless(src);
    }
}

TEST(CMakeLexer, TheTokensOfAnyInputAddUpToTheSource) {
    expectLossless(L"");
    expectLossless(L"\r\n\r\n   \t\r\n");
    expectLossless(L"cmake_minimum_required(VERSION 3.20)\r\nproject(x)\r\n# end");
    expectLossless(L"set(a\n  b # c\n  \"d\n e\" [[f]])\n");
    expectLossless(L"((()))  )))  \"  [[  #[[");
}

TEST(CMakeLexer, SplitModeEmitsOneSegmentPerLineAndStillAddsUp) {
    const std::wstring src = L"message(\"one\ntwo\nthree\") #[[a\nb]]\nset(x [=[\n..]=])";
    expectLossless(src, true);

    const auto tokens = lexAll(src, true);
    int continued = 0;
    int resumed = 0;
    for (const Token& t : tokens) {
        continued += t.has(TokenFlag_Continued);
        resumed += t.has(TokenFlag_Resumed);
        EXPECT_EQ(t.line, t.endLine) << "no segment crosses a line";
    }
    EXPECT_EQ(continued, resumed) << "each continued segment has a resumed one after it";
    EXPECT_GE(continued, 4);
}

TEST(CMakeLexer, ALineCanBeLexedAloneFromItsCheckpoint) {
    const std::wstring src = L"set(x\n  \"a\n  b\"\n  y)\nproject(p)";
    LexerOptions options;
    options.splitAtNewlines = true;

    CMakeLexer whole(src, options);
    std::vector<Token> expected = whole.tokenize();

    // Re-lex from the start of line 2 (inside the quoted argument) using the state the whole run had there.
    CMakeLexer first(src, options);
    Checkpoint atLine2;
    for (Token t = first.next(); !t.isEof(); t = first.next()) {
        if (t.line == 2) break;
        atLine2 = first.checkpoint();
    }
    CMakeLexer again(src, options);
    again.restore(atLine2);
    const std::vector<Token> rest = again.tokenize();

    ASSERT_FALSE(rest.empty());
    std::size_t k = expected.size() - rest.size();
    for (std::size_t n = 0; n < rest.size(); ++n, ++k) {
        EXPECT_EQ(rest[n].kind, expected[k].kind) << n;
        EXPECT_EQ(rest[n].offset, expected[k].offset) << n;
    }
}

TEST(CMakeLexer, NeverGetsStuckOnGarbage) {
    std::mt19937 random(12345);
    const std::wstring alphabet = L"ab_ ()#[]=\"\\$<>{}\n\r\t;-.A1";
    for (int round = 0; round < 300; ++round) {
        std::wstring src;
        const int length = static_cast<int>(random() % 60);
        for (int i = 0; i < length; ++i) src += alphabet[random() % alphabet.size()];
        expectLossless(src);
        expectLossless(src, true);
    }
}

TEST(CMakeLexer, ThisRepositorysOwnCMakeListsLexesToTheLastCharacter) {
    const std::wstring src = readFile(CMAKE_LEXER_TEST_ROOT "/CMakeLists.txt");
    ASSERT_GT(src.size(), 1000u);

    expectLossless(src);
    expectLossless(src, true);
    for (const Token& t : lexAll(src)) {
        EXPECT_FALSE(t.has(TokenFlag_Unterminated));
        EXPECT_FALSE(t.has(TokenFlag_Invalid)) << textOf(src, t);
    }
}
