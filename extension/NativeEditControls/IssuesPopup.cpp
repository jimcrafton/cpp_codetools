#include "IssuesPopup.h"
#include "CalloutPlacement.h"
#include "PickerRow.h"

#include <newui/application.h>
#include <newui/controls.h>
#include <newui/models.h>
#include <newui/rootview.h>
#include <newui/runloop.h>

#include <memory>

namespace CodeToolsVsix
{
    newui::PopupTool* IssuesPopup::show(newui::View& owner, const newui::Rect& anchorScreenRect,
                                        const std::vector<std::string>& rows,
                                        std::function<void(std::size_t)> onChosen)
    {
        newui::RootView* rootView = owner.rootView();
        if (rootView == nullptr || rootView->windowHandle() == nullptr || rows.empty()) {
            return nullptr;
        }

        const std::size_t visibleRows = rows.size() < kMaxVisibleRows ? rows.size() : kMaxVisibleRows;
        const float listHeight = kRowHeight * static_cast<float>(visibleRows);
        const float height = PickerRow::kTopReserve + kPadding * 2.0f + listHeight;
        const CalloutPlacement placement =
            placeCallout(anchorScreenRect, newui::Size(kWidth, height), designerWindowScreenRect(&owner));

        auto* popup = new newui::CalloutTool(rootView->windowHandle(), newui::Application::instance().instanceHandle(),
                                             placement.bounds, "issuesPopup");
        if (!popup->initialize()) {
            delete popup;
            return nullptr;
        }
        popup->setTailSide(placement.tailSide);
        popup->setTailPosition(placement.tailPosition);

        auto model = std::make_unique<newui::StringListModel>();
        for (const std::string& row : rows) {
            model->addItem(row);
        }
        auto* list = new newui::ListView();
        list->setVisible(true);
        list->setModel(std::move(model));
        list->setRowHeight(kRowHeight);

        auto chosen = std::make_shared<std::function<void(std::size_t)>>(std::move(onChosen));
        list->onSelectionChanged.add([popup, chosen](newui::ListView& sender) {
            const std::optional<std::size_t> selected = sender.selectedIndex();
            if (!selected.has_value()) {
                return newui::SyncReturn::Ignored;
            }
            // Locals first: dismiss() may free the popup, and this closure with it.
            std::shared_ptr<std::function<void(std::size_t)>> callback = chosen;
            const std::size_t index = *selected;
            popup->dismiss();
            // After the popup is gone: this runs inside the popup's own click dispatch.
            auto choose = [callback, index]() {
                if (*callback) {
                    (*callback)(index);
                }
            };
            if (newui::RunLoop::current()) {
                newui::RunLoop::current().post(std::move(choose));
            } else {
                choose();
            }
            return newui::SyncReturn::Handled;
        });

        // A ScrollView around the list, so more rows than kMaxVisibleRows scroll (the same shape
        // DropDownList's popup uses).
        auto* scroll = new newui::ScrollView();
        scroll->setVisible(true);
        scroll->addChild(list);

        // The reserved band is on the tail's side: above the list for a top tail, below it for a
        // bottom one (a popup opened above its anchor).
        const newui::Size actual = placement.bounds.size();
        const float listTop = placement.tailSide == newui::shapes::TailSide::Bottom
            ? kPadding
            : PickerRow::kTopReserve + kPadding;
        scroll->setBounds(newui::Rect(kPadding, listTop, actual.width - kPadding * 2.0f, listHeight));
        popup->addChild(scroll);

        popup->present();
        return popup;
    }
}
