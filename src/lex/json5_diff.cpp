#include <lex/json5_diff.h>

#include <algorithm>
#include <cstdint>
#include <unordered_map>

#include <lex/json5_hash.h>

namespace lex {
namespace json5 {

namespace {

constexpr std::size_t kNone = kNoIndex;

// The node whose members a slot's diff descends into: a property's value, or the
// node itself for array elements and the document root.
const ASTNode* valueOf(const ASTNode* n) {
    return n->type == ASTNodeType::Property ? n->valueNode() : n;
}

// A container's non-comment members, plus the comments that sit before each one:
// commentsBefore[s] precedes nodes[s]; commentsBefore[nodes.size()] trails the last.
struct Members {
    std::vector<const ASTNode*> nodes;
    std::vector<std::vector<const ASTNode*>> commentsBefore;
};

Members collect(const ASTNode& container) {
    Members m;
    m.commentsBefore.emplace_back();
    for (const ASTNodePtr& c : container.children()) {
        if (c->isComment()) {
            m.commentsBefore.back().push_back(c.get());
        } else {
            m.nodes.push_back(c.get());
            m.commentsBefore.emplace_back();
        }
    }
    return m;
}

DiffNode makeAdded(const ASTNode* n, std::size_t index) {
    DiffNode d;
    d.status = DiffStatus::Added;
    d.newNode = n;
    d.newIndex = index;
    return d;
}

DiffNode makeRemoved(const ASTNode* n, std::size_t index) {
    DiffNode d;
    d.status = DiffStatus::Removed;
    d.oldNode = n;
    d.oldIndex = index;
    return d;
}

void emitComments(const std::vector<const ASTNode*>& comments, std::vector<DiffNode>& out) {
    for (const ASTNode* c : comments) {
        DiffNode d;
        d.status = DiffStatus::Unchanged;
        d.newNode = c;
        out.push_back(std::move(d));
    }
}

// Longest common subsequence of two hash sequences by Myers' O(ND) greedy algorithm
// (D = number of insertions + deletions). Appends the matched (i, j) pairs in
// order. Returns false, with `matches` left empty, if D would exceed maxD.
bool myersLcs(const std::vector<std::size_t>& a, const std::vector<std::size_t>& b, std::size_t maxD,
              std::vector<std::pair<std::size_t, std::size_t>>& matches) {
    const int n = static_cast<int>(a.size());
    const int m = static_cast<int>(b.size());
    const int limit = static_cast<int>(std::min<std::size_t>(maxD, static_cast<std::size_t>(n) + m));
    const int off = limit + 1;

    std::vector<int> v(static_cast<std::size_t>(2 * limit + 3), 0);  // v[off + k]: furthest x on diagonal k
    std::vector<std::vector<int>> trace;                              // trace[d]: v before round d, k in [-d-1, d+1]

    for (int d = 0; d <= limit; ++d) {
        trace.emplace_back(v.begin() + (off - d - 1), v.begin() + (off + d + 2));

        for (int k = -d; k <= d; k += 2) {
            int x = (k == -d || (k != d && v[off + k - 1] < v[off + k + 1])) ? v[off + k + 1]
                                                                            : v[off + k - 1] + 1;
            int y = x - k;
            while (x < n && y < m && a[static_cast<std::size_t>(x)] == b[static_cast<std::size_t>(y)]) { ++x; ++y; }
            v[off + k] = x;

            if (x >= n && y >= m) {  // reached the end: walk back collecting the diagonals
                x = n;
                y = m;
                for (int dd = d; dd >= 0; --dd) {
                    const std::vector<int>& vv = trace[static_cast<std::size_t>(dd)];
                    auto at = [&](int kk) { return vv[static_cast<std::size_t>(kk + dd + 1)]; };
                    const int kk = x - y;
                    const int prevK = (kk == -dd || (kk != dd && at(kk - 1) < at(kk + 1))) ? kk + 1 : kk - 1;
                    const int prevX = at(prevK);
                    const int prevY = prevX - prevK;
                    while (x > prevX && y > prevY) {
                        --x;
                        --y;
                        matches.emplace_back(static_cast<std::size_t>(x), static_cast<std::size_t>(y));
                    }
                    x = prevX;
                    y = prevY;
                }
                std::reverse(matches.begin(), matches.end());
                return true;
            }
        }
    }
    return false;
}

class Differ {
public:
    explicit Differ(const DiffOptions& options) : opts_(options) {}

    // Compare two nodes occupying the same slot.
    DiffNode diffPair(const ASTNode* o, const ASTNode* n, std::size_t oldIndex, std::size_t newIndex) {
        DiffNode d;
        d.oldNode = o;
        d.newNode = n;
        d.oldIndex = oldIndex;
        d.newIndex = newIndex;

        if (o->structuralHash == n->structuralHash) {
            d.status = DiffStatus::Unchanged;
            return d;
        }
        d.status = DiffStatus::Mutated;

        const ASTNode* ov = valueOf(o);
        const ASTNode* nv = valueOf(n);
        if (ov && nv && ov->type == nv->type && ov->isContainer()) {
            if (ov->type == ASTNodeType::Object) diffObject(*ov, *nv, d.children);
            else diffArray(*ov, *nv, d.children);
        }
        return d;
    }

private:
    // ---- objects: an unordered map, members pair by key ----------------------

    void diffObject(const ASTNode& o, const ASTNode& n, std::vector<DiffNode>& out) {
        const Members om = collect(o);
        const Members nm = collect(n);
        const std::size_t no = om.nodes.size();
        const std::size_t nn = nm.nodes.size();

        // The k-th occurrence of a key on one side pairs with the k-th on the other.
        std::unordered_map<std::wstring, std::vector<std::size_t>> oldByKey;
        for (std::size_t i = 0; i < no; ++i) oldByKey[om.nodes[i]->key].push_back(i);

        std::unordered_map<std::wstring, std::size_t> used;
        std::vector<std::size_t> matchNew(nn, kNone);
        std::vector<char> oldMatched(no, 0);
        for (std::size_t j = 0; j < nn; ++j) {
            const auto it = oldByKey.find(nm.nodes[j]->key);
            if (it == oldByKey.end()) continue;
            std::size_t& cursor = used[nm.nodes[j]->key];
            if (cursor < it->second.size()) {
                const std::size_t i = it->second[cursor++];
                matchNew[j] = i;
                oldMatched[i] = 1;
            }
        }

        // Removed members go right after the nearest earlier old member that survived.
        std::vector<std::vector<std::size_t>> removedAfter(no + 1);  // slot = matched old index + 1
        std::size_t last = kNone;
        for (std::size_t i = 0; i < no; ++i) {
            if (oldMatched[i]) last = i;
            else removedAfter[last == kNone ? 0 : last + 1].push_back(i);
        }
        auto emitRemoved = [&](std::size_t slot) {
            for (std::size_t i : removedAfter[slot]) out.push_back(makeRemoved(om.nodes[i], i));
        };

        emitRemoved(0);
        for (std::size_t j = 0; j < nn; ++j) {
            emitComments(nm.commentsBefore[j], out);
            if (matchNew[j] == kNone) {
                out.push_back(makeAdded(nm.nodes[j], j));
            } else {
                const std::size_t i = matchNew[j];
                out.push_back(diffPair(om.nodes[i], nm.nodes[j], i, j));
                emitRemoved(i + 1);
            }
        }
        emitComments(nm.commentsBefore[nn], out);
    }

    // ---- arrays: ordered; LCS + moves + similar-pair in gaps ------------------

    // Which old element goes with which new one, and which of those pairs moved.
    // Computed in its own function so its bulky locals (LCS table, hash pool) are gone
    // before diffArray recurses: Debug builds keep every local alive for the whole
    // frame, and a deeply nested document would otherwise overflow the stack.
    struct ArrayMatch {
        std::vector<std::size_t> matchOld, matchNew;  // partner index or kNone
        std::vector<char> movedOld, movedNew;
    };

    ArrayMatch matchArray(const Members& om, const Members& nm) const {
        const std::size_t no = om.nodes.size();
        const std::size_t nn = nm.nodes.size();

        auto hashOld = [&](std::size_t i) { return om.nodes[i]->structuralHash; };
        auto hashNew = [&](std::size_t j) { return nm.nodes[j]->structuralHash; };

        std::vector<std::size_t> matchOld(no, kNone), matchNew(nn, kNone);
        std::vector<char> movedOld(no, 0), movedNew(nn, 0);
        auto link = [&](std::size_t i, std::size_t j) { matchOld[i] = j; matchNew[j] = i; };

        // 1. Common prefix and suffix.
        std::size_t prefix = 0;
        while (prefix < no && prefix < nn && hashOld(prefix) == hashNew(prefix)) {
            link(prefix, prefix);
            ++prefix;
        }
        std::size_t suffix = 0;
        while (suffix < no - prefix && suffix < nn - prefix &&
               hashOld(no - 1 - suffix) == hashNew(nn - 1 - suffix)) {
            link(no - 1 - suffix, nn - 1 - suffix);
            ++suffix;
        }

        // 2. LCS of what is left in the middle.
        const std::size_t ma = no - prefix - suffix;
        const std::size_t mb = nn - prefix - suffix;
        if (ma > 0 && mb > 0 && (ma + 1) * (mb + 1) > opts_.maxLcsCells) {
            // Too big for the table: Myers finds the same kind of answer in O(ND).
            std::vector<std::size_t> ha(ma), hb(mb);
            for (std::size_t i = 0; i < ma; ++i) ha[i] = hashOld(prefix + i);
            for (std::size_t j = 0; j < mb; ++j) hb[j] = hashNew(prefix + j);
            std::vector<std::pair<std::size_t, std::size_t>> matches;
            if (myersLcs(ha, hb, opts_.maxEditDistance, matches))
                for (const auto& mt : matches) link(prefix + mt.first, prefix + mt.second);
        } else if (ma > 0 && mb > 0) {
            const std::size_t w = mb + 1;
            std::vector<std::uint32_t> table((ma + 1) * w, 0);
            auto eq = [&](std::size_t i, std::size_t j) { return hashOld(prefix + i) == hashNew(prefix + j); };
            for (std::size_t i = ma; i-- > 0;) {
                for (std::size_t j = mb; j-- > 0;) {
                    table[i * w + j] = eq(i, j) ? table[(i + 1) * w + j + 1] + 1
                                                : std::max(table[(i + 1) * w + j], table[i * w + j + 1]);
                }
            }
            std::size_t i = 0, j = 0;
            while (i < ma && j < mb) {
                if (eq(i, j)) { link(prefix + i, prefix + j); ++i; ++j; }
                else if (table[(i + 1) * w + j] >= table[i * w + j + 1]) ++i;
                else ++j;
            }
        }

        // 3. Moves: an unmatched old element whose content exists among the
        //    unmatched new ones was relocated. A pair that lands on the same index
        //    stays put -- but only if the in-order matches don't cross it (the same
        //    number of them before it on both sides). Otherwise an element jumped
        //    over it, and it is reported as moved (its indices may then coincide).
        {
            std::vector<std::size_t> cntOld(no + 1, 0), cntNew(nn + 1, 0);
            for (std::size_t i = 0; i < no; ++i) cntOld[i + 1] = cntOld[i] + (matchOld[i] != kNone ? 1 : 0);
            for (std::size_t j = 0; j < nn; ++j) cntNew[j + 1] = cntNew[j] + (matchNew[j] != kNone ? 1 : 0);

            struct Pool { std::vector<std::size_t> idx; std::size_t head = 0; };
            std::unordered_map<std::size_t, Pool> pool;
            for (std::size_t j = 0; j < nn; ++j)
                if (matchNew[j] == kNone) pool[hashNew(j)].idx.push_back(j);

            for (std::size_t i = 0; i < no; ++i) {
                if (matchOld[i] != kNone) continue;
                const auto it = pool.find(hashOld(i));
                if (it == pool.end() || it->second.head >= it->second.idx.size()) continue;
                const std::size_t j = it->second.idx[it->second.head++];
                link(i, j);
                if (i != j || cntOld[i] != cntNew[i]) movedOld[i] = movedNew[j] = 1;
            }
        }

        // 4. Pair similar leftovers that sit in the same gap between in-order matches.
        if (opts_.pairSimilarInGaps) {
            std::size_t prevOld = kNone, prevNew = kNone;  // kNone acts as "-1"
            auto pairGap = [&](std::size_t oldEnd, std::size_t newEnd) {
                std::vector<std::size_t> gapOld, gapNew;
                for (std::size_t i = prevOld + 1; i < oldEnd; ++i)
                    if (matchOld[i] == kNone) gapOld.push_back(i);
                for (std::size_t j = prevNew + 1; j < newEnd; ++j)
                    if (matchNew[j] == kNone) gapNew.push_back(j);
                if (gapOld.empty() || gapNew.empty() || gapOld.size() * gapNew.size() > 1000000) return;

                std::size_t cursor = 0;
                for (std::size_t j : gapNew) {
                    for (std::size_t k = cursor; k < gapOld.size(); ++k) {
                        if (similarity(*om.nodes[gapOld[k]], *nm.nodes[j]) >= opts_.similarityThreshold) {
                            link(gapOld[k], j);
                            cursor = k + 1;
                            break;
                        }
                    }
                }
            };
            for (std::size_t i = 0; i < no; ++i) {
                if (matchOld[i] == kNone || movedOld[i]) continue;
                // An anchor. (Pairs made below are never anchors themselves, so the
                // anchor list can be walked in a single pass.)
                pairGap(i, matchOld[i]);
                prevOld = i;
                prevNew = matchOld[i];
            }
            pairGap(no, nn);
        }

        return ArrayMatch{std::move(matchOld), std::move(matchNew), std::move(movedOld), std::move(movedNew)};
    }

    void diffArray(const ASTNode& o, const ASTNode& n, std::vector<DiffNode>& out) {
        const Members om = collect(o);
        const Members nm = collect(n);
        const std::size_t no = om.nodes.size();
        const std::size_t nn = nm.nodes.size();

        const ArrayMatch match = matchArray(om, nm);
        const std::vector<std::size_t>& matchOld = match.matchOld;
        const std::vector<std::size_t>& matchNew = match.matchNew;
        const std::vector<char>& movedOld = match.movedOld;
        const std::vector<char>& movedNew = match.movedNew;

        // Removed elements go right after the nearest earlier old element that is
        // matched in order (moved elements aren't anchors: they're elsewhere).
        std::vector<std::vector<std::size_t>> removedAfter(no + 1);
        std::size_t last = kNone;
        for (std::size_t i = 0; i < no; ++i) {
            if (matchOld[i] == kNone) removedAfter[last == kNone ? 0 : last + 1].push_back(i);
            else if (!movedOld[i]) last = i;
        }
        auto emitRemoved = [&](std::size_t slot) {
            for (std::size_t i : removedAfter[slot]) out.push_back(makeRemoved(om.nodes[i], i));
        };

        emitRemoved(0);
        for (std::size_t j = 0; j < nn; ++j) {
            emitComments(nm.commentsBefore[j], out);
            const std::size_t i = matchNew[j];
            if (i == kNone) {
                out.push_back(makeAdded(nm.nodes[j], j));
            } else if (movedNew[j]) {
                DiffNode d;
                d.status = DiffStatus::Moved;
                d.oldNode = om.nodes[i];
                d.newNode = nm.nodes[j];
                d.oldIndex = i;
                d.newIndex = j;
                out.push_back(std::move(d));
            } else {
                out.push_back(diffPair(om.nodes[i], nm.nodes[j], i, j));
                emitRemoved(i + 1);
            }
        }
        emitComments(nm.commentsBefore[nn], out);
    }

    // ---- similarity of two candidates for pairing ------------------------------

    double similarity(const ASTNode& a, const ASTNode& b) const {
        if (a.type != b.type) return 0.0;

        switch (a.type) {
        case ASTNodeType::Primitive:
            return a.primitive == b.primitive ? 1.0 : 0.0;

        case ASTNodeType::Object: {
            const Members am = collect(a);
            const Members bm = collect(b);
            const std::size_t denom = std::max(am.nodes.size(), bm.nodes.size());
            if (denom == 0) return 1.0;

            std::unordered_map<std::wstring, std::vector<const ASTNode*>> byKey;
            for (const ASTNode* p : bm.nodes) byKey[p->key].push_back(p);
            std::unordered_map<std::wstring, std::size_t> used;

            double score = 0;
            for (const ASTNode* p : am.nodes) {
                const auto it = byKey.find(p->key);
                if (it == byKey.end()) continue;
                std::size_t& cursor = used[p->key];
                if (cursor >= it->second.size()) continue;
                const ASTNode* q = it->second[cursor++];
                score += p->structuralHash == q->structuralHash ? 1.0 : 0.5;
            }
            return score / static_cast<double>(denom);
        }

        case ASTNodeType::Array: {
            const Members am = collect(a);
            const Members bm = collect(b);
            const std::size_t denom = std::max(am.nodes.size(), bm.nodes.size());
            if (denom == 0) return 1.0;

            // Identical elements score 1. Leftovers on each side pair up in order and
            // score 0.5 when they are the same kind of thing (so [1] vs [2] still
            // resembles each other, and [[1]] vs [[2]] recurses).
            std::unordered_map<std::size_t, std::vector<std::size_t>> bByHash;  // hash -> unused b indices
            for (std::size_t j = 0; j < bm.nodes.size(); ++j) bByHash[bm.nodes[j]->structuralHash].push_back(j);
            std::unordered_map<std::size_t, std::size_t> head;  // per-hash cursor into bByHash

            std::vector<char> bUsed(bm.nodes.size(), 0);
            std::vector<const ASTNode*> leftoverA, leftoverB;
            double score = 0;
            for (const ASTNode* x : am.nodes) {
                const auto it = bByHash.find(x->structuralHash);
                std::size_t& h = head[x->structuralHash];
                if (it != bByHash.end() && h < it->second.size()) {
                    bUsed[it->second[h++]] = 1;
                    score += 1.0;
                } else {
                    leftoverA.push_back(x);
                }
            }
            for (std::size_t j = 0; j < bm.nodes.size(); ++j)
                if (!bUsed[j]) leftoverB.push_back(bm.nodes[j]);

            for (std::size_t k = 0; k < leftoverA.size() && k < leftoverB.size(); ++k) {
                const ASTNode& x = *leftoverA[k];
                const ASTNode& y = *leftoverB[k];
                if (x.type == y.type && (x.type != ASTNodeType::Primitive || x.primitive == y.primitive))
                    score += 0.5;
            }
            return score / static_cast<double>(denom);
        }

        default:
            return 0.0;
        }
    }

    DiffOptions opts_;
};

void countStats(const DiffNode& root, DiffStats& stats) {
    std::vector<const DiffNode*> stack{&root};
    while (!stack.empty()) {
        const DiffNode* d = stack.back();
        stack.pop_back();
        if (!d->isCommentEntry()) {
            switch (d->status) {
            case DiffStatus::Unchanged: ++stats.unchanged; break;
            case DiffStatus::Added:     ++stats.added; break;
            case DiffStatus::Removed:   ++stats.removed; break;
            case DiffStatus::Mutated:   ++stats.mutated; break;
            case DiffStatus::Moved:     ++stats.moved; break;
            }
        }
        for (const DiffNode& c : d->children) stack.push_back(&c);
    }
}

}  // namespace

DiffResult diff(const ParseResult& oldDoc, const ParseResult& newDoc, const DiffOptions& options) {
    DiffResult r;
    r.oldRoot = oldDoc.root;
    r.newRoot = newDoc.root;
    if (oldDoc.root) computeStructuralHashes(*oldDoc.root);
    if (newDoc.root) computeStructuralHashes(*newDoc.root);

    if (oldDoc.root && newDoc.root) {
        Differ differ(options);
        r.root = differ.diffPair(oldDoc.root.get(), newDoc.root.get(), 0, 0);
    } else if (newDoc.root) {
        r.root.status = DiffStatus::Added;
        r.root.newNode = newDoc.root.get();
    } else if (oldDoc.root) {
        r.root.status = DiffStatus::Removed;
        r.root.oldNode = oldDoc.root.get();
    }
    countStats(r.root, r.stats);
    return r;
}

// ---------------------------------------------------------------------------
// Debug dump
// ---------------------------------------------------------------------------
namespace {

std::wstring valueText(const ASTNode* n) {
    if (!n) return L"<missing>";
    switch (n->type) {
    case ASTNodeType::Object: return L"{...}";
    case ASTNodeType::Array:  return L"[...]";
    default:                  return n->text();
    }
}

// "key: value" for a property, the value alone otherwise.
std::wstring label(const ASTNode* n) {
    if (n->isComment()) return n->text();
    if (n->type == ASTNodeType::Property) return n->key + L": " + valueText(n->valueNode());
    return valueText(n);
}

std::wstring prefixOf(const ASTNode* n) {
    return n->type == ASTNodeType::Property ? n->key + L": " : std::wstring();
}

void dumpNode(const DiffNode& d, std::size_t depth, std::wstring& out) {
    out.append(depth * 2, L' ');
    switch (d.status) {
    case DiffStatus::Unchanged:
        out += L"= ";
        out += label(d.newNode ? d.newNode : d.oldNode);
        break;
    case DiffStatus::Added:
        out += L"+ ";
        out += label(d.newNode);
        break;
    case DiffStatus::Removed:
        out += L"- ";
        out += label(d.oldNode);
        break;
    case DiffStatus::Moved:
        out += L"> ";
        out += label(d.newNode);
        out += L" [" + std::to_wstring(d.oldIndex) + L" -> " + std::to_wstring(d.newIndex) + L"]";
        break;
    case DiffStatus::Mutated: {
        out += L"~ ";
        const ASTNode* nv = valueOf(d.newNode);
        if (!d.children.empty() && nv) {
            out += prefixOf(d.newNode) + (nv->type == ASTNodeType::Object ? L"{" : L"[");
        } else {
            out += prefixOf(d.oldNode) + valueText(valueOf(d.oldNode)) + L" -> " + valueText(nv);
        }
        break;
    }
    }
    out += L'\n';
    for (const DiffNode& c : d.children) dumpNode(c, depth + 1, out);
}

}  // namespace

std::wstring dumpDiff(const DiffResult& result) {
    std::wstring out;
    if (result.root.oldNode || result.root.newNode) dumpNode(result.root, 0, out);
    return out;
}

}  // namespace json5
}  // namespace lex
