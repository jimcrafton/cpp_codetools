// Tests for lex::LexerBase using a deliberately tiny toy language
// (identifiers, numbers, "strings", // and /* */ comments, punctuation).

#include <lex/lexer_base.h>
#include "test_util.h"

#include <string>

using namespace lex;

namespace toy {
enum : TokenKind { Ident = kind::FirstLanguage, Number, String, Punct, Op };
enum : std::uint32_t { InBlockComment = 1 };
}  // namespace toy

class ToyLexer : public LexerBase {
public:
    using LexerBase::LexerBase;

protected:
    Scan scanToken() override {
        // 1. Resume a multi-line token first (split mode).
        if (state_.mode == toy::InBlockComment) {
            const bool closed = consumeThrough(L"*/");
            std::uint8_t f = TokenFlag_Resumed;
            if (closed) state_ = {};
            else if (atEnd()) f |= TokenFlag_Unterminated;
            else f |= TokenFlag_Continued;
            return {kind::BlockComment, TokenClass::Comment, f};
        }

        const wchar_t c = cur();
        if (chars::isHorizontalSpace(c)) return scanWhitespace();

        if (lookingAt(L"//")) {
            acceptWhile([this](wchar_t ch) { return !isNewlineChar(ch); });
            return {kind::LineComment, TokenClass::Comment};
        }
        if (accept(L"/*")) {
            std::uint8_t f = 0;
            if (!consumeThrough(L"*/")) {
                if (atEnd()) f = TokenFlag_Unterminated;
                else { f = TokenFlag_Continued; state_.mode = toy::InBlockComment; }
            }
            return {kind::BlockComment, TokenClass::Comment, f};
        }

        if (chars::isIdentStart(c)) {
            const std::size_t start = pos();
            acceptWhile(chars::isIdentPart);
            static const KeywordTable kw{
                {L"true", toy::Ident, TokenClass::Constant},
                {L"null", toy::Ident, TokenClass::Constant},
            };
            const std::wstring_view word = source().substr(start, pos() - start);
            if (const auto* e = kw.find(word)) return {e->kind, e->cls};
            return {toy::Ident, TokenClass::Identifier};
        }
        if (chars::isDigit(c)) {
            acceptWhile(chars::isDigit);
            return {toy::Number, TokenClass::Number};
        }
        if (c == L'"') {
            advance();
            while (!atEnd() && !atNewline() && cur() != L'"') {
                if (cur() == L'\\' && peekAt(1) != L'\0' && !isNewlineChar(peekAt(1))) advance();
                advance();
            }
            if (accept(L'"')) return {toy::String, TokenClass::String};
            return {toy::String, TokenClass::String, TokenFlag_Unterminated};
        }

        static const std::wstring_view ops[] = {L"<", L"<=", L"<<", L"<<=", L"=", L"=="};
        if (const std::size_t n = matchLongest(ops)) {
            advanceBy(n);
            return {toy::Op, TokenClass::Operator};
        }
        if (c == L'{' || c == L'}' || c == L':' || c == L',') {
            advance();
            return {toy::Punct, TokenClass::Punctuation};
        }
        return {kind::Error, TokenClass::Error};  // consumed nothing -> base eats one unit
    }
};

static std::wstring join(const ToyLexer& lx, const std::vector<Token>& toks) {
    std::wstring s;
    for (const Token& t : toks) s += lx.text(t);
    return s;
}

void runLexerBaseTests() {
    // Lossless + line/col + endLine, mixed \n, \r\n, lone \r.
    {
        const std::wstring src = L"a: 12 // c\r\n/* x\ny */ \"s\"\r{ <<= == }\n";
        ToyLexer lx(src);
        auto toks = lx.tokenize();
        CHECK(join(lx, toks) == src);
        CHECK(lx.next().isEof());
        CHECK(lx.next().isEof());

        LineIndex idx(src);
        for (const Token& t : toks) {
            auto p = idx.positionOf(t.offset);
            CHECK(p.line == t.line && p.column == t.column);
            if (t.length && t.cls != TokenClass::Newline)
                CHECK(idx.lineOf(t.end() - 1) == t.endLine);
        }
        const Token* block = nullptr;
        for (const Token& t : toks) if (t.kind == kind::BlockComment) block = &t;
        CHECK(block && block->line == 1 && block->endLine == 2 && block->column == 0);
        CHECK(idx.lineCount() == 5);  // 4 breaks + trailing empty line
        CHECK(idx.lineEnd(0) == 10);  // "a: 12 // c" without \r\n
        CHECK(lx.text(toks.back()) == L"\n");
    }

    // Maximal munch, keywords.
    {
        const std::wstring src = L"<<= << < == = true x";
        ToyLexer lx(src);
        std::wstring got;
        for (const Token& t : lx.tokenize())
            if (t.cls == TokenClass::Operator || t.cls == TokenClass::Constant) { got += lx.text(t); got += L'|'; }
        CHECK(got == L"<<=|<<|<|==|=|true|");
    }

    // Unterminated string stops at the newline; error char makes progress.
    {
        const std::wstring src = L"\"abc\n@";
        ToyLexer lx(src);
        auto toks = lx.tokenize();
        CHECK(toks.size() == 3);
        CHECK(toks[0].has(TokenFlag_Unterminated) && lx.text(toks[0]) == L"\"abc");
        CHECK(toks[2].cls == TokenClass::Error && toks[2].length == 1);
    }

    // Split mode: block comment becomes one segment per line, state carried.
    {
        const std::wstring src = L"a /* one\n two\n three */ b\nc";
        LexerOptions o; o.splitAtNewlines = true;
        ToyLexer lx(src, o);
        auto toks = lx.tokenize();
        CHECK(join(lx, toks) == src);

        int comments = 0;
        for (const Token& t : toks) if (t.cls == TokenClass::Comment) {
            ++comments;
            CHECK(t.line == t.endLine);
        }
        CHECK(comments == 3);

        // Whole-token mode gives a single comment.
        ToyLexer whole(src);
        int wc = 0;
        for (const Token& t : whole.tokenize()) if (t.cls == TokenClass::Comment) ++wc;
        CHECK(wc == 1);

        // Incremental re-lex: restart at line 2 using the state recorded at its start.
        LineIndex idx(src);
        ToyLexer probe(src, o);
        LexState entry{};
        for (Token t = probe.next(); !t.isEof() && t.line < 2; t = probe.next()) entry = probe.state();
        CHECK(entry.mode == toy::InBlockComment);

        ToyLexer resumed(src, o);
        resumed.restore({idx.lineStart(2), 2, idx.lineStart(2), entry});
        Token first = resumed.next();  // " three */" is comment, then " b"
        CHECK(first.cls == TokenClass::Comment && first.has(TokenFlag_Resumed) &&
              lx.text(first) == L" three */");
        CHECK(resumed.state() == LexState{});
    }

    // peek() doesn't consume.
    {
        ToyLexer lx(L"ab cd");
        Token p = lx.peek();
        Token n = lx.next();
        CHECK(p.offset == n.offset && p.length == n.length);
    }
}
