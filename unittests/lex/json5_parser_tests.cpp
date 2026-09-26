// Tests for lex::json5::parse.

#include <lex/json5_parser.h>
#include "test_util.h"

#include <cstdint>
#include <cwchar>
#include <string>
#include <vector>

using namespace lex::json5;

namespace {

// One character per node: O object, A array, P property, p primitive,
// '/' line comment, '*' block comment.
std::wstring shape(const ASTChildren& kids) {
    std::wstring s;
    for (const ASTNodePtr& k : kids) {
        switch (k->type) {
        case ASTNodeType::Object:       s += L'O'; break;
        case ASTNodeType::Array:        s += L'A'; break;
        case ASTNodeType::Property:     s += L'P'; break;
        case ASTNodeType::Primitive:    s += L'p'; break;
        case ASTNodeType::CommentLine:  s += L'/'; break;
        case ASTNodeType::CommentBlock: s += L'*'; break;
        }
    }
    return s;
}

const ASTNode* propertyNamed(const ASTNode& obj, const std::wstring& key) {
    for (const ASTNodePtr& k : obj.children())
        if (k->type == ASTNodeType::Property && k->key == key) return k.get();
    return nullptr;
}

std::wstring escaped(const std::wstring& s) {
    std::wstring out;
    for (wchar_t c : s) {
        if (c >= 0x20 && c < 0x7f) out += c;
        else { wchar_t buf[8]; std::swprintf(buf, 8, L"\\x%X", static_cast<unsigned>(c)); out += buf; }
    }
    return out;
}

// Structural sanity that must hold for ANY input, valid or not.
bool wellFormed(const ASTNode& n, std::size_t srcSize, const ASTNode* parent) {
    if (n.startOffset > n.endOffset || n.endOffset > srcSize) return false;
    if (n.startLine > n.endLine) return false;
    if (parent && (n.startOffset < parent->startOffset || n.endOffset > parent->endOffset)) return false;

    switch (n.type) {
    case ASTNodeType::Object:
    case ASTNodeType::Array:
        for (const ASTNodePtr& c : n.children()) {
            if (!c) return false;
            if (c->type == ASTNodeType::Property && n.type == ASTNodeType::Array) return false;
            if (c->type == ASTNodeType::Primitive && n.type == ASTNodeType::Object) return false;
            if (!wellFormed(*c, srcSize, &n)) return false;
        }
        return true;
    case ASTNodeType::Property:
        if (n.children().size() > 1) return false;
        for (const ASTNodePtr& c : n.children()) {
            if (!c || c->isComment() || c->type == ASTNodeType::Property) return false;
            if (!wellFormed(*c, srcSize, &n)) return false;
        }
        return true;
    default:
        return n.children().empty();
    }
}

void checkTree(const std::wstring& src) {
    const int before = g_failures;
    {
        ParseResult r = parse(src);
        if (r.root) CHECK(wellFormed(*r.root, src.size(), nullptr));
        for (const ASTNodePtr& c : r.leadingComments) CHECK(c && c->isComment() && wellFormed(*c, src.size(), nullptr));
        for (const ASTNodePtr& c : r.trailingComments) CHECK(c && c->isComment() && wellFormed(*c, src.size(), nullptr));
        for (const ParseError& e : r.errors) CHECK(e.offset <= src.size() && e.offset + e.length <= src.size());
        if (!r.root) CHECK(!r.errors.empty());
    }
    if (g_failures != before) std::wprintf(L"  ^ source: %ls\n", escaped(src).c_str());
}

}  // namespace

void runJson5ParserTests() {
    // --- tree shape -----------------------------------------------------------------
    {
        const std::wstring src = L"{a:1,'b':[true,null,\"x\"],\"c\":{d:-Infinity}}";
        ParseResult r = parse(src);
        CHECK(r.ok());
        CHECK(r.root && r.root->type == ASTNodeType::Object);
        CHECK(shape(r.root->children()) == L"PPP");

        const ASTNode* a = propertyNamed(*r.root, L"a");
        CHECK(a && a->valueNode() && a->valueNode()->primitive == PrimitiveKind::Number &&
              a->valueNode()->text() == L"1");

        const ASTNode* b = propertyNamed(*r.root, L"b");
        CHECK(b && b->valueNode() && b->valueNode()->type == ASTNodeType::Array);
        if (b && b->valueNode()) {
            const ASTChildren& el = b->valueNode()->children();
            CHECK(shape(el) == L"ppp");
            CHECK(el[0]->primitive == PrimitiveKind::Boolean && el[0]->text() == L"true");
            CHECK(el[1]->primitive == PrimitiveKind::Null);
            CHECK(el[2]->primitive == PrimitiveKind::String && el[2]->text() == L"\"x\"");
        }

        const ASTNode* c = propertyNamed(*r.root, L"c");
        const ASTNode* d = c && c->valueNode() ? propertyNamed(*c->valueNode(), L"d") : nullptr;
        CHECK(d && d->valueNode() && d->valueNode()->text() == L"-Infinity");
    }

    // --- primitives at the root, trailing commas, empty containers ---------------------
    {
        ParseResult r1 = parse(L"  42  ");
        CHECK(r1.ok() && r1.root->type == ASTNodeType::Primitive && r1.root->text() == L"42");
        CHECK(r1.root->startOffset == 2 && r1.root->endOffset == 4);

        ParseResult r2 = parse(L"[1, 2, 3,]");
        CHECK(r2.ok() && shape(r2.root->children()) == L"ppp");

        ParseResult r3 = parse(L"{a: 1, b: 2,}");
        CHECK(r3.ok() && shape(r3.root->children()) == L"PP");

        ParseResult r4 = parse(L"{ }");
        CHECK(r4.ok() && r4.root->children().empty());
        ParseResult r5 = parse(L"[]");
        CHECK(r5.ok() && r5.root->children().empty());
    }

    // --- keys are decoded ---------------------------------------------------------------
    {
        const std::wstring src = L"{ 'a\\nb': 1, \\u0061: 2, true: 3, Infinity: 4, \"q\\\"\": 5 }";
        ParseResult r = parse(src);
        CHECK(r.ok());
        CHECK(r.root && r.root->children().size() == 5);
        if (r.root && r.root->children().size() == 5) {
            const ASTChildren& k = r.root->children();
            CHECK(k[0]->key == L"a\nb");
            CHECK(k[1]->key == L"a");
            CHECK(k[2]->key == L"true");
            CHECK(k[3]->key == L"Infinity");
            CHECK(k[4]->key == L"q\"");
        }
    }

    // --- comment interleaving ------------------------------------------------------------
    {
        const std::wstring src =
            L"// lead\n"
            L"{\n"
            L"  // c1\n"
            L"  a: 1, // t1\n"
            L"  /* c2 */ b /* k */ : /* v */ 2 /* p */, // t2\n"
            L"  c: [ // in-arr\n"
            L"    1, 2, // x\n"
            L"  ],\n"
            L"  /* last */\n"
            L"} // trail\n";
        ParseResult r = parse(src);
        CHECK(r.ok());
        CHECK(shape(r.leadingComments) == L"/");
        CHECK(shape(r.trailingComments) == L"/");
        CHECK(r.leadingComments.size() == 1 && r.leadingComments[0]->text() == L"// lead");
        CHECK(r.trailingComments.size() == 1 && r.trailingComments[0]->text() == L"// trail");
        CHECK(r.root && shape(r.root->children()) == L"/P/*P***/P*");

        const ASTNode* c = r.root ? propertyNamed(*r.root, L"c") : nullptr;
        CHECK(c && c->valueNode() && shape(c->valueNode()->children()) == L"/pp/");

        // Comment nodes keep their own positions: t1 is on the same line as property a.
        const ASTNode* a = r.root ? propertyNamed(*r.root, L"a") : nullptr;
        const ASTNodePtr& t1 = r.root->children()[2];
        CHECK(a && t1->text() == L"// t1" && t1->startLine == a->endLine);

        // Comment-only containers.
        ParseResult e = parse(L"{ /* nothing */ }");
        CHECK(e.ok() && shape(e.root->children()) == L"*");
        ParseResult e2 = parse(L"[ // nothing\n ]");
        CHECK(e2.ok() && shape(e2.root->children()) == L"/");

        // A comment before a nested container's value is not swallowed by it.
        ParseResult n = parse(L"{ a: /* before */ { b: 1 } }");
        CHECK(n.ok());
        const ASTNode* na = n.root ? propertyNamed(*n.root, L"a") : nullptr;
        CHECK(na && na->valueNode() && shape(na->valueNode()->children()) == L"P");
        CHECK(n.root && shape(n.root->children()) == L"P*");

        // Only comments.
        ParseResult oc = parse(L"// nothing here\n");
        CHECK(!oc.root && !oc.errors.empty() && shape(oc.leadingComments) == L"/");
    }

    // --- positions --------------------------------------------------------------------------
    {
        const std::wstring src = L"{\n  a: 1,\n  b: [2],\n  c: \"x\\\ny\"\n}";
        ParseResult r = parse(src);
        CHECK(r.ok());
        CHECK(r.root && r.root->startLine == 0 && r.root->endLine == 5 &&
              r.root->startOffset == 0 && r.root->endOffset == src.size());

        const ASTNode* a = propertyNamed(*r.root, L"a");
        const ASTNode* b = propertyNamed(*r.root, L"b");
        const ASTNode* c = propertyNamed(*r.root, L"c");
        CHECK(a && a->startLine == 1 && a->endLine == 1);
        CHECK(a && src.substr(a->startOffset, a->endOffset - a->startOffset) == L"a: 1");
        CHECK(b && b->startLine == 2 && src.substr(b->startOffset, b->endOffset - b->startOffset) == L"b: [2]");
        CHECK(b && b->valueNode() && src.substr(b->valueNode()->startOffset,
              b->valueNode()->endOffset - b->valueNode()->startOffset) == L"[2]");
        CHECK(c && c->startLine == 3 && c->endLine == 4);  // string with a line continuation
    }

    // --- errors and recovery ------------------------------------------------------------------
    {
        ParseResult r1 = parse(L"{a 1}");  // missing ':'
        CHECK(r1.errors.size() == 1 && r1.root && shape(r1.root->children()) == L"P");
        CHECK(propertyNamed(*r1.root, L"a") && propertyNamed(*r1.root, L"a")->valueNode() &&
              propertyNamed(*r1.root, L"a")->valueNode()->text() == L"1");

        ParseResult r2 = parse(L"{a:}");  // missing value
        CHECK(!r2.errors.empty() && r2.root && shape(r2.root->children()) == L"P");
        CHECK(propertyNamed(*r2.root, L"a") && !propertyNamed(*r2.root, L"a")->valueNode());

        ParseResult r3 = parse(L"[1 2]");  // missing comma
        CHECK(r3.errors.size() == 1 && shape(r3.root->children()) == L"pp");

        ParseResult r4 = parse(L"[1,,2]");  // doubled comma
        CHECK(r4.errors.size() == 1 && shape(r4.root->children()) == L"pp");

        ParseResult r5 = parse(L"{a:1 b:2}");  // missing comma
        CHECK(r5.errors.size() == 1 && shape(r5.root->children()) == L"PP");

        ParseResult r6 = parse(L"{a: b}");  // identifier where a value belongs
        CHECK(r6.errors.size() == 1 && r6.root && shape(r6.root->children()) == L"P");

        ParseResult r7 = parse(L"{\"a\":1");  // unterminated object
        CHECK(!r7.errors.empty() && r7.root && shape(r7.root->children()) == L"P");
        CHECK(r7.root && r7.root->endOffset == 6);

        ParseResult r8 = parse(L"{} {}");  // trailing content
        CHECK(r8.errors.size() == 1 && r8.root && r8.root->type == ASTNodeType::Object);

        ParseResult r9 = parse(L"");
        CHECK(!r9.root && r9.errors.size() == 1);

        ParseResult r10 = parse(L"]");
        CHECK(!r10.root && !r10.errors.empty());

        ParseResult r11 = parse(L"[01]");  // malformed number stays a node
        CHECK(r11.errors.size() == 1 && shape(r11.root->children()) == L"p" &&
              r11.root->children()[0]->text() == L"01");

        ParseResult r12 = parse(L"\"abc");  // unterminated string
        CHECK(r12.errors.size() == 1 && r12.root && r12.root->type == ASTNodeType::Primitive);

        ParseResult r13 = parse(L"-");
        CHECK(!r13.root && !r13.errors.empty());

        ParseResult r14 = parse(L"[1, @, 2]");  // lexer error inside an array
        CHECK(!r14.errors.empty() && shape(r14.root->children()) == L"pp");

        ParseResult r15 = parse(L"{1: 2}");  // numeric key
        CHECK(!r15.errors.empty() && r15.root);

        ParseResult r16 = parse(L"/* open");
        CHECK(!r16.root && !r16.errors.empty());
    }

    // --- nesting limit ---------------------------------------------------------------------------
    {
        ParseResult ok = parse(std::wstring(200, L'[') + std::wstring(200, L']'));
        CHECK(ok.ok());

        ParseResult deep = parse(std::wstring(300, L'[') + std::wstring(300, L']'));
        CHECK(!deep.errors.empty() && deep.root);

        const std::wstring huge(100000, L'{');  // must not overflow the stack
        checkTree(huge);
        checkTree(std::wstring(100000, L'['));  // hits maxDepth; tree teardown must not recurse per level
    }

    // --- decodeString / decodeIdentifier ---------------------------------------------------------------
    {
        CHECK(decodeString(L"'\\x41\\u0042\\n\\t\\\\\\'\"'") == L"AB\n\t\\'\"");
        CHECK(decodeString(L"\"a\\\nb\"") == L"ab");          // line continuation
        CHECK(decodeString(L"\"a\\\r\nb\"") == L"ab");        // CRLF continuation
        CHECK(decodeString(L"\"\\q\"") == L"q");               // unknown escape
        CHECK(decodeString(L"\"\\xZ1\"") == L"xZ1");           // malformed hex is literal
        CHECK(decodeString(L"\"abc") == L"abc");               // unterminated
        CHECK(decodeString(L"\"abc\\") == L"abc");             // dangling backslash
        CHECK(decodeString(L"\"\"") == L"");
        CHECK(decodeString(L"'\\0'") == std::wstring(1, L'\0'));
        CHECK(decodeString(L"\"\\ud83d\\ude00\"") == L"\xD83D\xDE00");  // surrogate pair stays paired
        CHECK(decodeIdentifier(L"a\\u0062c") == L"abc");
        CHECK(decodeIdentifier(L"a\\u00") == L"a\\u00");
    }

    // --- fuzz: any token soup must terminate with a well-formed tree ----------------------------------------
    {
        const wchar_t* pieces[] = {
            L"{", L"}", L"[", L"]", L":", L",", L"a", L"'s'", L"\"t\"", L"1", L"-1", L"0x", L"true", L"null",
            L"Infinity", L"//c\n", L"/*c*/", L"/*", L" ", L"\n", L"\"", L"\\", L"@", L".5", L"a:1", L"'k':[]",
        };
        const std::size_t n = sizeof(pieces) / sizeof(pieces[0]);
        std::uint32_t rng = 987654321;
        auto rnd = [&rng]() { rng = rng * 1664525u + 1013904223u; return rng >> 8; };
        for (int iter = 0; iter < 5000; ++iter) {
            std::wstring s;
            const std::size_t count = 1 + static_cast<std::size_t>(rnd() % 25);
            for (std::size_t i = 0; i < count; ++i) s += pieces[rnd() % n];
            checkTree(s);
        }
    }
}
