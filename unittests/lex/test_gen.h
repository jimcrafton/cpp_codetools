#pragma once

// Random JSON5 documents for fuzz tests: a small value tree (G), a serializer that
// can respell the same meaning in many ways, and a mutator that edits the tree.

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace testgen {

// ---- random documents -----------------------------------------------------------------

struct Rng {
    std::uint32_t s;
    std::size_t next() { s = s * 1664525u + 1013904223u; return static_cast<std::size_t>(s >> 8); }
    std::size_t below(std::size_t n) { return next() % n; }
};

struct G {
    enum Kind { Obj, Arr, Num, Str, Bool, Null } kind = Null;
    int num = 0;
    std::wstring str;
    bool b = false;
    std::vector<std::pair<std::wstring, G>> kids;  // object: key + value; array: empty key
};

inline const wchar_t* const kKeys[] = {L"a", L"b", L"c", L"d", L"e", L"f"};
inline const wchar_t* const kStrs[] = {L"x", L"y", L"zz", L"hello"};

inline G genPrimitive(Rng& r) {
    G g;
    switch (r.below(4)) {
    case 0: g.kind = G::Num; g.num = static_cast<int>(r.below(10)); break;
    case 1: g.kind = G::Str; g.str = kStrs[r.below(4)]; break;
    case 2: g.kind = G::Bool; g.b = r.below(2) == 0; break;
    default: g.kind = G::Null; break;
    }
    return g;
}

inline G gen(Rng& r, int depth) {
    if (depth >= 3 || r.below(10) < 5) return genPrimitive(r);
    G g;
    if (r.below(2) == 0) {
        g.kind = G::Obj;
        std::vector<int> keys = {0, 1, 2, 3, 4, 5};
        for (std::size_t i = keys.size(); i > 1; --i) std::swap(keys[i - 1], keys[r.below(i)]);
        for (std::size_t i = r.below(5); i > 0; --i) {
            g.kids.emplace_back(kKeys[keys.back()], gen(r, depth + 1));
            keys.pop_back();
        }
    } else {
        g.kind = G::Arr;
        for (std::size_t i = r.below(6); i > 0; --i) g.kids.emplace_back(L"", gen(r, depth + 1));
    }
    return g;
}

// `variant` = same meaning, different spelling: quotes, number forms, key order, comments.
inline void ser(const G& g, Rng& r, bool variant, std::wstring& out) {
    auto noise = [&]() {
        if (!variant) return;
        static const wchar_t* n[] = {L"", L" ", L"\n", L" /*c*/ ", L" // c\n"};
        out += n[r.below(5)];
    };
    switch (g.kind) {
    case G::Num: {
        const std::wstring d = std::to_wstring(g.num);
        if (!variant) { out += d; break; }
        switch (r.below(4)) {
        case 0: out += d; break;
        case 1: out += d + L".0"; break;
        case 2: out += L"+" + d; break;
        default: out += L"0x" + d; break;
        }
        break;
    }
    case G::Str: {
        const wchar_t q = variant && r.below(2) ? L'\'' : L'"';
        out += q + g.str + q;
        break;
    }
    case G::Bool: out += g.b ? L"true" : L"false"; break;
    case G::Null: out += L"null"; break;
    case G::Arr:
        out += L"[";
        noise();
        for (std::size_t i = 0; i < g.kids.size(); ++i) {
            if (i) { out += L","; noise(); }
            ser(g.kids[i].second, r, variant, out);
            noise();
        }
        out += L"]";
        break;
    case G::Obj: {
        std::vector<std::size_t> order(g.kids.size());
        for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
        if (variant) for (std::size_t i = order.size(); i > 1; --i) std::swap(order[i - 1], order[r.below(i)]);
        out += L"{";
        noise();
        for (std::size_t k = 0; k < order.size(); ++k) {
            if (k) { out += L","; noise(); }
            const auto& kid = g.kids[order[k]];
            if (variant && r.below(2)) out += L"'" + kid.first + L"'";
            else out += kid.first;
            noise();
            out += L":";
            noise();
            ser(kid.second, r, variant, out);
            noise();
        }
        out += L"}";
        break;
    }
    }
}

// Canonical text: object keys sorted, so equal meaning <=> equal text.
inline std::wstring canonical(const G& g) {
    G copy = g;
    std::function<void(G&)> sortKeys = [&](G& n) {
        if (n.kind == G::Obj)
            std::sort(n.kids.begin(), n.kids.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        for (auto& k : n.kids) sortKeys(k.second);
    };
    sortKeys(copy);
    Rng unused{1};
    std::wstring out;
    ser(copy, unused, false, out);
    return out;
}

inline void collectNodes(G& g, std::vector<G*>& out) {
    out.push_back(&g);
    for (auto& k : g.kids) collectNodes(k.second, out);
}

inline void mutate(G& root, Rng& r) {
    std::vector<G*> all;
    collectNodes(root, all);
    G& t = *all[r.below(all.size())];
    switch (t.kind) {
    case G::Num:  t.num = (t.num + 1 + static_cast<int>(r.below(8))) % 10; break;
    case G::Str:  t.str += L"!"; break;
    case G::Bool: t.b = !t.b; break;
    case G::Null: t = genPrimitive(r); t.kind = G::Num; t.num = static_cast<int>(r.below(10)); break;
    case G::Obj: {
        if (!t.kids.empty() && r.below(2)) { t.kids.erase(t.kids.begin() + static_cast<std::ptrdiff_t>(r.below(t.kids.size()))); break; }
        for (const wchar_t* key : kKeys) {
            bool taken = false;
            for (auto& k : t.kids) taken = taken || k.first == key;
            if (!taken) { t.kids.emplace_back(key, genPrimitive(r)); break; }
        }
        break;
    }
    case G::Arr: {
        const std::size_t n = t.kids.size();
        switch (r.below(4)) {
        case 0: if (n) t.kids.erase(t.kids.begin() + static_cast<std::ptrdiff_t>(r.below(n))); break;
        case 1: t.kids.insert(t.kids.begin() + static_cast<std::ptrdiff_t>(r.below(n + 1)), {L"", gen(r, 2)}); break;
        case 2: if (n > 1) std::swap(t.kids[r.below(n)], t.kids[r.below(n)]); break;
        default:
            if (n > 1) {
                auto item = t.kids[r.below(n)];
                t.kids.erase(std::find_if(t.kids.begin(), t.kids.end(), [&](auto&) { return true; }));
                t.kids.insert(t.kids.begin() + static_cast<std::ptrdiff_t>(r.below(t.kids.size() + 1)), item);
            }
        }
        break;
    }
    }
}


}  // namespace testgen
