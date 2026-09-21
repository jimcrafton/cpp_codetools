#include "PropertiesGrid.h"
#include "LayoutEditingPolicy.h"
#include "PaintUtils.h"
#include "TextEncoding.h"

#include <newui/rootview.h>
#include <newui/uicolormanager.h>

#include <cmath>
#include <typeindex>

namespace CodeToolsVsix
{
    std::any PropertiesGrid::StringListModel::value(const std::any& key)
    {
        if (const std::size_t* index = std::any_cast<std::size_t>(&key)) {
            if (*index < rows.size()) {
                return rows[*index];
            }
        }
        return std::any();
    }

    namespace
    {
        // A dropdown live editor's rect within valueRect: the full cell, or - for an editor that
        // also has a dialog (Color) - stopping short of its "..." button, which stays visible.
        newui::Rect liveDropdownRectFor(const newui::Rect& valueRect, bool hasEllipsisButton)
        {
            if (!hasEllipsisButton) {
                return valueRect;
            }
            newui::Rect ellipsisRect = PropertyItem::ellipsisButtonRectFor(valueRect);
            return newui::Rect(valueRect.left(), valueRect.top(),
                ellipsisRect.left() - valueRect.left() - 4.0f, valueRect.size().height);
        }
    }

    PropertiesGrid::PropertiesGrid()
    {
        setVisible(true);
        style().setBackgroundColor(newui::UIColorManager::colorFor(newui::UIColorRole::ControlBackground));

        treeView_ = new newui::TreeView();
        treeView_->setName("propertiesTreeView");
        treeView_->setVisible(true);
        treeView_->setController(std::make_unique<PropertiesTreeController>());
        treeView_->setModel(&model_);
        treeView_->onSelectionChanged.add(this, &PropertiesGrid::handleSelectionChanged);
        treeView_->onMouseDown.add(this, &PropertiesGrid::handleTreeMouseDown);
        treeView_->onMouseMove.add(this, &PropertiesGrid::handleTreeMouseMove);
        treeView_->onMouseUp.add(this, &PropertiesGrid::handleTreeMouseUp);
        treeView_->onMouseDblClick.add(this, &PropertiesGrid::handleTreeMouseDblClick);
        // The live editor is placed by absolute bounds inside treeView_, so it has to follow the
        // grid's own width changing (the pane's splitter being dragged) as well as the divider.
        onSizeChanged.add(this, &PropertiesGrid::handleSizeChanged);
        treeView_->onSizeChanged.add(this, &PropertiesGrid::handleSizeChanged);

        // ScrollView::addChild() redirects into its own viewport - not a
        // second, separate wrapping layer, same convention Toolbox's own
        // constructor comment documents.
        addChild(treeView_);

        // See handleScrollBarMouseDown()'s own doc comment (PropertiesGrid.h) for why this is
        // needed - vBar()/hBar() are ScrollView's own chrome, siblings of treeView_, so a click on
        // either one never reaches handleTreeMouseDown()/handleSelectionChanged() at all.
        vBar()->onMouseDown.add(this, &PropertiesGrid::handleScrollBarMouseDown);
        hBar()->onMouseDown.add(this, &PropertiesGrid::handleScrollBarMouseDown);
    }

    PropertiesGrid::~PropertiesGrid()
    {
        *aliveFlag_ = false;

        if (openTypePicker_ != nullptr) {
            // Removes our own onDismissed listener *before* dismiss() - dismiss() defers its real
            // teardown (and onDismissed firing) one tick via RunLoop::post() while a RunLoop is
            // current (PopupTool::dismiss()'s own doc comment), which would otherwise fire
            // handleTypePickerDismissed() against this PropertiesGrid well after this destructor
            // has already finished running - a real dangling-`this` callback, not just a
            // theoretical one (see this class's own openTypePicker_ comment, PropertiesGrid.h).
            // dismiss() itself only needs the popup, never this, so it's still safe to call here.
            openTypePicker_->onDismissed.remove(openTypePickerDismissConnection_);
            openTypePicker_->dismiss();
            openTypePicker_ = nullptr;
        }
    }

    void PropertiesGrid::setSelection(newui::SubView* selected)
    {
        treeView_->clearSelection();
        destroyLiveEditor();
        model_.setSelection(selected);
    }

    void PropertiesGrid::setFilterText(const std::string& text)
    {
        treeView_->clearSelection();
        destroyLiveEditor();
        model_.setFilter(text);
        resetExpansion();
    }

    void PropertiesGrid::setAlphabetical(bool alphabetical)
    {
        treeView_->clearSelection();
        destroyLiveEditor();
        model_.setAlphabetical(alphabetical);
        resetExpansion();
    }

    void PropertiesGrid::resetExpansion()
    {
        // TreeController remembers expansion by index path, which a filter or re-sort just
        // re-pointed at different rows - so it is set afresh rather than trusted. (It has no
        // "collapse everything", so walk the model.)
        newui::TreeController& controller = treeView_->controller();
        const bool filtering = !model_.filter().empty();
        std::vector<std::size_t> path;

        std::function<void(int)> visit = [&](int depth) {
            const std::size_t count = model_.childCount(path);
            const PropertiesModel::Node node = model_.nodeAt(path);
            // The root has no row of its own. A row with nothing under it is still set (closed): the
            // path may have been an open group before this change, and would reappear open if a
            // group ever lands there again.
            if (!path.empty()) {
                controller.setExpanded(path, filtering && count > 0);
            }
            if (count == 0 || depth > 6) {
                return;
            }
            // Filtering opens only the way to what matched: a group that matched by its own name
            // opens one level to show its fields, but its sub-groups stay closed.
            if (filtering && node.showAllChildren) {
                return;
            }
            for (std::size_t i = 0; i < count; ++i) {
                path.push_back(i);
                visit(depth + 1);
                path.pop_back();
            }
        };
        visit(0);
    }

    newui::SyncReturn PropertiesGrid::handleSelectionChanged(newui::TreeView& /*sender*/)
    {
        // Selecting a row no longer activates its editor by itself - only
        // a click that actually lands on the value column does (see
        // activateLiveEditorIfClickedOnValueColumn(), called from
        // handleTreeMouseDown() instead). Still tears down whatever was
        // being edited before, though - a newly selected row should never
        // show a stale previous row's live widget.
        destroyLiveEditor();
        return newui::SyncReturn::Handled;
    }

    void PropertiesGrid::destroyLiveEditor()
    {
        if (liveEditorView_ != nullptr) {
            treeView_->removeChild(liveEditorView_);
            delete liveEditorView_;
            liveEditorView_ = nullptr;
            // A real, live-debugged bug otherwise: removeChild() only
            // ever calls updateLayout(), never markDirty()/invalidate()
            // (same "nothing asks Windows to actually repaint" class of
            // bug this project has hit before - DesignerEditor's own
            // setupUI()/load() need the same explicit call, for the same
            // reason) - Escape/Enter both correctly destroyed the widget
            // internally, but the screen never redrew to show it gone,
            // reading as "the keypress had no effect" even though it did.
            treeView_->style().markDirty();
        }
        liveEditor_.reset();
        liveEditorSubIndex_.reset();
        liveEditorIsParentPicker_ = false;
        parentPickerCandidates_.clear();

        // An ordinary teardown path (this class is still alive throughout, unlike the destructor
        // - see openTypePicker_'s own comment for why that one needs the extra Connection-removal
        // care this doesn't) - selecting something else, or closing/reloading the document, should
        // close whatever type-swap popup was left open the same way it already discards
        // liveEditor_ itself.
        if (openTypePicker_ != nullptr) {
            openTypePicker_->dismiss();
            openTypePicker_ = nullptr;
        }
    }

    newui::SyncReturn PropertiesGrid::handleTypePickerDismissed(newui::PopupTool& /*sender*/)
    {
        openTypePicker_ = nullptr;
        return newui::SyncReturn::Handled;
    }

    namespace
    {
        // Everything that reads an edited view's properties and won't notice they changed on its
        // own: the parent's layout (FlexLayout/GridLayout/... read each child's desiredSize(),
        // margins and layout params during arrange() - View::setDesiredSize() itself only stores
        // the value, which is why an edit used to show only after something else, like a window
        // resize, happened to re-arrange), the view's own layout for its children, and a repaint.
        void refreshAfterPropertyChange(newui::SubView* view)
        {
            if (view == nullptr) {
                return;
            }
            view->style().markDirty();
            if (newui::View* parent = view->parent()) {
                parent->updateLayout();
            }
            view->updateLayout();
        }
    }

    void PropertiesGrid::markSelectedViewDirty()
    {
        refreshAfterPropertyChange(model_.selected());
    }

    void PropertiesGrid::rebuildLiveEditor()
    {
        destroyLiveEditor();

        std::optional<std::vector<std::size_t>> path = treeView_->selectedPath();
        if (!path.has_value()) {
            return;
        }

        PropertiesModel::Node node = model_.nodeAt(*path);
        bool isSubProperty = node.kind == PropertiesModel::Kind::SubPropertyEntry;
        bool isParentPicker = node.kind == PropertiesModel::Kind::ParentPicker;
        if (node.kind != PropertiesModel::Kind::PropertyLeaf && !isSubProperty && !isParentPicker) {
            return;
        }

        std::optional<newui::Rect> rowRect = treeView_->rectForPath(*path);
        if (!rowRect.has_value()) {
            return;
        }

        auto* propsController = dynamic_cast<PropertiesTreeController*>(&treeView_->controller());
        float keyColumnFraction = propsController != nullptr
            ? propsController->keyColumnFraction() : PropertiesTreeController::kDefaultKeyColumnFraction;
        newui::Rect valueRect = PropertyItem::valueRectFor(*rowRect, *path, keyColumnFraction);

        if (isParentPicker) {
            buildParentPickerLiveEditor(node, valueRect);
            return;
        }

        // For a SubPropertyEntry, node.property/ownerClass/ownerInstance
        // already describe the *parent* compound property (e.g.
        // "bounds") - same PropertyEditor either way, just read/written
        // through its subPropertyValueAsString()/
        // setSubPropertyValueFromString(node.subPropertyIndex, ...) below
        // instead of the whole-value valueAsString()/setValueFromString().
        liveEditor_ = PropertyEditorRegistry::instance().createEditor(node.property, node.ownerClass, node.ownerInstance);
        if (liveEditor_ == nullptr) {
            return;
        }
        liveEditor_->setUndoStack(undoStack_);
        liveEditorSubIndex_ = isSubProperty ? std::optional<std::size_t>(node.subPropertyIndex) : std::nullopt;

        // EditStyle::Dialog (Color/Gradient/FilePath) never reaches here at all anymore -
        // activateLiveEditorIfClickedOnValueColumn() routes those straight to
        // openDialogEditorFor() (its own "..." button / handleTreeMouseDblClick()'s double-click)
        // without ever calling this method, so there's nothing to special-case in this function
        // for it - see that method's own comment.

        // "bounds" specifically (see Node::readOnly's own comment, PropertiesModel.h - this row
        // is only ever reachable here at all once that gating has already confirmed the owning
        // View's real parent affords free positioning) - keeps AnchorLayoutParams in sync with
        // whatever bounds() ends up being after this commit (or after an undo/redo of it), the
        // same real trap FreePositionPolicy::commit() already guards against for a canvas drag.
        // model_.selected() is used here rather than node.ownerInstance precisely because this
        // class already knows it's a real newui::SubView* - PropertyEditor/RectPropertyEditor
        // themselves deliberately don't assume that (see setPostCommitSync()'s own comment).
        // Every live-editor commit - and each undo/redo of it, PropertyEditor runs this both ways -
        // ends by refreshing whatever reads the edited view (see refreshAfterPropertyChange()).
        newui::SubView* editedView = model_.selected();
        std::function<void()> boundsSync;
        if (node.property != nullptr && node.property->name() == "bounds") {
            boundsSync = [editedView]() {
                newui::View* parent = editedView != nullptr ? editedView->parent() : nullptr;
                auto* anchorLayout = parent != nullptr ? dynamic_cast<newui::AnchorLayout*>(parent->layout()) : nullptr;
                if (anchorLayout != nullptr) {
                    applyFreePositionAnchorParams(editedView, editedView->bounds());
                    parent->updateLayout();
                }
            };
        }
        liveEditor_->setPostCommitSync([editedView, boundsSync]() {
            if (boundsSync) {
                boundsSync();
            }
            refreshAfterPropertyChange(editedView);
        });

        std::string initialText = isSubProperty
            ? liveEditor_->subPropertyValueAsString(node.subPropertyIndex) : liveEditor_->valueAsString();

        // A SubPropertyEntry is plain text *unless* the editor itself flags this specific
        // sub-index as boolean-shaped (FlagsEnumPropertyEditor's per-bit rows, FontPropertyEditor's
        // bold/italic/underlined/strikeThrough) - those paint a checkbox glyph (PropertyItem::paint(),
        // via subPropertyIsBool()) and need a real Toggle here too, or clicking one would silently
        // swap it for a text field showing "true"/"false" instead of actually toggling.
        bool showsAsToggle = !isSubProperty
            ? node.property->type() == std::type_index(typeid(bool))
            : liveEditor_->subPropertyIsBool(node.subPropertyIndex);
        if (showsAsToggle) {
            auto* toggle = new newui::Toggle();
            toggle->setVisible(true);
            toggle->setChecked(initialText == "true");
            float box = kCheckboxSize;
            toggle->setBounds(newui::Rect(valueRect.left(), valueRect.top() + (valueRect.size().height - box) * 0.5f, box, box));
            toggle->onCheckedChanged.add(this, &PropertiesGrid::handleLiveToggleChanged);
            toggle->onLostFocus.add(this, &PropertiesGrid::handleLiveEditorLostFocus);
            toggle->onKeyDown.add(this, &PropertiesGrid::handleLiveEditorKeyDown);
            treeView_->addChild(toggle);
            liveEditorView_ = toggle;
            focusLiveEditorView();
            return;
        }

        std::vector<std::string> subDropdownValues = isSubProperty
            ? liveEditor_->subPropertyDropdownValues(node.subPropertyIndex) : std::vector<std::string>();
        bool showsAsDropdown = isSubProperty
            ? !subDropdownValues.empty()
            : liveEditor_->hasDropdown();
        if (showsAsDropdown) {
            dropdownModel_.rows = isSubProperty ? subDropdownValues : liveEditor_->dropdownValues();
            // What the dropdown highlights: valueAsString() for every plain dropdown, but a Color's
            // hex text is never a row - it's the matching preset's name, or nothing (custom color).
            if (!isSubProperty) {
                initialText = liveEditor_->dropdownCurrentValue();
            }

            auto* dropdown = new newui::DropDownList();
            dropdown->setVisible(true);
            // Before setModel(): swapping the controller swaps its model too (a fresh controller
            // has none), so a custom one installed afterwards would leave the dropdown empty and
            // its popup unable to open.
            if (!isSubProperty) {
                liveEditor_->customizeDropdown(*dropdown);
            }
            dropdown->setModel(&dropdownModel_);

            for (std::size_t i = 0; i < dropdownModel_.rows.size(); ++i) {
                if (dropdownModel_.rows[i] == initialText) {
                    dropdown->setSelectedIndex(i);
                    break;
                }
            }

            // A dialog-style editor's "..." button stays visible beside the dropdown.
            dropdown->setBounds(liveDropdownRectFor(valueRect, !isSubProperty && liveEditor_->hasDialog()));
            dropdown->onSelectionChanged.add(this, &PropertiesGrid::handleLiveDropdownChanged);
            dropdown->onLostFocus.add(this, &PropertiesGrid::handleLiveEditorLostFocus);
            dropdown->onKeyDown.add(this, &PropertiesGrid::handleLiveEditorKeyDown);
            treeView_->addChild(dropdown);
            liveEditorView_ = dropdown;
            focusLiveEditorView();
            return;
        }

        // EditStyle::None (Int/Float/String today) or a SubPropertyEntry - plain editable text,
        // same fallback PropertyRow::build() used to. Color used to need a special text-offset
        // branch here (its live TextField only covering the text portion of valueRect, leaving
        // its swatch preview to PropertyItem's own inactive painting underneath) - removed, since
        // Color is EditStyle::Dialog now and never reaches this function at all
        // (activateLiveEditorIfClickedOnValueColumn() routes it to openDialogEditorFor() instead).
        auto* textField = new newui::TextField();
        textField->setVisible(true);
        textField->setText(utf8ToWide(initialText));
        textField->setBounds(valueRect);
        textField->onLostFocus.add(this, &PropertiesGrid::handleLiveTextCommit);
        textField->onReturnPressed.add(this, &PropertiesGrid::handleLiveTextReturnPressed);
        textField->onKeyDown.add(this, &PropertiesGrid::handleLiveEditorKeyDown);
        treeView_->addChild(textField);
        liveEditorView_ = textField;
        focusLiveEditorView();
    }

    void PropertiesGrid::buildParentPickerLiveEditor(const PropertiesModel::Node& node, const newui::Rect& valueRect)
    {
        auto* view = static_cast<newui::SubView*>(node.ownerInstance);
        if (view == nullptr || !parentCandidatesProvider_) {
            return;
        }
        std::vector<std::pair<newui::SubView*, std::string>> candidates = parentCandidatesProvider_(view);

        dropdownModel_.rows.clear();
        parentPickerCandidates_.clear();
        for (const auto& [candidate, label] : candidates) {
            dropdownModel_.rows.push_back(label);
            parentPickerCandidates_.push_back(candidate);
        }

        auto* dropdown = new newui::DropDownList();
        dropdown->setVisible(true);
        dropdown->setModel(&dropdownModel_);

        newui::View* currentParent = view->parent();
        for (std::size_t i = 0; i < parentPickerCandidates_.size(); ++i) {
            if (parentPickerCandidates_[i] == currentParent) {
                dropdown->setSelectedIndex(i);
                break;
            }
        }

        dropdown->setBounds(valueRect);
        dropdown->onSelectionChanged.add(this, &PropertiesGrid::handleLiveDropdownChanged);
        dropdown->onLostFocus.add(this, &PropertiesGrid::handleLiveEditorLostFocus);
        dropdown->onKeyDown.add(this, &PropertiesGrid::handleLiveEditorKeyDown);
        treeView_->addChild(dropdown);
        liveEditorView_ = dropdown;
        liveEditorIsParentPicker_ = true;
        focusLiveEditorView();
    }

    void PropertiesGrid::focusLiveEditorView()
    {
        // See this method's own declaration comment (PropertiesGrid.h) for
        // why this is needed at all. treeView_->rootView() is null in
        // tests that construct a bare PropertiesGrid with no real RootView
        // above it - a no-op there, same as every other rootView()-
        // dependent call elsewhere in this codebase.
        if (newui::RootView* rootView = treeView_->rootView()) {
            rootView->setFocusedSubView(liveEditorView_);
        }
    }

    void PropertiesGrid::activateLiveEditorIfClickedOnValueColumn(const newui::Point& pt)
    {
        // Runs after TreeView's own internal mouse-down handler (its own
        // constructor registers it before PropertiesGrid ever subscribes
        // its own handleTreeMouseDown() - see this class's own header
        // comment) already updated selectedPath() for this same click.
        std::optional<std::vector<std::size_t>> path = treeView_->selectedPath();
        if (!path.has_value()) {
            return;
        }

        PropertiesModel::Node node = model_.nodeAt(*path);
        if (node.readOnly) {
            return;
        }

        std::optional<newui::Rect> rowRect = treeView_->rectForPath(*path);
        if (!rowRect.has_value()) {
            return;
        }

        // A Layout/ViewStyle-shaped PropertyGroup header row (see PropertiesModel::
        // classifyProperty()'s own comment) - the only PropertyGroup kind with its own
        // registered Dialog editor. Its own "..." button (right edge of the full-width header
        // row - see PropertyItem's own group-header paint()) opens the type-swap popup; a click
        // anywhere else on the row just expands/collapses, exactly as before (TreeView's own
        // internal mouse-down handler, registered before this one, already did that for this same
        // click - nothing further to do here for it).
        if (node.kind == PropertiesModel::Kind::PropertyGroup) {
            newui::Rect ellipsisRect = PropertyItem::ellipsisButtonRectFor(*rowRect);
            if (ellipsisRect.contains(pt)) {
                openDialogEditorFor(node, treeView_->localToScreen(ellipsisRect));
            }
            return;
        }

        bool isEditable = (node.kind == PropertiesModel::Kind::PropertyLeaf
            || node.kind == PropertiesModel::Kind::SubPropertyEntry
            || node.kind == PropertiesModel::Kind::ParentPicker);
        if (!isEditable) {
            return;
        }

        auto* propsController = dynamic_cast<PropertiesTreeController*>(&treeView_->controller());
        float keyColumnFraction = propsController != nullptr
            ? propsController->keyColumnFraction() : PropertiesTreeController::kDefaultKeyColumnFraction;
        newui::Rect valueRect = PropertyItem::valueRectFor(*rowRect, *path, keyColumnFraction);
        if (!valueRect.contains(pt)) {
            return;
        }

        // A Dialog-style leaf (Color/Gradient/FilePath/...) only opens from its own "..." button
        // now, not a plain click anywhere else in valueRect - see PropertyItem::
        // ellipsisButtonRectFor()'s own comment (PropertyItem.h) for why. SubPropertyEntry/
        // ParentPicker are never Dialog-styled (SubProperties editors, and ParentPicker isn't a
        // real PropertyEditor at all), so this check only ever applies to a PropertyLeaf.
        if (node.kind == PropertiesModel::Kind::PropertyLeaf) {
            auto editor = PropertyEditorRegistry::instance()
                .createEditor(node.property, node.ownerClass, node.ownerInstance);
            if (editor != nullptr && editor->hasDialog()) {
                newui::Rect ellipsisRect = PropertyItem::ellipsisButtonRectFor(valueRect);
                if (ellipsisRect.contains(pt)) {
                    openDialogEditorFor(node, treeView_->localToScreen(ellipsisRect));
                    return;
                }
                // A dialog-only editor (Gradient/FilePath) does nothing on the rest of the cell;
                // one that also has a dropdown (Color) falls through to build it below.
                if (!editor->hasDropdown()) {
                    return;
                }
            }
        }

        rebuildLiveEditor();
    }

    void PropertiesGrid::openDialogEditorFor(const PropertiesModel::Node& node, const newui::Rect& anchorScreenRect)
    {
        destroyLiveEditor();

        std::unique_ptr<PropertyEditor> editor = PropertyEditorRegistry::instance()
            .createEditor(node.property, node.ownerClass, node.ownerInstance);
        if (editor == nullptr || !editor->hasDialog()) {
            return;
        }
        editor->setUndoStack(undoStack_);
        // Needed for the async case (LayoutPropertyEditor/ViewStylePropertyEditor, whose real
        // commit happens later from a popup cell's own click handler, well after editor itself
        // goes out of scope at the end of this method) - harmless no-op timing-wise for a real
        // blocking showModal()/showOpenFile() editor (Color/Gradient/FilePath), which already
        // commits (if at all) before editAsync() returns below.
        // Swapping the Layout must also re-kind every child's LayoutParams to match it (an
        // AnchorLayoutParams left under a FlexLayout is silently ignored) - before the refresh
        // below, so it arranges with the right params. model_.selected() is read when this runs
        // (from the picker popup, later), not captured, so it never dangles.
        const bool swapsLayout = node.property != nullptr && node.property->name() == "layout";
        editor->setPostCommitSync([this, swapsLayout] {
            if (swapsLayout && model_.selected() != nullptr) {
                syncChildLayoutParams(*model_.selected());
            }
            markSelectedViewDirty();
            treeView_->style().markDirty();
        });

        // Deferred via RunLoop::post(), never called inline here - a real, reproduced Win32
        // focus-stealing bug otherwise: this method's own callers (activateLiveEditorIfClicked
        // OnValueColumn()/handleTreeMouseDblClick()) both run from *inside*
        // RootView::mouseDown()/mouseDblClick()'s own dispatch, and that same RootView's
        // WM_LBUTTONDOWN handler (rootview.cpp) unconditionally re-steals OS focus+capture back
        // onto *this* window right after mouseDown() returns - a tail fixup written for the
        // opposite case (a nested popup RootView tearing *itself* down mid-click, e.g.
        // DropDownList's own PopupFrame - see that handler's own comment), which doesn't account
        // for a *new* popup being created and focused mid-dispatch instead. Calling editAsync()
        // synchronously here let that tail fixup steal focus right back before the click even
        // finished, firing WM_KILLFOCUS on the brand-new CalloutTool - PopupTool::
        // dismissOnFocusLost() (the default) then closed it again immediately: "opens for one
        // frame, then instantly closes", confirmed live via testharness.exe. Posting instead runs
        // this once the current mouseDown/mouseDblClick dispatch (tail fixup included) has fully
        // unwound back to the message loop, so nothing is left to steal focus back from the popup
        // once it actually shows. Harmless timing-wise for a blocking editor (Color/Gradient/
        // FilePath's real showModal()/showOpenFile(), reached via the base editAsync() -> edit()
        // wrapper) - opening one tick later than the click that requested it is unobservable.
        newui::SubView* owner = model_.selected();
        std::shared_ptr<PropertyEditor> sharedEditor = std::move(editor);
        std::shared_ptr<bool> alive = aliveFlag_;
        auto openNow = [this, alive, sharedEditor, owner, anchorScreenRect] {
            // this PropertiesGrid could have been destroyed between the click that requested
            // this and the posted task actually running (e.g. the document/tab closing in the
            // same input burst) - same aliveFlag_ guard PopupTool's own postCreate() already uses
            // for its own posted first-repaint task, for the identical reason.
            if (!*alive) {
                return;
            }
            newui::PopupTool* popup = sharedEditor->editAsync(owner, anchorScreenRect);
            if (popup != nullptr) {
                // Tracked for lifetime only - the popup commits through its own captured
                // property_/instance_/postCommitSync_ copies (see LayoutPropertyEditor::
                // editAsync()'s own comment), never back through sharedEditor itself.
                openTypePicker_ = popup;
                openTypePickerDismissConnection_ = popup->onDismissed.add(this, &PropertiesGrid::handleTypePickerDismissed);
            }
            markSelectedViewDirty();
            treeView_->style().markDirty();
        };
        if (newui::RunLoop::current()) {
            newui::RunLoop::current().post(std::move(openNow));
        } else {
            openNow();
        }
    }

    newui::SyncReturn PropertiesGrid::handleTreeMouseDblClick(newui::View& /*sender*/, const newui::Point& pt,
        std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/)
    {
        std::optional<std::vector<std::size_t>> path = treeView_->selectedPath();
        if (!path.has_value()) {
            return newui::SyncReturn::Ignored;
        }

        PropertiesModel::Node node = model_.nodeAt(*path);
        if (node.readOnly) {
            return newui::SyncReturn::Ignored;
        }

        std::optional<newui::Rect> rowRect = treeView_->rectForPath(*path);
        if (!rowRect.has_value()) {
            return newui::SyncReturn::Ignored;
        }

        // The same ellipsis rect activateLiveEditorIfClickedOnValueColumn() anchors to - a
        // double-click doesn't need to land exactly on it (any point in the row's own value/label
        // area is enough, checked below), but the popup itself should still appear next to the
        // same "..." button regardless of which gesture opened it.
        newui::Rect ellipsisRect;
        if (node.kind == PropertiesModel::Kind::PropertyGroup) {
            // No key/value split on a header row - the whole row is this group's own "value"
            // area, same as its "..." button already treats it (activateLiveEditorIfClicked
            // OnValueColumn() above).
            if (!rowRect->contains(pt)) {
                return newui::SyncReturn::Ignored;
            }
            ellipsisRect = PropertyItem::ellipsisButtonRectFor(*rowRect);
        } else if (node.kind == PropertiesModel::Kind::PropertyLeaf) {
            auto* propsController = dynamic_cast<PropertiesTreeController*>(&treeView_->controller());
            float keyColumnFraction = propsController != nullptr
                ? propsController->keyColumnFraction() : PropertiesTreeController::kDefaultKeyColumnFraction;
            newui::Rect valueRect = PropertyItem::valueRectFor(*rowRect, *path, keyColumnFraction);
            if (!valueRect.contains(pt)) {
                return newui::SyncReturn::Ignored;
            }
            ellipsisRect = PropertyItem::ellipsisButtonRectFor(valueRect);
        } else {
            return newui::SyncReturn::Ignored;
        }

        auto editor = PropertyEditorRegistry::instance()
            .createEditor(node.property, node.ownerClass, node.ownerInstance);
        if (editor == nullptr || !editor->hasDialog()) {
            return newui::SyncReturn::Ignored;
        }

        openDialogEditorFor(node, treeView_->localToScreen(ellipsisRect));
        return newui::SyncReturn::Handled;
    }

    newui::SyncReturn PropertiesGrid::handleSizeChanged(newui::View& /*sender*/, const newui::Size& /*newSize*/)
    {
        repositionLiveEditor();
        return newui::SyncReturn::Ignored;  // other listeners (layout, scroll ranges) still run
    }

    void PropertiesGrid::repositionLiveEditor()
    {
        if (liveEditorView_ == nullptr) {
            return;
        }

        std::optional<std::vector<std::size_t>> path = treeView_->selectedPath();
        std::optional<newui::Rect> rowRect = path.has_value() ? treeView_->rectForPath(*path) : std::nullopt;
        if (!rowRect.has_value()) {
            return;
        }

        auto* propsController = dynamic_cast<PropertiesTreeController*>(&treeView_->controller());
        float keyColumnFraction = propsController != nullptr
            ? propsController->keyColumnFraction() : PropertiesTreeController::kDefaultKeyColumnFraction;
        newui::Rect valueRect = PropertyItem::valueRectFor(*rowRect, *path, keyColumnFraction);

        if (auto* toggle = dynamic_cast<newui::Toggle*>(liveEditorView_)) {
            float box = kCheckboxSize;
            toggle->setBounds(newui::Rect(valueRect.left(), valueRect.top() + (valueRect.size().height - box) * 0.5f, box, box));
            return;
        }
        if (auto* dropdown = dynamic_cast<newui::DropDownList*>(liveEditorView_)) {
            bool hasEllipsis = liveEditor_ != nullptr && !liveEditorSubIndex_.has_value() && liveEditor_->hasDialog();
            dropdown->setBounds(liveDropdownRectFor(valueRect, hasEllipsis));
            return;
        }
        if (auto* textField = dynamic_cast<newui::TextField*>(liveEditorView_)) {
            // Color's own swatch-offset branch removed here too - see rebuildLiveEditor()'s own
            // comment; Color is EditStyle::Dialog now and never builds a live TextField at all.
            textField->setBounds(valueRect);
        }
    }

    newui::SyncReturn PropertiesGrid::handleLiveTextCommit(newui::View& /*sender*/)
    {
        // Does NOT destroyLiveEditor() - see handleLiveEditorLostFocus()'s
        // own comment for why: this fires for far more than "the user
        // clicked something else in this app" (the only case that should
        // actually close the editor), and destroying it here was a real,
        // live-debugged bug (the editor silently vanishing on every
        // alt-tab away from the app, discovered when Escape/Enter
        // appeared to do nothing - by the time focus came back there was
        // nothing left to act on). handleSelectionChanged() already
        // closes it when a real click lands on a *different* row.
        auto* textField = dynamic_cast<newui::TextField*>(liveEditorView_);
        if (textField != nullptr && liveEditor_ != nullptr) {
            std::string text = wideToUtf8(textField->text());
            if (liveEditorSubIndex_.has_value()) {
                liveEditor_->setSubPropertyValueFromString(*liveEditorSubIndex_, text);
            } else {
                liveEditor_->setValueFromString(text);
            }
            markSelectedViewDirty();
            treeView_->style().markDirty();
        }
        return newui::SyncReturn::Ignored;
    }

    newui::SyncReturn PropertiesGrid::handleLiveTextReturnPressed(newui::TextField& sender)
    {
        // Commits, same as handleLiveTextCommit(), *then* closes the
        // editor - unlike that method, safe to do here: onReturnPressed
        // only ever fires for a real, deliberate Enter keypress
        // (TextField::handleKeyDown()'s own vkReturn special case,
        // controls.cpp), never for the entire window losing OS-level
        // focus (RootView::lostFocus(), e.g. alt-tab) the way
        // View::onLostFocus does - that distinction is exactly why
        // handleLiveTextCommit() itself no longer closes anything.
        newui::SyncReturn result = handleLiveTextCommit(sender);
        destroyLiveEditor();
        return result;
    }

    newui::SyncReturn PropertiesGrid::handleLiveEditorLostFocus(newui::View& /*sender*/)
    {
        // Deliberately a no-op now - see handleLiveTextCommit()'s own
        // comment. View::onLostFocus fires for this widget losing focus
        // to *anything*, including the entire window losing real OS-level
        // focus (RootView::lostFocus(), fired on WM_KILLFOCUS - e.g.
        // alt-tabbing away, unrelated to this app at all), not just a
        // real click on another control within this app - only the
        // latter should ever close a live editor, and there's no way to
        // tell the two apart from this callback alone. Kept (rather than
        // unwired entirely) as the one place to revisit if a real,
        // narrower "did focus move to a different control in this same
        // app" signal is ever added.
        return newui::SyncReturn::Ignored;
    }

    newui::SyncReturn PropertiesGrid::handleLiveEditorKeyDown(newui::View& /*sender*/, std::uint32_t /*keyMask*/,
        int /*keyCharVal*/, int /*repeatCount*/, std::uint32_t VKeyCode)
    {
        if (VKeyCode != static_cast<std::uint32_t>(newui::vkEscape)) {
            return newui::SyncReturn::Ignored;
        }
        // Discards whatever was typed/toggled/picked - never touches
        // liveEditor_->setValueFromString()/setSubPropertyValueFromString()
        // at all, unlike every commit path above.
        destroyLiveEditor();
        return newui::SyncReturn::Handled;
    }

    newui::SyncReturn PropertiesGrid::handleLiveToggleChanged(newui::Toggle& sender)
    {
        // liveEditorSubIndex_ is set when this Toggle is standing in for a boolean-shaped
        // sub-property row (FlagsEnumPropertyEditor's per-bit rows, FontPropertyEditor's
        // bold/italic/underlined/strikeThrough - see rebuildLiveEditor()'s showsAsToggle) rather
        // than a whole-value bool property.
        if (liveEditor_ != nullptr) {
            const char* text = sender.isChecked() ? "true" : "false";
            if (liveEditorSubIndex_.has_value()) {
                liveEditor_->setSubPropertyValueFromString(*liveEditorSubIndex_, text);
            } else {
                liveEditor_->setValueFromString(text);
            }
            markSelectedViewDirty();
            treeView_->style().markDirty();
        }
        return newui::SyncReturn::Handled;
    }

    newui::SyncReturn PropertiesGrid::handleLiveDropdownChanged(newui::DropDownList& sender)
    {
        if (!sender.selectedIndex().has_value()) {
            return newui::SyncReturn::Ignored;
        }

        if (liveEditorIsParentPicker_) {
            std::size_t index = *sender.selectedIndex();
            std::optional<std::vector<std::size_t>> path = treeView_->selectedPath();
            if (index >= parentPickerCandidates_.size() || !path.has_value()) {
                return newui::SyncReturn::Ignored;
            }
            auto* view = static_cast<newui::SubView*>(model_.nodeAt(*path).ownerInstance);
            newui::SubView* newParent = parentPickerCandidates_[index];
            if (view != nullptr && newParent != nullptr && parentChangeRequestedHandler_) {
                parentChangeRequestedHandler_(view, newParent);
                treeView_->style().markDirty();
            }
            return newui::SyncReturn::Handled;
        }

        // A plain Dropdown widget otherwise exists either for a whole-value property (e.g. an
        // Enum) or, when liveEditorSubIndex_ is set, a sub-property row whose editor offers a
        // fixed set of choices (FontPropertyEditor's "name" row - see subPropertyDropdownValues()).
        if (liveEditor_ == nullptr) {
            return newui::SyncReturn::Ignored;
        }
        std::any value = sender.model()->value(*sender.selectedIndex());
        if (const std::string* text = std::any_cast<std::string>(&value)) {
            if (liveEditorSubIndex_.has_value()) {
                liveEditor_->setSubPropertyValueFromString(*liveEditorSubIndex_, *text);
            } else {
                liveEditor_->setValueFromString(*text);
            }
            markSelectedViewDirty();
            treeView_->style().markDirty();
        }
        return newui::SyncReturn::Handled;
    }

    float PropertiesGrid::dividerX() const
    {
        auto* propsController = dynamic_cast<PropertiesTreeController*>(&treeView_->controller());
        float fraction = propsController != nullptr
            ? propsController->keyColumnFraction() : PropertiesTreeController::kDefaultKeyColumnFraction;
        newui::Rect clientBounds = treeView_->getClientBounds();
        return clientBounds.left() + clientBounds.size().width * fraction;
    }

    bool PropertiesGrid::isPointNearDivider(const newui::Point& localPt) const
    {
        newui::Rect clientBounds = treeView_->getClientBounds();
        if (clientBounds.size().width <= 0.0f) {
            return false;
        }
        return std::abs(localPt.x - dividerX()) <= kDividerHitSlop;
    }

    newui::SyncReturn PropertiesGrid::handleTreeMouseDown(newui::View& /*sender*/, const newui::Point& pt,
        std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/)
    {
        if (isPointNearDivider(pt)) {
            draggingDivider_ = true;
            return newui::SyncReturn::Handled;
        }
        activateLiveEditorIfClickedOnValueColumn(pt);
        return newui::SyncReturn::Ignored;
    }

    newui::SyncReturn PropertiesGrid::handleTreeMouseMove(newui::View& /*sender*/, const newui::Point& pt,
        std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/)
    {
        if (!draggingDivider_) {
            return newui::SyncReturn::Ignored;
        }

        newui::Rect clientBounds = treeView_->getClientBounds();
        if (clientBounds.size().width <= 0.0f) {
            return newui::SyncReturn::Ignored;
        }

        auto* propsController = dynamic_cast<PropertiesTreeController*>(&treeView_->controller());
        if (propsController == nullptr) {
            return newui::SyncReturn::Ignored;
        }

        float fraction = (pt.x - clientBounds.left()) / clientBounds.size().width;
        propsController->setKeyColumnFraction(fraction);
        // The live editor's own value-column rect depends on
        // keyColumnFraction too - reposition it in place (not
        // rebuildLiveEditor() - see that method's own doc comment for why
        // destroying/recreating it here would silently drop an in-progress
        // edit) so it tracks the divider live during the drag.
        repositionLiveEditor();
        return newui::SyncReturn::Handled;
    }

    newui::SyncReturn PropertiesGrid::handleTreeMouseUp(newui::View& /*sender*/, const newui::Point& /*pt*/,
        std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/)
    {
        if (!draggingDivider_) {
            return newui::SyncReturn::Ignored;
        }
        draggingDivider_ = false;
        return newui::SyncReturn::Handled;
    }

    newui::SyncReturn PropertiesGrid::handleScrollBarMouseDown(newui::View& /*sender*/, const newui::Point& /*pt*/,
        std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/)
    {
        destroyLiveEditor();
        return newui::SyncReturn::Ignored;
    }
}
