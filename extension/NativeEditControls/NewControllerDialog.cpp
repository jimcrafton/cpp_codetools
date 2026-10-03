#include "NewControllerDialog.h"

#include "ControllerWiringAction.h"
#include "TextEncoding.h"

#include <newui/bundle.h>

namespace CodeToolsVsix
{
    NewControllerDialog::NewControllerDialog()
    {
        setName("newcontroller");
        newui::Bundle::instance().loadDialog(*this);
        buildChrome();
    }

    void NewControllerDialog::buildChrome()
    {
        classNameField_ = dynamic_cast<newui::TextField*>(rootView().findView("classNameInput"));
        headerField_ = dynamic_cast<newui::TextField*>(rootView().findView("headerInput"));
        hintLabel_ = dynamic_cast<newui::Label*>(rootView().findView("hintLabel"));
        createButton_ = dynamic_cast<newui::Button*>(rootView().findView("createButton"));
        cancelButton_ = dynamic_cast<newui::Button*>(rootView().findView("cancelButton"));

        if (classNameField_ != nullptr) {
            classNameField_->model().onChanged.add([this](newui::Model&) {
                if (!settingFields_) {
                    // The header follows the class name only while it still holds what the class
                    // name last suggested - once the user has typed their own, it is left alone.
                    const std::string suggestion = classNameText() + ".h";
                    if (headerText() == lastSuggestedHeader_) {
                        setFieldText(headerField_, suggestion);
                    }
                    lastSuggestedHeader_ = suggestion;
                    refreshHint();
                }
                return newui::SyncReturn::Handled;
            });
            classNameField_->onReturnPressed.add([this](newui::TextField&) {
                accept();
                return newui::SyncReturn::Handled;
            });
        }
        if (headerField_ != nullptr) {
            headerField_->model().onChanged.add([this](newui::Model&) {
                if (!settingFields_) {
                    refreshHint();
                }
                return newui::SyncReturn::Handled;
            });
            headerField_->onReturnPressed.add([this](newui::TextField&) {
                accept();
                return newui::SyncReturn::Handled;
            });
        }
        if (createButton_ != nullptr) {
            createButton_->onClick.add([this](newui::Control&) {
                accept();
                return newui::SyncReturn::Handled;
            });
        }
        if (cancelButton_ != nullptr) {
            cancelButton_->onClick.add([this](newui::Control&) {
                cancel();
                return newui::SyncReturn::Handled;
            });
        }
    }

    std::string NewControllerDialog::classNameText() const
    {
        return classNameField_ != nullptr ? wideToUtf8(classNameField_->text()) : std::string();
    }

    std::string NewControllerDialog::headerText() const
    {
        return headerField_ != nullptr ? wideToUtf8(headerField_->text()) : std::string();
    }

    void NewControllerDialog::setFieldText(newui::TextField* field, const std::string& text)
    {
        if (field == nullptr) {
            return;
        }
        const bool was = settingFields_;
        settingFields_ = true;
        field->setText(utf8ToWide(text));
        settingFields_ = was;
    }

    void NewControllerDialog::setContext(const std::filesystem::path& documentPath, const std::string& className,
                                         const std::string& header)
    {
        documentPath_ = documentPath;
        lastSuggestedHeader_ = header;
        setFieldText(classNameField_, className);
        setFieldText(headerField_, header);
        refreshHint();
    }

    void NewControllerDialog::refreshHint()
    {
        const NewControllerCheck check = checkNewController(documentPath_, classNameText(), headerText());
        canCreate_ = check.ok;
        hint_ = (check.ok ? "" : "! ") + check.message;
        if (hintLabel_ != nullptr) {
            hintLabel_->setText(hint_);
        }
        if (createButton_ != nullptr) {
            createButton_->setEnabled(canCreate_);
        }
    }

    bool NewControllerDialog::accept()
    {
        refreshHint();
        if (!canCreate_) {
            return false;
        }
        // Captured now, while the controls still exist.
        choice_ = ControllerRef{ classNameText(), headerText() };
        close(newui::DialogResult::Ok);
        return true;
    }

    void NewControllerDialog::cancel()
    {
        close(newui::DialogResult::Cancel);
    }
}
