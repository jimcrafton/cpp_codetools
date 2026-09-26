#include <lex/cpp_lexer.h>

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace lex {

namespace {

// ---- raw-string delimiters, interned so a LexState (two 32-bit words) can carry one ----------

constexpr std::size_t kMaxDelimiters = 4096;
constexpr std::uint32_t kOverflowId = 0xFFFFFFFFu;

struct DelimiterTable {
    std::mutex mutex;
    std::vector<std::wstring> byId;                      // id -> delimiter; id 0 is ""
    std::unordered_map<std::wstring, std::uint32_t> ids;
    DelimiterTable() {
        byId.emplace_back();
        ids.emplace(std::wstring(), 0u);
    }
};

DelimiterTable& delimiters() {
    static DelimiterTable table;
    return table;
}

std::uint32_t internDelimiter(std::wstring_view delimiter) {
    DelimiterTable& t = delimiters();
    std::lock_guard<std::mutex> lock(t.mutex);
    const std::wstring key(delimiter);
    const auto found = t.ids.find(key);
    if (found != t.ids.end()) return found->second;
    if (t.byId.size() >= kMaxDelimiters) return kOverflowId;
    const std::uint32_t id = static_cast<std::uint32_t>(t.byId.size());
    t.byId.push_back(key);
    t.ids.emplace(key, id);
    return id;
}

// d-char: a basic source character other than space, parentheses, backslash and control characters.
bool isDelimiterChar(wchar_t c) noexcept {
    return c > 0x20 && c < 0x7F && c != L'(' && c != L')' && c != L'\\';
}

const KeywordTable& keywords() {
    using lex::cpp::Constant;
    using lex::cpp::Keyword;
    static const KeywordTable table{
        {L"alignas", Keyword, TokenClass::Keyword},
        {L"alignof", Keyword, TokenClass::Keyword},
        {L"and", Keyword, TokenClass::Keyword},
        {L"and_eq", Keyword, TokenClass::Keyword},
        {L"asm", Keyword, TokenClass::Keyword},
        {L"auto", Keyword, TokenClass::Keyword},
        {L"bitand", Keyword, TokenClass::Keyword},
        {L"bitor", Keyword, TokenClass::Keyword},
        {L"bool", Keyword, TokenClass::Keyword},
        {L"break", Keyword, TokenClass::Keyword},
        {L"case", Keyword, TokenClass::Keyword},
        {L"catch", Keyword, TokenClass::Keyword},
        {L"char", Keyword, TokenClass::Keyword},
        {L"char8_t", Keyword, TokenClass::Keyword},
        {L"char16_t", Keyword, TokenClass::Keyword},
        {L"char32_t", Keyword, TokenClass::Keyword},
        {L"class", Keyword, TokenClass::Keyword},
        {L"compl", Keyword, TokenClass::Keyword},
        {L"concept", Keyword, TokenClass::Keyword},
        {L"const", Keyword, TokenClass::Keyword},
        {L"consteval", Keyword, TokenClass::Keyword},
        {L"constexpr", Keyword, TokenClass::Keyword},
        {L"constinit", Keyword, TokenClass::Keyword},
        {L"const_cast", Keyword, TokenClass::Keyword},
        {L"continue", Keyword, TokenClass::Keyword},
        {L"co_await", Keyword, TokenClass::Keyword},
        {L"co_return", Keyword, TokenClass::Keyword},
        {L"co_yield", Keyword, TokenClass::Keyword},
        {L"decltype", Keyword, TokenClass::Keyword},
        {L"default", Keyword, TokenClass::Keyword},
        {L"delete", Keyword, TokenClass::Keyword},
        {L"do", Keyword, TokenClass::Keyword},
        {L"double", Keyword, TokenClass::Keyword},
        {L"dynamic_cast", Keyword, TokenClass::Keyword},
        {L"else", Keyword, TokenClass::Keyword},
        {L"enum", Keyword, TokenClass::Keyword},
        {L"explicit", Keyword, TokenClass::Keyword},
        {L"export", Keyword, TokenClass::Keyword},
        {L"extern", Keyword, TokenClass::Keyword},
        {L"false", Constant, TokenClass::Constant},
        {L"float", Keyword, TokenClass::Keyword},
        {L"for", Keyword, TokenClass::Keyword},
        {L"friend", Keyword, TokenClass::Keyword},
        {L"goto", Keyword, TokenClass::Keyword},
        {L"if", Keyword, TokenClass::Keyword},
        {L"inline", Keyword, TokenClass::Keyword},
        {L"int", Keyword, TokenClass::Keyword},
        {L"long", Keyword, TokenClass::Keyword},
        {L"mutable", Keyword, TokenClass::Keyword},
        {L"namespace", Keyword, TokenClass::Keyword},
        {L"new", Keyword, TokenClass::Keyword},
        {L"noexcept", Keyword, TokenClass::Keyword},
        {L"not", Keyword, TokenClass::Keyword},
        {L"not_eq", Keyword, TokenClass::Keyword},
        {L"nullptr", Constant, TokenClass::Constant},
        {L"operator", Keyword, TokenClass::Keyword},
        {L"or", Keyword, TokenClass::Keyword},
        {L"or_eq", Keyword, TokenClass::Keyword},
        {L"private", Keyword, TokenClass::Keyword},
        {L"protected", Keyword, TokenClass::Keyword},
        {L"public", Keyword, TokenClass::Keyword},
        {L"register", Keyword, TokenClass::Keyword},
        {L"reinterpret_cast", Keyword, TokenClass::Keyword},
        {L"requires", Keyword, TokenClass::Keyword},
        {L"return", Keyword, TokenClass::Keyword},
        {L"short", Keyword, TokenClass::Keyword},
        {L"signed", Keyword, TokenClass::Keyword},
        {L"sizeof", Keyword, TokenClass::Keyword},
        {L"static", Keyword, TokenClass::Keyword},
        {L"static_assert", Keyword, TokenClass::Keyword},
        {L"static_cast", Keyword, TokenClass::Keyword},
        {L"struct", Keyword, TokenClass::Keyword},
        {L"switch", Keyword, TokenClass::Keyword},
        {L"template", Keyword, TokenClass::Keyword},
        {L"this", Keyword, TokenClass::Keyword},
        {L"thread_local", Keyword, TokenClass::Keyword},
        {L"throw", Keyword, TokenClass::Keyword},
        {L"true", Constant, TokenClass::Constant},
        {L"try", Keyword, TokenClass::Keyword},
        {L"typedef", Keyword, TokenClass::Keyword},
        {L"typeid", Keyword, TokenClass::Keyword},
        {L"typename", Keyword, TokenClass::Keyword},
        {L"union", Keyword, TokenClass::Keyword},
        {L"unsigned", Keyword, TokenClass::Keyword},
        {L"using", Keyword, TokenClass::Keyword},
        {L"virtual", Keyword, TokenClass::Keyword},
        {L"void", Keyword, TokenClass::Keyword},
        {L"volatile", Keyword, TokenClass::Keyword},
        {L"wchar_t", Keyword, TokenClass::Keyword},
        {L"while", Keyword, TokenClass::Keyword},
        {L"xor", Keyword, TokenClass::Keyword},
        {L"xor_eq", Keyword, TokenClass::Keyword},
        // Contextual, but worth colouring.
        {L"override", Keyword, TokenClass::Keyword},
        {L"final", Keyword, TokenClass::Keyword},
        // MSVC.
        {L"__asm", Keyword, TokenClass::Keyword},
        {L"__cdecl", Keyword, TokenClass::Keyword},
        {L"__declspec", Keyword, TokenClass::Keyword},
        {L"__except", Keyword, TokenClass::Keyword},
        {L"__fastcall", Keyword, TokenClass::Keyword},
        {L"__finally", Keyword, TokenClass::Keyword},
        {L"__forceinline", Keyword, TokenClass::Keyword},
        {L"__int8", Keyword, TokenClass::Keyword},
        {L"__int16", Keyword, TokenClass::Keyword},
        {L"__int32", Keyword, TokenClass::Keyword},
        {L"__int64", Keyword, TokenClass::Keyword},
        {L"__leave", Keyword, TokenClass::Keyword},
        {L"__stdcall", Keyword, TokenClass::Keyword},
        {L"__try", Keyword, TokenClass::Keyword},
        {L"__vectorcall", Keyword, TokenClass::Keyword},
    };
    return table;
}

bool isBinaryDigit(wchar_t c) noexcept { return c == L'0' || c == L'1'; }

}  // namespace

std::wstring cpp::rawStringDelimiter(std::uint32_t id) {
    DelimiterTable& t = delimiters();
    std::lock_guard<std::mutex> lock(t.mutex);
    return id < t.byId.size() ? t.byId[id] : std::wstring();
}

CppLexer::CppLexer(std::wstring_view source, LexerOptions options) noexcept
    : LexerBase(source, [&] {
          options.unicodeLineSeparators = false;  // C++ doesn't treat U+2028 / U+2029 as line ends
          return options;
      }()) {}

LexerBase::Scan CppLexer::scanToken() {
    lastWasSplice_ = false;
    const Scan scan = scanOne();
    if (scan.cls != TokenClass::Whitespace && scan.cls != TokenClass::Comment && scan.kind != cpp::Directive) {
        state_.mode &= ~static_cast<std::uint32_t>(cpp::ExpectHeaderName);
    }
    updateDirectiveState(scan);
    return scan;
}

void CppLexer::updateDirectiveState(const Scan& scan) noexcept {
    if (!inDirective() || lastWasSplice_ || (scan.flags & TokenFlag_Continued) != 0) return;
    if (atEnd() || atNewline()) {
        state_.mode &= ~static_cast<std::uint32_t>(cpp::InDirective | cpp::ExpectHeaderName);
    }
}

bool CppLexer::previousLineEndsWithBackslash() const noexcept {
    const std::wstring_view src = source();
    std::size_t end = checkpoint().lineStart;
    if (end == 0) return true;
    end -= (src[end - 1] == L'\n' && end >= 2 && src[end - 2] == L'\r') ? 2 : 1;   // the previous break
    return end > 0 && src[end - 1] == L'\\';
}

LexerBase::Scan CppLexer::scanOne() {
    // A splice carries a string, a // comment or a directive onto the next line only if the previous
    // line really ended in a backslash. An empty line in between produces no token to say so, so
    // the first token of a line checks.
    if ((multiLine() == cpp::InString || multiLine() == cpp::InLineComment || inDirective()) &&
        pos() == checkpoint().lineStart && !previousLineEndsWithBackslash()) {
        state_.mode &= ~static_cast<std::uint32_t>(cpp::InDirective | cpp::ExpectHeaderName);
        if (multiLine() == cpp::InString || multiLine() == cpp::InLineComment) setMultiLine(cpp::ModeCode);
    }

    // Resume a multi-line token first (split mode is the only way state is left set mid-file).
    switch (multiLine()) {
    case cpp::InBlockComment: return scanBlockComment(true);
    case cpp::InLineComment:  return scanLineComment(true);
    case cpp::InString:       return scanQuoted(static_cast<wchar_t>(state_.aux), true);
    case cpp::InRawString:    return scanRawString(true);
    default: break;
    }

    const wchar_t c = cur();
    if (chars::isHorizontalSpace(c)) return scanWhitespace();

    switch (c) {
    case L'\\':
        if (peekAt(1) == L'\n' || peekAt(1) == L'\r') {  // a line splice
            advance();
            lastWasSplice_ = true;
            return {kind::Whitespace, TokenClass::Whitespace};
        }
        if (peekAt(1) == L'u' || peekAt(1) == L'U') return scanIdentifier();  // a UCN starting a name
        return {kind::Error, TokenClass::Error};

    case L'/':
        if (peekAt(1) == L'/') return scanLineComment(false);
        if (peekAt(1) == L'*') return scanBlockComment(false);
        break;

    case L'"':
    case L'\'':
        advance();
        return scanQuoted(c, false);

    case L'#':
        if (!inDirective() && atDirectiveStart()) return scanDirective();
        if (peekAt(1) == L'#') advanceBy(2); else advance();
        return {cpp::Hash, TokenClass::Operator};

    case L'<':
        if ((state_.mode & cpp::ExpectHeaderName) != 0) return scanHeaderName();
        break;

    case L'.':
        if (chars::isDigit(peekAt(1))) return scanNumber();
        break;

    default:
        break;
    }

    if (chars::isDigit(c)) return scanNumber();
    if (chars::isIdentStart(c)) return scanIdentifier();
    return scanOperator();
}

// ---- comments ------------------------------------------------------------------------------

LexerBase::Scan CppLexer::scanBlockComment(bool resumed) {
    std::uint8_t flags = resumed ? TokenFlag_Resumed : TokenFlag_None;
    if (!resumed) advanceBy(2);  // "/*"
    if (consumeThrough(L"*/")) {
        setMultiLine(cpp::ModeCode);
        return {kind::BlockComment, TokenClass::Comment, flags};
    }
    // EOF, or (split mode) the end of this line.
    setMultiLine(cpp::InBlockComment);
    flags |= atEnd() ? TokenFlag_Unterminated : TokenFlag_Continued;
    return {kind::BlockComment, TokenClass::Comment, flags};
}

LexerBase::Scan CppLexer::scanLineComment(bool resumed) {
    std::uint8_t flags = resumed ? TokenFlag_Resumed : TokenFlag_None;
    if (!resumed) advanceBy(2);  // "//"
    for (;;) {
        while (!atEnd() && !atNewline()) advance();
        // A backslash right before the break splices the next line into the comment.
        if (atNewline() && pos() > 0 && source()[pos() - 1] == L'\\') {
            if (options().splitAtNewlines) {
                setMultiLine(cpp::InLineComment);
                return {kind::LineComment, TokenClass::Comment, std::uint8_t(flags | TokenFlag_Continued)};
            }
            consumeNewline();
            continue;
        }
        setMultiLine(cpp::ModeCode);
        return {kind::LineComment, TokenClass::Comment, flags};
    }
}

// ---- strings and character literals ---------------------------------------------------------

// The opening quote has been consumed (or, resumed, the string is continuing on this line).
LexerBase::Scan CppLexer::scanQuoted(wchar_t quote, bool resumed) {
    const bool split = options().splitAtNewlines;
    const TokenKind kindOfToken = quote == L'"' ? cpp::String : cpp::CharLiteral;
    std::uint8_t flags = resumed ? TokenFlag_Resumed : TokenFlag_None;
    const bool emptyChar = quote == L'\'' && !resumed && cur() == quote;

    for (;;) {
        if (atEnd()) {
            setMultiLine(cpp::ModeCode);
            return {kindOfToken, TokenClass::String, std::uint8_t(flags | TokenFlag_Unterminated)};
        }
        const wchar_t c = cur();

        if (c == quote) {
            advance();
            acceptWhile(chars::isIdentPart);  // a user-defined suffix
            if (emptyChar) flags |= TokenFlag_Invalid;
            setMultiLine(cpp::ModeCode);
            return {kindOfToken, TokenClass::String, flags};
        }

        if (c == L'\\') {
            if (pos() + 1 >= source().size()) {  // lone backslash at EOF
                advance();
                continue;
            }
            if (isNewlineChar(peekAt(1))) {  // a splice inside the literal
                advance();                   // the backslash
                if (split) {
                    setMultiLine(cpp::InString, static_cast<std::uint32_t>(quote));
                    return {kindOfToken, TokenClass::String, std::uint8_t(flags | TokenFlag_Continued)};
                }
                consumeNewline();
                continue;
            }
            advance();  // the backslash
            advance();  // the escaped character (\x, \u, \0 ... aren't validated here)
            continue;
        }

        if (isNewlineChar(c)) {  // a bare line break ends an unterminated literal
            setMultiLine(cpp::ModeCode);
            return {kindOfToken, TokenClass::String, std::uint8_t(flags | TokenFlag_Unterminated)};
        }
        advance();
    }
}

// Cursor just after the R" of a raw string (not resumed), or continuing one (resumed).
LexerBase::Scan CppLexer::scanRawString(bool resumed) {
    std::uint8_t flags = resumed ? TokenFlag_Resumed : TokenFlag_None;
    std::uint32_t id = 0;

    if (!resumed) {
        const Checkpoint start = checkpoint();
        const std::size_t begin = pos();
        while (!atEnd() && pos() - begin <= 16 && isDelimiterChar(cur())) advance();
        if (cur() != L'(' || pos() - begin > 16) {
            // No usable delimiter: an ordinary string, flagged.
            restore(start);
            Scan scan = scanQuoted(L'"', false);
            scan.flags |= TokenFlag_Invalid;
            return scan;
        }
        id = internDelimiter(source().substr(begin, pos() - begin));
        advance();  // '('
    } else {
        id = state_.aux;
    }

    std::wstring closer = L")";
    closer += cpp::rawStringDelimiter(id);
    closer += L'"';
    if (consumeThrough(closer)) {
        acceptWhile(chars::isIdentPart);  // a user-defined suffix
        setMultiLine(cpp::ModeCode);
        return {cpp::RawString, TokenClass::String, flags};
    }
    setMultiLine(cpp::InRawString, id);
    flags |= atEnd() ? TokenFlag_Unterminated : TokenFlag_Continued;
    return {cpp::RawString, TokenClass::String, flags};
}

// ---- numbers --------------------------------------------------------------------------------

// A C++ pp-number: digits, letters, '.', digit separators, and a sign after an exponent letter.
LexerBase::Scan CppLexer::scanNumber() {
    const std::size_t start = pos();
    const bool hexLike = cur() == L'0' && (peekAt(1) == L'x' || peekAt(1) == L'X');
    const bool binaryLike = cur() == L'0' && (peekAt(1) == L'b' || peekAt(1) == L'B');

    for (;;) {
        const wchar_t c = cur();
        if (atEnd()) break;
        if ((c == L'+' || c == L'-')) {
            const wchar_t before = source()[pos() - 1];
            const bool exponent = hexLike ? (before == L'p' || before == L'P')
                                          : (before == L'e' || before == L'E');
            if (!exponent) break;
            advance();
        } else if (c == L'\'') {
            // A digit separator: only between two alphanumerics.
            if (chars::isIdentPart(peekAt(1)) && peekAt(1) != L'\'') advance(); else break;
        } else if (c == L'.' || chars::isIdentPart(c)) {
            advance();
        } else {
            break;
        }
    }

    // Judge it: lexically malformed numbers are flagged, and still one token.
    const std::wstring_view t = source().substr(start, pos() - start);
    std::uint8_t flags = TokenFlag_None;
    std::size_t i = 0;
    if (hexLike) {
        bool digit = false;
        for (std::size_t k = 2; k < t.size() && !digit; ++k) digit = chars::isHexDigit(t[k]);
        if (t.size() <= 2 || !digit) flags |= TokenFlag_Invalid;
    } else if (binaryLike) {
        i = 2;
        std::size_t digits = 0;
        while (i < t.size() && (isBinaryDigit(t[i]) || t[i] == L'\'')) { digits += t[i] != L'\''; ++i; }
        if (digits == 0 || (i < t.size() && chars::isDigit(t[i]))) flags |= TokenFlag_Invalid;  // "0b", "0b12"
    } else {
        while (i < t.size() && (chars::isDigit(t[i]) || t[i] == L'.' || t[i] == L'\'')) ++i;
        const std::size_t mantissaEnd = i;
        bool floating = t.substr(0, mantissaEnd).find(L'.') != std::wstring_view::npos;
        if (i < t.size() && (t[i] == L'e' || t[i] == L'E')) {
            floating = true;
            ++i;
            if (i < t.size() && (t[i] == L'+' || t[i] == L'-')) ++i;
            if (i >= t.size() || !chars::isDigit(t[i])) flags |= TokenFlag_Invalid;  // "1e", "1e+"
        }
        // An octal integer can't have an 8 or a 9 ("08"); floats can ("08.5").
        if (!floating && mantissaEnd > 1 && t[0] == L'0') {
            for (std::size_t k = 1; k < mantissaEnd; ++k) {
                if (t[k] == L'8' || t[k] == L'9') { flags |= TokenFlag_Invalid; break; }
            }
        }
    }
    return {cpp::Number, TokenClass::Number, flags};
}

// ---- identifiers, keywords, prefixed literals -----------------------------------------------

LexerBase::Scan CppLexer::scanIdentifier() {
    const std::size_t start = pos();
    bool escaped = false;
    std::uint8_t flags = TokenFlag_None;

    for (;;) {
        const wchar_t c = cur();
        if (!atEnd() && chars::isIdentPart(c)) {
            advance();
        } else if (c == L'\\' && (peekAt(1) == L'u' || peekAt(1) == L'U')) {
            const std::size_t digitsWanted = peekAt(1) == L'u' ? 4 : 8;
            advanceBy(2);
            escaped = true;
            std::size_t n = 0;
            while (n < digitsWanted && !atEnd() && chars::isHexDigit(cur())) { advance(); ++n; }
            if (n < digitsWanted) flags |= TokenFlag_Invalid;
        } else {
            break;
        }
    }
    const std::wstring_view word = source().substr(start, pos() - start);

    // An encoding prefix runs straight into a literal: u8"x", L'x', R"(x)", u8R"(x)".
    if (!escaped && (cur() == L'"' || cur() == L'\'')) {
        const bool encoding = word == L"L" || word == L"u" || word == L"U" || word == L"u8";
        const bool raw = word == L"R" || word == L"LR" || word == L"uR" || word == L"UR" || word == L"u8R";
        if (encoding) {
            const wchar_t quote = cur();
            advance();
            return scanQuoted(quote, false);
        }
        if (raw && cur() == L'"') {
            advance();
            return scanRawString(false);
        }
    }

    if (!escaped) {
        if (const KeywordTable::Entry* e = keywords().find(word)) return {e->kind, e->cls};
    }
    return {cpp::Identifier, TokenClass::Identifier, flags};
}

// ---- preprocessor ---------------------------------------------------------------------------

bool CppLexer::atDirectiveStart() const noexcept {
    const std::wstring_view src = source();
    std::size_t i = checkpoint().lineStart;
    const std::size_t end = pos();
    while (i < end) {
        if (chars::isHorizontalSpace(src[i])) {
            ++i;
        } else if (src[i] == L'/' && i + 1 < src.size() && src[i + 1] == L'*') {
            const std::size_t close = src.find(L"*/", i + 2);
            if (close == std::wstring_view::npos || close + 2 > end) return false;
            i = close + 2;
        } else {
            return false;
        }
    }
    return true;
}

LexerBase::Scan CppLexer::scanDirective() {
    advance();  // '#'
    acceptWhile(chars::isHorizontalSpace);
    const std::size_t nameStart = pos();
    acceptWhile(chars::isIdentPart);
    const std::wstring_view name = source().substr(nameStart, pos() - nameStart);

    state_.mode |= cpp::InDirective;
    if (name == L"include" || name == L"import" || name == L"include_next") state_.mode |= cpp::ExpectHeaderName;
    return {cpp::Directive, TokenClass::Preprocessor};
}

LexerBase::Scan CppLexer::scanHeaderName() {
    advance();  // '<'
    while (!atEnd() && !atNewline() && cur() != L'>') advance();
    if (cur() == L'>' && !atEnd()) {
        advance();
        return {cpp::HeaderName, TokenClass::String};
    }
    return {cpp::HeaderName, TokenClass::String, TokenFlag_Unterminated};
}

// ---- operators and punctuators --------------------------------------------------------------

LexerBase::Scan CppLexer::scanOperator() {
    switch (cur()) {
    case L'{': advance(); return {cpp::LBrace, TokenClass::Punctuation};
    case L'}': advance(); return {cpp::RBrace, TokenClass::Punctuation};
    case L'(': advance(); return {cpp::LParen, TokenClass::Punctuation};
    case L')': advance(); return {cpp::RParen, TokenClass::Punctuation};
    case L'[': advance(); return {cpp::LBracket, TokenClass::Punctuation};
    case L']': advance(); return {cpp::RBracket, TokenClass::Punctuation};
    case L';': advance(); return {cpp::Semicolon, TokenClass::Punctuation};
    case L',': advance(); return {cpp::Comma, TokenClass::Punctuation};
    default: break;
    }

    static const std::wstring_view spellings[] = {
        L"<<=", L">>=", L"<=>", L"->*", L"...",
        L"::", L"->", L".*", L"++", L"--", L"<<", L">>", L"<=", L">=", L"==", L"!=", L"&&", L"||",
        L"+=", L"-=", L"*=", L"/=", L"%=", L"^=", L"&=", L"|=",
        L"+", L"-", L"*", L"/", L"%", L"^", L"&", L"|", L"~", L"!", L"=", L"<", L">", L".", L"?", L":",
    };
    const std::size_t n = matchLongest(spellings);
    if (n == 0) return {kind::Error, TokenClass::Error};  // '@', '`', a stray '\\': the base eats one unit
    advanceBy(n);
    return {cpp::Operator, TokenClass::Operator};
}

}  // namespace lex
