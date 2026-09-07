#pragma once

#include <newui/models.h>
#include <newui/subview.h>

#include <optional>
#include <vector>

namespace CodeToolsVsix
{
    // The M in View Designer's own MVC - the real newui::Model
    // ViewDesignerController's own header comment had deliberately left
    // unset ("the loaded .newui document isn't itself wrapped in a formal
    // newui::Model today - it's just a SubView/RootViewProxy tree").
    // Wraps exactly that tree (root() == Workspace::rootViewProxy()) -
    // not a copy, a live view onto whatever DesignerEditor::load()/
    // Toolbox double-click last did to it - giving both
    // ViewDesignerController (via Controller::setModel(), now genuinely
    // pointed somewhere) and Document Outline's own DocumentOutlineModel
    // (a thin TreeModel-shaped adapter over this, see DocumentOutline.h)
    // one shared source of truth instead of two independent tree walks.
    //
    // Deliberately a plain Model, not newui::Document - DesignerEditor
    // (via the NativeEditor interface) already owns the real file-level
    // load()/save()/isDirty() contract for the .newui bundle
    // (bundle-name resolution, Bundle::loadRootView()/writeRootView(),
    // the separate loadFrame() call just for the title - none of which
    // fits Document::readFromFile()/writeToFile()'s "read one file's
    // bytes" shape). A Document-based ViewDesignerModel would just be a
    // second, competing filePath()/isModified() tracker never actually
    // exercised through its own load()/save(). This class only ever
    // needs Model's onChanged notification, not file I/O.
    //
    // Owns no data of its own beyond root_ - every query below walks the
    // live View::childViews() tree fresh, matching designer-plan.md's own
    // "no new data model, it's the live tree" framing for Document
    // Outline specifically, now generalized to whatever else needs to
    // read the same tree.
    //
    // Path convention (shared with DocumentOutlineModel, which forwards
    // straight through to this): path {0} is root() itself (the mockup's
    // own "cppEditorRoot <RootView>" row) - not the implicit invisible
    // super-root a plain newui::TreeModel's path {} usually means. path
    // {0, i} is root()'s i-th real child, {0, i, j} that child's own
    // j-th child, and so on - a single top-level item, not the
    // multi-top-level shape e.g. ToolboxModel's categories use.
    class ViewDesignerModel : public newui::Model
    {
    public:
        // Fires onChanged() - a real, structural change (a fresh document
        // loaded, or the design surface cleared), not just a refresh of
        // the same tree's contents. Always fires, even if root == the
        // current root() already - same "always notify, real callers
        // only call this in direct response to an actual change"
        // convention ViewDesignerController::selectExclusive() already
        // documents.
        void setRoot(newui::SubView* root);
        newui::SubView* root() const { return root_; }

        // Fires onChanged() with root() left unchanged - call after
        // mutating the live tree elsewhere (addChild()/removeChild(),
        // Bundle::loadRootView() repopulating root()'s own children in
        // place) since View itself never notifies a Model of structural
        // changes on its own.
        void refresh();

        std::size_t childCount(const std::vector<std::size_t>& path) const;

        // The real SubView* at path, or nullptr if path doesn't resolve
        // (stale after a tree mutation with no refresh() yet, out of
        // range, root() is null, ...).
        newui::SubView* viewAt(const std::vector<std::size_t>& path) const;

        // The reverse of viewAt() - walks view's own parent() chain back
        // up to root() (nullptr, or a view not under root() at all, =>
        // std::nullopt) - computed fresh every call, never cached, same
        // "no new data model" spirit as viewAt().
        std::optional<std::vector<std::size_t>> pathFor(const newui::SubView* view) const;

    private:
        newui::SubView* root_ = nullptr;
    };
}
