#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include <newui/reflection.h>
#include <newui/view.h>

#include "ControllerRef.h"
#include "DocumentEditService.h"

namespace CodeToolsVsix
{
    // "savedialog.newui" -> "SavedialogController": the file's base name made a C++ identifier (letters,
    // digits, underscores; a leading digit gets an underscore) with its first letter capitalized, plus
    // "Controller". A starting point the user can change.
    std::string defaultControllerClassName(const std::filesystem::path& documentPath);

    // The text of a new, empty controller header: a RootController subclass that inherits its
    // constructors. Wiring adds the fields, handlers and internal_init() to it.
    std::string newControllerHeaderText(const std::string& className);

    // What creating a controller with these values would do, for showing before it happens.
    struct NewControllerCheck
    {
        bool ok = false;
        std::string message;   // "Will create X.h next to the document", "X.h already defines Y - it will be used", or why not
    };
    NewControllerCheck checkNewController(const std::filesystem::path& documentPath, const std::string& className,
                                          const std::string& header);

    // Where wireDelegate() gets the headers the controller needs for the classes its code names.
    // Defaults to IncludeManager::headersOf (each class's defining header, from reflection). Replaceable
    // so a test with a stand-in header can keep it free of real #includes; pass nullptr to restore.
    using HeaderLookup = std::function<std::vector<std::string>(const std::vector<const newui::reflection::Class*>&)>;
    void setWiringHeaderLookup(HeaderLookup lookup);

    enum class CreateControllerStatus
    {
        Created,        // the header was written
        Adopted,        // a header of that name already defines the class; nothing was written
        ExistsWithout,  // a file of that name exists but doesn't define the class; not touched
        WriteFailed,
        InvalidName,    // not a C++ identifier
        NoDocument,     // (DesignerEditor) no saved Frame document to put it next to
    };

    // Step 0: creates `header` (relative to the document's folder) holding className, unless it is
    // already there. Never overwrites a file. On Created/Adopted the caller records the controller
    // (DesignerEditor::setControllerRef).
    CreateControllerStatus createController(const std::filesystem::path& documentPath, const std::string& className,
                                            const std::string& header);

    enum class WireStatus
    {
        Ok,
        UnknownDelegate,        // the control's class has no such (reflected) delegate
        HeaderUnreadable,
        ControllerClassMissing, // the header doesn't define the controller class
        HandlerExists,          // the controller already has that handler; nothing was changed
        NoDocument,             // (DesignerEditor) no saved Frame document
        NoController,           // (DesignerEditor) the document has no controller yet - create one first
        EditFailed,             // the edit couldn't be applied (see editStatus)
    };

    struct WireResult
    {
        WireStatus status = WireStatus::Ok;
        std::string handlerName;
        std::string descriptor;     // "this@Class.handler" - what to record on the control
        EditStatus editStatus = EditStatus::Ok;
    };

    // Wires `view`'s `delegateName` event to a new handler on the controller: adds the connect field,
    // the handler (with the signature the delegate needs) and the `.add()` call to the controller's header
    // through the edit service - so an unsaved edit in an open editor is what is changed, not the disk
    // copy - and, once that succeeds, records the descriptor on `view` (addDescriptorListener) so the
    // document keeps the mapping. The editor and disk routes complete before this returns; a host may
    // complete later. handlerName empty = the default on<Control><Event>.
    void wireDelegate(DocumentEditService& service, const std::filesystem::path& documentPath,
                      const ControllerRef& controller, newui::View& view, const std::string& viewName,
                      const newui::reflection::Class& viewClass, const std::string& delegateName,
                      const std::string& handlerName, std::function<void(WireResult)> done);
}
