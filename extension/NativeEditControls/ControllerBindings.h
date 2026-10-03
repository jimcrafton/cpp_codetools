#pragma once

#include <string>
#include <vector>

#include <cpptools/delegatebindings.h>
#include <newui/view.h>

namespace CodeToolsVsix
{
    // One delegate->handler mapping recorded on a control in the design tree, ready to verify.
    struct DesignerBinding
    {
        std::string viewName;    // the control's name in the document ("saveButton")
        cpptools::RecordedBinding binding;   // viewField is viewName + "_", the controller's member for it
    };

    // The result for one of them.
    struct VerifiedBinding
    {
        std::string viewName;
        std::string delegate;
        std::string descriptor;
        cpptools::BindingCheck check;
    };

    // Every `<object>@<Class>.<method>` listener recorded on a named control under `root`, with the
    // signature its event's handler must have (from reflection). The design-time load keeps a
    // document's "delegates" blocks as descriptor-only listeners, which is what this reads back.
    // Controls without a name (no controller field can refer to them) and descriptors that aren't
    // of that form (a bare function name) are skipped.
    std::vector<DesignerBinding> collectRecordedBindings(newui::View& root);

    // Checks each against the controller's source (headerText, UTF-8) with libclang - a second or two
    // on real headers, so not on the UI thread. compileArgs: what cpptools::compileFlagsFor() gives the
    // header. result[i] is for bindings[i].
    std::vector<VerifiedBinding> verifyBindings(const std::vector<DesignerBinding>& bindings,
                                                const std::string& controllerClass,
                                                const std::string& headerText,
                                                const std::vector<std::string>& compileArgs);

    // The same result when the header couldn't be read at all.
    std::vector<VerifiedBinding> unverifiableBindings(const std::vector<DesignerBinding>& bindings,
                                                      const std::string& reason);
}
