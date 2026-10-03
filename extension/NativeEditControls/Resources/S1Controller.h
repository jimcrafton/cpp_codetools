#pragma once

#include <newui/rootcontroller.h>
#include <newui/controls.h>

#include <bojangle>
#include <newui/view.h>
#include <newui/geometry.h>

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

    button2_->onClick.add(this, &S1Controller::onButton2Click2);
    button2_->onSizeChanged.add(this, &S1Controller::onButton2SizeChanged);
    button2_->onCheckedChanged.add(this, &S1Controller::onButton2CheckedChanged);
    button2_->onStateChanged.add(this, &S1Controller::onButton2StateChanged);
    return true;
    }

private:
    //@reflect connect=true
    newui::Button* button1_ = nullptr;





private:
    newui::SyncReturn onButton2Click(newui::Control& sender) {
        // TODO: handle onClick
        return newui::SyncReturn::Handled;
    }

private:
    newui::SyncReturn onButton2Click2(newui::Control& sender) {
        // TODO: handle onClick
        return newui::SyncReturn::Handled;
    }

private:
    newui::SyncReturn onButton2SizeChanged(newui::View& sender, const newui::Size& size) {
        // TODO: handle onSizeChanged
        return newui::SyncReturn::Handled;
    }

private:
    newui::SyncReturn onButton2CheckedChanged(newui::Button& sender) {
        // TODO: handle onCheckedChanged
        return newui::SyncReturn::Handled;
    }

private:
    newui::SyncReturn onButton2StateChanged(newui::Control& sender) {
        // TODO: handle onStateChanged
        return newui::SyncReturn::Handled;
    }
};
