#include "DirectoryTree.h"

#include <newui/uicolormanager.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <set>

namespace CodeToolsVsix
{
    namespace
    {
        // Extensions cpptools/cppoutline actually parse - matches
        // defaultCompileArgs()'s own "-xc++" assumption (parser.cpp): this is a C++ source/header
        // browser, not a generic file tree.
        bool isSourceFile(const std::filesystem::path& path)
        {
            static const std::set<std::string> kExtensions = {
                ".c", ".cc", ".cpp", ".cxx", ".h", ".hpp", ".hxx", ".inl"
            };
            std::string ext = path.extension().string();
            for (char& c : ext) {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            return kExtensions.count(ext) != 0;
        }

        bool caseInsensitiveNameLess(const DirectoryTreeModel::Entry& a, const DirectoryTreeModel::Entry& b)
        {
            std::string lhs = a.name;
            std::string rhs = b.name;
            for (char& c : lhs) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            for (char& c : rhs) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return lhs < rhs;
        }
    }

    void DirectoryTreeModel::setRootPath(const std::string& path)
    {
        rootPath_ = path;
        root_ = Entry();
        root_.fullPath = path;
        root_.isDirectory = true;
        onChanged(*this);
    }

    void DirectoryTreeModel::refresh()
    {
        root_.children.clear();
        root_.childrenLoaded = false;
        onChanged(*this);
    }

    void DirectoryTreeModel::ensureChildrenLoaded(const Entry& entryConst) const
    {
        Entry& entry = const_cast<Entry&>(entryConst);
        if (entry.childrenLoaded || !entry.isDirectory) {
            return;
        }
        entry.childrenLoaded = true;

        std::vector<Entry> dirs;
        std::vector<Entry> files;

        std::error_code ec;
        auto it = std::filesystem::directory_iterator(entry.fullPath,
            std::filesystem::directory_options::skip_permission_denied, ec);
        if (ec) {
            return;
        }

        for (const auto& dirEntry : it) {
            std::string name = dirEntry.path().filename().string();
            // Skips .git/.vs/.claude/etc - real project noise, never files a user is "working
            // with" in the source-browsing sense this pane exists for.
            if (name.empty() || name.front() == '.') {
                continue;
            }

            std::error_code typeEc;
            bool isDir = dirEntry.is_directory(typeEc);
            if (typeEc) {
                continue;
            }

            Entry child;
            child.name = name;
            child.fullPath = dirEntry.path().string();
            child.isDirectory = isDir;

            if (isDir) {
                dirs.push_back(std::move(child));
            } else if (isSourceFile(dirEntry.path())) {
                files.push_back(std::move(child));
            }
        }

        std::sort(dirs.begin(), dirs.end(), caseInsensitiveNameLess);
        std::sort(files.begin(), files.end(), caseInsensitiveNameLess);

        entry.children = std::move(dirs);
        entry.children.insert(entry.children.end(),
            std::make_move_iterator(files.begin()), std::make_move_iterator(files.end()));
    }

    const DirectoryTreeModel::Entry* DirectoryTreeModel::entryAt(const std::vector<std::size_t>& path) const
    {
        const Entry* current = &root_;
        for (std::size_t index : path) {
            ensureChildrenLoaded(*current);
            if (index >= current->children.size()) {
                return nullptr;
            }
            current = &current->children[index];
        }
        return current;
    }

    std::size_t DirectoryTreeModel::childCount(const std::vector<std::size_t>& path) const
    {
        const Entry* entry = entryAt(path);
        if (entry == nullptr || !entry->isDirectory) {
            return 0;
        }
        ensureChildrenLoaded(*entry);
        return entry->children.size();
    }

    std::any DirectoryTreeModel::value(const std::any& key)
    {
        auto path = std::any_cast<std::vector<std::size_t>>(key);
        const Entry* entry = entryAt(path);
        if (entry == nullptr) {
            return std::string();
        }
        // A directory with no matching children (e.g. one holding only non-C++ files, like
        // bluesky/designer-surface's own *.dc.html mockups) has no expand glyph to mark it as a
        // folder either (TreeItem::paint()'s default only draws one when hasChildren() is true) -
        // a trailing "/" is the only other text-only (no icons yet - see this class's own header
        // comment) way left to tell it apart from a same-named leaf file.
        return entry->isDirectory ? entry->name + "/" : entry->name;
    }

    DirectoryTree::DirectoryTree()
    {
        setVisible(true);
        style().setBackgroundColor(newui::UIColorManager::colorFor(newui::UIColorRole::ControlBackground));

        treeView_ = new newui::TreeView();
        treeView_->setName("directoryTreeView");
        treeView_->setVisible(true);
        treeView_->setController(std::make_unique<newui::TreeController>());
        // Plain TreeController's own default createItem() instantiates defaultItemClassName()
        // via reflection (ItemController::instantiateItem()) - left unset, that's "" and throws
        // std::runtime_error on the very first paint (an uncaught exception unwinding through a
        // WM_PAINT-triggered call stack, which is what the "needs a resize, then a crash" bug
        // actually was). Toolbox/DocumentOutline never hit this because they use their own
        // TreeController subclasses that override createItem() directly instead of going through
        // the reflection pool - same "TreeItem" name ListController::ListController() already
        // sets via "ListItem" for its own plain-item case.
        treeView_->controller().setDefaultItemClassName("TreeItem");
        treeView_->setModel(&model_);
        treeView_->onMouseDblClick.add(this, &DirectoryTree::handleTreeDblClick);

        // ScrollView::addChild() redirects into its own viewport - not a second, separate
        // wrapping layer, this *is* DirectoryTree's whole content (same shape as Toolbox's/
        // DocumentOutline's own constructors).
        addChild(treeView_);
    }

    newui::SyncReturn DirectoryTree::handleTreeDblClick(newui::View& /*sender*/, const newui::Point& /*pt*/,
        std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/)
    {
        auto path = treeView_->selectedPath();
        if (!path) {
            return newui::SyncReturn::Ignored;
        }

        const DirectoryTreeModel::Entry* entry = model_.entryAt(*path);
        if (entry == nullptr || entry->isDirectory) {
            // A directory row's double-click already expands/collapses it (TreeView's own
            // default handling) - nothing further to do here.
            return newui::SyncReturn::Ignored;
        }

        onFileActivated(*this, entry->fullPath);
        return newui::SyncReturn::Handled;
    }
}
