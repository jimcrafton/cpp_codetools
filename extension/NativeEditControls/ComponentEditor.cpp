#include "ComponentEditor.h"

#include <newui/layout.h>
#include <newui/subview.h>
#include <newui/controls.h>

#include <memory>

namespace CodeToolsVsix
{
    void ComponentEditor::runAction(newui::UndoableAction action)
    {
        PostExecuteSync sync = postExecuteSync_;
        std::function<void()> doIt = std::move(action.doIt);
        std::function<void()> undoIt = std::move(action.undoIt);
        action.doIt = [doIt, sync] {
            doIt();
            if (sync) { sync(); }
        };
        action.undoIt = [undoIt, sync] {
            undoIt();
            if (sync) { sync(); }
        };

        if (undoStack_ == nullptr) {
            action.doIt();
            return;
        }
        undoStack_->push(std::move(action));
    }

    std::size_t TabControlEditor::verbCount() const
    {
        return static_cast<newui::TabControl*>(view_)->tabCount() > 0 ? 2 : 1;
    }

    std::string TabControlEditor::verb(std::size_t index) const
    {
        switch (index) {
        case 0: return "Add Tab";
        case 1: return "Remove Last Tab";
        default: return {};
        }
    }

    void TabControlEditor::executeVerb(std::size_t index)
    {
        if (index == 0) {
            addTab();
        } else if (index == 1) {
            removeLastTab();
        }
    }

    void TabControlEditor::addTab()
    {
        auto* tabs = static_cast<newui::TabControl*>(view_);
        const std::size_t index = tabs->tabCount();
        const std::string text = "Tab " + std::to_string(index + 1);

        auto* page = new newui::TabPage();
        page->setName("Page " + std::to_string(index + 1));
        page->setVisible(true);
        page->setLayout(std::make_unique<newui::AnchorLayout>());  // a container: controls can be dropped onto the page

        newui::UndoableAction action;
        action.description = "Add Tab";
        action.doIt = [tabs, text, page] { tabs->addTab(text, page); };
        action.undoIt = [tabs, index] { tabs->removeTab(index); };  // page stays alive, detached
        runAction(std::move(action));
    }

    void TabControlEditor::removeLastTab()
    {
        auto* tabs = static_cast<newui::TabControl*>(view_);
        if (tabs->tabCount() == 0) {
            return;
        }

        const std::size_t index = tabs->tabCount() - 1;
        const std::string text = tabs->tabButton(index)->name();  // a button is named after its label
        const std::size_t selectedBefore = tabs->selectedIndex();
        auto detachedPage = std::make_shared<newui::SubView*>(nullptr);

        newui::UndoableAction action;
        action.description = "Remove Tab";
        action.doIt = [tabs, index, detachedPage] { *detachedPage = tabs->removeTab(index); };
        action.undoIt = [tabs, text, selectedBefore, detachedPage] {
            tabs->addTab(text, *detachedPage);
            tabs->selectTab(selectedBefore);
        };
        runAction(std::move(action));
    }

    ComponentEditorRegistry& ComponentEditorRegistry::instance()
    {
        static ComponentEditorRegistry registry;
        return registry;
    }

    void ComponentEditorRegistry::registerEditor(const newui::reflection::Class* owningClass, Factory factory)
    {
        entries_.emplace_back(owningClass, std::move(factory));
    }

    void ComponentEditorRegistry::registerBuiltinEditors()
    {
        if (builtinsRegistered_) {
            return;
        }
        builtinsRegistered_ = true;

        if (const newui::reflection::Class* tabControlClass = newui::reflection::classinfo(typeid(newui::TabControl))) {
            registerEditor(tabControlClass,
                [](newui::View* view) { return std::make_unique<TabControlEditor>(view); });
        }
    }

    std::unique_ptr<ComponentEditor> ComponentEditorRegistry::createEditor(
        const newui::reflection::Class* owningClass,
        newui::View* view) const
    {
        for (const newui::reflection::Class* c = owningClass; c != nullptr; c = c->parentClass()) {
            for (const auto& entry : entries_) {
                if (entry.first == c) {
                    return entry.second(view);
                }
            }
        }
        return nullptr;
    }
}
