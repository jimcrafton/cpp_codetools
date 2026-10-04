#include "cpptools_codegen/controllerwiring.h"

#include <cctype>

#include "cpptools_codegen/classinsertion.h"
#include "cpptools_codegen/fieldinsertion.h"
#include "cpptools_codegen/methodbuilder.h"

namespace cpptools_codegen {

namespace {

std::string capitalized(std::string text) {
    if (!text.empty()) {
        text[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
    }
    return text;
}

// "onClick" -> "Click"; anything not starting with "on"+Uppercase is kept whole.
std::string eventStem(const std::string& delegateName) {
    if (delegateName.size() > 2 && delegateName.compare(0, 2, "on") == 0 &&
        std::isupper(static_cast<unsigned char>(delegateName[2]))) {
        return delegateName.substr(2);
    }
    return capitalized(delegateName);
}

} // namespace

std::string defaultHandlerName(const std::string& viewName, const std::string& delegateName) {
    return "on" + capitalized(viewName) + eventStem(delegateName);
}

std::string controllerFieldName(const std::string& viewName) {
    return viewName + "_";
}

ControllerWiringPlan planControllerDelegateWiring(const std::string& content, const ControllerWiringRequest& request) {
    ControllerWiringPlan plan;
    plan.handlerName = request.handlerName.empty() ? defaultHandlerName(request.viewName, request.delegateName)
                                                   : request.handlerName;
    plan.fieldName = controllerFieldName(request.viewName);

    if (!classInsertionPoint(content, request.className).has_value()) {
        plan.status = ControllerWiringStatus::ClassNotFound;
        return plan;
    }
    const bool handlerPresent = classHasMember(content, request.className, plan.handlerName);
    if (request.reuseExistingHandler ? !handlerPresent : handlerPresent) {
        plan.status = request.reuseExistingHandler ? ControllerWiringStatus::HandlerNotFound
                                                   : ControllerWiringStatus::HandlerExists;
        return plan;
    }

    // Field first: it lands at the same offset as the handler and applies in the order given.
    if (auto field = planConnectField(content, request.className, request.viewType, plan.fieldName)) {
        plan.edits.push_back(std::move(*field));
    }

    MethodBuilder handler(plan.handlerName);
    handler.returnType("newui::SyncReturn").addArgument(request.senderType + "&", "sender");
    for (const auto& argument : request.arguments) {
        handler.addArgument(argument.first, argument.second);
    }
    handler.addBodyLine("// TODO: handle " + request.delegateName)
        .addBodyLine("return newui::SyncReturn::Handled;");

    const std::string wiringCall = plan.fieldName + "->" + request.delegateName + ".add(this, &" + request.className +
                                   "::" + plan.handlerName + ");";

    auto wiring = planDelegateWiring(content, request.className,
                                     request.reuseExistingHandler ? std::string() : handler.toString(1), wiringCall);
    if (!wiring.has_value()) {
        plan.status = ControllerWiringStatus::ClassNotFound;
        plan.edits.clear();
        return plan;
    }
    for (DelegateWiringEdit& edit : *wiring) {
        plan.edits.push_back(std::move(edit));
    }
    return plan;
}

} // namespace cpptools_codegen
