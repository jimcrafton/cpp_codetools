#pragma once

// Deterministic structural fingerprints for the diff engine (plan, section 3).
//
// Two nodes get the same hash exactly when they mean the same thing:
//   * comments, whitespace, quote style and line/offset positions are ignored
//   * object members are an unordered map: key order does not matter
//   * array elements are ordered
//   * strings compare by decoded value ('a' == "a", '\x41' == "A")
//   * numbers compare by numeric value (1 == 1.0 == +1 == 0x1 == 1e0, 0 == -0)
//     -- values beyond double precision compare equal if their doubles are
//   * a key is compared decoded (a == 'a' == "a" == a)
//
// Hashes are 64-bit; equal hashes are treated as equal content (collision odds
// are negligible for a diff view). On a 32-bit build size_t truncates them.

#include <string_view>

#include "json5_parser.h"

namespace lex {
namespace json5 {

// Fill ASTNode::structuralHash for `root` and every node below it (comment nodes get 0).
void computeStructuralHashes(ASTNode& root);

// Numeric value of a Number token's text (sign, decimal, hex, Infinity, NaN).
// Returns false if the text isn't a well-formed number (e.g. "1e", "0x").
bool parseNumberValue(std::wstring_view text, double& out);

}  // namespace json5
}  // namespace lex
