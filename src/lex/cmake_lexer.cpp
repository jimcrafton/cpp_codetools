#include <lex/cmake_lexer.h>

#include <string>

namespace lex {

namespace {

constexpr bool isCommandStart(wchar_t c) noexcept {
    return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || c == L'_';
}

constexpr bool isCommandPart(wchar_t c) noexcept { return isCommandStart(c) || chars::isDigit(c); }

}  // namespace

CMakeLexer::CMakeLexer(std::wstring_view source, LexerOptions options) noexcept
    : LexerBase(source, options) {}

void CMakeLexer::setState(std::uint32_t mode, std::uint32_t level) noexcept {
    state_.mode = mode;
    state_.aux = (depth() << 16) | (level & 0xFFFFu);
}

int CMakeLexer::bracketLevelAt(std::size_t ahead) const noexcept {
    if (peekAt(ahead) != L'[') return -1;
    std::size_t n = 0;
    while (peekAt(ahead + 1 + n) == L'=') ++n;
    return peekAt(ahead + 1 + n) == L'[' ? static_cast<int>(n) : -1;
}

LexerBase::Scan CMakeLexer::scanBracket(bool comment, bool resumed) {
    std::uint32_t level = state_.aux & 0xFFFFu;
    if (!resumed) {
        // At the opening '[' (a comment's '#' is already consumed).
        advance();
        level = 0;
        while (cur() == L'=') { advance(); ++level; }
        advance();  // the second '['
    }
    const std::wstring closer = L"]" + std::wstring(level, L'=') + L"]";
    const TokenKind tokenKind = comment ? kind::BlockComment : cmake::Bracket;
    const TokenClass tokenClass = comment ? TokenClass::Comment : TokenClass::String;

    std::uint8_t flags = resumed ? TokenFlag_Resumed : TokenFlag_None;
    if (consumeThrough(closer)) {
        setState(cmake::ModeCode, 0);
    } else if (atEnd()) {
        setState(cmake::ModeCode, 0);
        flags |= TokenFlag_Unterminated;
    } else {  // split mode, stopped in front of a newline: the rest is on the next line
        setState(comment ? cmake::InBracketComment : cmake::InBracketArg, level);
        flags |= TokenFlag_Continued;
    }
    return {tokenKind, tokenClass, flags};
}

LexerBase::Scan CMakeLexer::scanQuoted(bool resumed) {
    std::uint8_t flags = resumed ? TokenFlag_Resumed : TokenFlag_None;
    while (!atEnd()) {
        if (options().splitAtNewlines && atNewline()) {
            setState(cmake::InQuoted, 0);
            flags |= TokenFlag_Continued;
            return {cmake::Quoted, TokenClass::String, flags};
        }
        const wchar_t c = advance();
        if (c == L'"') {
            setState(cmake::ModeCode, 0);
            return {cmake::Quoted, TokenClass::String, flags};
        }
        if (c == L'\\' && !atEnd() && !(options().splitAtNewlines && atNewline())) advance();
    }
    setState(cmake::ModeCode, 0);
    return {cmake::Quoted, TokenClass::String, static_cast<std::uint8_t>(flags | TokenFlag_Unterminated)};
}

LexerBase::Scan CMakeLexer::scanUnquoted(bool atTopLevel) {
    const std::size_t start = pos();
    while (!atEnd()) {
        const wchar_t c = cur();
        if (isNewlineChar(c) || chars::isHorizontalSpace(c) || c == L'(' || c == L')' || c == L'#') break;
        if (c == L'"') {
            if (pos() == start) break;
            // A quoted part inside an unquoted argument (-Dname="a b"), if it closes on this line.
            std::size_t ahead = 1;
            while (peekAt(ahead) != L'\0' && !isNewlineChar(peekAt(ahead)) && peekAt(ahead) != L'"') {
                ahead += peekAt(ahead) == L'\\' ? 2 : 1;
            }
            if (peekAt(ahead) != L'"') break;
            advanceBy(ahead + 1);
            continue;
        }
        if (c == L'\\') {
            advance();
            if (!atEnd() && !atNewline()) advance();
            continue;
        }
        if (c == L'$' && (peekAt(1) == L'{' || peekAt(1) == L'<')) {
            // ${...} and $<...>, nested, up to the matching closer or the end of the word.
            const wchar_t open = peekAt(1);
            const wchar_t close = open == L'{' ? L'}' : L'>';
            advanceBy(2);
            int nest = 1;
            while (!atEnd() && nest > 0 && !isNewlineChar(cur()) && !chars::isHorizontalSpace(cur())) {
                const wchar_t d = advance();
                if (d == open) ++nest;
                else if (d == close) --nest;
                else if (d == L'\\' && !atEnd() && !atNewline()) advance();
            }
            continue;
        }
        advance();
    }
    return {cmake::Unquoted, TokenClass::Identifier,
            static_cast<std::uint8_t>(atTopLevel ? TokenFlag_Invalid : TokenFlag_None)};
}

LexerBase::Scan CMakeLexer::scanToken() {
    // Resume a multi-line token first (split mode only ever leaves a mode set).
    if (state_.mode == cmake::InBracketComment) return scanBracket(true, true);
    if (state_.mode == cmake::InBracketArg) return scanBracket(false, true);
    if (state_.mode == cmake::InQuoted) return scanQuoted(true);

    const wchar_t c = cur();
    if (chars::isHorizontalSpace(c)) return scanWhitespace();

    switch (c) {
    case L'#':
        if (bracketLevelAt(1) >= 0) {
            advance();
            return scanBracket(true, false);
        }
        acceptWhile([this](wchar_t ch) { return !isNewlineChar(ch); });
        return {kind::LineComment, TokenClass::Comment};
    case L'(':
        advance();
        state_.aux = ((depth() + 1) << 16) | (state_.aux & 0xFFFFu);
        return {cmake::LParen, TokenClass::Punctuation};
    case L')':
        advance();
        if (depth() > 0) state_.aux = ((depth() - 1) << 16) | (state_.aux & 0xFFFFu);
        return {cmake::RParen, TokenClass::Punctuation};
    case L'"':
        advance();
        return scanQuoted(false);
    case L'[':
        if (bracketLevelAt(0) >= 0) return scanBracket(false, false);
        break;
    default:
        break;
    }

    if (depth() == 0 && isCommandStart(c)) {
        acceptWhile(isCommandPart);
        return {cmake::CommandName, TokenClass::Keyword};
    }
    return scanUnquoted(depth() == 0);
}

}  // namespace lex
