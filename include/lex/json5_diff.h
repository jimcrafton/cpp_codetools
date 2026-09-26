#pragma once

// Semantic tree diff of two JSON5 documents (plan, sections 3 and 6/Phase 2).
//
// The result is a tree of DiffNodes that mirrors the documents wherever they
// differ and stops where they don't:
//
//   Unchanged   both sides have the node and it means the same thing (equal
//               structural hash). Not expanded: render it from newNode.
//   Added       only in the new document. Render the newNode subtree.
//   Removed     only in the old document. Render the oldNode subtree.
//   Mutated     both sides have a node in the same slot but the content differs.
//               If both values are containers of the same type, `children` holds
//               the diff of their members; otherwise (a primitive changed, or a
//               value changed type) it is a leaf: show old -> new.
//   Moved       an array element with identical content at a different position;
//               oldIndex / newIndex give the shift for the "[ 4 -> 1 ]" badge.
//
// What a "slot" is: for an object, a property (paired by key, so key order is
// never a change); for an array, an element. A Property DiffNode's oldNode /
// newNode are the Property nodes, and its `children` (when Mutated) describe the
// members of the property's object / array value.
//
// Arrays: common prefix/suffix are trimmed, an LCS over the element hashes finds
// the in-order matches, unmatched elements whose content exists elsewhere become
// Moved, and (optionally) leftovers in the same gap that resemble each other are
// paired as Mutated so a changed child shows a nested diff instead of a full
// remove + add.
//
// Comments are NOT part of the comparison (they don't affect hashes): a comment-only
// change is Unchanged. Inside a Mutated container the NEW side's comments are
// interleaved among the children as Unchanged entries whose newNode is a comment,
// so a renderer can show them in place.
//
// Order of `children`: the new document's order (properties in new source order,
// array elements in new order), with Removed entries placed right after the entry
// that preceded them in the old document.
//
// The DiffNodes point into the two documents' trees; DiffResult keeps both roots
// alive, so the result stays valid after the ParseResults are gone.

#include <cstddef>
#include <string>
#include <vector>

#include "json5_ast.h"
#include "json5_parser.h"

namespace lex {
namespace json5 {

enum class DiffStatus { Unchanged, Added, Removed, Mutated, Moved };

inline constexpr std::size_t kNoIndex = static_cast<std::size_t>(-1);

struct DiffNode {
    DiffStatus status = DiffStatus::Unchanged;
    const ASTNode* oldNode = nullptr;  // null when Added
    const ASTNode* newNode = nullptr;  // null when Removed
    // Position among the non-comment members of the parent container (kNoIndex if
    // the node is absent on that side, or for the root).
    std::size_t oldIndex = kNoIndex;
    std::size_t newIndex = kNoIndex;
    std::vector<DiffNode> children;

    bool isCommentEntry() const noexcept { return newNode && newNode->isComment(); }
};

struct DiffOptions {
    // Array LCS of the part between the common prefix and suffix uses a
    // (n+1)*(m+1) table of 32-bit counts. Above this many cells it switches to
    // Myers' O(ND) algorithm, whose cost depends on the number of differences, not
    // the array size: a 20,000-element array with a few edits is instant.
    std::size_t maxLcsCells = 4000000;

    // Myers gives up beyond this many insertions + deletions (its memory grows
    // with the square of it). Then no LCS is used at all: only prefix/suffix and
    // move detection apply. Still correct, just less minimal.
    std::size_t maxEditDistance = 2000;

    // Pair similar unmatched elements of an array gap as Mutated (nested diff).
    bool pairSimilarInGaps = true;

    // Minimum similarity (0..1) for such a pairing. Objects score the share of
    // identical properties (same key and value = 1, same key with a new value = 0.5);
    // arrays the share of identical elements; primitives of the same kind always pair.
    double similarityThreshold = 0.5;
};

// Counts of DiffNodes by status (comment entries excluded). An Unchanged / Added /
// Removed subtree counts once, not per AST node inside it.
struct DiffStats {
    std::size_t unchanged = 0;
    std::size_t added = 0;
    std::size_t removed = 0;
    std::size_t mutated = 0;
    std::size_t moved = 0;
};

struct DiffResult {
    DiffNode root;
    ASTNodePtr oldRoot;  // keep the trees the nodes point into alive
    ASTNodePtr newRoot;
    DiffStats stats;

    bool identical() const noexcept { return root.status == DiffStatus::Unchanged; }
};

// Diffs two parsed documents. Fills ASTNode::structuralHash on both trees.
// Documents with parse errors are diffed on whatever tree the parser recovered.
DiffResult diff(const ParseResult& oldDoc, const ParseResult& newDoc, const DiffOptions& options = {});

// Human-readable dump, one entry per line, for debugging and tests:
//   "= a: 1"   unchanged     "+ d: 4"   added     "- c: 3"   removed
//   "~ b: 2 -> 20" mutated leaf, "~ v: {" mutated container (children indented)
//   "> x [3 -> 0]" moved     "= // text" a comment from the new side
std::wstring dumpDiff(const DiffResult& result);

}  // namespace json5
}  // namespace lex
