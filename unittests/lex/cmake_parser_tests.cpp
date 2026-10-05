// Tests for lex::cmake::parse.

#include <lex/cmake_parser.h>

#include <gtest/gtest.h>

#include <fstream>
#include <iterator>
#include <string>

using namespace lex;

namespace {

std::wstring readFile(const char* path) {
    std::ifstream in(path, std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return std::wstring(bytes.begin(), bytes.end());   // CMake files here are ASCII
}

std::wstring slice(const std::wstring& src, std::size_t from, std::size_t to) { return src.substr(from, to - from); }

}  // namespace

TEST(CMakeParser, ACommandHasItsNameParenthesesAndEveryArgumentRange) {
    const std::wstring src = L"  add_library(core STATIC  src/a.cpp)\n";
    const cmake::ParseResult parsed = cmake::parse(src);

    ASSERT_TRUE(parsed.ok());
    ASSERT_EQ(parsed.commands.size(), 1u);
    const cmake::Command& c = parsed.commands[0];
    EXPECT_EQ(c.name, L"add_library");
    EXPECT_EQ(slice(src, c.offset, c.nameEnd), L"add_library");
    EXPECT_EQ(src[c.openParen], L'(');
    EXPECT_EQ(src[c.closeParen], L')');
    EXPECT_EQ(slice(src, c.offset, c.end), L"add_library(core STATIC  src/a.cpp)");
    EXPECT_TRUE(c.complete);
    ASSERT_EQ(c.args.size(), 3u);
    EXPECT_EQ(c.args[2].value, L"src/a.cpp");
    EXPECT_EQ(slice(src, c.args[2].offset, c.args[2].end), L"src/a.cpp");
    EXPECT_EQ(c.line, 0u);
}

TEST(CMakeParser, CommandNamesAreCaseInsensitive) {
    const cmake::ParseResult parsed = cmake::parse(L"ADD_Library(x)");

    ASSERT_EQ(parsed.commands.size(), 1u);
    EXPECT_TRUE(parsed.commands[0].is(L"add_library"));
    EXPECT_FALSE(parsed.commands[0].is(L"add_executable"));
    EXPECT_EQ(parsed.commands[0].name, L"ADD_Library") << "the name is kept as written";
}

TEST(CMakeParser, QuotedAndBracketArgumentsGiveTheirContentAndTheirWholeRange) {
    const std::wstring src = L"set(a \"x y\" [==[\nbracket]==] \"\")";
    const cmake::ParseResult parsed = cmake::parse(src);

    ASSERT_TRUE(parsed.ok());
    const auto& args = parsed.commands[0].args;
    ASSERT_EQ(args.size(), 4u);
    EXPECT_EQ(args[1].kind, cmake::ArgKind::Quoted);
    EXPECT_EQ(args[1].value, L"x y");
    EXPECT_EQ(slice(src, args[1].offset, args[1].end), L"\"x y\"");
    EXPECT_EQ(args[2].kind, cmake::ArgKind::Bracket);
    EXPECT_EQ(args[2].value, L"bracket") << "a newline right after the opener is not part of the value";
    EXPECT_EQ(args[3].value, L"");
}

TEST(CMakeParser, NestedParenthesesAreArgumentsOfTheirOwn) {
    const cmake::ParseResult parsed = cmake::parse(L"if((A OR B) AND C)\nendif()");

    ASSERT_TRUE(parsed.ok());
    const auto& args = parsed.commands[0].args;
    ASSERT_EQ(args.size(), 7u);
    EXPECT_TRUE(args[0].isParen());
    EXPECT_EQ(args[1].value, L"A");
    EXPECT_TRUE(args[4].isParen());
    EXPECT_EQ(parsed.commands[1].name, L"endif");
}

TEST(CMakeParser, DynamicArgumentsAreMarked) {
    const cmake::ParseResult parsed = cmake::parse(L"set(a b ${C} $<TARGET_FILE:x> \"${D}\" [[${E}]])");

    const auto& args = parsed.commands[0].args;
    ASSERT_EQ(args.size(), 6u);
    EXPECT_FALSE(args[1].isDynamic());
    EXPECT_TRUE(args[2].isDynamic());
    EXPECT_TRUE(args[3].isDynamic());
    EXPECT_TRUE(args[4].isDynamic());
    EXPECT_FALSE(args[5].isDynamic()) << "a bracket argument is literal";
}

TEST(CMakeParser, CommentsAreCollectedWhereverTheyAre) {
    const std::wstring src = L"# top\nset(a # inside\n  b #[[ block ]] c)\nproject # before paren\n(x)\n";
    const cmake::ParseResult parsed = cmake::parse(src);

    ASSERT_TRUE(parsed.ok());
    ASSERT_EQ(parsed.commands.size(), 2u);
    EXPECT_EQ(parsed.commands[0].args.size(), 3u) << "comments are not arguments";
    EXPECT_EQ(parsed.commands[1].name, L"project");
    ASSERT_EQ(parsed.comments.size(), 4u);
    EXPECT_EQ(slice(src, parsed.comments[0].offset, parsed.comments[0].end), L"# top");
    EXPECT_TRUE(parsed.comments[2].bracket);
}

TEST(CMakeParser, BlocksNestAndTheEnclosingOnesCanBeListed) {
    const std::wstring src =
        L"function(f)\n"
        L"  foreach(x a b)\n"
        L"    if(x)\n"
        L"      add_library(l)\n"
        L"    elseif(y)\n"
        L"      add_library(m)\n"
        L"    else()\n"
        L"    endif()\n"
        L"  endforeach()\n"
        L"endfunction()\n"
        L"add_library(top)\n";
    const cmake::ParseResult parsed = cmake::parse(src);
    ASSERT_TRUE(parsed.ok());
    ASSERT_EQ(parsed.commands.size(), 11u);

    const auto& l = parsed.commands[3];
    EXPECT_EQ(l.name, L"add_library");
    const std::vector<std::wstring> around = parsed.enclosing(l);
    ASSERT_EQ(around.size(), 3u);
    EXPECT_EQ(around[0], L"if");
    EXPECT_EQ(around[1], L"foreach");
    EXPECT_EQ(around[2], L"function");

    EXPECT_EQ(parsed.commands[4].parent, parsed.commands[2].parent) << "elseif is at the level of its if";
    EXPECT_EQ(parsed.commands[7].parent, parsed.commands[2].parent) << "endif too";
    EXPECT_EQ(parsed.commands[9].parent, -1) << "endfunction is back at the top";
    EXPECT_EQ(parsed.commands[10].parent, -1);
    EXPECT_TRUE(parsed.enclosing(parsed.commands[10]).empty());
    EXPECT_TRUE(parsed.commands[0].opensBlock());
    EXPECT_TRUE(parsed.commands[9].closesBlock());
}

TEST(CMakeParser, ACommandMissingItsClosingParenthesisEndsAtTheEndOfTheFile) {
    const std::wstring src = L"set(a b\n  c";
    const cmake::ParseResult parsed = cmake::parse(src);

    ASSERT_EQ(parsed.commands.size(), 1u);
    EXPECT_FALSE(parsed.commands[0].complete);
    EXPECT_EQ(parsed.commands[0].args.size(), 3u);
    EXPECT_EQ(parsed.commands[0].end, src.size());
    EXPECT_FALSE(parsed.ok());
}

TEST(CMakeParser, ANameWithoutParenthesesIsAnErrorAndParsingGoesOn) {
    const cmake::ParseResult parsed = cmake::parse(L"oops\nset(x 1)\n");

    EXPECT_FALSE(parsed.ok());
    ASSERT_EQ(parsed.commands.size(), 1u);
    EXPECT_EQ(parsed.commands[0].name, L"set");
}

TEST(CMakeParser, StrayTokensAndUnmatchedEndsAreReportedNotFatal) {
    const cmake::ParseResult parsed = cmake::parse(L"} ) \nendif()\nset(x)\nif(a)\n");

    EXPECT_FALSE(parsed.ok());
    ASSERT_EQ(parsed.commands.size(), 3u);
    EXPECT_EQ(parsed.commands[1].name, L"set");
    bool unclosed = false;
    bool unmatched = false;
    for (const auto& e : parsed.errors) {
        unclosed = unclosed || e.message.find(L"never closed") != std::wstring::npos;
        unmatched = unmatched || e.message.find(L"no matching") != std::wstring::npos;
    }
    EXPECT_TRUE(unclosed);
    EXPECT_TRUE(unmatched);
}

TEST(CMakeParser, AnEmptyFileHasNoCommands) {
    const cmake::ParseResult parsed = cmake::parse(L"");
    EXPECT_TRUE(parsed.ok());
    EXPECT_TRUE(parsed.commands.empty());
    EXPECT_TRUE(cmake::parse(L"# only a comment\n\n").commands.empty());
}

TEST(CMakeParser, LinesAreZeroBasedAndCrlfCountsOnce) {
    const cmake::ParseResult parsed = cmake::parse(L"a()\r\n\r\nb(x\r\ny)\r\n");

    ASSERT_EQ(parsed.commands.size(), 2u);
    EXPECT_EQ(parsed.commands[0].line, 0u);
    EXPECT_EQ(parsed.commands[1].line, 2u);
    EXPECT_EQ(parsed.commands[1].endLine, 3u);
    EXPECT_EQ(parsed.commands[1].args[1].line, 3u);
}

TEST(CMakeParser, ThisRepositorysOwnCMakeListsParsesCleanly) {
    const std::wstring src = readFile(CMAKE_LEXER_TEST_ROOT "/CMakeLists.txt");
    const cmake::ParseResult parsed = cmake::parse(src);

    EXPECT_TRUE(parsed.ok()) << (parsed.errors.empty() ? std::wstring() : parsed.errors[0].message);
    EXPECT_GT(parsed.commands.size(), 100u);
    for (const cmake::Command& c : parsed.commands) {
        EXPECT_EQ(src.compare(c.offset, c.name.size(), c.name), 0);
        EXPECT_TRUE(c.complete);
        for (const cmake::Argument& a : c.args) {
            EXPECT_EQ(src.compare(a.valueOffset, a.value.size(), a.value), 0) << c.name;
        }
    }
}

TEST(CMakeParser, ASourceCanBeAddedByAPatchAtTheLastArgumentAndTheResultReparses) {
    const std::wstring src =
        L"add_library(cpptools_codegen STATIC\n"
        L"    src/codegen/a.cpp\n"
        L"    include/a.h)   # keep this comment\n"
        L"target_link_libraries(cpptools_codegen PUBLIC x)\n";
    const cmake::ParseResult before = cmake::parse(src);
    ASSERT_EQ(before.commands.size(), 2u);
    const cmake::Command& add = before.commands[0];
    ASSERT_EQ(add.args.size(), 4u);

    // Insert after the last argument, on its own line with the indent of the one before it.
    std::wstring patched = src;
    patched.insert(add.args.back().end, L"\n    src/codegen/b.cpp");
    const cmake::ParseResult after = cmake::parse(patched);

    ASSERT_TRUE(after.ok());
    ASSERT_EQ(after.commands.size(), 2u);
    ASSERT_EQ(after.commands[0].args.size(), 5u);
    EXPECT_EQ(after.commands[0].args[4].value, L"src/codegen/b.cpp");
    EXPECT_EQ(after.comments.size(), 1u) << "the comment is untouched";
    EXPECT_EQ(patched.substr(patched.size() - 51), src.substr(src.size() - 51)) << "so is everything after the call";
}
