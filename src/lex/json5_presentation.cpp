#include <lex/json5_presentation.h>

#include <algorithm>
#include <unordered_map>

#include <lex/lexer_base.h>

namespace lex {
namespace json5 {

// ---------------------------------------------------------------------------
// ViewState
// ---------------------------------------------------------------------------

bool ViewState::isExpanded(const std::wstring& path, bool defaultExpanded, bool isRoot) const {
    if (collapsed_.count(path)) return false;
    if (expanded_.count(path)) return true;
    if (isRoot) return true;  // the root only closes on an explicit request
    switch (mode_) {
    case Mode::ExpandAll:   return true;
    case Mode::CollapseAll: return false;
    default:                return defaultExpanded;
    }
}

void ViewState::setExpanded(const std::wstring& path, bool expanded) {
    if (expanded) { expanded_.insert(path); collapsed_.erase(path); }
    else { collapsed_.insert(path); expanded_.erase(path); }
}

void ViewState::toggle(const VisualLine& line) {
    if (line.isCollapsible) setExpanded(line.path, !line.isExpanded);
}

void ViewState::expandAll() { mode_ = Mode::ExpandAll; expanded_.clear(); collapsed_.clear(); }
void ViewState::collapseAll() { mode_ = Mode::CollapseAll; expanded_.clear(); collapsed_.clear(); }
void ViewState::reset() { mode_ = Mode::Auto; expanded_.clear(); collapsed_.clear(); }

// ---------------------------------------------------------------------------
// Text formatting
// ---------------------------------------------------------------------------
namespace {

using Block = std::vector<VisualLine>;

const ASTNode* valueOf(const ASTNode* n) {
    return n->type == ASTNodeType::Property ? n->valueNode() : n;
}

std::wstring hex(unsigned v, int digits) {
    static const wchar_t d[] = L"0123456789ABCDEF";
    std::wstring s;
    for (int i = digits - 1; i >= 0; --i) s += d[(v >> (4 * i)) & 0xF];
    return s;
}

// A key prints bare if it is a plain identifier name, else as a double-quoted string.
std::wstring formatKey(const std::wstring& key) {
    bool bare = !key.empty() && chars::isIdentStart(key[0]);
    for (wchar_t c : key) bare = bare && chars::isIdentPart(c);
    if (bare) return key;

    std::wstring out = L"\"";
    for (wchar_t c : key) {
        switch (c) {
        case L'"':  out += L"\\\""; break;
        case L'\\': out += L"\\\\"; break;
        case L'\n': out += L"\\n"; break;
        case L'\r': out += L"\\r"; break;
        case L'\t': out += L"\\t"; break;
        case L'\b': out += L"\\b"; break;
        case L'\f': out += L"\\f"; break;
        case L'\v': out += L"\\v"; break;
        case 0x2028: out += L"\\u2028"; break;
        case 0x2029: out += L"\\u2029"; break;
        default:
            if (c < 0x20) out += L"\\x" + hex(c, 2);
            else out += c;
        }
    }
    return out + L"\"";
}

// The lines of a comment. A block comment spanning lines becomes several lines,
// each without its leading whitespace.
std::vector<std::wstring> commentLines(const std::wstring& text) {
    std::vector<std::wstring> lines;
    std::wstring cur;
    auto flush = [&]() {
        std::size_t k = 0;
        if (!lines.empty()) while (k < cur.size() && chars::isHorizontalSpace(cur[k])) ++k;
        lines.push_back(cur.substr(k));
        cur.clear();
    };
    for (std::size_t i = 0; i < text.size(); ++i) {
        const wchar_t c = text[i];
        if (c == L'\r' && i + 1 < text.size() && text[i + 1] == L'\n') { flush(); ++i; }
        else if (c == L'\n' || c == L'\r' || c == 0x2028 || c == 0x2029) flush();
        else cur += c;
    }
    flush();
    return lines;
}

// ---------------------------------------------------------------------------
// Rendering an AST subtree into lines
// ---------------------------------------------------------------------------

// What every line of a rendered node is stamped with.
struct Tag {
    DiffStatus status = DiffStatus::Unchanged;
    LineSide side = LineSide::Both;
    const DiffNode* diffNode = nullptr;
    std::size_t moveFrom = kNoIndex;
    std::size_t moveTo = kNoIndex;

    // What the node's contents (its members) are stamped with: a moved container's
    // members did not themselves move.
    Tag forChildren() const {
        Tag t = *this;
        if (t.status == DiffStatus::Moved) t.status = DiffStatus::Unchanged;
        t.moveFrom = t.moveTo = kNoIndex;
        return t;
    }
};

class Flattener {
public:
    Flattener(const ViewState& state, const FlattenOptions& options, std::vector<VisualLine>* unified,
              SplitView* split)
        : state_(state), opts_(options), unified_(unified), split_(split) {}

    void run(const DiffNode& root) {
        if (!root.oldNode && !root.newNode) return;
        emitDiff(root, 0, std::wstring(), false);
    }

private:
    // ---- output -------------------------------------------------------------

    static VisualLine spacerFor(const VisualLine& like) {
        VisualLine s;
        s.kind = LineKind::Spacer;
        s.depth = like.depth;
        s.status = like.status;
        s.side = like.side == LineSide::New ? LineSide::Old : LineSide::New;
        s.diffNode = like.diffNode;
        return s;
    }

    // The same lines on both sides (unified: once).
    void same(Block&& b) {
        if (unified_) {
            for (VisualLine& l : b) unified_->push_back(std::move(l));
        } else {
            split_->left.insert(split_->left.end(), b.begin(), b.end());
            for (VisualLine& l : b) split_->right.push_back(std::move(l));
        }
    }

    // Different content per side. Unified: old lines, then new lines. Split: side by
    // side, the shorter block padded with spacers.
    void pair(Block&& l, Block&& r) {
        if (unified_) {
            for (VisualLine& x : l) unified_->push_back(std::move(x));
            for (VisualLine& x : r) unified_->push_back(std::move(x));
            return;
        }
        const std::size_t n = std::max(l.size(), r.size());
        for (std::size_t i = 0; i < n; ++i) {
            VisualLine left = i < l.size() ? std::move(l[i]) : spacerFor(r[i]);
            VisualLine right = i < r.size() ? std::move(r[i]) : spacerFor(left);
            split_->left.push_back(std::move(left));
            split_->right.push_back(std::move(right));
        }
    }

    bool splitMode() const { return split_ != nullptr; }

    // ---- lines --------------------------------------------------------------

    bool defaultExpanded(DiffStatus s) const {
        switch (s) {
        case DiffStatus::Mutated:
        case DiffStatus::Added:
        case DiffStatus::Removed: return true;
        default:                  return !opts_.collapseUnchanged;
        }
    }

    VisualLine make(LineKind kind, std::wstring text, int depth, const std::wstring& path, const Tag& tag,
                    const ASTNode* node) const {
        VisualLine l;
        l.kind = kind;
        l.text = std::move(text);
        l.depth = depth;
        l.path = path;
        l.status = tag.status;
        l.side = tag.side;
        l.diffNode = tag.diffNode;
        l.sourceNode = node;
        l.moveFrom = tag.moveFrom;
        l.moveTo = tag.moveTo;
        return l;
    }

    void commentBlock(const ASTNode& c, int depth, const Tag& tag, Block& out) const {
        for (std::wstring& t : commentLines(c.text()))
            out.push_back(make(LineKind::Comment, std::move(t), depth, std::wstring(), tag, &c));
    }

    // ---- compact containers and previews --------------------------------------

    // The one-line form of a container made only of primitives, with the range each
    // member occupies in it (indexed like the container's non-comment members).
    struct CompactLine {
        std::wstring text;  // the whole line, prefix and comma included
        std::vector<TextRange> members;
    };

    static std::wstring prefixFor(const ASTNode& node) {
        return node.type == ASTNodeType::Property ? formatKey(node.key) + L": " : std::wstring();
    }

    bool compactLine(const ASTNode& node, bool comma, CompactLine& out) const {
        const ASTNode* v = valueOf(&node);
        if (!opts_.compactContainers || !v || !v->isContainer()) return false;

        const bool isObject = v->type == ASTNodeType::Object;
        const std::wstring prefix = prefixFor(node);
        const std::wstring open = v->children().empty() ? (isObject ? L"{" : L"[") : (isObject ? L"{ " : L"[");
        const std::wstring close = v->children().empty() ? (isObject ? L"}" : L"]") : (isObject ? L" }" : L"]");

        std::wstring body;
        std::vector<TextRange> members;
        for (const ASTNodePtr& k : v->children()) {
            if (k->isComment() || members.size() >= opts_.compactMaxMembers) return false;
            std::wstring item;
            if (isObject) {
                const ASTNode* pv = k->type == ASTNodeType::Property ? k->valueNode() : nullptr;
                if (!pv || pv->type != ASTNodeType::Primitive) return false;
                item = formatKey(k->key) + L": " + pv->text();
            } else {
                if (k->type != ASTNodeType::Primitive) return false;
                item = k->text();
            }
            if (!members.empty()) body += L", ";
            TextRange r;
            r.start = prefix.size() + open.size() + body.size();
            r.length = item.size();
            members.push_back(r);
            body += item;
        }

        out.text = prefix + open + body + close + (comma ? L"," : L"");
        out.members = std::move(members);
        return out.text.size() <= opts_.compactMaxWidth;
    }

    std::wstring cut(const std::wstring& s, std::size_t width) const {
        return s.size() <= width ? s : s.substr(0, width > 3 ? width - 3 : 0) + L"...";
    }

    // A value as it appears inside a preview.
    std::wstring brief(const ASTNode& v) const {
        if (v.type == ASTNodeType::Primitive) return cut(v.text(), opts_.previewValueWidth);
        const bool isObject = v.type == ASTNodeType::Object;
        if (v.children().empty()) return isObject ? L"{}" : L"[]";
        // The compact form (no prefix, no comma: `v` is a container, not a property) when short enough.
        CompactLine cl;
        if (compactLine(v, false, cl) && cl.text.size() <= opts_.previewValueWidth) return cl.text;
        return isObject ? L"{...}" : L"[...]";
    }

    // "{ type: 'Button', text: 'Cancel', ... }" for a container node's value.
    std::wstring previewOf(const ASTNode& v) const {
        const bool isObject = v.type == ASTNodeType::Object;

        std::vector<const ASTNode*> members;
        for (const ASTNodePtr& k : v.children())
            if (!k->isComment()) members.push_back(k.get());

        // Identifying keys first (an object only), each once, then the rest in order.
        std::vector<const ASTNode*> ordered;
        if (isObject) {
            std::vector<char> taken(members.size(), 0);
            for (const std::wstring& key : opts_.previewKeys)
                for (std::size_t i = 0; i < members.size(); ++i)
                    if (!taken[i] && members[i]->key == key) { ordered.push_back(members[i]); taken[i] = 1; break; }
            for (std::size_t i = 0; i < members.size(); ++i)
                if (!taken[i]) ordered.push_back(members[i]);
        } else {
            ordered = members;
        }

        std::wstring body;
        bool truncated = false;
        for (const ASTNode* m : ordered) {
            std::wstring item;
            if (isObject) {
                const ASTNode* pv = m->valueNode();
                item = formatKey(m->key) + L": " + (pv ? brief(*pv) : std::wstring(L"<missing>"));
            } else {
                item = brief(*m);
            }
            const std::size_t needed = body.size() + (body.empty() ? 0 : 2) + item.size();
            if (!body.empty() && needed > opts_.previewMaxWidth) { truncated = true; break; }
            if (!body.empty()) body += L", ";
            body += item;
        }
        if (truncated) body += L", ...";
        return isObject ? L"{ " + body + L" }" : L"[" + body + L"]";
    }

    // Path segment for a member: the key (a repeated key gets "#n"), or "[index]".
    static std::wstring segment(const ASTNode& member, std::size_t index,
                                std::unordered_map<std::wstring, int>& seen) {
        if (member.type == ASTNodeType::Property) {
            const int n = seen[member.key]++;
            return n == 0 ? member.key : member.key + L"#" + std::to_wstring(n);
        }
        return L"[" + std::to_wstring(index) + L"]";
    }

    // Render `node` (a property, an element, or the root) and everything visible below it.
    // The recursion runs renderNode -> renderChildren -> renderNode, and those two frames
    // are kept tiny on purpose (see emitDiff): Debug builds keep every local of every
    // branch alive for the whole call, so a bulky recursive frame overflows the stack
    // on a document nested a couple of hundred levels deep.
    void renderNode(const ASTNode& node, int depth, const std::wstring& path, const Tag& tag, bool comma,
                    Block& out) const {
        if (renderSingleLine(node, depth, path, tag, comma, out)) return;
        pushEdge(node, depth, path, tag, comma, true, out);
        renderChildren(node, depth, path, tag, out);
        pushEdge(node, depth, path, tag, comma, false, out);
    }

    // Everything that fits on one line: a primitive, an empty or compact container, a
    // folded container. False if `node` is a container that must be shown expanded.
    bool renderSingleLine(const ASTNode& node, int depth, const std::wstring& path, const Tag& tag, bool comma,
                          Block& out) const {
        const ASTNode* v = valueOf(&node);
        const std::wstring prefix = prefixFor(node);
        const std::wstring tail = comma ? L"," : L"";

        if (!v) {  // a property whose value the parser couldn't recover
            out.push_back(make(LineKind::Leaf, prefix + L"<missing>" + tail, depth, path, tag, &node));
            return true;
        }
        if (v->type == ASTNodeType::Primitive) {
            out.push_back(make(LineKind::Leaf, prefix + v->text() + tail, depth, path, tag, &node));
            return true;
        }

        const bool isObject = v->type == ASTNodeType::Object;
        const std::wstring open = isObject ? L"{" : L"[";
        const std::wstring close = isObject ? L"}" : L"]";
        const ASTChildren& kids = v->children();

        if (kids.empty()) {
            out.push_back(make(LineKind::Leaf, prefix + open + close + tail, depth, path, tag, &node));
            return true;
        }

        // A container of primitives that fits prints on one line and has nothing to fold.
        if (!path.empty()) {
            CompactLine cl;
            if (compactLine(node, comma, cl)) {
                out.push_back(make(LineKind::Leaf, std::move(cl.text), depth, path, tag, &node));
                return true;
            }
        }

        if (!state_.isExpanded(path, defaultExpanded(tag.status), path.empty())) {
            std::size_t members = 0;
            for (const ASTNodePtr& k : kids) members += k->isComment() ? 0 : 1;
            const std::wstring summary = opts_.preview ? previewOf(*v) : open + L"..." + close;
            VisualLine l = make(LineKind::Collapsed, prefix + summary + tail, depth, path, tag, &node);
            l.isCollapsible = true;
            l.isExpanded = false;
            l.hiddenCount = members;
            out.push_back(std::move(l));
            return true;
        }
        return false;
    }

    // The line that opens an expanded container, or the one that closes it.
    void pushEdge(const ASTNode& node, int depth, const std::wstring& path, const Tag& tag, bool comma, bool open,
                  Block& out) const {
        const bool isObject = valueOf(&node)->type == ASTNodeType::Object;
        if (open) {
            VisualLine head = make(LineKind::Open, prefixFor(node) + (isObject ? L"{" : L"["), depth, path, tag, &node);
            head.isCollapsible = true;
            head.isExpanded = true;
            out.push_back(std::move(head));
        } else {
            out.push_back(make(LineKind::Close, std::wstring(isObject ? L"}" : L"]") + (comma ? L"," : L""), depth,
                               path, tag, &node));
        }
    }

    void renderChildren(const ASTNode& node, int depth, const std::wstring& path, const Tag& tag, Block& out) const {
        const Tag childTag = tag.forChildren();
        std::unordered_map<std::wstring, int> seen;
        std::size_t index = 0;
        for (const ASTNodePtr& k : valueOf(&node)->children()) {
            if (k->isComment()) commentBlock(*k, depth + 1, childTag, out);
            else renderNode(*k, depth + 1, path + L"/" + segment(*k, index++, seen), childTag, true, out);
        }
    }

    // ---- walking the diff ------------------------------------------------------

    // Deliberately tiny: this frame (with emitMutatedContainer's) stacks up once per
    // nesting level, so everything bulky lives in the helpers they call. Debug builds
    // keep every local of every branch alive for the whole call, and a 250-deep
    // document would otherwise overflow the stack.
    void emitDiff(const DiffNode& d, int depth, const std::wstring& path, bool comma) {
        if (d.status != DiffStatus::Mutated) emitWhole(d, depth, path, comma);
        else if (d.children.empty()) emitValueChange(d, depth, path, comma);
        else if (path.empty() || !emitCompactChange(d, depth, path, comma)) emitMutatedContainer(d, depth, path, comma);
    }

    // A node shown as a block of lines from one side (or the same on both).
    void emitWhole(const DiffNode& d, int depth, const std::wstring& path, bool comma) {
        Block b;
        switch (d.status) {
        case DiffStatus::Unchanged: {
            Tag t;
            t.status = DiffStatus::Unchanged;
            t.diffNode = &d;
            renderNode(*d.newNode, depth, path, t, comma, b);
            same(std::move(b));
            break;
        }
        case DiffStatus::Added: {
            Tag t;
            t.status = DiffStatus::Added;
            t.side = LineSide::New;
            t.diffNode = &d;
            renderNode(*d.newNode, depth, path, t, comma, b);
            pair(Block(), std::move(b));
            break;
        }
        case DiffStatus::Removed: {
            Tag t;
            t.status = DiffStatus::Removed;
            t.side = LineSide::Old;
            t.diffNode = &d;
            renderNode(*d.oldNode, depth, path, t, comma, b);
            pair(std::move(b), Block());
            break;
        }
        case DiffStatus::Moved: {  // the destination; the ghost at the old position is emitted by the parent
            Tag t;
            t.status = DiffStatus::Moved;
            t.side = LineSide::New;
            t.diffNode = &d;
            t.moveFrom = d.oldIndex;
            t.moveTo = d.newIndex;
            renderNode(*d.newNode, depth, path, t, comma, b);
            pair(Block(), std::move(b));
            break;
        }
        case DiffStatus::Mutated:  // handled by emitDiff
            break;
        }
    }

    static Tag sideTag(const DiffNode& d, LineSide side) {
        Tag t;
        t.status = DiffStatus::Mutated;
        t.side = side;
        t.diffNode = &d;
        return t;
    }

    // A value changed, or changed type: the old rendering, then the new one.
    void emitValueChange(const DiffNode& d, int depth, const std::wstring& path, bool comma) {
        Block l, r;
        renderNode(*d.oldNode, depth, path, sideTag(d, LineSide::Old), comma, l);
        renderNode(*d.newNode, depth, path, sideTag(d, LineSide::New), comma, r);

        // Two primitives on one line each: mark the changed value.
        const ASTNode* ov = valueOf(d.oldNode);
        const ASTNode* nv = valueOf(d.newNode);
        if (l.size() == 1 && r.size() == 1 && ov && nv && ov->type == ASTNodeType::Primitive &&
            nv->type == ASTNodeType::Primitive) {
            const std::size_t start = prefixFor(*d.oldNode).size();
            l[0].changedRanges.push_back({start, ov->text().size()});
            r[0].changedRanges.push_back({start, nv->text().size()});
        }
        pair(std::move(l), std::move(r));
    }

    // Both sides are one-line containers: show them as an old line and a new line, with
    // the members that differ marked, instead of a row per member. False if either isn't compact.
    bool emitCompactChange(const DiffNode& d, int depth, const std::wstring& path, bool comma) {
        CompactLine oldLine, newLine;
        if (!compactLine(*d.oldNode, comma, oldLine) || !compactLine(*d.newNode, comma, newLine)) return false;

        Block l, r;
        l.push_back(make(LineKind::Leaf, std::move(oldLine.text), depth, path, sideTag(d, LineSide::Old), d.oldNode));
        r.push_back(make(LineKind::Leaf, std::move(newLine.text), depth, path, sideTag(d, LineSide::New), d.newNode));

        for (const DiffNode& c : d.children) {
            if (c.isCommentEntry()) continue;
            const bool inOld = c.status == DiffStatus::Removed || c.status == DiffStatus::Mutated ||
                               c.status == DiffStatus::Moved;
            const bool inNew = c.status == DiffStatus::Added || c.status == DiffStatus::Mutated ||
                               c.status == DiffStatus::Moved;
            if (inOld && c.oldIndex < oldLine.members.size()) l[0].changedRanges.push_back(oldLine.members[c.oldIndex]);
            if (inNew && c.newIndex < newLine.members.size()) r[0].changedRanges.push_back(newLine.members[c.newIndex]);
        }
        pair(std::move(l), std::move(r));
        return true;
    }

    // ---- a container whose members changed --------------------------------------------
    // (The helpers below keep emitMutatedContainer's own frame small: see emitDiff.)

    // The user collapsed it: renderNode notices and emits one line.
    void emitCollapsedMutated(const DiffNode& d, int depth, const std::wstring& path, bool comma) {
        Block b;
        renderNode(*d.newNode, depth, path, sideTag(d, LineSide::Both), comma, b);
        same(std::move(b));
    }

    // The "key: {" line that opens it, or the "}," line that closes it.
    void emitContainerEdge(const DiffNode& d, int depth, const std::wstring& path, bool comma, bool open) {
        const bool isObject = valueOf(d.newNode)->type == ASTNodeType::Object;
        const Tag tag = sideTag(d, LineSide::Both);
        Block b;
        if (open) {
            VisualLine head = make(LineKind::Open, prefixFor(*d.newNode) + (isObject ? L"{" : L"["), depth, path, tag,
                                   d.newNode);
            head.isCollapsible = true;
            head.isExpanded = true;
            b.push_back(std::move(head));
        } else {
            b.push_back(make(LineKind::Close, std::wstring(isObject ? L"}" : L"]") + (comma ? L"," : L""), depth,
                             path, tag, d.newNode));
        }
        same(std::move(b));
    }

    // Split view: a moved element also shows at its OLD position (a ghost in the left
    // column), so that column reads in the old document's order. The ghost goes right
    // after the nearest preceding old element that stays in order. Result: for each slot
    // (0 = before the first child, k = after child k-1), the ghosts to emit there.
    std::vector<std::vector<const DiffNode*>> collectGhosts(const DiffNode& d) const {
        std::vector<std::vector<const DiffNode*>> ghostsAfter(d.children.size() + 1);
        if (!splitMode()) return ghostsAfter;

        std::vector<std::pair<std::size_t, std::size_t>> inOrder;  // (oldIndex, child position)
        for (std::size_t ci = 0; ci < d.children.size(); ++ci) {
            const DiffNode& c = d.children[ci];
            if (!c.isCommentEntry() && c.status != DiffStatus::Moved && c.oldIndex != kNoIndex)
                inOrder.emplace_back(c.oldIndex, ci);
        }
        std::sort(inOrder.begin(), inOrder.end());
        for (const DiffNode& c : d.children) {
            if (c.status != DiffStatus::Moved) continue;
            const auto it = std::lower_bound(inOrder.begin(), inOrder.end(), std::make_pair(c.oldIndex, std::size_t(0)));
            const std::size_t slot = it == inOrder.begin() ? 0 : (it - 1)->second + 1;
            ghostsAfter[slot].push_back(&c);
        }
        for (auto& g : ghostsAfter)
            std::sort(g.begin(), g.end(), [](const DiffNode* a, const DiffNode* b) { return a->oldIndex < b->oldIndex; });
        return ghostsAfter;
    }

    void emitGhostsAt(const std::vector<const DiffNode*>& ghosts, int depth, const std::wstring& path) {
        for (const DiffNode* g : ghosts) {
            Tag t;
            t.status = DiffStatus::Moved;
            t.side = LineSide::Old;
            t.diffNode = g;
            t.moveFrom = g->oldIndex;
            t.moveTo = g->newIndex;
            Block b;
            renderNode(*g->oldNode, depth + 1, path + L"/-[" + std::to_wstring(g->oldIndex) + L"]", t, true, b);
            pair(std::move(b), Block());
        }
    }

    // A comment from the new side, interleaved among the children.
    void emitCommentEntry(const DiffNode& c, int depth) {
        Tag t;
        t.status = DiffStatus::Unchanged;
        t.diffNode = &c;
        Block b;
        commentBlock(*c.newNode, depth, t, b);
        same(std::move(b));
    }

    // Path of a child entry: its parent's path plus a segment (removed ones are marked).
    static std::wstring childPath(const DiffNode& c, const std::wstring& path, std::unordered_map<std::wstring, int>& seenNew,
                                  std::unordered_map<std::wstring, int>& seenOld) {
        if (c.status == DiffStatus::Removed) return path + L"/-" + segment(*c.oldNode, c.oldIndex, seenOld);
        return path + L"/" + segment(*c.newNode, c.newIndex, seenNew);
    }

    void emitMutatedContainer(const DiffNode& d, int depth, const std::wstring& path, bool comma) {
        if (!state_.isExpanded(path, true, path.empty())) {
            emitCollapsedMutated(d, depth, path, comma);
            return;
        }
        emitContainerEdge(d, depth, path, comma, true);

        const std::vector<std::vector<const DiffNode*>> ghostsAfter = collectGhosts(d);
        std::unordered_map<std::wstring, int> seenNew, seenOld;
        emitGhostsAt(ghostsAfter[0], depth, path);
        for (std::size_t ci = 0; ci < d.children.size(); ++ci) {
            const DiffNode& c = d.children[ci];
            if (c.isCommentEntry()) emitCommentEntry(c, depth + 1);
            else emitDiff(c, depth + 1, childPath(c, path, seenNew, seenOld), true);
            emitGhostsAt(ghostsAfter[ci + 1], depth, path);
        }

        emitContainerEdge(d, depth, path, comma, false);
    }

    const ViewState& state_;
    const FlattenOptions& opts_;
    std::vector<VisualLine>* unified_;
    SplitView* split_;
};

}  // namespace

std::vector<VisualLine> flattenUnified(const DiffResult& diff, const ViewState& state,
                                       const FlattenOptions& options) {
    std::vector<VisualLine> lines;
    Flattener(state, options, &lines, nullptr).run(diff.root);
    return lines;
}

SplitView flattenSplit(const DiffResult& diff, const ViewState& state, const FlattenOptions& options) {
    SplitView view;
    Flattener(state, options, nullptr, &view).run(diff.root);
    return view;
}

// ---------------------------------------------------------------------------
// Dumps
// ---------------------------------------------------------------------------
namespace {

const wchar_t* marker(const VisualLine& l) {
    if (l.isSpacer()) return L"   ";
    switch (l.status) {
    case DiffStatus::Unchanged: return L"   ";
    case DiffStatus::Added:     return L" + ";
    case DiffStatus::Removed:   return L" - ";
    case DiffStatus::Mutated:
        return l.side == LineSide::Old ? L"~- " : l.side == LineSide::New ? L"~+ " : L"~  ";
    case DiffStatus::Moved:     return l.side == LineSide::Old ? L" < " : L" > ";
    }
    return L"   ";
}

std::wstring render(const VisualLine& l) {
    std::wstring s = marker(l);
    if (!l.isSpacer()) {
        s.append(static_cast<std::size_t>(l.depth) * 2, L' ');
        s += l.text;
    }
    return s;
}

}  // namespace

std::wstring dumpLines(const std::vector<VisualLine>& lines) {
    std::wstring out;
    for (const VisualLine& l : lines) out += render(l) + L"\n";
    return out;
}

std::wstring dumpSplit(const SplitView& view) {
    std::size_t width = 0;
    for (const VisualLine& l : view.left) width = std::max(width, render(l).size());

    std::wstring out;
    for (std::size_t i = 0; i < view.left.size(); ++i) {
        std::wstring left = render(view.left[i]);
        left.resize(width, L' ');
        std::wstring row = left + L" | " + render(view.right[i]);
        while (!row.empty() && row.back() == L' ') row.pop_back();
        out += row + L"\n";
    }
    return out;
}

}  // namespace json5
}  // namespace lex
