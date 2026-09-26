#pragma once

// C++ lexer on top of lex::LexerBase - lexical only (translation phase 3 and the preprocessor's
// view of a line): it doesn't expand macros, evaluate #if, or know about templates, so ">>" is one
// token and "a < b" vs "a<b>" is the parser's business.
//
// What it does:
//  * Lossless and never fails, like the base: every character is in exactly one token.
//  * Keywords (C++23 plus override / final and a few MSVC ones) are Keyword; true / false / nullptr
//    are Constant. Everything else identifier-shaped is Identifier (UCNs like \u00e9 included).
//  * Numbers are C++ pp-numbers: 42, 0x1F, 0b101, 1'000'000, 3.14f, 1e-9, 0x1.8p3, 12ULL, and
//    user-defined suffixes (1_km). Obviously malformed ones (0x, 1e, 0b2, 08) are flagged
//    TokenFlag_Invalid.
//  * Strings and character literals, with u8 / u / U / L prefixes, escapes, and a user-defined
//    suffix ("abc"s) inside the same token. A raw string R"delim(...)delim" is one RawString token
//    (a bad delimiter degrades to an ordinary, Invalid-flagged string). An unclosed string or
//    character literal ends at the line break and is flagged Unterminated; a backslash before the
//    break continues it.
//  * Comments: // (a trailing backslash continues it) and /* */.
//  * Preprocessor: "#" as the first thing on a line (only whitespace or comments before it) and its
//    name are one Directive token ("#include", "# define"); the rest of the line lexes normally,
//    except that after include / import / include_next a <...> is one HeaderName token. A directive
//    continues over backslash-newline. Elsewhere '#' and '##' are Hash tokens (stringize / paste).
//  * A backslash immediately before a line break is a line splice: its own one-character
//    Whitespace-class token (so it is trivia, and a directive or comment carries on past it).
//    Splices inside identifiers, numbers and operators are not joined up.
//  * Line terminators are \n, \r\n and a lone \r (C++ ignores U+2028 / U+2029).
//  * splitAtNewlines: block comments, raw strings, continued strings and continued comments come
//    out one segment per line, and LexState carries the rest: mode = cpp::Mode bits (the multi-line
//    construct, plus InDirective while a directive continues), aux = the quote character for a
//    continued string or the interned delimiter for a raw string.

#include "lexer_base.h"

#include <string>

namespace lex {

namespace cpp {

enum Kind : TokenKind {
    Identifier = kind::FirstLanguage,
    Keyword,
    Constant,     // true / false / nullptr
    Number,
    String,       // "..." with an optional prefix and suffix
    RawString,    // R"delim( ... )delim"
    CharLiteral,  // '...' with an optional prefix and suffix
    HeaderName,   // <...> after #include
    Directive,    // "#include", "#  define", ... (the '#' and the name)
    LBrace,
    RBrace,
    LParen,
    RParen,
    LBracket,
    RBracket,
    Semicolon,
    Comma,
    Hash,         // '#' or '##' that isn't starting a directive
    Operator,     // every other operator / punctuator, longest match ("<<=", "->*", "<=>", "::", "...")
};

// The low byte says which multi-line construct is open; InDirective is a flag on top of it (a
// directive line that continues, possibly with a comment or string still open inside it).
enum Mode : std::uint32_t {
    ModeCode = 0,
    InBlockComment = 1,
    InLineComment = 2,  // a // comment continued by a trailing backslash
    InString = 3,       // aux = the quote character
    InRawString = 4,    // aux = the interned delimiter (see rawStringDelimiter())
    MultiLineMask = 0xFF,
    InDirective = 0x100,
    ExpectHeaderName = 0x200,  // just after #include / #import (only ever set inside a directive)
};

// The delimiter text behind a raw string's LexState::aux ("" for the empty delimiter). Ids are
// handed out by the lexer as it meets delimiters; the table is process-wide and bounded (past the
// bound every new delimiter shares one id and is matched as ")\"").
std::wstring rawStringDelimiter(std::uint32_t id);

}  // namespace cpp

class CppLexer : public LexerBase {
public:
    explicit CppLexer(std::wstring_view source, LexerOptions options = {}) noexcept;

protected:
    Scan scanToken() override;

private:
    Scan scanOne();
    Scan scanBlockComment(bool resumed);
    Scan scanLineComment(bool resumed);
    // The opening quote is already consumed (with `hasPrefix` whatever came before it).
    Scan scanQuoted(wchar_t quote, bool resumed);
    Scan scanRawString(bool resumed);
    Scan scanNumber();
    Scan scanIdentifier();
    Scan scanHeaderName();
    Scan scanDirective();
    Scan scanOperator();

    // Only whitespace and complete /* */ comments between the start of the line and the cursor.
    bool atDirectiveStart() const noexcept;
    // Whether the line before this one ends in a backslash (true when there is no line before it
    // to look at: a line lexed on its own, with its entry state given).
    bool previousLineEndsWithBackslash() const noexcept;
    // After a token: the directive flag ends with the line unless it was spliced or a multi-line
    // token is still open.
    void updateDirectiveState(const Scan& scan) noexcept;

    bool inDirective() const noexcept { return (state_.mode & cpp::InDirective) != 0; }
    std::uint32_t multiLine() const noexcept { return state_.mode & cpp::MultiLineMask; }
    // Sets which construct is open, keeping the directive flags.
    void setMultiLine(std::uint32_t mode, std::uint32_t aux = 0) noexcept {
        state_.mode = (state_.mode & (cpp::InDirective | cpp::ExpectHeaderName)) | mode;
        state_.aux = aux;
    }

    // Everything that outlives a token is in state_ (so checkpoint() / restore() / peek() are
    // exact); this is only the last scanToken()'s own note to updateDirectiveState().
    bool lastWasSplice_ = false;
};

}  // namespace lex
