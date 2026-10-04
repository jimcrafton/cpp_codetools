#pragma once

#include <string>
#include <utility>
#include <vector>

#include "cpptools_codegen/delegatewiring.h"

namespace cpptools_codegen {

// One "wire this control's event to a handler on the controller" request - what the Designer's
// Delegates row knows once the user picks an event on a control.
struct ControllerWiringRequest {
    std::string className;      // the controller class in the header, e.g. "SaveDialogController"
    std::string viewName;       // the control's name in the .newui, e.g. "saveButton"
    std::string viewType;       // its C++ type, e.g. "newui::Button"
    std::string delegateName;   // the event, e.g. "onClick"
    std::string senderType;     // the delegate's sender type, e.g. "newui::Control"
    // The delegate's extra arguments after the sender (type, name), e.g. {"std::uint32_t", "flags"}.
    std::vector<std::pair<std::string, std::string>> arguments;
    // Empty = defaultHandlerName().
    std::string handlerName;
    // Wire to a handler the class already has (handlerName must name it) instead of generating one:
    // only the connect field (if missing) and the `.add()` call are planned.
    bool reuseExistingHandler = false;
};

enum class ControllerWiringStatus {
    Ok,
    ClassNotFound,   // className isn't a class/struct definition in the content
    HandlerExists,   // the class already has a member with the handler's name; nothing planned
    HandlerNotFound, // reuseExistingHandler, but the class has no member of that name; nothing planned
};

struct ControllerWiringPlan {
    ControllerWiringStatus status = ControllerWiringStatus::Ok;
    std::string handlerName;
    std::string fieldName;
    // Insertions against the content given to the planner (UTF-8 byte offsets). Two edits can share
    // an offset (the class's closing brace); applying them in the order given keeps them in order.
    std::vector<DelegateWiringEdit> edits;
};

// "on" + Capitalized(view) + Capitalized(event without its leading "on"): saveButton + onClick ->
// onSaveButtonClick (the WinForms/VCL convention, decided 2026-09-27; renamable afterwards via Rename).
std::string defaultHandlerName(const std::string& viewName, const std::string& delegateName);

// The controller's field for a view: the view's name plus a trailing underscore (the project's
// private-member convention, and what RootController's auto-binding strips to find the view).
std::string controllerFieldName(const std::string& viewName);

// Plans everything one wiring needs in the controller's source: a `//@reflect connect=true`
// field for the view (skipped if the class already has one), the handler method with the signature
// the delegate needs, and the `.add()` call in internal_init() (created if absent). The handler is
// defined inline in the class - the header-only style small controllers use here.
ControllerWiringPlan planControllerDelegateWiring(const std::string& content, const ControllerWiringRequest& request);

} // namespace cpptools_codegen
