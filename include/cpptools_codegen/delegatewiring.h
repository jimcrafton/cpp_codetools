#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace cpptools_codegen {

// One text insertion, at an offset into the *original* content planDelegateWiring() was given -
// not against whatever a previous edit in the same plan already changed. Apply edits in
// descending offset order if splicing them into the same growing buffer, same multi-edit
// discipline Rename's own multi-occurrence apply already uses.
struct DelegateWiringEdit {
    std::size_t offset = 0;
    std::string text;
};

// Plans the edits needed to add a new event handler to an existing class and wire it up - the
// DE-01/CCE-06 "generate a handler, then wire it" flow bluesky/cpp-codegen-plan.md describes,
// combining classInsertionPoint() (where a new member goes), methodInsertionPoint() (where an
// existing internal_init() override's body ends), and a caller-built handler declaration (a
// MethodBuilder::toString(), typically - this function doesn't build one itself, since the
// caller already has to resolve the delegate's own signature via reflection to build it).
//
// - handlerDeclarationText: the complete handler method text (e.g. a MethodBuilder::toString()
//   result), inserted at the end of className's private section (a new section only if it has none).
//   Empty = the handler already exists: only the wiring call is planned.
// - wiringCallLine: one statement (e.g. "saveButton_->onClick.add(this,
//   &MyDialog::onSaveButtonClicked);"), inserted into className's internal_init() override - if
//   className already has one, appended just before its closing brace; if not, a new protected
//   internal_init() override is generated (chaining to Component::internal_init() first, matching
//   every other override in this codebase) and inserted alongside the handler declaration.
//
// Returns std::nullopt if className isn't a real class/struct definition in content - the only
// hard prerequisite (an existing internal_init() override is optional, not required).
std::optional<std::vector<DelegateWiringEdit>> planDelegateWiring(const std::string& content,
                                                                    const std::string& className,
                                                                    const std::string& handlerDeclarationText,
                                                                    const std::string& wiringCallLine);

} // namespace cpptools_codegen
