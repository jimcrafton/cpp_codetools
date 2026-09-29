#include "cpptools_codegen/delegatewiring.h"

#include "cpptools_codegen/classinsertion.h"
#include "cpptools_codegen/methodbuilder.h"
#include "cpptools_codegen/methodinsertion.h"

namespace cpptools_codegen {

std::optional<std::vector<DelegateWiringEdit>> planDelegateWiring(const std::string& content,
                                                                    const std::string& className,
                                                                    const std::string& handlerDeclarationText,
                                                                    const std::string& wiringCallLine) {
    const std::optional<BraceRange> classBraces = classInsertionPoint(content, className);
    if (!classBraces.has_value()) {
        return std::nullopt;
    }

    std::vector<DelegateWiringEdit> edits;

    const std::optional<std::size_t> initInsertionOffset =
        functionBodyInsertionPoint(content, className, "internal_init");
    if (initInsertionOffset.has_value()) {
        // internal_init() already exists - the handler is the only new member.
        edits.push_back(DelegateWiringEdit{classBraces->closeOffset, "\nprivate:\n" + handlerDeclarationText});
        edits.push_back(DelegateWiringEdit{*initInsertionOffset, wiringCallLine + "\n    "});
    } else {
        // No internal_init() override yet - generate one alongside the handler, both landing at
        // the same class-closing-brace offset, so this is one combined edit rather than two edits
        // at the same offset (which would otherwise need the caller to reason about their order).
        MethodBuilder internalInit("internal_init");
        internalInit.returnType("bool")
            .makeOverride()
            .addBodyLine("if (!Component::internal_init()) {")
            .addBodyLine("    return false;")
            .addBodyLine("}")
            .addBodyLine(wiringCallLine)
            .addBodyLine("return true;");

        std::string combined = "\nprivate:\n" + handlerDeclarationText;
        combined += "\nprotected:\n" + internalInit.toString(1);

        edits.push_back(DelegateWiringEdit{classBraces->closeOffset, std::move(combined)});
    }

    return edits;
}

} // namespace cpptools_codegen
