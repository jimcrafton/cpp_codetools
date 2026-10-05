#pragma once

// A CMake file as a list of command calls, each with the exact source range of its name, its
// parentheses and every argument, so an edit can be made as a text patch that leaves the rest of the
// file (formatting, comments) untouched.
//
// Error handling mirrors json5::parse(): parse() never throws and always terminates. Problems go in
// ParseResult::errors and the parser recovers (a stray token is skipped, a command missing its ')' ends
// at the end of input), so a half-edited file still gives a usable list. The ranges are right whether or
// not there are errors.
//
// What it does not do: evaluate anything. A variable reference or generator expression stays text in the
// argument; if/foreach/function are ordinary commands, related only by Command::parent.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace lex {
namespace cmake {

enum class ArgKind {
    Unquoted,
    Quoted,
    Bracket,
    Paren,   // a '(' or ')' nested inside the argument list, as in if((A OR B) AND C)
};

struct Argument {
    ArgKind kind = ArgKind::Unquoted;
    std::size_t offset = 0;        // the whole argument, quotes and brackets included
    std::size_t end = 0;           // one past its last code unit
    std::size_t valueOffset = 0;   // the text between the quotes / brackets (the argument itself when unquoted)
    std::size_t valueEnd = 0;
    std::wstring value;            // that text as written: escapes and ${...} are not resolved
    std::uint32_t line = 0;        // 0-based line of its first character

    bool isParen() const noexcept { return kind == ArgKind::Paren; }
    // Contains ${...}, $ENV{...} or a $<...> generator expression, so its value is not known statically.
    bool isDynamic() const noexcept;
};

struct Command {
    std::wstring name;             // as written ("target_link_libraries")
    std::size_t offset = 0;        // start of the name
    std::size_t end = 0;           // one past the ')' (or the last token of an unterminated command)
    std::size_t nameEnd = 0;
    std::size_t openParen = 0;     // offset of '('
    std::size_t closeParen = 0;    // offset of ')'; meaningless unless `complete`
    std::uint32_t line = 0;        // 0-based line of the name
    std::uint32_t endLine = 0;
    bool complete = true;          // false: the file ended before the closing ')'
    std::vector<Argument> args;

    // The index in ParseResult::commands of the if / foreach / while / function / macro / block this
    // command is inside, or -1 at the top level. elseif / else / end* sit at their opener's own level.
    int parent = -1;

    // Names are case-insensitive in CMake: is(L"add_library") matches ADD_LIBRARY. `lowerName` must
    // already be lower case.
    bool is(std::wstring_view lowerName) const noexcept;
    bool opensBlock() const noexcept;
    bool closesBlock() const noexcept;
};

struct Comment {
    std::size_t offset = 0;
    std::size_t end = 0;
    std::uint32_t line = 0;
    bool bracket = false;          // #[[ ... ]] rather than # ...
};

struct ParseError {
    std::size_t offset = 0;
    std::size_t length = 0;
    std::uint32_t line = 0;        // 0-based
    std::uint32_t column = 0;      // 0-based, UTF-16 code units
    std::wstring message;
};

struct ParseResult {
    std::vector<Command> commands;   // in source order
    std::vector<Comment> comments;   // in source order, including those inside a command's arguments
    std::vector<ParseError> errors;

    bool ok() const noexcept { return errors.empty(); }

    // The enclosing block openers of a command, innermost first ("foreach", "if", ...), lower case.
    std::vector<std::wstring> enclosing(const Command& command) const;
};

ParseResult parse(std::wstring_view source);

}  // namespace cmake
}  // namespace lex
