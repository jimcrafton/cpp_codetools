#pragma once

#include <newui/rootcontroller.h>
#include <newui/controls.h>

class New1Controller : public newui::RootController {
public:
    using RootController::RootController;

private:
    //@reflect connect=true
    newui::Button* button1_ = nullptr;

private:
    newui::SyncReturn onButton1Click(newui::Control& sender) {
        // TODO: handle onClick
        return newui::SyncReturn::Handled;
    }

protected:
    bool internal_init() override {
        if (!Component::internal_init()) {
            return false;
        }
        button1_->onClick.add(this, &New1Controller::onButton1Click);
        return true;
    }
};
