#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <newui/rootview.h>
#include <newui/subview.h>

namespace CodeToolsVsix
{
    // Copy/paste/duplicate support for the View Designer: a control (and its whole subtree) is
    // serialized with newui's own reflection writer - the same text a saved .newui file holds - and
    // recreated from that text with the reader, so a clone carries every saved property, its
    // LayoutParams and all its children, exactly as reloading it from a file would.
    class DesignerClipboard
    {
    public:
        // The MIME type the designer's copies are stored under on the system clipboard.
        static const wchar_t* mimeType();

        // views minus any whose ancestor is also in views (copying a container already copies its
        // children), in the order given.
        static std::vector<newui::SubView*> topLevelOf(const std::vector<newui::SubView*>& views);

        // One view's text: its runtime class, every saved property and its whole subtree.
        static std::string serialize(newui::SubView& view);

        // A fresh, unattached, design-time-flagged view built from serialize()'s text; nullptr if the
        // text can't be read (or names a class that isn't a SubView).
        static newui::SubView* create(const std::string& text);

        // Every non-internal view in clone's subtree whose name is already taken in root has its
        // name cleared, so attaching it under root generates a fresh unique one ("button2") instead
        // of a duplicate. Internal parts (a TabControl's strip) keep their names - nothing addresses
        // them and the owning control refers to them by role.
        static void uniquifyNames(newui::SubView& clone, newui::RootView& root);

        // The framing that carries several serialized views in one clipboard payload.
        static std::vector<std::uint8_t> pack(const std::vector<std::string>& serializedViews);
        static std::vector<std::string> unpack(const std::vector<std::uint8_t>& payload);  // empty if malformed

        // The system clipboard: copy topLevelOf(views), and read back whatever the designer last
        // copied (empty if the clipboard holds none).
        static bool copyToClipboard(const std::vector<newui::SubView*>& views, newui::View* owner);
        static std::vector<std::string> readClipboard();
    };
}
