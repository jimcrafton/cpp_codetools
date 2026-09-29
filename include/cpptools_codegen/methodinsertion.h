#pragma once

#include <cstddef>
#include <optional>
#include <string>

namespace cpptools_codegen {

// Where a new statement can be appended into an existing method's body - the byte offset just
// before its closing brace, UNLESS the body's last statement is a `return` (as every
// internal_init() override in this codebase ends with one - see newui::View/SubView/RootView) -
// inserting there instead, since "just before the closing brace" would otherwise land the new
// statement after the return as dead, unreachable code. Returns std::nullopt if className or
// methodName doesn't resolve to a real method definition with a body (missing entirely,
// declaration-only, or a pure-virtual with no body to append into).
std::optional<std::size_t> functionBodyInsertionPoint(const std::string& content, const std::string& className,
                                                        const std::string& methodName);

} // namespace cpptools_codegen
