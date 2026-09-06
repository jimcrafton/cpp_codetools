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
        // Commits the typed text (rebuildLiveEditor()'s own TextField),
        // then closes the editor - a lost-focus event fires for *any*
        // click elsewhere (RootView::mouseDown() reassigns focus for
        // every real click, rootview.cpp - see this class's own header
        // comment), which is exactly what "clicking outside the property
        // editor dismisses it" means: the widget goes away, having
        // applied whatever was typed.
        newui::SyncReturn handleLiveTextCommit(newui::View& sender);
        // Same commit-and-close as handleLiveTextCommit() above, just
        // fired by Enter/Return specifically (TextField::onReturnPressed)
        // rather than losing focus - literally forwards to it, since both
        // mean "apply what's typed, I'm done here".
        newui::SyncReturn handleLiveTextReturnPressed(newui::TextField& sender);
        newui::SyncReturn handleLiveToggleChanged(newui::Toggle& sender);
        newui::SyncReturn handleLiveDropdownChanged(newui::DropDownList& sender);
        // Toggle/DropDownList already commit synchronously from their own
        // native interaction (handleLive*Changed above) - losing focus for
        // either one just needs to close the (already-committed) editor,
        // no separate commit step.
        newui::SyncReturn handleLiveEditorLostFocus(newui::View& sender);
        // Wired to every live editor widget's own onKeyDown, regardless of
        // type - Escape closes it without applying anything (destroys the
        // widget directly, never touching liveEditor_->setValueFromString()/
        // setSubPropertyValueFromString() at all), matching how Enter/blur
        // above are the only two ways a pending text edit gets applied.
        newui::SyncReturn handleLiveEditorKeyDown(newui::View& sender, std::uint32_t keyMask,
            int keyCharVal, int repeatCount, std::uint32_t VKeyCode);

        // Destroys any current live editor widget, then - if the
        // currently selected path resolves to a real PropertyLeaf/
        // SubPropertyEntry that's currently visible (TreeView::
        // rectForPath()) - builds a fresh one, matching PropertyRow::
        // build()'s own bool/Dropdown/Color/plain-text widget choice.
        // Called only from activateLiveEditorIfClickedOnValueColumn()
        // below now - selection changing by itself no longer activates an
        // editor (see that method's own comment for why).
        void rebuildLiveEditor();
        void destroyLiveEditor();

        // Explicitly claims real keyboard focus for liveEditorView_ - a
        // real, reported bug otherwise: this View is created *during* the
        // very RootView::mouseDown() dispatch that activated it (this
        // class's own activateLiveEditorIfClickedOnValueColumn(), called
        // from handleTreeMouseDown()), which runs *after* RootView::
        // mouseDown() already hit-tested and called setFocusedSubView()
        // for this same click (rootview.cpp) - so it never receives focus
        // on its own, and every subsequent keystroke (Escape, Enter,
        // ordinary typing) keeps routing to whatever real View the
        // original hit-test found (treeView_ itself) instead of the
        // freshly created one. Called once, right after each
        // liveEditorView_ is actually created (rebuildLiveEditor()).
        void focusLiveEditorView();

        // Gate for "don't activate the property editor until the user
        // clicks the value itself" - selecting a row (handleSelectionChanged)
        // only ever tears down whatever was being edited before; this is
        // the one path that actually calls rebuildLiveEditor(), and only
        // when pt (already treeView_-local, matching handleTreeMouseDown's
        // own space) falls within the *selected* row's own valueRectFor()
        // - clicking anywhere else in the row (the key/label column, the
        // expand glyph, ...) just selects it, same as every other property
        // grid convention. Runs after TreeView's own internal mouse-down
        // handler (registered first, in its own constructor) has already
        // updated selectedPath() for this same click, so that's already
        // current by the time this reads it.
        void activateLiveEditorIfClickedOnValueColumn(const newui::Point& pt);

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
        newui::SubView* liveEditorView_ = nullptr;
        StringListModel dropdownModel_;

        // Set only when the currently-selected row is a
        // PropertiesModel::Kind::SubPropertyEntry (e.g. bounds' own "x"
        // row) - liveEditor_ itself is still built against the *parent*
        // compound property either way (node.property/ownerClass/
        // ownerInstance), so the three handleLive*Changed handlers need
        // this to know whether to call setValueFromString() (the whole
        // value) or setSubPropertyValueFromString(*liveEditorSubIndex_,
        // ...) (just this one synthetic component) - std::nullopt means
        // "not editing a sub-property", not "index 0".
        std::optional<std::size_t> liveEditorSubIndex_;
    };
}
