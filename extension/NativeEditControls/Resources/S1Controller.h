#pragma once

#include <newui/rootcontroller.h>
#include <newui/controls.h>

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
        button1_->onClick.add(this, &S1Controller::onButton1Click);
    button2_->onClick.add(this, &S1Controller::onButton2ClickAgain);
    button2_->onClick.add(this, &S1Controller::onButton2ClickAgainAgain);
    return true;
    }

private:
    //@reflect connect=true
    newui::Button* button1_ = nullptr;

private:
    newui::SyncReturn onButton1Click(newui::Control& sender) {
        // TODO: handle onClick
        return newui::SyncReturn::Handled;
    }

private:
    newui::SyncReturn onButton2ClickAgain(newui::Control& sender) {
        // TODO: handle onClick
        return newui::SyncReturn::Handled;
    }

private:
    newui::SyncReturn onButton2ClickAgainAgain(newui::Control& sender) {
        // TODO: handle onClick
        return newui::SyncReturn::Handled;
    }
};
