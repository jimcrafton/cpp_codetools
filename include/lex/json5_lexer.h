#pragma once

// JSON5 lexer (https://spec.json5.org) on top of lex::LexerBase.
//
// Notes for the parser and the highlighter:
//  * Object keys are context-sensitive. An unquoted key can lex as Identifier,
//    Null/True/False, or (for `Infinity` / `NaN`) Number -- use
//    isIdentifierName() on a token that is followed by ':'. Quoted keys are String.
//  * A '+' or '-' directly before a number, Infinity or NaN is part of the Number
//    token ("-Infinity", "+.5"). A sign that isn't followed by a number is a
//    Sign token (the parser reports the error).
//  * Escape sequences inside strings are not validated or split out; a string is
//    one token. Malformed numbers and \u identifier escapes are flagged
//    TokenFlag_Invalid.
//  * Line terminators are \n, \r\n, \r, U+2028 and U+2029 (forced on regardless
//    of the options passed in).
//  * splitAtNewlines: block comments and strings continued with `\<newline>` come
//    out one segment per line; LexState is { mode = json5::Mode, aux = quote char }.

#include "lexer_base.h"

namespace lex {

namespace json5 {

enum Kind : TokenKind {
    LBrace = kind::FirstLanguage,
    RBrace,
    LBracket,
    RBracket,
    Colon,
    Comma,
    Identifier,  // unquoted name (may include \uXXXX escapes)
    String,      // '...' or "..."
    Number,      // decimal, hex, Infinity, NaN, optionally signed
    True,
    False,
    Null,
    Sign,        // '+' or '-' not followed by a number
};

enum Mode : std::uint32_t {
    ModeCode = 0,
    InBlockComment = 1,
    InString = 2,  // state.aux is the opening quote character
};

}  // namespace json5

class Json5Lexer : public LexerBase {
public:
    explicit Json5Lexer(std::wstring_view source, LexerOptions options = {}) noexcept;

    // True if `t` can be an unquoted object key (JSON5 IdentifierName): an
    // identifier, null/true/false, or the words Infinity / NaN.
    bool isIdentifierName(const Token& t) const noexcept;

protected:
    Scan scanToken() override;

private:
    Scan scanBlockComment(bool resumed);
    Scan scanString(wchar_t quote, bool resumed);
    Scan scanNumber();
    Scan scanIdentifier();

    // Does `word` start `ahead` code units past the cursor and end at a word boundary?
    bool wordAt(std::size_t ahead, std::wstring_view word) const noexcept;
    bool numberFollowsSign() const noexcept;
};

}  // namespace lex
