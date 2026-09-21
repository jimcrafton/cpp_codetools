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

    newui::GridLayout* GridLayoutEditor::grid() const
    {
        return dynamic_cast<newui::GridLayout*>(view_->layout());
    }

    std::vector<GridLayoutEditor::Verb> GridLayoutEditor::applicableVerbs() const
    {
        std::vector<Verb> verbs = { Verb::AddRow, Verb::AddColumn };
        if (newui::GridLayout* layout = grid()) {
            if (!layout->rows().empty()) {
                verbs.push_back(Verb::RemoveRow);
            }
            if (!layout->columns().empty()) {
                verbs.push_back(Verb::RemoveColumn);
            }
        }
        return verbs;
    }

    std::size_t GridLayoutEditor::verbCount() const
    {
        return grid() != nullptr ? applicableVerbs().size() : 0;
    }

    std::string GridLayoutEditor::verb(std::size_t index) const
    {
        std::vector<Verb> verbs = applicableVerbs();
        if (index >= verbs.size()) {
            return {};
        }
        switch (verbs[index]) {
        case Verb::AddRow: return "Add Row";
        case Verb::AddColumn: return "Add Column";
        case Verb::RemoveRow: return "Remove Last Row";
        case Verb::RemoveColumn: return "Remove Last Column";
        }
        return {};
    }

    void GridLayoutEditor::executeVerb(std::size_t index)
    {
        std::vector<Verb> verbs = applicableVerbs();
        if (grid() == nullptr || index >= verbs.size()) {
            return;
        }
        switch (verbs[index]) {
        case Verb::AddRow: changeTracks(true, true); break;
        case Verb::AddColumn: changeTracks(false, true); break;
        case Verb::RemoveRow: changeTracks(true, false); break;
        case Verb::RemoveColumn: changeTracks(false, false); break;
        }
    }

    void GridLayoutEditor::changeTracks(bool rows, bool add)
    {
        newui::GridLayout* layout = grid();
        std::vector<newui::GridTrack> before = rows ? layout->rows() : layout->columns();
        std::vector<newui::GridTrack> after = before;
        if (add) {
            after.push_back(newui::GridTrack{ newui::GridTrackKind::Star, 1.0f });
        } else if (!after.empty()) {
            after.pop_back();
        }

        // The layout is re-fetched from the view each time, never held: it could have been swapped
        // for another Layout between the do and its undo.
        newui::View* view = view_;
        auto apply = [view, rows](const std::vector<newui::GridTrack>& tracks) {
            if (auto* current = dynamic_cast<newui::GridLayout*>(view->layout())) {
                (rows ? current->rows() : current->columns()) = tracks;
                view->updateLayout();
            }
        };

        newui::UndoableAction action;
        action.description = std::string(add ? "Add " : "Remove ") + (rows ? "Row" : "Column");
        action.doIt = [apply, after] { apply(after); };
        action.undoIt = [apply, before] { apply(before); };
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
