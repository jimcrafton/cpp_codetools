#pragma once

#include <newui/rootcontroller.h>

class S1Controller : public newui::RootController {
public:
    using RootController::RootController;

private:
    //@reflect connect=true
    newui::Button* button2_ = nullptr;

private:
    newui::SyncReturn onButton2Click(newui::Control& sender) {
        // TODO: handle onClick
        return newui::SyncReturn::Handled;
    }

protected:
    bool internal_init() override {
        if (!Component::internal_init()) {
            return false;
        }
        button2_->onClick.add(this, &S1Controller::onButton2Click);
        return true;
    }
};
