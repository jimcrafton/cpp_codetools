#include <lex/json5_parser.h>

#include <iterator>

#include <lex/json5_lexer.h>

namespace lex {
namespace json5 {

// ---------------------------------------------------------------------------
// String / identifier decoding
// ---------------------------------------------------------------------------
namespace {

int hexValue(wchar_t c) noexcept {
    if (c >= L'0' && c <= L'9') return c - L'0';
    if (c >= L'a' && c <= L'f') return c - L'a' + 10;
    if (c >= L'A' && c <= L'F') return c - L'A' + 10;
    return -1;
}

// Read exactly `count` hex digits at raw[i]; advances i only on success.
bool readHex(std::wstring_view raw, std::size_t& i, int count, wchar_t& out) noexcept {
    if (i + count > raw.size()) return false;
    unsigned v = 0;
    for (int k = 0; k < count; ++k) {
        const int d = hexValue(raw[i + k]);
        if (d < 0) return false;
        v = v * 16 + static_cast<unsigned>(d);
    }
    i += count;
    out = static_cast<wchar_t>(v);
    return true;
}

}  // namespace

std::wstring decodeString(std::wstring_view raw) {
    if (raw.empty() || (raw[0] != L'"' && raw[0] != L'\'')) return std::wstring(raw);

    const wchar_t quote = raw[0];
    const std::size_t n = raw.size();
    std::wstring out;
    out.reserve(n);

    std::size_t i = 1;
    while (i < n) {
        wchar_t c = raw[i++];
        if (c == quote) break;  // closing quote
        if (c != L'\\') { out += c; continue; }
        if (i >= n) break;      // lone backslash at the end of an unterminated string

        c = raw[i++];
        wchar_t v = 0;
        switch (c) {
        case L'b': out += L'\b'; break;
        case L'f': out += L'\f'; break;
        case L'n': out += L'\n'; break;
        case L'r': out += L'\r'; break;
        case L't': out += L'\t'; break;
        case L'v': out += L'\v'; break;
        case L'0': out += L'\0'; break;
        case L'x': if (readHex(raw, i, 2, v)) out += v; else out += c; break;
        case L'u': if (readHex(raw, i, 4, v)) out += v; else out += c; break;
        case L'\r': if (i < n && raw[i] == L'\n') ++i; break;  // line continuation
        case L'\n':
        case 0x2028:
        case 0x2029: break;                                     // line continuation
        default: out += c; break;
        }
    }
    return out;
}

std::wstring decodeIdentifier(std::wstring_view raw) {
    std::wstring out;
    out.reserve(raw.size());
    for (std::size_t i = 0; i < raw.size();) {
        wchar_t v = 0;
        if (raw[i] == L'\\' && i + 1 < raw.size() && raw[i + 1] == L'u') {
            std::size_t j = i + 2;
            if (readHex(raw, j, 4, v)) { out += v; i = j; continue; }
        }
        out += raw[i++];
    }
    return out;
}

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------
namespace {

bool startsValue(TokenKind k) noexcept {
    switch (k) {
    case LBrace: case LBracket: case String: case Number: case True: case False: case Null:
        return true;
    default:
        return false;
    }
}

// Tokens that can legitimately follow a missing value; anything else in value
// position is a stray token we skip.
bool isStructural(TokenKind k) noexcept {
    return k == Comma || k == RBrace || k == RBracket || k == Colon || k == kind::Eof;
}

void moveAll(ASTChildren& dst, ASTChildren& src) {
    for (ASTNodePtr& p : src) dst.push_back(std::move(p));
    src.clear();
}

ASTChildren& kidsOf(const ASTNodePtr& n) { return std::get<ASTChildren>(n->value); }

class Parser {
public:
    Parser(std::wstring_view src, const ParseOptions& opts) : src_(src), opts_(opts), lx_(src) {}

    ParseResult run() {
        advance();

        if (!startsValue(cur_.kind)) {
            error(cur_, cur_.isEof() ? L"empty document: expected a value" : L"expected a value at the start of the document");
            while (!cur_.isEof() && !startsValue(cur_.kind)) consume();
        }
        result_.leadingComments = std::move(pending_);
        pending_.clear();

        result_.root = parseValue(0);

        if (!cur_.isEof()) {
            error(cur_, L"unexpected content after the top-level value");
            while (!cur_.isEof()) consume();
        }
        result_.trailingComments = std::move(pending_);
        return std::move(result_);
    }

private:
    // --- token plumbing ----------------------------------------------------

    // Load the next significant token into cur_. Whitespace is dropped, comments
    // are queued in pending_ (they precede cur_), lexer errors are reported.
    void advance() {
        if (aborted_) { cur_ = eof_; return; }
        for (;;) {
            const Token t = lx_.next();
            if (isTrivia(t.cls)) continue;
            if (t.cls == TokenClass::Comment) {
                if (t.has(TokenFlag_Unterminated)) error(t, L"unterminated block comment");
                pending_.push_back(makeNode(t.kind == kind::LineComment ? ASTNodeType::CommentLine
                                                                        : ASTNodeType::CommentBlock, t));
                continue;
            }
            if (t.cls == TokenClass::Error) {
                error(t, L"unexpected character '" + std::wstring(lx_.text(t)) + L"'");
                continue;
            }
            cur_ = t;
            return;
        }
    }

    // Accept cur_ and move on.
    void consume() {
        lastEnd_ = cur_.end();
        lastEndLine_ = cur_.endLine;
        advance();
    }

    void error(const Token& t, std::wstring message) {
        if (result_.errors.size() >= opts_.maxErrors) return;
        ParseError e;
        e.offset = t.offset;
        e.length = t.length;
        e.line = t.line;
        e.column = t.column;
        e.message = std::move(message);
        result_.errors.push_back(std::move(e));
    }

    std::wstring describe(const Token& t) const {
        if (t.isEof()) return L"end of input";
        std::wstring s(lx_.text(t));
        if (s.size() > 20) { s.resize(20); s += L"..."; }
        return L"'" + s + L"'";
    }

    // --- node construction -------------------------------------------------

    ASTNodePtr makeNode(ASTNodeType type, const Token& t) {
        auto n = std::make_shared<ASTNode>();
        n->type = type;
        n->startOffset = t.offset;
        n->endOffset = t.end();
        n->startLine = t.line;
        n->endLine = t.endLine;
        switch (type) {
        case ASTNodeType::Object:
        case ASTNodeType::Array:
        case ASTNodeType::Property:
            n->value = ASTChildren{};
            break;
        default:
            n->value = std::wstring(lx_.text(t));
            break;
        }
        return n;
    }

    static void extendTo(ASTNode& n, std::size_t offset, std::size_t line) {
        if (offset > n.endOffset) { n.endOffset = offset; n.endLine = line; }
    }

    void finishUnclosed(const ASTNodePtr& n) {
        extendTo(*n, lastEnd_, lastEndLine_);
        for (const ASTNodePtr& c : kidsOf(n)) extendTo(*n, c->endOffset, c->endLine);
    }

    // --- grammar -----------------------------------------------------------

    // Returns null (consuming nothing) if cur_ can't start a value.
    //
    // Comments queued before the value (between a key and its value) are set aside
    // while it parses so a nested container doesn't claim them, then put back in
    // front of the comments found after the value.
    ASTNodePtr parseValue(std::size_t depth) {
        if (!startsValue(cur_.kind)) return nullptr;

        ASTChildren before = std::move(pending_);
        pending_.clear();

        ASTNodePtr n;
        switch (cur_.kind) {
        case LBrace:   n = parseObject(depth); break;
        case LBracket: n = parseArray(depth); break;
        default:       n = parsePrimitive(); break;
        }

        pending_.insert(pending_.begin(), std::make_move_iterator(before.begin()),
                        std::make_move_iterator(before.end()));
        return n;
    }

    ASTNodePtr parsePrimitive() {
        const Token t = cur_;
        ASTNodePtr n = makeNode(ASTNodeType::Primitive, t);
        switch (t.kind) {
        case String:
            n->primitive = PrimitiveKind::String;
            if (t.has(TokenFlag_Unterminated)) error(t, L"unterminated string");
            break;
        case Number:
            n->primitive = PrimitiveKind::Number;
            if (t.has(TokenFlag_Invalid)) error(t, L"malformed number");
            break;
        case True:
        case False: n->primitive = PrimitiveKind::Boolean; break;
        default:    n->primitive = PrimitiveKind::Null; break;
        }
        consume();
        return n;
    }

    // Called with cur_ on a container's opening bracket. Returns true if the
    // nesting limit was hit (the parser then winds down to end of input).
    bool tooDeep(std::size_t depth) {
        if (depth < opts_.maxDepth) return false;
        error(cur_, L"nesting too deep");
        aborted_ = true;
        eof_ = Token{};
        eof_.offset = src_.size();
        eof_.line = cur_.endLine;
        cur_ = eof_;
        return true;
    }

    ASTNodePtr parseObject(std::size_t depth) {
        ASTNodePtr n = makeNode(ASTNodeType::Object, cur_);
        if (tooDeep(depth)) return n;
        ASTChildren& kids = kidsOf(n);

        consume();  // '{'
        bool closed = false;
        for (;;) {
            moveAll(kids, pending_);
            if (cur_.kind == RBrace) {
                n->endOffset = cur_.end();
                n->endLine = cur_.endLine;
                closed = true;
                consume();
                break;
            }
            if (cur_.isEof()) { error(cur_, L"unterminated object: expected '}'"); break; }
            if (cur_.kind == Comma) { error(cur_, L"unexpected ','"); consume(); continue; }

            if (ASTNodePtr prop = parseMember(depth)) kids.push_back(std::move(prop));
            moveAll(kids, pending_);

            if (cur_.kind == Comma) { consume(); continue; }
            if (cur_.kind == RBrace || cur_.isEof()) continue;
            error(cur_, L"expected ',' or '}' but found " + describe(cur_));
        }
        if (!closed) finishUnclosed(n);
        return n;
    }

    ASTNodePtr parseArray(std::size_t depth) {
        ASTNodePtr n = makeNode(ASTNodeType::Array, cur_);
        if (tooDeep(depth)) return n;
        ASTChildren& kids = kidsOf(n);

        consume();  // '['
        bool closed = false;
        for (;;) {
            moveAll(kids, pending_);
            if (cur_.kind == RBracket) {
                n->endOffset = cur_.end();
                n->endLine = cur_.endLine;
                closed = true;
                consume();
                break;
            }
            if (cur_.isEof()) { error(cur_, L"unterminated array: expected ']'"); break; }
            if (cur_.kind == Comma) { error(cur_, L"unexpected ','"); consume(); continue; }

            ASTNodePtr v = parseValue(depth + 1);
            if (!v) {
                error(cur_, L"expected a value but found " + describe(cur_));
                consume();  // skip the stray token so we always make progress
                continue;
            }
            kids.push_back(std::move(v));
            moveAll(kids, pending_);

            if (cur_.kind == Comma) { consume(); continue; }
            if (cur_.kind == RBracket || cur_.isEof()) continue;
            error(cur_, L"expected ',' or ']' but found " + describe(cur_));
        }
        if (!closed) finishUnclosed(n);
        return n;
    }

    // Returns null only when it skipped a stray token instead of finding a key.
    ASTNodePtr parseMember(std::size_t depth) {
        const Token keyTok = cur_;
        const bool quoted = keyTok.kind == String;
        if (!quoted && !lx_.isIdentifierName(keyTok)) {
            error(keyTok, L"expected a property name but found " + describe(keyTok));
            consume();
            return nullptr;
        }

        ASTNodePtr prop = makeNode(ASTNodeType::Property, keyTok);
        const std::wstring_view raw = lx_.text(keyTok);
        prop->key = quoted ? decodeString(raw) : decodeIdentifier(raw);
        if (keyTok.has(TokenFlag_Unterminated)) error(keyTok, L"unterminated string");
        if (keyTok.has(TokenFlag_Invalid)) error(keyTok, L"malformed escape in property name");
        consume();

        if (cur_.kind == Colon) consume();
        else error(cur_, L"expected ':' after property name but found " + describe(cur_));

        ASTNodePtr val = parseValue(depth + 1);
        if (val) {
            extendTo(*prop, val->endOffset, val->endLine);
            kidsOf(prop).push_back(std::move(val));
        } else {
            error(cur_, L"expected a value but found " + describe(cur_));
            if (!isStructural(cur_.kind)) consume();  // stray token in value position
            extendTo(*prop, lastEnd_, lastEndLine_);
        }
        return prop;
    }

    std::wstring_view src_;
    ParseOptions opts_;
    Json5Lexer lx_;

    Token cur_;
    Token eof_;
    ASTChildren pending_;  // comments seen before cur_, not yet placed in a container
    std::size_t lastEnd_ = 0;
    std::size_t lastEndLine_ = 0;
    bool aborted_ = false;
    ParseResult result_;
};

}  // namespace

ParseResult parse(std::wstring_view source, const ParseOptions& options) {
    return Parser(source, options).run();
}

}  // namespace json5
}  // namespace lex
