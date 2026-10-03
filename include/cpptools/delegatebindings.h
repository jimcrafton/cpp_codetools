#pragma once

#include <string>
#include <vector>

namespace cpptools {

// One `<view>_-><event>.add(<target>, &Class::method)` call found in a controller's internal_init().
struct DelegateWiringCall {
    std::string viewField;    // the controller member the delegate is reached through ("saveButton_"), or ""
    std::string delegate;     // the event ("onClick")
    std::string target;       // "this", the target member's name ("presenter_"), or the expression's text
    std::string methodClass;  // the class the method belongs to
    std::string method;       // "onSaveButtonClick"
};

// Every delegate `.add(target, &Class::method)` (with or without a leading descriptor argument) in
// controllerClass's internal_init() bodies defined in content. Read-only analysis through libclang
// (like the rest of cpptools - fast, and independent of the LibTooling code generators), so give it
// the include paths a real build uses (compileArgs, e.g. -std=c++17 -xc++ -I...) or `.add` won't
// resolve. Empty if the class isn't found or nothing is wired.
std::vector<DelegateWiringCall> findDelegateWirings(const std::string& content, const std::string& controllerClass,
                                                    const std::vector<std::string>& compileArgs);

// A delegate->method mapping as the .newui records it (descriptor "<object>@<Class>.<method>", where
// <object> is "this" for the controller itself or one of its members), plus the signature the
// event's handler must have - from reflection (Delegate::senderSpelling()/Argument::spelling).
struct RecordedBinding {
    std::string viewField;                   // the controller member holding the control ("saveButton_")
    std::string delegate;                    // the event ("onClick")
    std::string descriptor;                  // "this@SaveDialogController.onSaveButtonClick"
    std::string senderType;                  // "newui::Control" - the plain type; the handler takes it by reference
    std::vector<std::string> argumentTypes;  // after the sender, e.g. {"const newui::Size&"}
};

enum class BindingStatus {
    Ok,
    ControllerMissing,  // the controller class isn't in the source
    ObjectMissing,      // the descriptor's <object> isn't a member of the controller
    MethodMissing,      // the target's class has no method of that name
    HeaderErrors,       // the method's signature uses a type the header can't resolve (a missing #include?)
    SignatureMismatch,  // it exists but doesn't take what the event passes / doesn't return SyncReturn
    NotWired,           // the method is fine but no `.add()` in internal_init connects it
    CannotVerify,       // e.g. a member whose type isn't defined here, or a malformed descriptor
};

struct BindingCheck {
    BindingStatus status = BindingStatus::Ok;
    std::string detail;  // short human-readable reason, empty for Ok
};

// Checks each recorded binding against the controller source; result[i] is for bindings[i].
// Same compileArgs advice as findDelegateWirings().
std::vector<BindingCheck> verifyDelegateBindings(const std::string& content, const std::string& controllerClass,
                                                 const std::vector<RecordedBinding>& bindings,
                                                 const std::vector<std::string>& compileArgs);

} // namespace cpptools
