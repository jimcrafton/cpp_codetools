#pragma once

#include <string>
#include <vector>

#include <newui/reflection.h>

#include "cpptools_codegen/controllerwiring.h"

namespace CodeToolsVsix
{
    // The reflected classes the code that wires `delegateName` names - the control's own class (the
    // controller's field), the delegate's sender class (the handler's first parameter) and every
    // class-typed argument (Size, Point, ...); primitives and unreflected types are skipped; no
    // duplicates. What a controller needs #includes for (see IncludeManager::headersOf()).
    std::vector<const newui::reflection::Class*> classesNamedBy(const newui::reflection::Class& viewClass,
                                                                const std::string& delegateName);

    // Fills a cpptools_codegen::ControllerWiringRequest from reflection, so the handler generated
    // for a control's event has exactly the signature that event's newui::Delegate needs (the
    // sender and every argument spelled as C++ source writes them, const/references included -
    // Delegate::add() only accepts an exact match).
    //
    // viewClass is the control's own (most-derived) reflected class, delegateName one of its
    // delegates (own or inherited). Returns false, leaving `out` untouched, if the delegate isn't
    // found or reflection has no spelling for it (a hand-built Delegate never recorded one).
    // Parameter names come from the argument types (const newui::Size& -> size; a primitive gets
    // arg<position>) and are made unique; they are only a starting point the user can rename.
    bool buildControllerWiringRequest(const newui::reflection::Class& viewClass,
                                      const std::string& delegateName,
                                      const std::string& viewName,
                                      const std::string& controllerClass,
                                      cpptools_codegen::ControllerWiringRequest& out);
}
