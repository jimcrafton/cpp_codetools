#include "cpptools/delegatebindings.h"

#include <clang-c/Index.h>

#include <algorithm>
#include <functional>
#include <memory>

namespace cpptools {

namespace {

std::string toString(CXString s) {
    const char* c = clang_getCString(s);
    std::string result = c != nullptr ? c : "";
    clang_disposeString(s);
    return result;
}

std::string spellingOf(CXCursor cursor) {
    return toString(clang_getCursorSpelling(cursor));
}

// "const newui::Size &" -> "const newui::Size&": drop the spaces clang prints around & and *.
std::string normalizedType(const std::string& type) {
    std::string out;
    for (std::size_t i = 0; i < type.size(); ++i) {
        const bool nextIsPunct = i + 1 < type.size() && (type[i + 1] == '&' || type[i + 1] == '*');
        if (type[i] == ' ' && nextIsPunct) {
            continue;
        }
        out += type[i];
    }
    return out;
}

std::string canonicalSpelling(CXType type) {
    return normalizedType(toString(clang_getTypeSpelling(clang_getCanonicalType(type))));
}

// Visits every descendant of `cursor` (not `cursor` itself); the callback returns false to stop.
using Visit = std::function<bool(CXCursor)>;

struct VisitState {
    const Visit* visit;
    bool stopped = false;
};

CXChildVisitResult visitTrampoline(CXCursor cursor, CXCursor, CXClientData data) {
    auto* state = static_cast<VisitState*>(data);
    if (!(*state->visit)(cursor)) {
        state->stopped = true;
        return CXChildVisit_Break;
    }
    return CXChildVisit_Recurse;
}

void forEachDescendant(CXCursor cursor, const Visit& visit) {
    VisitState state{&visit};
    clang_visitChildren(cursor, &visitTrampoline, &state);
}

struct ChildrenState {
    std::vector<CXCursor> children;
};

CXChildVisitResult collectChildren(CXCursor cursor, CXCursor, CXClientData data) {
    static_cast<ChildrenState*>(data)->children.push_back(cursor);
    return CXChildVisit_Continue;
}

std::vector<CXCursor> childrenOf(CXCursor cursor) {
    ChildrenState state;
    clang_visitChildren(cursor, &collectChildren, &state);
    return state.children;
}

// Looks through the implicit wrappers (casts, parentheses) libclang reports as UnexposedExpr.
CXCursor unwrapped(CXCursor cursor) {
    for (;;) {
        const CXCursorKind kind = clang_getCursorKind(cursor);
        if (kind != CXCursor_UnexposedExpr && kind != CXCursor_ParenExpr) {
            return cursor;
        }
        const std::vector<CXCursor> kids = childrenOf(cursor);
        if (kids.size() != 1) {
            return cursor;
        }
        cursor = kids[0];
    }
}

bool isClassLike(CXCursor cursor) {
    const CXCursorKind kind = clang_getCursorKind(cursor);
    return kind == CXCursor_ClassDecl || kind == CXCursor_StructDecl;
}

// The controller class definition in the main file (searching through namespaces).
bool findClass(CXTranslationUnit tu, const std::string& name, CXCursor& out) {
    bool found = false;
    forEachDescendant(clang_getTranslationUnitCursor(tu), [&](CXCursor cursor) {
        if (isClassLike(cursor) && clang_isCursorDefinition(cursor) && spellingOf(cursor) == name &&
            clang_Location_isFromMainFile(clang_getCursorLocation(cursor)) != 0) {
            out = cursor;
            found = true;
            return false;
        }
        return true;
    });
    return found;
}

// The first cursor at or under `cursor` that refers to a method - `&Class::method` is a UnaryOperator
// around a DeclRefExpr to it.
bool referencedMethod(CXCursor cursor, CXCursor& method) {
    auto refersToMethod = [&](CXCursor c) {
        const CXCursor referenced = clang_getCursorReferenced(c);
        if (clang_getCursorKind(referenced) == CXCursor_CXXMethod) {
            method = referenced;
            return true;
        }
        return false;
    };
    if (refersToMethod(cursor)) {
        return true;
    }
    bool found = false;
    forEachDescendant(cursor, [&](CXCursor c) {
        found = refersToMethod(c);
        return !found;
    });
    return found;
}

bool containsKind(CXCursor cursor, CXCursorKind kind) {
    if (clang_getCursorKind(cursor) == kind) {
        return true;
    }
    bool found = false;
    forEachDescendant(cursor, [&](CXCursor c) {
        found = clang_getCursorKind(c) == kind;
        return !found;
    });
    return found;
}

// `this` -> "this"; `&presenter_` -> "presenter_"; otherwise the spelling of whatever it is.
std::string targetName(CXCursor argument) {
    if (containsKind(argument, CXCursor_CXXThisExpr) && spellingOf(unwrapped(argument)).empty()) {
        return "this";
    }
    CXCursor inner = unwrapped(argument);
    if (clang_getCursorKind(inner) == CXCursor_UnaryOperator) {
        const std::vector<CXCursor> kids = childrenOf(inner);
        if (kids.size() == 1) {
            inner = unwrapped(kids[0]);
        }
    }
    if (clang_getCursorKind(inner) == CXCursor_CXXThisExpr) {
        return "this";
    }
    return spellingOf(inner);
}

// One `<...>.add(...)` call, if it is a delegate wiring.
bool wiringFromCall(CXCursor call, DelegateWiringCall& out) {
    const int argumentCount = clang_Cursor_getNumArguments(call);
    if (argumentCount < 2) {
        return false;
    }
    const std::vector<CXCursor> kids = childrenOf(call);
    if (kids.empty()) {
        return false;
    }
    // kids[0] is the callee `<object>.add`; its child is the object the call is made on.
    const std::vector<CXCursor> calleeKids = childrenOf(unwrapped(kids[0]));
    if (calleeKids.empty()) {
        return false;
    }
    const CXCursor delegateMember = unwrapped(calleeKids[0]);
    if (clang_getCursorKind(delegateMember) != CXCursor_MemberRefExpr) {
        return false;
    }
    if (toString(clang_getTypeSpelling(clang_getCanonicalType(clang_getCursorType(delegateMember)))).find("Delegate<") ==
        std::string::npos) {
        return false;
    }

    CXCursor method;
    if (!referencedMethod(clang_Cursor_getArgument(call, static_cast<unsigned>(argumentCount - 1)), method)) {
        return false;
    }

    out.delegate = spellingOf(delegateMember);
    const std::vector<CXCursor> memberKids = childrenOf(delegateMember);
    if (!memberKids.empty()) {
        const CXCursor view = unwrapped(memberKids[0]);
        if (clang_getCursorKind(view) == CXCursor_MemberRefExpr) {
            out.viewField = spellingOf(view);
        }
    }
    out.target = targetName(clang_Cursor_getArgument(call, static_cast<unsigned>(argumentCount - 2)));
    out.methodClass = spellingOf(clang_getCursorSemanticParent(method));
    out.method = spellingOf(method);
    return true;
}

void collectWirings(CXCursor controller, std::vector<DelegateWiringCall>& wirings) {
    for (const CXCursor member : childrenOf(controller)) {
        if (clang_getCursorKind(member) != CXCursor_CXXMethod || spellingOf(member) != "internal_init" ||
            !clang_isCursorDefinition(member)) {
            continue;
        }
        forEachDescendant(member, [&](CXCursor cursor) {
            if (clang_getCursorKind(cursor) == CXCursor_CallExpr && spellingOf(cursor) == "add") {
                DelegateWiringCall wiring;
                if (wiringFromCall(cursor, wiring)) {
                    wirings.push_back(std::move(wiring));
                }
            }
            return true;
        });
    }
}

// Every method called `name` on the class or, recursively, its bases.
void collectMethods(CXCursor classCursor, const std::string& name, std::vector<CXCursor>& out, int depth = 0) {
    if (depth > 16) {
        return;
    }
    for (const CXCursor member : childrenOf(classCursor)) {
        const CXCursorKind kind = clang_getCursorKind(member);
        if (kind == CXCursor_CXXMethod && spellingOf(member) == name) {
            out.push_back(member);
        } else if (kind == CXCursor_CXXBaseSpecifier) {
            const CXCursor base = clang_getCursorReferenced(member);
            if (isClassLike(base)) {
                collectMethods(clang_getCursorDefinition(base), name, out, depth + 1);
            }
        }
    }
}

// "this@SaveDialogController.onSaveButtonClick" -> object, class, method.
bool parseDescriptor(const std::string& descriptor, std::string& object, std::string& cls, std::string& method) {
    const std::size_t at = descriptor.find('@');
    if (at == std::string::npos || at == 0) {
        return false;
    }
    const std::size_t dot = descriptor.find('.', at);
    if (dot == std::string::npos || dot == at + 1 || dot + 1 >= descriptor.size()) {
        return false;
    }
    object = descriptor.substr(0, at);
    cls = descriptor.substr(at + 1, dot - at - 1);
    method = descriptor.substr(dot + 1);
    return true;
}

// True if libclang recovered from an error in the method's signature (an unknown type turns into a
// placeholder) - its spelled types can't be trusted.
bool hasErrors(CXCursor method) {
    if (clang_isInvalidDeclaration(method) != 0) {
        return true;
    }
    const int count = clang_Cursor_getNumArguments(method);
    for (int i = 0; i < count; ++i) {
        if (clang_isInvalidDeclaration(clang_Cursor_getArgument(method, static_cast<unsigned>(i))) != 0) {
            return true;
        }
    }
    return false;
}

bool signatureMatches(CXCursor method, const RecordedBinding& binding) {
    const int parameterCount = clang_Cursor_getNumArguments(method);
    if (parameterCount < 0 || static_cast<std::size_t>(parameterCount) != 1 + binding.argumentTypes.size()) {
        return false;
    }
    const std::string returnType = canonicalSpelling(clang_getCursorResultType(method));
    const std::string syncReturn = "SyncReturn";
    if (returnType.size() < syncReturn.size() ||
        returnType.compare(returnType.size() - syncReturn.size(), syncReturn.size(), syncReturn) != 0) {
        return false;
    }
    auto parameterType = [&](unsigned index) {
        return canonicalSpelling(clang_getCursorType(clang_Cursor_getArgument(method, index)));
    };
    // A delegate hands its handler the sender by reference (Delegate::SenderRefT).
    if (parameterType(0) != normalizedType(binding.senderType) + "&") {
        return false;
    }
    for (std::size_t i = 0; i < binding.argumentTypes.size(); ++i) {
        if (parameterType(static_cast<unsigned>(i + 1)) != normalizedType(binding.argumentTypes[i])) {
            return false;
        }
    }
    return true;
}

// "SyncReturn(newui::Control&, const newui::Size&)" - a method's signature as compared.
std::string signatureOf(CXCursor method) {
    std::string result = canonicalSpelling(clang_getCursorResultType(method)) + "(";
    const int count = clang_Cursor_getNumArguments(method);
    for (int i = 0; i < count; ++i) {
        result += (i > 0 ? ", " : "") + canonicalSpelling(clang_getCursorType(clang_Cursor_getArgument(method, static_cast<unsigned>(i))));
    }
    return result + ")";
}

std::string expectedSignature(const RecordedBinding& binding) {
    std::string result = "SyncReturn(" + normalizedType(binding.senderType) + "&";
    for (const std::string& argument : binding.argumentTypes) {
        result += ", " + normalizedType(argument);
    }
    return result + ")";
}

BindingCheck checkOne(CXCursor controller, const RecordedBinding& binding,
                      const std::vector<DelegateWiringCall>& wirings) {
    std::string object, className, methodName;
    if (!parseDescriptor(binding.descriptor, object, className, methodName)) {
        return {BindingStatus::CannotVerify, "malformed descriptor '" + binding.descriptor + "'"};
    }

    // Which class the method lives on: the controller itself, or the type of one of its members.
    CXCursor targetClass = controller;
    if (object != "this") {
        bool haveField = false;
        CXCursor field = clang_getNullCursor();
        for (const CXCursor member : childrenOf(controller)) {
            if (clang_getCursorKind(member) == CXCursor_FieldDecl && spellingOf(member) == object) {
                field = member;
                haveField = true;
                break;
            }
        }
        if (!haveField) {
            return {BindingStatus::ObjectMissing, "the controller has no member '" + object + "'"};
        }
        CXType type = clang_getCursorType(field);
        if (type.kind == CXType_Pointer || type.kind == CXType_LValueReference || type.kind == CXType_RValueReference) {
            type = clang_getPointeeType(type);
        }
        const CXCursor declaration = clang_getTypeDeclaration(clang_getCanonicalType(type));
        const CXCursor definition = isClassLike(declaration) ? clang_getCursorDefinition(declaration) : clang_getNullCursor();
        if (clang_Cursor_isNull(definition) || !isClassLike(definition)) {
            return {BindingStatus::CannotVerify,
                    "the type of '" + object + "' isn't defined where the controller can see it"};
        }
        targetClass = definition;
    }

    std::vector<CXCursor> candidates;
    collectMethods(targetClass, methodName, candidates);
    if (candidates.empty()) {
        return {BindingStatus::MethodMissing, "no method '" + methodName + "' on " + spellingOf(targetClass)};
    }
    // A signature with a type the header can't resolve says nothing about what was intended - report
    // that, not a mismatch.
    candidates.erase(std::remove_if(candidates.begin(), candidates.end(), hasErrors), candidates.end());
    if (candidates.empty()) {
        return {BindingStatus::HeaderErrors,
                "'" + methodName + "' uses a type its header can't resolve - a missing #include?"};
    }
    const bool anyMatches = std::any_of(candidates.begin(), candidates.end(),
                                        [&](CXCursor method) { return signatureMatches(method, binding); });
    if (!anyMatches) {
        return {BindingStatus::SignatureMismatch, "'" + methodName + "' is " + signatureOf(candidates[0]) + " but " +
                                                      binding.delegate + " needs " + expectedSignature(binding)};
    }

    const bool wired = std::any_of(wirings.begin(), wirings.end(), [&](const DelegateWiringCall& wiring) {
        return wiring.viewField == binding.viewField && wiring.delegate == binding.delegate &&
               wiring.target == object && wiring.method == methodName;
    });
    if (!wired) {
        return {BindingStatus::NotWired, "internal_init() never connects " + binding.viewField + "->" +
                                             binding.delegate + " to " + methodName};
    }
    return {};
}

struct TranslationUnitDeleter {
    void operator()(CXTranslationUnit tu) const { clang_disposeTranslationUnit(tu); }
};
using UniqueTu = std::unique_ptr<CXTranslationUnitImpl, TranslationUnitDeleter>;

struct IndexHolder {
    CXIndex index = clang_createIndex(0, 0);
    ~IndexHolder() { clang_disposeIndex(index); }
};

struct Analysis {
    bool controllerFound = false;
    std::vector<DelegateWiringCall> wirings;
    std::vector<BindingCheck> checks;
};

Analysis analyze(const std::string& content, const std::string& controllerClass,
                 const std::vector<RecordedBinding>& bindings, const std::vector<std::string>& compileArgs) {
    Analysis result;
    const std::string fileName = "controller.h";

    IndexHolder holder;
    CXUnsavedFile unsaved;
    unsaved.Filename = fileName.c_str();
    unsaved.Contents = content.c_str();
    unsaved.Length = static_cast<unsigned long>(content.size());

    std::vector<const char*> args;
    for (const std::string& arg : compileArgs) {
        args.push_back(arg.c_str());
    }

    // KeepGoing: carry on past a missing #include. Function bodies are kept - internal_init's is what
    // is being read.
    CXTranslationUnit raw = nullptr;
    const CXErrorCode error = clang_parseTranslationUnit2(holder.index, fileName.c_str(), args.data(),
                                                          static_cast<int>(args.size()), &unsaved, 1,
                                                          CXTranslationUnit_KeepGoing, &raw);
    UniqueTu tu(error == CXError_Success ? raw : nullptr);

    CXCursor controller;
    if (tu && findClass(tu.get(), controllerClass, controller)) {
        result.controllerFound = true;
        collectWirings(controller, result.wirings);
        for (const RecordedBinding& binding : bindings) {
            result.checks.push_back(checkOne(controller, binding, result.wirings));
        }
    } else {
        result.checks.assign(bindings.size(),
                             BindingCheck{BindingStatus::ControllerMissing,
                                          "class '" + controllerClass + "' isn't defined in the controller source"});
    }
    return result;
}

} // namespace

std::vector<DelegateWiringCall> findDelegateWirings(const std::string& content, const std::string& controllerClass,
                                                    const std::vector<std::string>& compileArgs) {
    return analyze(content, controllerClass, {}, compileArgs).wirings;
}

std::vector<BindingCheck> verifyDelegateBindings(const std::string& content, const std::string& controllerClass,
                                                 const std::vector<RecordedBinding>& bindings,
                                                 const std::vector<std::string>& compileArgs) {
    return analyze(content, controllerClass, bindings, compileArgs).checks;
}

} // namespace cpptools
