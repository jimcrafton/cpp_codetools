#pragma once

#include <newui/rootcontroller.h>
#include <newui/controls.h>

class New2Controller : public newui::RootController {
public:
    using RootController::RootController;

private:
    //@reflect connect=true
    newui::Label* label1_ = nullptr;

private:
    newui::SyncReturn onLabel1Click(newui::Control& sender) {
        // TODO: handle onClick
        return newui::SyncReturn::Handled;
    }

protected:
    bool internal_init() override {
        if (!Component::internal_init()) {
            return false;
        }
        label1_->onClick.add(this, &New2Controller::onLabel1Click);
        return true;
    }
};
