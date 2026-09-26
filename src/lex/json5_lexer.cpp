#include <lex/json5_lexer.h>

namespace lex {

namespace {
LexerOptions withJson5Options(LexerOptions o) noexcept {
    o.unicodeLineSeparators = true;
    return o;
}
}  // namespace

Json5Lexer::Json5Lexer(std::wstring_view source, LexerOptions options) noexcept
    : LexerBase(source, withJson5Options(options)) {}

bool Json5Lexer::isIdentifierName(const Token& t) const noexcept {
    switch (t.kind) {
    case json5::Identifier:
    case json5::Null:
    case json5::True:
    case json5::False:
        return true;
    case json5::Number: {
        const std::wstring_view s = text(t);
        return s == L"Infinity" || s == L"NaN";
    }
    default:
        return false;
    }
}

bool Json5Lexer::wordAt(std::size_t ahead, std::wstring_view word) const noexcept {
    return source().substr(pos() + ahead, word.size()) == word &&
           !chars::isIdentPart(peekAt(ahead + word.size()));
}

bool Json5Lexer::numberFollowsSign() const noexcept {
    const wchar_t n = peekAt(1);
    return chars::isDigit(n) || (n == L'.' && chars::isDigit(peekAt(2))) ||
           wordAt(1, L"Infinity") || wordAt(1, L"NaN");
}

LexerBase::Scan Json5Lexer::scanToken() {
    // Resume a multi-line token first (split mode only ever leaves state set).
    if (state_.mode == json5::InBlockComment) return scanBlockComment(true);
    if (state_.mode == json5::InString) return scanString(static_cast<wchar_t>(state_.aux), true);

    const wchar_t c = cur();
    if (chars::isHorizontalSpace(c)) return scanWhitespace();

    switch (c) {
    case L'{': advance(); return {json5::LBrace, TokenClass::Punctuation};
    case L'}': advance(); return {json5::RBrace, TokenClass::Punctuation};
    case L'[': advance(); return {json5::LBracket, TokenClass::Punctuation};
    case L']': advance(); return {json5::RBracket, TokenClass::Punctuation};
    case L':': advance(); return {json5::Colon, TokenClass::Punctuation};
    case L',': advance(); return {json5::Comma, TokenClass::Punctuation};

    case L'"':
    case L'\'':
        advance();
        return scanString(c, false);

    case L'/':
        if (peekAt(1) == L'/') {
            acceptWhile([this](wchar_t ch) { return !isNewlineChar(ch); });
            return {kind::LineComment, TokenClass::Comment};
        }
        if (peekAt(1) == L'*') {
            advanceBy(2);
            return scanBlockComment(false);
        }
        return {kind::Error, TokenClass::Error};

    case L'+':
    case L'-':
        if (numberFollowsSign()) return scanNumber();
        advance();
        return {json5::Sign, TokenClass::Operator};

    case L'.':
        if (chars::isDigit(peekAt(1))) return scanNumber();
        return {kind::Error, TokenClass::Error};

    default:
        break;
    }

    if (chars::isDigit(c)) return scanNumber();
    if (chars::isIdentStart(c) || (c == L'\\' && peekAt(1) == L'u')) return scanIdentifier();
    return {kind::Error, TokenClass::Error};  // nothing consumed: the base eats one unit
}

LexerBase::Scan Json5Lexer::scanBlockComment(bool resumed) {
    std::uint8_t flags = resumed ? TokenFlag_Resumed : TokenFlag_None;
    if (consumeThrough(L"*/")) {
        state_ = {};
        return {kind::BlockComment, TokenClass::Comment, flags};
    }
    // Not closed: either EOF, or (split mode) the end of this line. Keep the
    // state either way so the line's exit state is right even at EOF.
    state_ = {json5::InBlockComment, 0};
    flags |= atEnd() ? TokenFlag_Unterminated : TokenFlag_Continued;
    return {kind::BlockComment, TokenClass::Comment, flags};
}

// The opening quote (if any) has already been consumed.
LexerBase::Scan Json5Lexer::scanString(wchar_t quote, bool resumed) {
    const bool split = options().splitAtNewlines;
    const std::uint8_t base = resumed ? TokenFlag_Resumed : TokenFlag_None;

    for (;;) {
        if (atEnd()) {
            state_ = {};
            return {json5::String, TokenClass::String, std::uint8_t(base | TokenFlag_Unterminated)};
        }
        const wchar_t c = cur();

        if (c == quote) {
            advance();
            state_ = {};
            return {json5::String, TokenClass::String, base};
        }

        if (c == L'\\') {
            if (pos() + 1 >= source().size()) {  // lone backslash at EOF
                advance();
                continue;
            }
            if (isNewlineChar(peekAt(1))) {  // line continuation
                advance();                   // the backslash
                if (split) {
                    state_ = {json5::InString, static_cast<std::uint32_t>(quote)};
                    return {json5::String, TokenClass::String, std::uint8_t(base | TokenFlag_Continued)};
                }
                consumeNewline();
                continue;
            }
            advance();  // backslash
            advance();  // escaped character; \x, \u, \0 ... are not validated here
            continue;
        }

        if (c == L'\n' || c == L'\r') {  // raw line break ends an unterminated string
            state_ = {};
            return {json5::String, TokenClass::String, std::uint8_t(base | TokenFlag_Unterminated)};
        }

        if (isNewlineChar(c)) {  // U+2028 / U+2029: legal unescaped inside a JSON5 string
            if (split) {
                state_ = {json5::InString, static_cast<std::uint32_t>(quote)};
                return {json5::String, TokenClass::String, std::uint8_t(base | TokenFlag_Continued)};
            }
        }
        advance();
    }
}

// Cursor is on the sign, digit or '.'. Any Infinity / NaN spelling has been
// verified by the caller (numberFollowsSign).
LexerBase::Scan Json5Lexer::scanNumber() {
    std::uint8_t flags = TokenFlag_None;

    if (cur() == L'+' || cur() == L'-') advance();

    if (cur() == L'I' || cur() == L'N') {
        acceptWhile(chars::isIdentPart);
        return {json5::Number, TokenClass::Number};
    }

    if (cur() == L'0' && (peekAt(1) == L'x' || peekAt(1) == L'X')) {
        advanceBy(2);
        if (acceptWhile(chars::isHexDigit) == 0) flags |= TokenFlag_Invalid;
    } else {
        const std::size_t digitsStart = pos();
        const std::size_t intDigits = acceptWhile(chars::isDigit);
        if (intDigits > 1 && source()[digitsStart] == L'0') flags |= TokenFlag_Invalid;  // "01"

        if (cur() == L'.') {
            advance();
            acceptWhile(chars::isDigit);  // "5." is legal in JSON5
        }
        if (cur() == L'e' || cur() == L'E') {
            advance();
            if (cur() == L'+' || cur() == L'-') advance();
            if (acceptWhile(chars::isDigit) == 0) flags |= TokenFlag_Invalid;  // "1e"
        }
    }

    // A number may not run straight into an identifier character ("12abc").
    if (chars::isIdentPart(cur())) {
        acceptWhile(chars::isIdentPart);
        flags |= TokenFlag_Invalid;
    }
    return {json5::Number, TokenClass::Number, flags};
}

LexerBase::Scan Json5Lexer::scanIdentifier() {
    static const KeywordTable keywords{
        {L"null", json5::Null, TokenClass::Constant},
        {L"true", json5::True, TokenClass::Constant},
        {L"false", json5::False, TokenClass::Constant},
        {L"Infinity", json5::Number, TokenClass::Number},
        {L"NaN", json5::Number, TokenClass::Number},
    };

    const std::size_t start = pos();
    bool escaped = false;
    std::uint8_t flags = TokenFlag_None;

    for (;;) {
        const wchar_t c = cur();
        if (!atEnd() && chars::isIdentPart(c)) {
            advance();
        } else if (c == L'\\' && peekAt(1) == L'u') {
            advanceBy(2);
            escaped = true;
            int n = 0;
            while (n < 4 && !atEnd() && chars::isHexDigit(cur())) { advance(); ++n; }
            if (n < 4) flags |= TokenFlag_Invalid;
        } else {
            break;
        }
    }

    if (!escaped) {
        if (const KeywordTable::Entry* e = keywords.find(source().substr(start, pos() - start)))
            return {e->kind, e->cls};
    }
    return {json5::Identifier, TokenClass::Identifier, flags};
}

}  // namespace lex
