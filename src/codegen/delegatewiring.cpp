#include "cpptools_codegen/delegatewiring.h"

#include "cpptools_codegen/classinsertion.h"
#include "cpptools_codegen/methodbuilder.h"
#include "cpptools_codegen/methodinsertion.h"

namespace cpptools_codegen {

std::optional<std::vector<DelegateWiringEdit>> planDelegateWiring(const std::string& content,
                                                                    const std::string& className,
                                                                    const std::string& handlerDeclarationText,
                                                                    const std::string& wiringCallLine) {
    const std::optional<MemberInsertion> handlerAt = memberInsertionPoint(content, className, MemberAccess::Private);
    if (!handlerAt.has_value()) {
        return std::nullopt;
    }

    std::vector<DelegateWiringEdit> edits;
    edits.push_back(DelegateWiringEdit{handlerAt->offset,
                                       handlerAt->prefix + handlerDeclarationText + handlerAt->suffix});

    const std::optional<std::size_t> initInsertionOffset =
        functionBodyInsertionPoint(content, className, "internal_init");
    if (initInsertionOffset.has_value()) {
        // internal_init() already exists - the handler is the only new member.
        edits.push_back(DelegateWiringEdit{*initInsertionOffset, wiringCallLine + "\n    "});
        return edits;
    }

    // No internal_init() override yet - generate one in the protected section.
    MethodBuilder internalInit("internal_init");
    internalInit.returnType("bool")
        .makeOverride()
        .addBodyLine("if (!Component::internal_init()) {")
        .addBodyLine("    return false;")
        .addBodyLine("}")
        .addBodyLine(wiringCallLine)
        .addBodyLine("return true;");

    const std::optional<MemberInsertion> initAt = memberInsertionPoint(content, className, MemberAccess::Protected);
    const std::string initText = initAt->prefix + internalInit.toString(1) + initAt->suffix;
    if (initAt->offset == handlerAt->offset) {
        // Same offset: one combined edit, so the caller needn't reason about same-offset order.
        edits.front().text += initText;
    } else {
        edits.push_back(DelegateWiringEdit{initAt->offset, initText});
    }
    return edits;
}

} // namespace cpptools_codegen
