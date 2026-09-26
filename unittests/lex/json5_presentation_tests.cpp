// Tests for the flat presentation list (json5_presentation.h).

#include <lex/json5_hash.h>
#include <lex/json5_presentation.h>
#include "test_gen.h"
#include "test_util.h"

#include <cstdint>
#include <cwchar>
#include <string>
#include <vector>

using namespace lex::json5;
using namespace testgen;

namespace {

DiffResult run(const std::wstring& a, const std::wstring& b, const DiffOptions& o = {}) {
    const ParseResult pa = parse(a);
    const ParseResult pb = parse(b);
    return diff(pa, pb, o);
}

// The expected form of one dumped line.
std::wstring row(const wchar_t* marker, int depth, const std::wstring& text) {
    return std::wstring(marker) + std::wstring(static_cast<std::size_t>(depth) * 2, L' ') + text + L"\n";
}

// A column as one string: texts joined with '|', spacers shown as '_'.
std::wstring col(const std::vector<VisualLine>& lines) {
    std::wstring s;
    for (const VisualLine& l : lines) {
        if (!s.empty()) s += L'|';
        s += l.isSpacer() ? std::wstring(L"_") : l.text;
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

// Re-assemble one document from lines: those of `want` (or shared by both).
std::wstring textOfSide(const std::vector<VisualLine>& lines, LineSide want) {
    std::wstring out;
    for (const VisualLine& l : lines) {
        if (l.isSpacer() || (l.side != LineSide::Both && l.side != want)) continue;
        out.append(static_cast<std::size_t>(l.depth) * 2, L' ');
        out += l.text + L"\n";
    }
    return out;
}

std::wstring textOfColumn(const std::vector<VisualLine>& lines) {
    std::wstring out;
    for (const VisualLine& l : lines) {
        if (l.isSpacer()) continue;
        out.append(static_cast<std::size_t>(l.depth) * 2, L' ');
        out += l.text + L"\n";
    }
    return out;
}

// Does `text` parse cleanly into a document with structural hash `hash`?
bool parsesTo(const std::wstring& text, std::size_t hash) {
    ParseResult r = parse(text);
    if (!r.ok()) return false;
    computeStructuralHashes(*r.root);
    return r.root->structuralHash == hash;
}

// Row / count invariants that hold for any view state.
bool shapeOk(const std::vector<VisualLine>& uni, const SplitView& sp) {
    if (sp.left.size() != sp.right.size()) return false;
    std::size_t expected = 0;
    for (std::size_t i = 0; i < sp.left.size(); ++i) {
        const VisualLine& l = sp.left[i];
        const VisualLine& r = sp.right[i];
        if (l.isSpacer() && r.isSpacer()) return false;
        // A moved element's ghost (its old position, children included) exists only in split view.
        if (!l.isSpacer() && l.side == LineSide::Old && l.diffNode && l.diffNode->status == DiffStatus::Moved)
            continue;
        // Unified shows a shared line once and every other line once.
        const bool shared = !l.isSpacer() && !r.isSpacer() && l.side == LineSide::Both && r.side == LineSide::Both;
        expected += shared ? 1 : (l.isSpacer() ? 0 : 1) + (r.isSpacer() ? 0 : 1);
    }
    return expected == uni.size();
}

// The original layout (no compact one-liners, no previews): the older tests describe it.
FlattenOptions legacyOptions() {
    FlattenOptions o;
    o.compactContainers = false;
    o.preview = false;
    return o;
}
std::vector<VisualLine> plainUnified(const DiffResult& d, const ViewState& s = ViewState(),
                                     const FlattenOptions& o = legacyOptions()) {
    return flattenUnified(d, s, o);
}
SplitView plainSplit(const DiffResult& d, const ViewState& s = ViewState(),
                     const FlattenOptions& o = legacyOptions()) {
    return flattenSplit(d, s, o);
}

}  // namespace

void runJson5PresentationTests() {
    // --- unified: an update shows old line(s) right before new line(s) ---------------------
    {
        DiffResult d = run(L"{a:1,b:2,c:3}", L"{a:1,b:20,d:4}");
        const auto lines = plainUnified(d);
        CHECK(dumpLines(lines) == row(L"~  ", 0, L"{") + row(L"   ", 1, L"a: 1,") + row(L"~- ", 1, L"b: 2,") +
                                      row(L"~+ ", 1, L"b: 20,") + row(L" - ", 1, L"c: 3,") +
                                      row(L" + ", 1, L"d: 4,") + row(L"~  ", 0, L"}"));
        CHECK(lines[2].side == LineSide::Old && lines[3].side == LineSide::New);
        CHECK(lines[2].status == DiffStatus::Mutated && lines[3].status == DiffStatus::Mutated);
        CHECK(lines[0].kind == LineKind::Open && lines[0].isCollapsible && lines[0].isExpanded);
        CHECK(lines[6].kind == LineKind::Close && !lines[6].isCollapsible);
        CHECK(lines[1].sourceNode && lines[1].sourceNode->key == L"a");
        CHECK(lines[1].diffNode && lines[1].diffNode->status == DiffStatus::Unchanged);

        // Split: the same rows side by side, spacers where a side has no counterpart.
        const SplitView sp = plainSplit(d);
        CHECK(col(sp.left) == L"{|a: 1,|b: 2,|c: 3,|_|}");
        CHECK(col(sp.right) == L"{|a: 1,|b: 20,|_|d: 4,|}");
        CHECK(sp.left[3].kind == LineKind::Leaf && sp.right[3].isSpacer());
        CHECK(sp.left[4].isSpacer() && sp.right[4].kind == LineKind::Leaf);
        CHECK(sp.right[3].status == DiffStatus::Removed);  // a spacer carries its row's status
        CHECK(sp.rowCount() == 6);
        CHECK(shapeOk(lines, sp));
    }

    // --- collapsing ---------------------------------------------------------------------------------
    {
        DiffResult d = run(L"{keep:{x:1,y:2},v:1}", L"{keep:{x:1,y:2},v:2}");

        auto lines = plainUnified(d);
        CHECK(dumpLines(lines) == row(L"~  ", 0, L"{") + row(L"   ", 1, L"keep: {...},") +
                                      row(L"~- ", 1, L"v: 1,") + row(L"~+ ", 1, L"v: 2,") + row(L"~  ", 0, L"}"));
        CHECK(lines[1].kind == LineKind::Collapsed && lines[1].isCollapsible && !lines[1].isExpanded);
        CHECK(lines[1].hiddenCount == 2 && lines[1].path == L"/keep");

        ViewState state;
        state.toggle(lines[1]);  // expand it
        lines = plainUnified(d, state);
        CHECK(dumpLines(lines) == row(L"~  ", 0, L"{") + row(L"   ", 1, L"keep: {") + row(L"   ", 2, L"x: 1,") +
                                      row(L"   ", 2, L"y: 2,") + row(L"   ", 1, L"},") + row(L"~- ", 1, L"v: 1,") +
                                      row(L"~+ ", 1, L"v: 2,") + row(L"~  ", 0, L"}"));
        CHECK(lines[1].kind == LineKind::Open && lines[1].isExpanded);
        CHECK(lines[4].kind == LineKind::Close && lines[4].path == L"/keep");

        state.toggle(lines[1]);  // and collapse again
        CHECK(plainUnified(d, state).size() == 5);
        state.toggle(lines[0]);  // a non-collapsible line? line 0 is the root: collapsible
        CHECK(plainUnified(d, state).size() == 1);  // the root itself can be collapsed on request

        // The default rule and the global modes.
        FlattenOptions expandUnchanged = legacyOptions();
        expandUnchanged.collapseUnchanged = false;
        CHECK(plainUnified(d, ViewState(), expandUnchanged).size() == 8);

        ViewState all;
        all.expandAll();
        CHECK(plainUnified(d, all).size() == 8);
        ViewState none;
        none.collapseAll();
        CHECK(plainUnified(d, none).size() == 5);  // root stays open; keep stays closed

        // A user-collapsed Mutated container becomes one line, still marked Mutated.
        DiffResult m = run(L"{a:{x:1,y:2}}", L"{a:{x:1,y:3}}");
        ViewState closed;
        closed.setExpanded(L"/a", false);
        const auto ml = plainUnified(m, closed);
        CHECK(ml.size() == 3 && ml[1].kind == LineKind::Collapsed && ml[1].status == DiffStatus::Mutated &&
              ml[1].side == LineSide::Both && ml[1].text == L"a: {...},");

        // Collapse state is keyed by path, so it survives a re-diff of edited text.
        DiffResult d2 = run(L"{keep:{x:1,y:2},v:1}", L"{keep:{x:1,y:2},v:99,w:0}");
        ViewState kept;
        kept.setExpanded(L"/keep", true);
        const auto l2 = plainUnified(d2, kept);
        CHECK(l2[1].kind == LineKind::Open && l2[1].path == L"/keep");
        kept.reset();
        CHECK(plainUnified(d2, kept)[1].kind == LineKind::Collapsed);
    }

    // --- added and removed subtrees ---------------------------------------------------------------------
    {
        DiffResult d = run(L"{}", L"{a:{b:1}}");
        const auto lines = plainUnified(d);
        CHECK(dumpLines(lines) == row(L"~  ", 0, L"{") + row(L" + ", 1, L"a: {") + row(L" + ", 2, L"b: 1,") +
                                      row(L" + ", 1, L"},") + row(L"~  ", 0, L"}"));
        const SplitView sp = plainSplit(d);
        CHECK(col(sp.left) == L"{|_|_|_|}" && col(sp.right) == L"{|a: {|b: 1,|},|}");
        CHECK(sp.left[1].status == DiffStatus::Added && sp.left[1].isSpacer());

        DiffResult r = run(L"{a:[1,2]}", L"{}");
        const SplitView rs = plainSplit(r);
        CHECK(col(rs.left) == L"{|a: [|1,|2,|],|}" && col(rs.right) == L"{|_|_|_|_|}");
        CHECK(shapeOk(plainUnified(r), rs));
    }

    // --- a value that changed type -----------------------------------------------------------------------
    {
        DiffResult d = run(L"{a:{b:1,c:2}}", L"{a:5}");
        const auto lines = plainUnified(d);
        CHECK(dumpLines(lines) == row(L"~  ", 0, L"{") + row(L"~- ", 1, L"a: {") + row(L"~- ", 2, L"b: 1,") +
                                      row(L"~- ", 2, L"c: 2,") + row(L"~- ", 1, L"},") + row(L"~+ ", 1, L"a: 5,") +
                                      row(L"~  ", 0, L"}"));
        const SplitView sp = plainSplit(d);
        CHECK(col(sp.left) == L"{|a: {|b: 1,|c: 2,|},|}");
        CHECK(col(sp.right) == L"{|a: 5,|_|_|_|}");
        CHECK(shapeOk(lines, sp));
    }

    // --- moves ----------------------------------------------------------------------------------------------
    {
        DiffResult d = run(L"[\"a\",\"b\",\"c\",\"d\"]", L"[\"d\",\"a\",\"b\",\"c\"]");
        const auto lines = plainUnified(d);
        CHECK(dumpLines(lines) == row(L"~  ", 0, L"[") + row(L" > ", 1, L"\"d\",") + row(L"   ", 1, L"\"a\",") +
                                      row(L"   ", 1, L"\"b\",") + row(L"   ", 1, L"\"c\",") + row(L"~  ", 0, L"]"));
        CHECK(lines[1].moveFrom == 3 && lines[1].moveTo == 0 && lines[1].side == LineSide::New);

        // Split: a ghost at the old position (left), the element at the new one (right).
        const SplitView sp = plainSplit(d);
        CHECK(col(sp.left) == L"[|_|\"a\",|\"b\",|\"c\",|\"d\",|]");
        CHECK(col(sp.right) == L"[|\"d\",|\"a\",|\"b\",|\"c\",|_|]");
        const VisualLine& ghost = sp.left[5];
        const VisualLine& dest = sp.right[1];
        CHECK(ghost.status == DiffStatus::Moved && ghost.side == LineSide::Old);
        CHECK(dest.status == DiffStatus::Moved && dest.side == LineSide::New);
        CHECK(ghost.moveFrom == 3 && ghost.moveTo == 0 && dest.moveFrom == 3 && dest.moveTo == 0);
        CHECK(ghost.diffNode == dest.diffNode && ghost.path != dest.path);
        CHECK(shapeOk(lines, sp));

        // A moved container is one collapsed line until expanded; its members didn't move.
        DiffResult c = run(L"[{k:1},{k:2},{k:3}]", L"[{k:3},{k:1},{k:2}]");
        const auto cl = plainUnified(c);
        CHECK(cl[1].status == DiffStatus::Moved && cl[1].kind == LineKind::Collapsed && cl[1].text == L"{...},");
        ViewState open;
        open.expandAll();
        const auto ol = plainUnified(c, open);
        CHECK(ol[1].status == DiffStatus::Moved && ol[2].status == DiffStatus::Unchanged &&
              ol[2].text == L"k: 3," && ol[3].status == DiffStatus::Moved);  // header and close are the element's
    }

    // --- comments -------------------------------------------------------------------------------------------
    {
        DiffResult d = run(L"{a:1}", L"{ // note\n a:2 }");
        CHECK(dumpLines(plainUnified(d)) == row(L"~  ", 0, L"{") + row(L"   ", 1, L"// note") +
                                                  row(L"~- ", 1, L"a: 1,") + row(L"~+ ", 1, L"a: 2,") +
                                                  row(L"~  ", 0, L"}"));
        CHECK(plainUnified(d)[1].kind == LineKind::Comment);

        // A block comment spanning lines becomes one line per line, indentation trimmed.
        DiffResult b = run(L"{a:1}", L"{ /* one\n     two */ a:2 }");
        const auto bl = plainUnified(b);
        CHECK(bl[1].text == L"/* one" && bl[2].text == L"two */" && bl[2].kind == LineKind::Comment);
        CHECK(bl[1].sourceNode == bl[2].sourceNode);
    }

    // --- key formatting -----------------------------------------------------------------------------------------
    {
        DiffResult d = run(L"{}", L"{\"a b\":1,'x\"y':2,class:3,\"\":4,\\u0061:5,\"1a\":6,\"tab\\t\":7}");
        const auto lines = plainUnified(d);
        CHECK(lines.size() == 9);
        CHECK(lines[1].text == L"\"a b\": 1,");
        CHECK(lines[2].text == L"\"x\\\"y\": 2,");
        CHECK(lines[3].text == L"class: 3,");
        CHECK(lines[4].text == L"\"\": 4,");
        CHECK(lines[5].text == L"a: 5,");
        CHECK(lines[6].text == L"\"1a\": 6,");
        CHECK(lines[7].text == L"\"tab\\t\": 7,");
    }

    // --- empty containers, roots, missing pieces -----------------------------------------------------------------------
    {
        DiffResult e = run(L"", L"");
        CHECK(plainUnified(e).empty() && plainSplit(e).rowCount() == 0);

        DiffResult p = run(L"1", L"2");
        CHECK(dumpLines(plainUnified(p)) == row(L"~- ", 0, L"1") + row(L"~+ ", 0, L"2"));
        const SplitView ps = plainSplit(p);
        CHECK(col(ps.left) == L"1" && col(ps.right) == L"2");

        DiffResult a = run(L"", L"[]");
        CHECK(dumpLines(plainUnified(a)) == row(L" + ", 0, L"[]"));

        DiffResult same = run(L"{a:[],b:{}}", L"{a:[],b:{}}");
        CHECK(same.identical());
        CHECK(dumpLines(plainUnified(same)) == row(L"   ", 0, L"{") + row(L"   ", 1, L"a: [],") +
                                                     row(L"   ", 1, L"b: {},") + row(L"   ", 0, L"}"));
        CHECK(!plainUnified(same)[1].isCollapsible);  // nothing to fold
        // Even a fully identical document keeps its top level visible and folds what's inside.
        DiffResult nested = run(L"{a:{x:1}}", L"{a:{x:1}}");
        CHECK(dumpLines(plainUnified(nested)) == row(L"   ", 0, L"{") + row(L"   ", 1, L"a: {...},") +
                                                       row(L"   ", 0, L"}"));
    }

    // --- compact containers ------------------------------------------------------------------------
    {
        // Both sides one-liners: an old line and a new line, the changed element marked.
        DiffResult d = run(L"{frame:[0,0,640,480]}", L"{frame:[0,0,800,480]}");
        const auto lines = flattenUnified(d);
        CHECK(dumpLines(lines) == row(L"~  ", 0, L"{") + row(L"~- ", 1, L"frame: [0, 0, 640, 480],") +
                                      row(L"~+ ", 1, L"frame: [0, 0, 800, 480],") + row(L"~  ", 0, L"}"));
        CHECK(lines[1].kind == LineKind::Leaf && !lines[1].isCollapsible);
        CHECK(lines[1].changedRanges.size() == 1 && lines[1].changedRanges[0].start == 14 &&
              lines[1].changedRanges[0].length == 3);
        CHECK(lines[2].changedRanges.size() == 1 && lines[2].changedRanges[0].start == 14 &&
              lines[2].changedRanges[0].length == 3);
        const SplitView sp = flattenSplit(d);
        CHECK(col(sp.left) == L"{|frame: [0, 0, 640, 480],|}" && col(sp.right) == L"{|frame: [0, 0, 800, 480],|}");
        CHECK(shapeOk(lines, sp));

        // The legacy layout spends a row per element.
        DiffResult p = run(L"{a:[1,2]}", L"{a:[1,3]}");
        CHECK(flattenUnified(p).size() == 4 && plainUnified(p).size() == 7);

        // Added and removed containers are one-liners too, objects with a space inside the braces.
        DiffResult add = run(L"{}", L"{a:[1,2],b:{x:1}}");
        const auto al = flattenUnified(add);
        CHECK(al.size() == 4 && al[1].text == L"a: [1, 2]," && al[2].text == L"b: { x: 1 },");
        CHECK(al[1].kind == LineKind::Leaf && al[1].status == DiffStatus::Added);

        // An object: the changed and the added member are marked on their side.
        DiffResult obj = run(L"{o:{a:1,b:2}}", L"{o:{a:1,b:3,c:4}}");
        const auto ol = flattenUnified(obj);
        CHECK(ol.size() == 4 && ol[1].text == L"o: { a: 1, b: 2 }," && ol[2].text == L"o: { a: 1, b: 3, c: 4 },");
        CHECK(ol[1].changedRanges.size() == 1 && ol[1].changedRanges[0].start == 11 &&
              ol[1].changedRanges[0].length == 4);
        CHECK(ol[2].changedRanges.size() == 2 && ol[2].changedRanges[0].start == 11 &&
              ol[2].changedRanges[1].start == 17);

        // An empty container on one side is still compact.
        DiffResult grow = run(L"{a:[]}", L"{a:[1]}");
        const auto gl = flattenUnified(grow);
        CHECK(gl.size() == 4 && gl[1].text == L"a: []," && gl[2].text == L"a: [1],");
        CHECK(gl[1].changedRanges.empty() && gl[2].changedRanges.size() == 1 && gl[2].changedRanges[0].start == 4);

        // A changed primitive marks its value.
        DiffResult n = run(L"{n:1}", L"{n:20}");
        const auto nl = flattenUnified(n);
        CHECK(nl[1].changedRanges.size() == 1 && nl[1].changedRanges[0].start == 3 &&
              nl[1].changedRanges[0].length == 1);
        CHECK(nl[2].changedRanges.size() == 1 && nl[2].changedRanges[0].start == 3 &&
              nl[2].changedRanges[0].length == 2);

        // A change of type is not a compact-vs-compact edit: no ranges.
        DiffResult t = run(L"{a:[1,2]}", L"{a:5}");
        const auto tl = flattenUnified(t);
        CHECK(dumpLines(tl) == row(L"~  ", 0, L"{") + row(L"~- ", 1, L"a: [1, 2],") + row(L"~+ ", 1, L"a: 5,") +
                                   row(L"~  ", 0, L"}"));
        CHECK(tl[1].changedRanges.empty());

        // Moved compact elements.
        DiffResult mv = run(L"[{k:1},{k:2},{k:3}]", L"[{k:3},{k:1},{k:2}]");
        CHECK(dumpLines(flattenUnified(mv)) == row(L"~  ", 0, L"[") + row(L" > ", 1, L"{ k: 3 },") +
                                                   row(L"   ", 1, L"{ k: 1 },") + row(L"   ", 1, L"{ k: 2 },") +
                                                   row(L"~  ", 0, L"]"));

        // When only one side is compact the change is diffed member by member.
        DiffResult half = run(L"{a:[1,2]}", L"{a:[1,2,[3]]}");
        CHECK(dumpLines(flattenUnified(half)) == row(L"~  ", 0, L"{") + row(L"~  ", 1, L"a: [") +
                                                     row(L"   ", 2, L"1,") + row(L"   ", 2, L"2,") +
                                                     row(L" + ", 2, L"[3],") + row(L"~  ", 1, L"],") +
                                                     row(L"~  ", 0, L"}"));

        // What does not compact: the root, comments, too many members, too wide.
        CHECK(flattenUnified(run(L"{a:1}", L"{a:1}")).size() == 3);
        CHECK(flattenUnified(run(L"{a:[1,/*c*/2]}", L"{a:[1,/*c*/2]}"))[1].kind == LineKind::Collapsed);
        const std::wstring many = L"{a:[1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17]}";
        CHECK(flattenUnified(run(many, many))[1].kind == LineKind::Collapsed);
        const std::wstring wideDoc =
            L"{a:['aaaaaaaaaaaaaaaaaaaa','bbbbbbbbbbbbbbbbbbbb','cccccccccccccccccccc','dddddddddddddddddddd']}";
        CHECK(flattenUnified(run(wideDoc, wideDoc))[1].kind == LineKind::Collapsed);
        FlattenOptions wide;
        wide.compactMaxWidth = 200;
        CHECK(flattenUnified(run(wideDoc, wideDoc), ViewState(), wide)[1].kind == LineKind::Leaf);
    }

    // --- previews of collapsed containers ------------------------------------------------------------
    {
        // Identifying keys first, then source order; a short compact value is shown in full.
        DiffResult d = run(L"{keep:{x:{z:1},y:2,type:'Box'},v:1}", L"{keep:{x:{z:1},y:2,type:'Box'},v:2}");
        const auto lines = flattenUnified(d);
        CHECK(lines[1].kind == LineKind::Collapsed && lines[1].hiddenCount == 3);
        CHECK(lines[1].text == L"keep: { type: 'Box', x: { z: 1 }, y: 2 },");

        // Long previews stop at the width limit and say so.
        const std::wstring bigDoc = L"{big:{a:{p:1},b:{p:2},c:{p:3},d:{p:4},e:{p:5},f:{p:6},g:{p:7},h:{p:8},i:{p:9}}}";
        const auto bl = flattenUnified(run(bigDoc, bigDoc));
        CHECK(bl[1].text == L"big: { a: { p: 1 }, b: { p: 2 }, c: { p: 3 }, d: { p: 4 }, ... },");
        CHECK(bl[1].hiddenCount == 9);

        // Long primitives are cut.
        const std::wstring longDoc = L"{o:{title:'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa',n:{q:1}}}";
        const std::wstring lt = flattenUnified(run(longDoc, longDoc))[1].text;
        CHECK(lt.find(L"title: 'aaaaaaaaaaaaaaaaaaaa...") != std::wstring::npos);
        CHECK(lt.find(L"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa") == std::wstring::npos);

        // An array previews its elements; nested containers that aren't short show as {...}.
        const std::wstring arrDoc = L"{l:[[1],[2],{a:{b:1}}]}";
        CHECK(flattenUnified(run(arrDoc, arrDoc))[1].text == L"l: [[1], [2], {...}],");

        // Off: the plain marker.
        FlattenOptions noPreview;
        noPreview.preview = false;
        CHECK(flattenUnified(d, ViewState(), noPreview)[1].text == L"keep: {...},");

        // A moved container shows its preview at the new position and at the ghost.
        DiffResult mv = run(L"[{k:{z:1}},{k:{z:2}},{k:{z:3}}]", L"[{k:{z:3}},{k:{z:1}},{k:{z:2}}]");
        const auto ml = flattenUnified(mv);
        CHECK(ml[1].status == DiffStatus::Moved && ml[1].kind == LineKind::Collapsed &&
              ml[1].text == L"{ k: { z: 3 } },");
        const SplitView ms = flattenSplit(mv);
        CHECK(col(ms.left) == L"[|_|{ k: { z: 1 } },|{ k: { z: 2 } },|{ k: { z: 3 } },|]");
        CHECK(col(ms.right) == L"[|{ k: { z: 3 } },|{ k: { z: 1 } },|{ k: { z: 2 } },|_|]");
    }

    // --- depth: recursion at the parser's limit (deep Debug frames must not overflow) ---------------------------
    {
        const std::wstring a = std::wstring(250, L'[') + L"1" + std::wstring(250, L']');
        const std::wstring b = std::wstring(250, L'[') + L"2" + std::wstring(250, L']');
        DiffResult d = run(a, b);
        const auto lines = flattenUnified(d);
        const SplitView sp = flattenSplit(d);
        CHECK(lines.size() > 400 && shapeOk(lines, sp));

        // A deep subtree that is entirely added is rendered by a different recursion.
        const std::wstring added = L"{a:" + a + L"}";
        DiffResult ad = run(L"{}", added);
        ViewState all;
        all.expandAll();
        const auto al = flattenUnified(ad, all);
        const SplitView as = flattenSplit(ad, all);
        CHECK(al.size() > 500 && shapeOk(al, as));
        CHECK(flattenUnified(run(added, added), all).size() > 500);  // ... and an unchanged one
    }

    // --- round trip: the lines rebuild both documents ----------------------------------------------------------------------
    // Everything expanded, the new-side lines must parse to the new document and (split view,
    // where moves have ghosts) the old-side lines to the old one. Then random collapse states
    // must keep rows aligned and counts consistent. Under both layouts.
    {
        Rng r{20260919};
        DiffOptions noPair;
        noPair.pairSimilarInGaps = false;
        const DiffOptions optionSets[] = {DiffOptions{}, noPair};
        const FlattenOptions layouts[] = {legacyOptions(), FlattenOptions{}};

        for (int iter = 0; iter < 500; ++iter) {
            const G before = gen(r, 0);
            G after = before;
            for (std::size_t m = 1 + r.below(3); m > 0; --m) mutate(after, r);

            std::wstring plain, edited;
            ser(before, r, false, plain);
            ser(after, r, r.below(2) == 0, edited);

            const int failuresBefore = g_failures;
            const DiffOptions& opts = optionSets[iter % 2];
            const ParseResult pa = parse(plain);
            const ParseResult pb = parse(edited);
            const DiffResult d = diff(pa, pb, opts);
            const std::size_t oldHash = pa.root->structuralHash;
            const std::size_t newHash = pb.root->structuralHash;

            for (const FlattenOptions& fo : layouts) {
                ViewState all;
                all.expandAll();
                const auto uni = flattenUnified(d, all, fo);
                const SplitView sp = flattenSplit(d, all, fo);

                CHECK(shapeOk(uni, sp));
                CHECK(parsesTo(textOfSide(uni, LineSide::New), newHash));
                if (d.stats.moved == 0) CHECK(parsesTo(textOfSide(uni, LineSide::Old), oldHash));
                CHECK(parsesTo(textOfColumn(sp.right), newHash));
                CHECK(parsesTo(textOfColumn(sp.left), oldHash));

                // Changed-range marks stay inside their line and only sit on changed lines.
                for (const VisualLine& l : uni)
                    for (const TextRange& rg : l.changedRanges)
                        CHECK(rg.start + rg.length <= l.text.size() && l.status == DiffStatus::Mutated);

                // Random collapsing: still aligned, still consistent.
                ViewState some;
                for (const VisualLine& l : uni)
                    if (l.isCollapsible && r.below(3) == 0) some.setExpanded(l.path, false);
                const auto u2 = flattenUnified(d, some, fo);
                const SplitView s2 = flattenSplit(d, some, fo);
                CHECK(shapeOk(u2, s2));
                CHECK(u2.size() <= uni.size());

                // The default view is never longer than the fully expanded one.
                CHECK(flattenUnified(d, ViewState(), fo).size() <= uni.size());
            }

            if (g_failures != failuresBefore) {
                std::wprintf(L"  ^ before: %ls\n    after:  %ls\n", escaped(plain).c_str(), escaped(edited).c_str());
                break;
            }
        }
    }
}
