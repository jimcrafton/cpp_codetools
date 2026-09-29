#pragma once

#include <cstddef>
#include <optional>
#include <string>

namespace cpptools_codegen {

// A [openOffset, closeOffset] pair of byte offsets into the source content that was matched
// against - the position of a class/struct definition's own '{' and '}'. A new member goes
// just before closeOffset.
struct BraceRange {
    std::size_t openOffset = 0;
    std::size_t closeOffset = 0;
};

// Finds the brace range of the definition of the class/struct named className in content (an
// in-memory translation unit, not read from disk - same testability shape as
// cpptools::Parser::parseBuffer()). Returns std::nullopt if className doesn't parse as a class/
// struct *definition* in content (missing entirely, or only forward-declared).
std::optional<BraceRange> classInsertionPoint(const std::string& content, const std::string& className);

} // namespace cpptools_codegen
