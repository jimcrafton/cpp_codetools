#pragma once

#include "ControllerRef.h"

#include <newui/controls.h>
#include <newui/dialogs.h>

#include <filesystem>
#include <optional>
#include <string>

namespace CodeToolsVsix
{
    // Step 0 of wiring a Designer event to C++: which class, in which header, holds this document's
    // event handlers. The chrome is Resources/newcontroller.newui (loaded by Bundle::loadDialog - the
    // .newui file is the design); this class finds its named nodes and wires behavior onto them, the
    // way ColorEditorDialog does.
    //
    // Live: every edit re-checks the choice (checkNewController()) and shows what Create would do - or
    // why it can't - in the hint, and Create is disabled until it can. While the header still holds
    // the name the class name suggested, it follows the class name as it is typed.
    //
    // The chosen values are captured into plain members when Create is pressed: the dialog's controls
    // are freed before showModal() returns (see ColorEditorDialog::color()'s comment), so choice() never
    // reads a control.
    class NewControllerDialog : public newui::Dialog
    {
    public:
        NewControllerDialog();

        // Seeds both fields with a suggestion and remembers the document (its folder is what the header
        // is relative to). Call before showModal().
        void setContext(const std::filesystem::path& documentPath, const std::string& className, const std::string& header);

        // What Create does: false (dialog stays open, hint says why) unless the current choice is
        // usable; otherwise captures it and closes with Ok.
        bool accept();
        // What Cancel does.
        void cancel();

        // The controller chosen - empty unless accept() succeeded.
        const std::optional<ControllerRef>& choice() const { return choice_; }
        // The hint as plain text (what the label shows) and whether Create would work.
        const std::string& hint() const { return hint_; }
        bool canCreate() const { return canCreate_; }

        // The named controls, exposed for tests - null if the resource lacked one.
        newui::TextField* classNameField() const { return classNameField_; }
        newui::TextField* headerField() const { return headerField_; }
        newui::Label* hintLabel() const { return hintLabel_; }
        newui::Label* folderLabel() const { return folderLabel_; }
        newui::Button* createButton() const { return createButton_; }
        newui::Button* cancelButton() const { return cancelButton_; }

    private:
        void buildChrome();
        void refreshHint();
        std::string classNameText() const;
        std::string headerText() const;
        void setFieldText(newui::TextField* field, const std::string& text);

        std::filesystem::path documentPath_;
        std::string lastSuggestedHeader_;   // the header the class name last suggested
        bool settingFields_ = false;        // a programmatic change, not the user typing
        std::string hint_;
        bool canCreate_ = false;
        std::optional<ControllerRef> choice_;

        newui::TextField* classNameField_ = nullptr;
        newui::TextField* headerField_ = nullptr;
        newui::Label* hintLabel_ = nullptr;
        newui::Label* folderLabel_ = nullptr;
        newui::Button* createButton_ = nullptr;
        newui::Button* cancelButton_ = nullptr;
    };
}
