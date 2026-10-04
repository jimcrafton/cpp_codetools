#pragma once

#include <newui/rootcontroller.h>
#include <newui/controls.h>

class Foobar1Controller : public newui::RootController {
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

    //@reflect connect=true
    newui::Button* button3_ = nullptr;

    newui::SyncReturn onButton3Click(newui::Control& sender) {
        // TODO: handle onClick
        return newui::SyncReturn::Handled;
    }

protected:
    bool internal_init() override {
        if (!Component::internal_init()) {
            return false;
        }
        button1_->onClick.add(this, &Foobar1Controller::onButton1Click);
    button3_->onClick.add(this, &Foobar1Controller::onButton3Click);
    button3_->onClick.add(this, &Foobar1Controller::onButton1Click);
    button3_->onClick.add(this, &Foobar1Controller::onButton1Click);
    button1_->onClick.add(this, &Foobar1Controller::onButton1Click);
    button1_->onClick.add(this, &Foobar1Controller::onButton3Click);
    return true;
    }
};
