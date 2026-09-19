#include "DesignerClipboard.h"

#include <newui/clipboardmgr.h>
#include <newui/reflection.h>
#include <newui/reflectionio.h>

#include <algorithm>
#include <cstring>

namespace CodeToolsVsix
{
    namespace
    {
        constexpr char kMagic[] = "codetools-designer-views/1\n";

        void uniquifyRecursive(newui::SubView& view, newui::RootView& root)
        {
            if (view.isInternal()) {
                return;
            }
            const std::string name = view.name();
            if (!name.empty() && root.nameManager().isTaken(name)) {
                view.setName(std::string());
            }
            for (newui::SubView* child : view.childViews()) {
                uniquifyRecursive(*child, root);
            }
        }
    }

    const wchar_t* DesignerClipboard::mimeType()
    {
        return L"application/x-codetools-designer-views";
    }

    std::vector<newui::SubView*> DesignerClipboard::topLevelOf(const std::vector<newui::SubView*>& views)
    {
        std::vector<newui::SubView*> result;
        for (newui::SubView* view : views) {
            bool ancestorSelected = false;
            for (const newui::View* ancestor = view->parent(); ancestor != nullptr; ancestor = ancestor->parent()) {
                if (std::find(views.begin(), views.end(), ancestor) != views.end()) {
                    ancestorSelected = true;
                    break;
                }
            }
            if (!ancestorSelected && std::find(result.begin(), result.end(), view) == result.end()) {
                result.push_back(view);
            }
        }
        return result;
    }

    std::string DesignerClipboard::serialize(newui::SubView& view)
    {
        const newui::reflection::Class* clazz = newui::reflection::classinfo(typeid(view));
        if (clazz == nullptr) {
            return std::string();
        }
        newui::reflection::ObjectWriter writer;
        clazz->write(&view, &writer, std::string());
        return json5::to_string(writer.doc);
    }

    newui::SubView* DesignerClipboard::create(const std::string& text)
    {
        if (text.empty()) {
            return nullptr;
        }
        newui::reflection::ObjectReader reader;
        if (json5::from_string(text, reader.doc)) {
            return nullptr;   // non-zero: a parse error
        }
        reader.setDesignMode(true);   // the loaded views come back design-time flagged, like a Load
        newui::View* view = reader.readNew<newui::View>();
        auto* sub = dynamic_cast<newui::SubView*>(view);
        if (sub == nullptr && view != nullptr) {
            view->destroy();
            delete view;
        }
        return sub;
    }

    void DesignerClipboard::uniquifyNames(newui::SubView& clone, newui::RootView& root)
    {
        uniquifyRecursive(clone, root);
    }

    std::vector<std::uint8_t> DesignerClipboard::pack(const std::vector<std::string>& serializedViews)
    {
        std::string out = kMagic;
        for (const std::string& view : serializedViews) {
            out += std::to_string(view.size());
            out += '\n';
            out += view;
        }
        return std::vector<std::uint8_t>(out.begin(), out.end());
    }

    std::vector<std::string> DesignerClipboard::unpack(const std::vector<std::uint8_t>& payload)
    {
        const std::size_t magicLength = sizeof(kMagic) - 1;
        std::vector<std::string> views;
        if (payload.size() < magicLength || std::memcmp(payload.data(), kMagic, magicLength) != 0) {
            return views;
        }
        std::size_t pos = magicLength;
        while (pos < payload.size()) {
            std::size_t lineEnd = pos;
            while (lineEnd < payload.size() && payload[lineEnd] != '\n') {
                ++lineEnd;
            }
            if (lineEnd >= payload.size() || lineEnd == pos) {
                return {};
            }
            std::size_t length = 0;
            for (std::size_t i = pos; i < lineEnd; ++i) {
                if (payload[i] < '0' || payload[i] > '9') {
                    return {};
                }
                length = length * 10 + (payload[i] - '0');
            }
            pos = lineEnd + 1;
            if (length > payload.size() - pos) {
                return {};
            }
            views.emplace_back(reinterpret_cast<const char*>(payload.data() + pos), length);
            pos += length;
        }
        return views;
    }

    bool DesignerClipboard::copyToClipboard(const std::vector<newui::SubView*>& views, newui::View* owner)
    {
        std::vector<std::string> serialized;
        for (newui::SubView* view : topLevelOf(views)) {
            std::string text = serialize(*view);
            if (!text.empty()) {
                serialized.push_back(std::move(text));
            }
        }
        if (serialized.empty()) {
            return false;
        }
        return newui::ClipboardManager::setMimeData(mimeType(), pack(serialized), owner);
    }

    std::vector<std::string> DesignerClipboard::readClipboard()
    {
        std::vector<std::uint8_t> payload;
        if (!newui::ClipboardManager::getMimeData(mimeType(), payload)) {
            return {};
        }
        return unpack(payload);
    }
}
