#pragma once

#include "PropertiesModel.h"
#include "PropertyItem.h"

#include <newui/controllers.h>
#include <newui/controls.h>
#include <newui/undostack.h>

#include <any>
#include <memory>
#include <optional>
#include <vector>

namespace CodeToolsVsix
{
    // The TreeView-based replacement for PropertiesPanel/PropertyRow's
    // hand-rolled SubView rows (bluesky/property-grid-design.md) - a real
    // newui::ScrollView hosting a real newui::TreeView (PropertiesModel/
    // PropertyItem/PropertiesTreeController), same "ScrollView::addChild()
    // redirects into its own viewport, no manual setContentSize()" shape
    // Toolbox already established.
    //
    // Exactly one live editor widget at a time (a real TextField/Toggle/
    // DropDownList - PropertyRow::build()'s own widget-choice logic,
    // reused verbatim here), positioned over whichever row is currently
    // selected via newui::TreeView::rectForPath() + PropertyItem::
    // keyRectFor()/valueRectFor() - every other row just paints its value
    // through PropertyItem's own inactive rendering. See the design doc's
    // own "exactly one live editor, not one per row" section for why: a
    // TreeItem/Item isn't a View (items.h), so there's no way to host N
    // live child widgets recycled across scrolling the way PropertyRow's
    // rows used to - only the one row that's actually being edited needs
    // one.
    class PropertiesGrid : public newui::ScrollView
    {
    public:
        PropertiesGrid();

        // Rebuilds the tree against selected's real properties/delegates
        // (classinfo(typeid(*selected))) - nullptr clears it. Always
        // clears the current selection/live editor first - a newly
        // selected object starts with nothing selected, same as
        // PropertiesPanel::setSelection() used to (a leftover selected
        // path from a previous object would otherwise silently resolve
        // against completely different data at that same index).
        void setSelection(newui::SubView* selected);
        newui::SubView* selected() const { return model_.selected(); }

        // Attaches the UndoStack every live PropertyEditor this class
        // creates is given (PropertyEditor::setUndoStack()) - same
        // nullptr-means-direct-commit default as PropertyEditor itself.
        void setUndoStack(newui::UndoStack* undoStack) { undoStack_ = undoStack; }

        // Exposed for testability - same convention Toolbox::treeView()
        // already uses.
        newui::TreeView* treeView() const { return treeView_; }
        PropertiesModel& model() { return model_; }

    private:
        // Minimal newui::ListModel over a plain string list - mirrors
        // PropertyRow::StringListModel exactly (same reasoning: local to
        // one class, not exported).
        class StringListModel : public newui::ListModel
        {
        public:
            std::vector<std::string> rows;
            std::any value(const std::any& key) override;
            std::size_t size() const override { return rows.size(); }
        };

        newui::SyncReturn handleSelectionChanged(newui::TreeView& sender);
        newui::SyncReturn handleLiveTextCommit(newui::View& sender);
        newui::SyncReturn handleLiveToggleChanged(newui::Toggle& sender);
        newui::SyncReturn handleLiveDropdownChanged(newui::DropDownList& sender);

        // Destroys any current live editor widget, then - if the
        // currently selected path resolves to a real PropertyLeaf that's
        // currently visible (TreeView::rectForPath()) - builds a fresh
        // one, matching PropertyRow::build()'s own bool/Dropdown/Color/
        // plain-text widget choice.
        void rebuildLiveEditor();
        void destroyLiveEditor();

        // Just moves the existing live editor widget's bounds to match a
        // changed keyColumnFraction (divider drag) - deliberately not a
        // rebuildLiveEditor() call: that destroys and recreates the
        // widget, which would silently discard any typed-but-not-yet-
        // committed edit (PropertyEditor::setValueFromString() only ever
        // runs on lost-focus/checked-changed/selection-changed, not
        // continuously) every time the user also drags the divider while
        // editing. A no-op if nothing is currently being edited.
        void repositionLiveEditor();

        // Resizable key/value divider (bluesky/property-grid-design.md) -
        // hooked directly onto treeView_'s own onMouseDown/Move/Up
        // (View::onMouseDown et al. fire every subscriber via
        // Delegate::syncCall(), not just the first Handled one - see
        // delegate.h - so this runs alongside TreeView's own internal
        // row-selection/expand-glyph handling every time, not instead of
        // it; a click landing exactly on the thin divider line can
        // therefore also select whatever row is at that Y, same as
        // clicking anywhere else in the row would - a minor, accepted
        // overlap, not a hazard). Same dividerRect()/isPointInDivider()/
        // drag-state shape as newui::Splitter's own divider drag
        // (splitter.cpp), just against PropertiesTreeController's shared
        // keyColumnFraction instead of a pixel splitPosition_.
        static constexpr float kDividerHitSlop = 4.0f;
        float dividerX() const;
        bool isPointNearDivider(const newui::Point& localPt) const;
        newui::SyncReturn handleTreeMouseDown(newui::View& sender, const newui::Point& pt,
            std::uint32_t btnMask, std::uint32_t keyMask);
        newui::SyncReturn handleTreeMouseMove(newui::View& sender, const newui::Point& pt,
            std::uint32_t btnMask, std::uint32_t keyMask);
        newui::SyncReturn handleTreeMouseUp(newui::View& sender, const newui::Point& pt,
            std::uint32_t btnMask, std::uint32_t keyMask);

        PropertiesModel model_;
        newui::TreeView* treeView_ = nullptr;
        newui::UndoStack* undoStack_ = nullptr;
        bool draggingDivider_ = false;

        std::unique_ptr<PropertyEditor> liveEditor_;
        newui::SubView* liveEditorWidget_ = nullptr;
        StringListModel dropdownModel_;
    };
}
