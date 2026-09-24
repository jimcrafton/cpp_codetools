#include "ComponentEditor.h"
#include "SelectionOverlay.h"

#include <newui/layout.h>
#include <newui/subview.h>
#include <newui/controls.h>
#include <newui/splitter.h>
#include <newui/segmentedcontrol.h>

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

    void ComponentEditor::edit()
    {
        std::vector<std::string> path = defaultPropertyPath();
        if (!path.empty()) {
            requestEditProperty(view_, path);
        }
    }

    void ComponentEditor::requestEditProperty(newui::View* target, const std::vector<std::string>& path)
    {
        if (target != nullptr && editPropertyHandler_) {
            editPropertyHandler_(target, path);
        }
    }

    bool TabControlEditor::canEdit() const
    {
        return static_cast<newui::TabControl*>(view_)->tabCount() > 0;
    }

    void TabControlEditor::edit()
    {
        auto* tabs = static_cast<newui::TabControl*>(view_);
        requestEditProperty(tabs->page(tabs->selectedIndex()), { "title" });
    }

    void TabControlEditor::editAt(const newui::Point& rootPt)
    {
        auto* tabs = static_cast<newui::TabControl*>(view_);
        for (std::size_t i = 0; i < tabs->tabCount(); ++i) {
            newui::SubView* button = tabs->tabButton(i);
            if (button != nullptr && SelectionOverlay::boundsInRootView(button).contains(rootPt)) {
                tabs->selectTab(i);
                break;
            }
        }
        edit();
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

    std::size_t ToolbarEditor::verbCount() const
    {
        return view_->childViews().empty() ? 2 : 3;
    }

    std::string ToolbarEditor::verb(std::size_t index) const
    {
        switch (index) {
        case 0: return "Add Button";
        case 1: return "Add Separator";
        case 2: return view_->childViews().empty() ? std::string() : "Remove Last Item";
        default: return {};
        }
    }

    void ToolbarEditor::executeVerb(std::size_t index)
    {
        switch (index) {
        case 0: addButton(); break;
        case 1: addSeparator(); break;
        case 2: removeLastItem(); break;
        default: break;
        }
    }

    void ToolbarEditor::addButton()
    {
        auto* toolbar = static_cast<newui::Toolbar*>(view_);
        std::size_t existing = 0;
        for (newui::SubView* child : toolbar->childViews()) {
            if (dynamic_cast<newui::ToolbarButton*>(child) != nullptr) {
                ++existing;
            }
        }

        // A ToolbarButton never self-measures, so it needs an explicit size (Workspace's own toolbar does the same).
        const bool horizontal = toolbar->orientation() == newui::Orientation::Horizontal;
        auto* button = new newui::ToolbarButton();
        button->setName("Button " + std::to_string(existing + 1));
        button->setText("Button");
        button->setVisible(true);
        button->setDesiredSize(horizontal ? newui::Size(50.0f, 24.0f) : newui::Size(24.0f, 50.0f));

        newui::UndoableAction action;
        action.description = "Add Button";
        action.doIt = [toolbar, button] { toolbar->addChild(button); };
        action.undoIt = [toolbar, button] { toolbar->removeChild(button); };  // stays alive, detached
        runAction(std::move(action));
    }

    void ToolbarEditor::addSeparator()
    {
        auto* toolbar = static_cast<newui::Toolbar*>(view_);
        std::size_t existing = 0;
        for (newui::SubView* child : toolbar->childViews()) {
            if (dynamic_cast<newui::ToolbarSeparator*>(child) != nullptr) {
                ++existing;
            }
        }

        auto* separator = new newui::ToolbarSeparator();
        separator->setName("Separator " + std::to_string(existing + 1));
        separator->setHorizontal(toolbar->orientation() == newui::Orientation::Horizontal);
        separator->setVisible(true);

        newui::UndoableAction action;
        action.description = "Add Separator";
        action.doIt = [toolbar, separator] { toolbar->addChild(separator); };
        action.undoIt = [toolbar, separator] { toolbar->removeChild(separator); };
        runAction(std::move(action));
    }

    void ToolbarEditor::removeLastItem()
    {
        auto* toolbar = static_cast<newui::Toolbar*>(view_);
        if (toolbar->childViews().empty()) {
            return;
        }

        newui::SubView* item = toolbar->childViews().back();
        newui::UndoableAction action;
        action.description = "Remove Toolbar Item";
        action.doIt = [toolbar, item] { toolbar->removeChild(item); };
        action.undoIt = [toolbar, item] { toolbar->addChild(item); };  // it was last, and addChild() appends
        runAction(std::move(action));
    }

    std::vector<SplitterEditor::Verb> SplitterEditor::applicableVerbs() const
    {
        const std::size_t panes = view_->childViews().size();
        std::vector<Verb> verbs;
        if (panes < 2) {
            verbs.push_back(Verb::AddPane);
        }
        if (panes > 0) {
            verbs.push_back(Verb::RemovePane);
        }
        if (panes == 2) {
            verbs.push_back(Verb::SwapPanes);
        }
        return verbs;
    }

    std::size_t SplitterEditor::verbCount() const
    {
        return applicableVerbs().size();
    }

    std::string SplitterEditor::verb(std::size_t index) const
    {
        std::vector<Verb> verbs = applicableVerbs();
        if (index >= verbs.size()) {
            return {};
        }
        switch (verbs[index]) {
        case Verb::AddPane: return "Add Pane";
        case Verb::RemovePane: return "Remove Last Pane";
        case Verb::SwapPanes: return "Swap Panes";
        }
        return {};
    }

    void SplitterEditor::executeVerb(std::size_t index)
    {
        std::vector<Verb> verbs = applicableVerbs();
        if (index >= verbs.size()) {
            return;
        }
        switch (verbs[index]) {
        case Verb::AddPane: addPane(); break;
        case Verb::RemovePane: removeLastPane(); break;
        case Verb::SwapPanes: swapPanes(); break;
        }
    }

    void SplitterEditor::addPane()
    {
        auto* splitter = static_cast<newui::Splitter*>(view_);
        if (splitter->childViews().size() >= 2) {
            return;
        }

        auto* pane = new newui::SubView();
        pane->setName("Pane " + std::to_string(splitter->childViews().size() + 1));
        pane->setVisible(true);
        pane->setLayout(std::make_unique<newui::AnchorLayout>());  // a container: controls can be dropped onto the pane

        newui::UndoableAction action;
        action.description = "Add Pane";
        action.doIt = [splitter, pane] { splitter->addChild(pane); };
        action.undoIt = [splitter, pane] { splitter->removeChild(pane); };  // stays alive, detached
        runAction(std::move(action));
    }

    void SplitterEditor::removeLastPane()
    {
        auto* splitter = static_cast<newui::Splitter*>(view_);
        if (splitter->childViews().empty()) {
            return;
        }

        newui::SubView* pane = splitter->childViews().back();
        newui::UndoableAction action;
        action.description = "Remove Pane";
        action.doIt = [splitter, pane] { splitter->removeChild(pane); };
        action.undoIt = [splitter, pane] { splitter->addChild(pane); };  // it was last, and addChild() appends
        runAction(std::move(action));
    }

    void SplitterEditor::swapPanes()
    {
        auto* splitter = static_cast<newui::Splitter*>(view_);
        if (splitter->childViews().size() != 2) {
            return;
        }

        // Its own inverse. Splitter has no Layout, so View::reorderChild()'s updateLayout() does not
        // re-arrange the panes - re-setting the split position does.
        auto swap = [splitter] {
            if (splitter->childViews().size() == 2) {
                splitter->reorderChild(splitter->childViews()[1], 0);
                splitter->setSplitPosition(splitter->splitPosition());
            }
        };

        newui::UndoableAction action;
        action.description = "Swap Panes";
        action.doIt = swap;
        action.undoIt = swap;
        runAction(std::move(action));
    }

    namespace
    {
        // The model of a ListView or DropDownList (its controller owns it), or nullptr.
        newui::ListModel* modelOf(newui::View* view)
        {
            if (auto* list = dynamic_cast<newui::ListView*>(view)) {
                return list->model();
            }
            if (auto* dropDown = dynamic_cast<newui::DropDownList*>(view)) {
                return dropDown->model();
            }
            return nullptr;
        }

        void setModelOf(newui::View* view, std::unique_ptr<newui::ListModel> model)
        {
            if (auto* list = dynamic_cast<newui::ListView*>(view)) {
                list->setModel(std::move(model));
            } else if (auto* dropDown = dynamic_cast<newui::DropDownList*>(view)) {
                dropDown->setModel(std::move(model));
            }
        }
    }

    newui::StringListModel* ListModelEditor::stringModel() const
    {
        return dynamic_cast<newui::StringListModel*>(modelOf(view_));
    }

    bool ListModelEditor::applicable() const
    {
        return modelOf(view_) == nullptr || stringModel() != nullptr;
    }

    std::size_t ListModelEditor::verbCount() const
    {
        if (!applicable()) {
            return 0;
        }
        newui::StringListModel* model = stringModel();
        return model != nullptr && !model->items().empty() ? 2 : 1;
    }

    std::string ListModelEditor::verb(std::size_t index) const
    {
        switch (index) {
        case 0: return "Add Item";
        case 1: return verbCount() > 1 ? "Remove Last Item" : std::string();
        default: return {};
        }
    }

    void ListModelEditor::executeVerb(std::size_t index)
    {
        if (index == 0) {
            addItem();
        } else if (index == 1) {
            removeLastItem();
        }
    }

    void ListModelEditor::addItem()
    {
        if (!applicable()) {
            return;
        }

        // The view is re-resolved on every do/undo, never a model held: another action may have
        // replaced the model in between.
        newui::View* view = view_;
        const bool createsModel = modelOf(view) == nullptr;
        const std::size_t existing = createsModel ? 0 : stringModel()->items().size();
        const std::string text = "Item " + std::to_string(existing + 1);

        newui::UndoableAction action;
        action.description = "Add Item";
        action.doIt = [view, text, createsModel] {
            if (createsModel && modelOf(view) == nullptr) {
                setModelOf(view, std::make_unique<newui::StringListModel>());
            }
            if (auto* model = dynamic_cast<newui::StringListModel*>(modelOf(view))) {
                model->addItem(text);
            }
        };
        action.undoIt = [view, createsModel] {
            if (auto* model = dynamic_cast<newui::StringListModel*>(modelOf(view))) {
                if (!model->items().empty()) {
                    model->removeItem(model->items().size() - 1);
                }
            }
            if (createsModel) {
                setModelOf(view, nullptr);
            }
        };
        runAction(std::move(action));
    }

    void ListModelEditor::removeLastItem()
    {
        newui::StringListModel* model = stringModel();
        if (model == nullptr || model->items().empty()) {
            return;
        }

        newui::View* view = view_;
        const std::string text = model->items().back();

        newui::UndoableAction action;
        action.description = "Remove Item";
        action.doIt = [view] {
            if (auto* current = dynamic_cast<newui::StringListModel*>(modelOf(view))) {
                if (!current->items().empty()) {
                    current->removeItem(current->items().size() - 1);
                }
            }
        };
        action.undoIt = [view, text] {
            if (auto* current = dynamic_cast<newui::StringListModel*>(modelOf(view))) {
                current->addItem(text);
            }
        };
        runAction(std::move(action));
    }

    namespace
    {
        newui::TreeModel* treeModelOf(newui::View* view)
        {
            auto* tree = dynamic_cast<newui::TreeView*>(view);
            return tree != nullptr ? tree->model() : nullptr;
        }

        void setTreeModelOf(newui::View* view, std::unique_ptr<newui::TreeModel> model)
        {
            if (auto* tree = dynamic_cast<newui::TreeView*>(view)) {
                tree->setModel(std::move(model));
            }
        }
    }

    newui::StringTreeModel* TreeModelEditor::stringModel() const
    {
        return dynamic_cast<newui::StringTreeModel*>(treeModelOf(view_));
    }

    bool TreeModelEditor::applicable() const
    {
        return treeModelOf(view_) == nullptr || stringModel() != nullptr;
    }

    std::size_t TreeModelEditor::verbCount() const
    {
        if (!applicable()) {
            return 0;
        }
        newui::StringTreeModel* model = stringModel();
        return model != nullptr && !model->rows().empty() ? 2 : 1;
    }

    std::string TreeModelEditor::verb(std::size_t index) const
    {
        switch (index) {
        case 0: return "Add Item";
        case 1: return verbCount() > 1 ? "Remove Last Item" : std::string();
        default: return {};
        }
    }

    void TreeModelEditor::executeVerb(std::size_t index)
    {
        if (index == 0) {
            addItem();
        } else if (index == 1) {
            removeLastItem();
        }
    }

    void TreeModelEditor::addItem()
    {
        if (!applicable()) {
            return;
        }

        // Re-resolved on every do/undo, never a model held - another action may have replaced it.
        newui::View* view = view_;
        const bool createsModel = treeModelOf(view) == nullptr;
        const std::size_t existing = createsModel ? 0 : stringModel()->rows().size();
        const std::string text = "Item " + std::to_string(existing + 1);

        newui::UndoableAction action;
        action.description = "Add Item";
        action.doIt = [view, text, createsModel] {
            if (createsModel && treeModelOf(view) == nullptr) {
                setTreeModelOf(view, std::make_unique<newui::StringTreeModel>());
            }
            if (auto* model = dynamic_cast<newui::StringTreeModel*>(treeModelOf(view))) {
                model->addItem(text);
            }
        };
        action.undoIt = [view, createsModel] {
            if (auto* model = dynamic_cast<newui::StringTreeModel*>(treeModelOf(view))) {
                model->removeLastItem();
            }
            if (createsModel) {
                setTreeModelOf(view, nullptr);
            }
        };
        runAction(std::move(action));
    }

    void TreeModelEditor::removeLastItem()
    {
        newui::StringTreeModel* model = stringModel();
        if (model == nullptr || model->rows().empty()) {
            return;
        }

        newui::View* view = view_;
        const newui::TreeRow removed = model->rows().back();

        newui::UndoableAction action;
        action.description = "Remove Item";
        action.doIt = [view] {
            if (auto* current = dynamic_cast<newui::StringTreeModel*>(treeModelOf(view))) {
                current->removeLastItem();
            }
        };
        action.undoIt = [view, removed] {
            if (auto* current = dynamic_cast<newui::StringTreeModel*>(treeModelOf(view))) {
                current->rows().push_back(removed);   // exact depth+text, not just addItem()'s root-only shape
                current->onChanged(*current);   // rows() bypasses onChanged - tell the view directly
            }
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
        if (const newui::reflection::Class* toolbarClass = newui::reflection::classinfo(typeid(newui::Toolbar))) {
            registerEditor(toolbarClass,
                [](newui::View* view) { return std::make_unique<ToolbarEditor>(view); });
        }
        for (const std::type_info* type : { &typeid(newui::ListView), &typeid(newui::DropDownList) }) {
            if (const newui::reflection::Class* listClass = newui::reflection::classinfo(*type)) {
                registerEditor(listClass,
                    [](newui::View* view) { return std::make_unique<ListModelEditor>(view); });
            }
        }
        if (const newui::reflection::Class* treeViewClass = newui::reflection::classinfo(typeid(newui::TreeView))) {
            registerEditor(treeViewClass,
                [](newui::View* view) { return std::make_unique<TreeModelEditor>(view); });
        }
        if (const newui::reflection::Class* splitterClass = newui::reflection::classinfo(typeid(newui::Splitter))) {
            registerEditor(splitterClass,
                [](newui::View* view) { return std::make_unique<SplitterEditor>(view); });
        }

        // Double-click-only editors: no verbs, just the property a double-click opens.
        const std::pair<const std::type_info*, const char*> defaultProperties[] = {
            { &typeid(newui::Button), "text" },
            { &typeid(newui::ToolbarButton), "text" },
            { &typeid(newui::Label), "text" },
            { &typeid(newui::GroupBox), "text" },
            { &typeid(newui::TextField), "text" },
            { &typeid(newui::TextControl), "text" },
            { &typeid(newui::TabPage), "title" },
            { &typeid(newui::Image), "imagePath" },
            { &typeid(newui::SegmentedControl), "segments" },
            { &typeid(newui::Slider), "value" },
            { &typeid(newui::Progress), "value" },
            { &typeid(newui::Stepper), "value" },
        };
        for (const auto& [type, property] : defaultProperties) {
            if (const newui::reflection::Class* clazz = newui::reflection::classinfo(*type)) {
                std::string name = property;
                registerEditor(clazz, [name](newui::View* view) {
                    return std::make_unique<DefaultPropertyEditor>(view, std::vector<std::string>{ name });
                });
            }
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
