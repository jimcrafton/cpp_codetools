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

enum class MemberAccess { Public, Protected, Private };

// Where a new member of the given access goes: at the end of the class's last section with that
// access (the implicit leading section of a class/struct counts when it holds members), and only
// when there is none, in a new section just before the closing brace. The caller inserts
// prefix + memberText + suffix at offset.
struct MemberInsertion {
    std::size_t offset = 0;
    std::string prefix;
    std::string suffix;
};

// Returns std::nullopt if className isn't a class/struct definition in content.
std::optional<MemberInsertion> memberInsertionPoint(const std::string& content, const std::string& className,
                                                      MemberAccess access);

} // namespace cpptools_codegen
