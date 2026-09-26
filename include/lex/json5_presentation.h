#pragma once

// Flat presentation list for the diff view (plan, sections 4 and 6/Phase 3).
//
// Turns a DiffResult into the rows a renderer draws, with no UI dependency:
//
//   flattenUnified()   one list; a changed value shows its old line(s) directly
//                      before its new line(s)
//   flattenSplit()     two parallel columns of equal length; the shorter side of
//                      each change is padded with Spacer lines so rows line up
//
// Lines are pretty-printed from the tree, not copied from the source: keys are bare
// when they can be and quoted otherwise, values keep their spelling, every member
// line ends in a comma (trailing commas are valid JSON5, and they mean a removed or
// added line never needs a comma fix-up), and the root has no comma. `text` carries
// no indentation; indent by `depth` (plan: INDENT_WIDTH per level).
//
// Unchanged content is rendered from the NEW document on both sides.
//
// Collapsing: a container with members is collapsible. Its state lives in a ViewState
// keyed by `VisualLine::path`, which is stable across re-diffing (property keys and
// array indices), so expanded/collapsed containers survive an edit. Default: changed
// containers (Mutated / Added / Removed) are expanded; Unchanged and Moved ones are
// collapsed to "{...}" (FlattenOptions::collapseUnchanged). The root is always shown.
//
// Moves: in unified view a moved element appears once, at its new position. In split
// view it appears twice, so a renderer can link the two rows: a "ghost" at the old
// position (left column, side Old) and the element at the new position (right column,
// side New); both carry moveFrom / moveTo.
//
// The lines point into the DiffResult / its trees; keep the DiffResult alive while
// you use them. Document-level leading/trailing comments are not part of the list.

#include <cstddef>
#include <string>
#include <unordered_set>
#include <vector>

#include "json5_diff.h"

namespace lex {
namespace json5 {

// Which document a line belongs to. Unchanged content is Both; a mutated value shows
// its Old line and its New line.
enum class LineSide : std::uint8_t { Both, Old, New };

enum class LineKind : std::uint8_t {
    Open,       // "key: {" / "["  -- first line of an expanded container
    Close,      // "}," / "],"
    Leaf,       // "key: value,"; an empty container "key: {},"; or a compact container "key: [1, 2],"
    Collapsed,  // "key: { type: 'Button', ... },"  -- a container folded to one line (with a preview)
    Comment,    // one line of a comment
    Spacer,     // split view only: empty padding opposite a line that has no counterpart
};

// A span of code units inside VisualLine::text.
struct TextRange {
    std::size_t start = 0;
    std::size_t length = 0;
};

struct VisualLine {
    int depth = 0;
    std::wstring text;  // no indentation
    DiffStatus status = DiffStatus::Unchanged;
    LineSide side = LineSide::Both;
    LineKind kind = LineKind::Leaf;

    bool isCollapsible = false;  // Open / Collapsed line of a container that can be toggled
    bool isExpanded = false;     // meaningful when isCollapsible

    const ASTNode* sourceNode = nullptr;  // node this line stands for (null for spacers)
    const DiffNode* diffNode = nullptr;   // the diff entry whose block produced this line

    std::wstring path;                // identity for ViewState; shared by a container's lines
    std::size_t hiddenCount = 0;      // Collapsed: number of members hidden
    std::size_t moveFrom = kNoIndex;  // Moved: element index in the old / new array
    std::size_t moveTo = kNoIndex;

    // Mutated lines: the parts of `text` that differ from the other side's line, for
    // inline highlighting -- the changed value of "n: 1" -> "n: 20", or the changed
    // elements inside a compact "[0, 0, 640, 480]".
    std::vector<TextRange> changedRanges;

    bool isSpacer() const noexcept { return kind == LineKind::Spacer; }
};

// Which containers are expanded, by path. Overrides sit on top of the default rule.
class ViewState {
public:
    // The rule the flattener applies: explicit override, else the mode, else `defaultExpanded`.
    bool isExpanded(const std::wstring& path, bool defaultExpanded, bool isRoot) const;

    void setExpanded(const std::wstring& path, bool expanded);
    // Flip a collapsible line (uses its current isExpanded); no-op for other lines.
    void toggle(const VisualLine& line);

    void expandAll();    // every container open (overrides cleared)
    void collapseAll();  // every container closed except the root (overrides cleared)
    void reset();        // back to the default rule

private:
    enum class Mode { Auto, ExpandAll, CollapseAll };
    Mode mode_ = Mode::Auto;
    std::unordered_set<std::wstring> expanded_;
    std::unordered_set<std::wstring> collapsed_;
};

struct FlattenOptions {
    // Collapse Unchanged and Moved containers by default (focus on the changes).
    bool collapseUnchanged = true;

    // Compact containers: one made only of primitives (an array of them, or an object
    // whose values are all primitives), with no comments, prints on one line --
    //   frame: [0, 0, 640, 480],      o: { a: 1, b: 2 },
    // instead of a line per member. It can't be folded, and when both sides of a change
    // are compact the change is one old line + one new line, with `changedRanges`
    // marking the differing members. The root is never compacted. Containers with
    // more members or a longer line than these limits stay expanded.
    bool compactContainers = true;
    std::size_t compactMaxMembers = 16;
    std::size_t compactMaxWidth = 80;  // whole line: "key: [..],"

    // A collapsed container shows a preview instead of "{...}":
    //   { type: 'Button', text: 'Cancel', frame: [100, 400, 80, 30] }
    // Members listed in previewKeys come first (they identify an element), then the
    // rest in source order, until previewMaxWidth is reached, then ", ...". Nested
    // containers show as {...} / [...] unless they are compact and short. Long
    // primitives are cut to previewValueWidth.
    bool preview = true;
    std::size_t previewMaxWidth = 60;
    std::size_t previewValueWidth = 24;
    std::vector<std::wstring> previewKeys = {L"type", L"id", L"name", L"title", L"text", L"label"};
};

std::vector<VisualLine> flattenUnified(const DiffResult& diff, const ViewState& state = ViewState(),
                                       const FlattenOptions& options = {});

struct SplitView {
    std::vector<VisualLine> left;   // old document
    std::vector<VisualLine> right;  // new document; same length as `left`
    std::size_t rowCount() const noexcept { return left.size(); }
};

SplitView flattenSplit(const DiffResult& diff, const ViewState& state = ViewState(),
                       const FlattenOptions& options = {});

// Debug dumps. Each line: a 3-character marker, the indentation, the text.
//   "   " unchanged  " + " added  " - " removed  "~  " mutated container
//   "~- " / "~+ " old / new line of a mutated value  " > " moved  " < " moved-from ghost
std::wstring dumpLines(const std::vector<VisualLine>& lines);
// "left | right" per row, left padded to the widest left line.
std::wstring dumpSplit(const SplitView& view);

}  // namespace json5
}  // namespace lex
