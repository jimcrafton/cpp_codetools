#pragma once

#include <optional>
#include <string>

#include "cpptools_codegen/delegatewiring.h"

namespace cpptools_codegen {

// The text of a connect field: a `//@reflect connect=true` comment (see newui's reflectgen) plus
// `Type* name = nullptr;`, as two indented lines. RootController::initialize() binds it by
// name (minus a trailing underscore) to the child view of that name.
std::string connectFieldDeclaration(const std::string& type, const std::string& name, const std::string& indent = "    ");

// True if className (a class/struct definition in content) declares a member - field, method, or
// nested type - called memberName.
bool classHasMember(const std::string& content, const std::string& className, const std::string& memberName);

// Plans the edit that adds a private connect field to an existing class, just before its closing
// brace. Returns std::nullopt if className isn't a class/struct definition in content, or if it
// already declares a field called name (so applying a plan twice never duplicates it).
std::optional<DelegateWiringEdit> planConnectField(const std::string& content, const std::string& className,
                                                    const std::string& type, const std::string& name);

} // namespace cpptools_codegen
