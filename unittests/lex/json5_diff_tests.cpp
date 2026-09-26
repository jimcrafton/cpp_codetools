// Tests for structural hashing and the semantic diff (json5_hash.h, json5_diff.h).

#include <lex/json5_diff.h>
#include <lex/json5_hash.h>
#include "test_gen.h"
#include "test_util.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <functional>
#include <string>
#include <vector>

using namespace lex::json5;
using namespace testgen;

namespace {

std::size_t hashOf(const wchar_t* src) {
    ParseResult r = parse(src);
    if (!r.root) return 0;
    computeStructuralHashes(*r.root);
    return r.root->structuralHash;
}

DiffResult run(const std::wstring& a, const std::wstring& b, const DiffOptions& o = {}) {
    const ParseResult pa = parse(a);
    const ParseResult pb = parse(b);
    return diff(pa, pb, o);  // the result keeps the trees alive
}

// One character per child entry: = unchanged, + added, - removed, ~ mutated, > moved, # comment.
std::wstring kids(const DiffNode& d) {
    std::wstring s;
    for (const DiffNode& c : d.children) {
        if (c.isCommentEntry()) { s += L'#'; continue; }
        switch (c.status) {
        case DiffStatus::Unchanged: s += L'='; break;
        case DiffStatus::Added:     s += L'+'; break;
        case DiffStatus::Removed:   s += L'-'; break;
        case DiffStatus::Mutated:   s += L'~'; break;
        case DiffStatus::Moved:     s += L'>'; break;
        }
    }
    return s;
}

std::wstring escaped(const std::wstring& s) {
    std::wstring out;
    for (wchar_t c : s) {
        if (c >= 0x20 && c < 0x7f) out += c;
        else { wchar_t buf[8]; std::swprintf(buf, 8, L"\\x%X", static_cast<unsigned>(c)); out += buf; }
    }
    return out;
}

// ---- structural checks that must hold for ANY diff --------------------------------

const ASTNode* valOf(const ASTNode* n) { return n->type == ASTNodeType::Property ? n->valueNode() : n; }

std::vector<const ASTNode*> nonComments(const ASTNode& c) {
    std::vector<const ASTNode*> v;
    for (const ASTNodePtr& k : c.children()) if (!k->isComment()) v.push_back(k.get());
    return v;
}
std::vector<const ASTNode*> commentsOf(const ASTNode& c) {
    std::vector<const ASTNode*> v;
    for (const ASTNodePtr& k : c.children()) if (k->isComment()) v.push_back(k.get());
    return v;
}

// Statuses agree with the nodes/hashes, and a Mutated container accounts for every
// old member exactly once and every new member (and comment) once, in new order.
bool consistent(const DiffNode& d) {
    if (d.isCommentEntry()) return d.status == DiffStatus::Unchanged && !d.oldNode && d.children.empty();

    switch (d.status) {
    case DiffStatus::Unchanged:
        if (!d.oldNode || !d.newNode || d.oldNode->structuralHash != d.newNode->structuralHash ||
            !d.children.empty()) return false;
        break;
    case DiffStatus::Added:
        if (d.oldNode || !d.newNode || !d.children.empty()) return false;
        break;
    case DiffStatus::Removed:
        if (!d.oldNode || d.newNode || !d.children.empty()) return false;
        break;
    case DiffStatus::Mutated:
        if (!d.oldNode || !d.newNode || d.oldNode->structuralHash == d.newNode->structuralHash) return false;
        break;
    case DiffStatus::Moved:
        // (Indices may coincide: an element that only *appears* not to have moved because
        // another one jumped over it still counts as moved.)
        if (!d.oldNode || !d.newNode || d.oldNode->structuralHash != d.newNode->structuralHash ||
            d.oldIndex == kNoIndex || d.newIndex == kNoIndex || !d.children.empty()) return false;
        break;
    }

    if (d.status != DiffStatus::Mutated || d.children.empty()) return true;

    const ASTNode* ov = valOf(d.oldNode);
    const ASTNode* nv = valOf(d.newNode);
    if (!ov || !nv || ov->type != nv->type || !ov->isContainer()) return false;

    const std::vector<const ASTNode*> oldList = nonComments(*ov);
    const std::vector<const ASTNode*> newList = nonComments(*nv);
    std::vector<const ASTNode*> seqNew, seqComments, seenOld;
    for (const DiffNode& c : d.children) {
        if (c.isCommentEntry()) { seqComments.push_back(c.newNode); continue; }
        if (c.newNode) {
            seqNew.push_back(c.newNode);
            if (c.newIndex >= newList.size() || newList[c.newIndex] != c.newNode) return false;
        }
        if (c.oldNode) {
            seenOld.push_back(c.oldNode);
            if (c.oldIndex >= oldList.size() || oldList[c.oldIndex] != c.oldNode) return false;
        }
    }
    if (seqNew != newList) return false;                    // new members, in new order
    if (seqComments != commentsOf(*nv)) return false;       // new-side comments, in order
    std::vector<const ASTNode*> sortedOld = oldList;
    std::sort(seenOld.begin(), seenOld.end());
    std::sort(sortedOld.begin(), sortedOld.end());
    if (seenOld != sortedOld) return false;                 // each old member exactly once

    for (const DiffNode& c : d.children)
        if (!consistent(c)) return false;
    return true;
}

}  // namespace

void runJson5DiffTests() {
    // --- number parsing ---------------------------------------------------------------
    {
        double v = 0;
        CHECK(parseNumberValue(L".5", v) && v == 0.5);
        CHECK(parseNumberValue(L"5.", v) && v == 5.0);
        CHECK(parseNumberValue(L"-.5", v) && v == -0.5);
        CHECK(parseNumberValue(L"+1e2", v) && v == 100.0);
        CHECK(parseNumberValue(L"0xFF", v) && v == 255.0);
        CHECK(parseNumberValue(L"-0x10", v) && v == -16.0);
        CHECK(parseNumberValue(L"Infinity", v) && std::isinf(v) && v > 0);
        CHECK(parseNumberValue(L"-Infinity", v) && std::isinf(v) && v < 0);
        CHECK(parseNumberValue(L"NaN", v) && std::isnan(v));
        for (const wchar_t* bad : {L"1e", L"0x", L"", L"abc", L"1_0", L"0xZ", L"-", L"1e999"})
            CHECK(!parseNumberValue(bad, v));
    }

    // --- hashing: same meaning => same hash -------------------------------------------------
    {
        CHECK(hashOf(L"1") == hashOf(L"1.0"));
        CHECK(hashOf(L"1") == hashOf(L"+1"));
        CHECK(hashOf(L"1") == hashOf(L"0x1"));
        CHECK(hashOf(L"1") == hashOf(L"1e0"));
        CHECK(hashOf(L"1") == hashOf(L"1."));
        CHECK(hashOf(L"31") == hashOf(L"0x1F"));
        CHECK(hashOf(L"0") == hashOf(L"-0"));
        CHECK(hashOf(L"NaN") == hashOf(L"-NaN"));
        CHECK(hashOf(L"Infinity") == hashOf(L"+Infinity"));
        CHECK(hashOf(L"'a'") == hashOf(L"\"a\""));
        CHECK(hashOf(L"'\\x41'") == hashOf(L"\"A\""));
        CHECK(hashOf(L"\"a\\\nb\"") == hashOf(L"\"ab\""));  // line continuation
        CHECK(hashOf(L"{a:1,b:2}") == hashOf(L"{b:2,a:1}"));
        CHECK(hashOf(L"{a:1,b:2}") == hashOf(L"{'b':2,\"a\":1}"));
        CHECK(hashOf(L"{\\u0061:1}") == hashOf(L"{a:1}"));
        CHECK(hashOf(L"{a:1 /*x*/, // y\n b:[1,/*z*/2]}") == hashOf(L"{b:[1,2],a:1}"));
        CHECK(hashOf(L"{a:1,a:2}") == hashOf(L"{a:2,a:1}"));  // duplicate keys: a multiset
    }

    // --- hashing: different meaning => different hash ------------------------------------------
    {
        CHECK(hashOf(L"1") != hashOf(L"2"));
        CHECK(hashOf(L"1") != hashOf(L"\"1\""));
        CHECK(hashOf(L"true") != hashOf(L"\"true\""));
        CHECK(hashOf(L"true") != hashOf(L"false"));
        CHECK(hashOf(L"null") != hashOf(L"false"));
        CHECK(hashOf(L"null") != hashOf(L"0"));
        CHECK(hashOf(L"Infinity") != hashOf(L"-Infinity"));
        CHECK(hashOf(L"'a'") != hashOf(L"'b'"));
        CHECK(hashOf(L"[]") != hashOf(L"{}"));
        CHECK(hashOf(L"[1,2]") != hashOf(L"[2,1]"));
        CHECK(hashOf(L"[[1]]") != hashOf(L"[1]"));
        CHECK(hashOf(L"{a:1}") != hashOf(L"{a:2}"));
        CHECK(hashOf(L"{a:1}") != hashOf(L"{b:1}"));
        CHECK(hashOf(L"{a:1}") != hashOf(L"{a:1,b:2}"));
        CHECK(hashOf(L"{a:{b:1}}") != hashOf(L"{a:{b:2}}"));
        CHECK(hashOf(L"{a:[1]}") != hashOf(L"{a:{}}"));
    }

    // --- objects -------------------------------------------------------------------------------------
    {
        DiffResult d = run(L"{a:1,b:2,c:3}", L"{a:1,b:20,d:4}");
        CHECK(d.root.status == DiffStatus::Mutated && kids(d.root) == L"=~-+");
        CHECK(dumpDiff(d) == L"~ {\n  = a: 1\n  ~ b: 2 -> 20\n  - c: 3\n  + d: 4\n");
        CHECK(d.stats.unchanged == 1 && d.stats.mutated == 2 && d.stats.removed == 1 &&
              d.stats.added == 1 && d.stats.moved == 0);
        CHECK(consistent(d.root));

        CHECK(run(L"{a:1,b:2}", L"{b:2,a:1}").identical());          // key order isn't a change
        CHECK(run(L"{a:1}", L"{ // hi\n a:1 }").identical());         // comments aren't a change
        CHECK(run(L"{a:1}", L"{'a':1.0}").identical());

        DiffResult n = run(L"{v:{x:1,y:2}}", L"{v:{x:1,y:3}}");
        CHECK(kids(n.root) == L"~" && kids(n.root.children[0]) == L"=~");
        CHECK(consistent(n.root));

        DiffResult dup = run(L"{a:1,a:2}", L"{a:1,a:3}");
        CHECK(kids(dup.root) == L"=~");

        // A value that changed type is a leaf mutation, not a nested diff.
        DiffResult t1 = run(L"{a:1}", L"{a:{b:1}}");
        CHECK(kids(t1.root) == L"~" && t1.root.children[0].children.empty());
        CHECK(dumpDiff(t1) == L"~ {\n  ~ a: 1 -> {...}\n");
        DiffResult t2 = run(L"{a:[1]}", L"{a:{b:1}}");
        CHECK(kids(t2.root) == L"~" && t2.root.children[0].children.empty());
        DiffResult t3 = run(L"[1]", L"{a:1}");
        CHECK(t3.root.status == DiffStatus::Mutated && t3.root.children.empty());
    }

    // --- arrays -----------------------------------------------------------------------------------------
    {
        CHECK(kids(run(L"[1,2,3]", L"[1,3]").root) == L"=-=");
        CHECK(kids(run(L"[1,3]", L"[1,2,3]").root) == L"=+=");
        CHECK(kids(run(L"[1,1,2]", L"[1,2]").root) == L"=-=");
        CHECK(kids(run(L"[1,2,3]", L"[1,5,3]").root) == L"=~=");  // same-kind leftovers pair up
        CHECK(run(L"[1,2,3]", L"[1,2,3]").identical());

        // Move: "d" jumps to the front; the others keep their relative order.
        DiffResult mv = run(L"[\"a\",\"b\",\"c\",\"d\"]", L"[\"d\",\"a\",\"b\",\"c\"]");
        CHECK(kids(mv.root) == L">===");
        CHECK(mv.root.children[0].oldIndex == 3 && mv.root.children[0].newIndex == 0);
        CHECK(mv.stats.moved == 1 && mv.stats.removed == 0 && mv.stats.added == 0);
        CHECK(dumpDiff(mv) == L"~ [\n  > \"d\" [3 -> 0]\n  = \"a\"\n  = \"b\"\n  = \"c\"\n");
        CHECK(consistent(mv.root));

        // Changed child of a children array: nested diff, not remove + add.
        DiffResult ch = run(L"[{id:1,v:1},{id:2,v:2}]", L"[{id:1,v:1},{id:2,v:9}]");
        CHECK(kids(ch.root) == L"=~" && kids(ch.root.children[1]) == L"=~");
        CHECK(dumpDiff(ch) == L"~ [\n  = {...}\n  ~ {\n    = id: 2\n    ~ v: 2 -> 9\n");
        CHECK(consistent(ch.root));

        DiffOptions noPair;
        noPair.pairSimilarInGaps = false;
        DiffResult np = run(L"[{id:1,v:1},{id:2,v:2}]", L"[{id:1,v:1},{id:2,v:9}]", noPair);
        CHECK(kids(np.root) == L"=-+");

        // Dissimilar leftovers are not paired.
        DiffResult far = run(L"[{a:1,b:2,c:3}]", L"[{x:1,y:2,z:3}]");
        CHECK(kids(far.root) == L"-+");

        // Without any LCS (no table, no Myers) only prefix/suffix + moves apply; still consistent.
        DiffOptions noLcs;
        noLcs.maxLcsCells = 1;
        noLcs.maxEditDistance = 0;
        DiffResult rev = run(L"[\"a\",\"b\",\"c\"]", L"[\"c\",\"b\",\"a\"]", noLcs);
        CHECK(kids(rev.root) == L">=>");  // b stays at index 1: not a move
        CHECK(rev.stats.moved == 2 && consistent(rev.root));

        // A big array with one element relocated: table too large, so Myers finds the
        // LCS and exactly one element is reported as moved (not thousands).
        std::wstring big = L"[";
        for (int i = 0; i < 3000; ++i) big += std::to_wstring(i) + L",";
        big += L"]";
        std::wstring moved = big;
        const std::size_t cut = moved.find(L",10,") + 1;
        moved.erase(cut, 3);                  // remove "10,"
        moved.insert(moved.size() - 1, L"10,");  // ... and append it
        DiffResult bigDiff = run(big, moved);
        CHECK(bigDiff.stats.moved == 1 && bigDiff.stats.added == 0 && bigDiff.stats.removed == 0);
        CHECK(consistent(bigDiff.root));

        // Same shape with real edits at both ends: one deleted, one inserted.
        DiffResult del = run(big, L"[" + big.substr(3, big.size() - 4) + L",3000]");
        CHECK(del.stats.removed == 1 && del.stats.added == 1 && del.stats.moved == 0);
        CHECK(consistent(del.root));

        // Nested arrays.
        DiffResult nest = run(L"[[1,2],[3]]", L"[[1,2,4],[3]]");
        CHECK(kids(nest.root) == L"~=" && kids(nest.root.children[0]) == L"==+");
        CHECK(consistent(nest.root));

        // Property holding an array: the property is the slot, its children the elements.
        DiffResult pa = run(L"{list:[1,2]}", L"{list:[1,2,3]}");
        CHECK(kids(pa.root) == L"~" && kids(pa.root.children[0]) == L"==+");
        CHECK(dumpDiff(pa) == L"~ {\n  ~ list: [\n    = 1\n    = 2\n    + 3\n");
    }

    // --- comments ------------------------------------------------------------------------------------------
    {
        DiffResult c = run(L"{a:1}", L"{ /* c */ a:2 }");
        CHECK(kids(c.root) == L"#~");
        CHECK(dumpDiff(c) == L"~ {\n  = /* c */\n  ~ a: 1 -> 2\n");
        CHECK(consistent(c.root));
        CHECK(c.stats.unchanged == 0 && c.stats.mutated == 2);  // comment entries aren't counted
    }

    // --- roots ------------------------------------------------------------------------------------------------
    {
        DiffResult added = run(L"", L"{a:1}");
        CHECK(added.root.status == DiffStatus::Added && added.stats.added == 1);
        DiffResult removed = run(L"[1]", L"");
        CHECK(removed.root.status == DiffStatus::Removed && removed.stats.removed == 1);
        DiffResult none = run(L"", L"");
        CHECK(none.identical() && dumpDiff(none).empty());

        // The result outlives the parse results it was built from (run() drops them).
        DiffResult kept = run(L"{a:1}", L"{a:2}");
        CHECK(kept.root.children[0].oldNode->valueNode()->text() == L"1");
    }

    // --- depth: teardown and recursion at the parser's limit ------------------------------------------------------
    {
        const std::wstring deepA = std::wstring(250, L'[') + L"1" + std::wstring(250, L']');
        const std::wstring deepB = std::wstring(250, L'[') + L"2" + std::wstring(250, L']');
        DiffResult d = run(deepA, deepB);
        CHECK(d.root.status == DiffStatus::Mutated && d.stats.mutated == 251);
        CHECK(consistent(d.root));
    }

    // --- fuzz: random documents and edits ---------------------------------------------------------------------------
    {
        Rng r{20260919};
        DiffOptions myers;         // table disabled: every array goes through Myers
        myers.maxLcsCells = 1;
        DiffOptions noLcs;         // neither: prefix/suffix + moves only
        noLcs.maxLcsCells = 1;
        noLcs.maxEditDistance = 1;
        DiffOptions noPair;
        noPair.pairSimilarInGaps = false;
        const DiffOptions optionSets[] = {DiffOptions{}, myers, noLcs, noPair};

        for (int iter = 0; iter < 600; ++iter) {
            const G before = gen(r, 0);
            G after = before;
            for (std::size_t m = 1 + r.below(3); m > 0; --m) mutate(after, r);

            std::wstring plain, spelledDifferently, edited;
            Rng r1 = r, r2 = r;
            ser(before, r1, false, plain);
            ser(before, r2, true, spelledDifferently);
            ser(after, r, r.below(2) == 0, edited);

            const int failuresBefore = g_failures;

            // Respelling (quotes, number forms, key order, comments) never shows up as a change.
            CHECK(run(plain, spelledDifferently).identical());

            const bool sameMeaning = canonical(before) == canonical(after);
            for (const DiffOptions& o : optionSets) {
                DiffResult d = run(plain, edited, o);
                CHECK(consistent(d.root));
                CHECK(d.identical() == sameMeaning);
                CHECK(run(edited, edited, o).identical());
            }

            if (g_failures != failuresBefore) {
                std::wprintf(L"  ^ before: %ls\n    after:  %ls\n", escaped(plain).c_str(), escaped(edited).c_str());
                break;
            }
        }
    }
}
