#pragma once

#include <newui/controllers.h>
#include <newui/controls.h>
#include <newui/delegate.h>
#include <newui/models.h>

#include <any>
#include <string>
#include <vector>

namespace CodeToolsVsix
{
    // Backs DirectoryTree's own newui::TreeView (below) - a stand-in for what a real IDE's
    // Solution Explorer would show ("we need to know what directory/project we're working on"
    // even though testharness has no VS host providing that). Lazily scans the real filesystem
    // one directory level at a time (std::filesystem::directory_iterator), caching each already-
    // scanned directory's own children rather than walking the whole tree eagerly up front - a
    // directory is only ever hit again on refresh() (which just drops the cache and lets the
    // next query re-scan it). Filtered to directories (always shown, for navigation) plus files
    // this project's own tools actually care about (isSourceFile() in the .cpp) - not a generic
    // file browser.
    class DirectoryTreeModel : public newui::TreeModel
    {
    public:
        struct Entry
        {
            std::string name;      // filename only, no path
            std::string fullPath;
            bool isDirectory = false;
            mutable std::vector<Entry> children;
            mutable bool childrenLoaded = false;
        };

        // Points this model at a new root folder and fires onChanged() - the previous root's own
        // cached scan (if any) is discarded; the new root's own top level loads lazily, same as
        // any other node, the first time childCount()/value() asks about it.
        void setRootPath(const std::string& path);
        const std::string& rootPath() const { return rootPath_; }

        // Drops every cached directory scan (collapsing back to "nothing loaded yet") and fires
        // onChanged() - the simplest honest "F5" for this tree: nothing here watches the
        // filesystem for live changes, and re-scanning only the root level (rather than trying to
        // selectively re-validate every already-expanded subtree) keeps this from needing to
        // reconcile stale child indices against whatever changed on disk.
        void refresh();

        std::size_t childCount(const std::vector<std::size_t>& path) const override;
        std::any value(const std::any& key) override;

        // Resolves a tree path to the real Entry it names - nullptr if path no longer resolves
        // (e.g. stale after a refresh(), or simply out of range). DirectoryTree's own double-
        // click handling uses this to recover the activated file's real filesystem path.
        const Entry* entryAt(const std::vector<std::size_t>& path) const;

    private:
        // Scans entry's own immediate children from disk into entry.children if they haven't
        // been loaded yet (entry.childrenLoaded) - a no-op otherwise, and for a non-directory
        // entry. const because every read path (childCount()/value()/entryAt()) is const on this
        // model but still needs to trigger this lazy load - entry's own children/childrenLoaded
        // are mutable specifically for this.
        void ensureChildrenLoaded(const Entry& entry) const;

        std::string rootPath_;
        Entry root_;
    };

    // The directory tree pane itself (a real newui::ScrollView hosting a real newui::TreeView,
    // same "ScrollView::addChild() redirects into its own viewport" shape Toolbox/DocumentOutline
    // both already establish) - plain newui::TreeController/newui::TreeItem, no custom subclass
    // of either: a name-only row (no per-type icon yet) is all a v1 "which files are in my
    // project" browser needs, same "text-only first" call already made for Toolbox/Document
    // Outline before icons were added to those.
    class DirectoryTree : public newui::ScrollView
    {
    public:
        DirectoryTree();

        void setRootPath(const std::string& path) { model_.setRootPath(path); }
        const std::string& rootPath() const { return model_.rootPath(); }
        void refresh() { model_.refresh(); }

        // Fired when the user double-clicks a file row (never a directory - double-clicking a
        // directory just expands/collapses it, TreeView's own default) - carries the real
        // filesystem path. testharness wires this to load the file into whatever editor is
        // already hosted, the same way its own "Open C++" menu item does.
        typedef newui::Delegate<DirectoryTree, const std::string&> FileActivatedDelegate;
        FileActivatedDelegate onFileActivated;

        // Exposed for testability - same convention Toolbox::treeView()/DocumentOutline::
        // treeView() already use.
        newui::TreeView* treeView() const { return treeView_; }

    private:
        newui::SyncReturn handleTreeDblClick(newui::View& sender, const newui::Point& pt,
            std::uint32_t btnMask, std::uint32_t keyMask);

        DirectoryTreeModel model_;
        newui::TreeView* treeView_ = nullptr;
    };
}
