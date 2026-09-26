#pragma once

// Recursive-descent JSON5 parser producing the lossless tree in json5_ast.h.
//
// Error handling: parse() never throws and always terminates. Syntax errors are
// collected in ParseResult::errors and the parser recovers (skips a stray token,
// assumes a missing comma / colon, closes containers at end of input), so an
// editor showing a half-typed document still gets a usable tree. The tree is only
// guaranteed to be *well-formed* (spans nested, children valid); it is faithful
// to the source only when errors is empty.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "json5_ast.h"

namespace lex {
namespace json5 {

struct ParseError {
    std::size_t offset = 0;
    std::size_t length = 0;
    std::uint32_t line = 0;    // 0-based
    std::uint32_t column = 0;  // 0-based, UTF-16 code units
    std::wstring message;
};

struct ParseOptions {
    std::size_t maxDepth = 256;   // container nesting limit; deeper input is an error, not a stack overflow
    std::size_t maxErrors = 100;  // further errors are still recovered from but not recorded
};

struct ParseResult {
    ASTNodePtr root;                    // null for an empty / unparsable document
    ASTChildren leadingComments;        // comment nodes before the root value
    ASTChildren trailingComments;       // comment nodes after the root value
    std::vector<ParseError> errors;

    bool ok() const noexcept { return root && errors.empty(); }
};

ParseResult parse(std::wstring_view source, const ParseOptions& options = {});

// Decode the raw text of a String token (quotes included) into its value:
// \b \f \n \r \t \v \0 \xHH \uHHHH, line continuations, and \<any> -> <any>.
// Lenient: malformed escapes decode to the escaped character; a missing closing
// quote is tolerated.
std::wstring decodeString(std::wstring_view raw);

// Decode an unquoted property name (resolves \uHHHH escapes).
std::wstring decodeIdentifier(std::wstring_view raw);

}  // namespace json5
}  // namespace lex
