#pragma once

// CMake language lexer (https://cmake.org/cmake/help/latest/manual/cmake-language.7.html) on top of
// lex::LexerBase.
//
// Notes for the parser and the highlighter:
//  * Context matters in one place: at the top level (outside any parentheses) a word is a CommandName
//    name; inside parentheses every word is an Unquoted argument. The paren depth is kept in
//    LexState::aux so a line can be lexed on its own.
//  * Arguments are Unquoted (maximal run up to whitespace, parentheses, '#' or a quote; backslash
//    escapes, ${...} references and $<...> generator expressions stay inside the token), Quoted
//    ("..." with backslash escapes, may span lines) or Bracket ([[...]], [=[...]=], ...).
//  * Comments are LineComment ('# ...') or BlockComment (bracket comment '#[[ ... ]]').
//  * An unterminated quoted argument or bracket argument/comment is flagged
//    TokenFlag_Unterminated; a stray word at the top level is an Unquoted token flagged
//    TokenFlag_Invalid. Nothing throws.
//  * splitAtNewlines: quoted arguments and bracket arguments/comments come out one segment per line;
//    LexState is { mode = cmake::Mode, aux = (paren depth << 16) | bracket '=' count }.

#include "lexer_base.h"

namespace lex {

namespace cmake {

enum Kind : TokenKind {
    LParen = kind::FirstLanguage,
    RParen,
    CommandName,  // a command name: a word at the top level
    Unquoted,  // an unquoted argument
    Quoted,    // "..."
    Bracket,   // [[...]] and its [=[...]=] forms
};

enum Mode : std::uint32_t {
    ModeCode = 0,
    InBracketComment = 1,
    InBracketArg = 2,
    InQuoted = 3,
};

}  // namespace cmake

class CMakeLexer : public LexerBase {
public:
    explicit CMakeLexer(std::wstring_view source, LexerOptions options = {}) noexcept;

protected:
    Scan scanToken() override;

private:
    // Bracket opener '[' '='* '[' at `ahead` code units past the cursor: its '=' count, or -1.
    int bracketLevelAt(std::size_t ahead) const noexcept;

    Scan scanBracket(bool comment, bool resumed);
    Scan scanQuoted(bool resumed);
    Scan scanUnquoted(bool atTopLevel);

    std::uint32_t depth() const noexcept { return state_.aux >> 16; }
    void setState(std::uint32_t mode, std::uint32_t level) noexcept;
};

}  // namespace lex
