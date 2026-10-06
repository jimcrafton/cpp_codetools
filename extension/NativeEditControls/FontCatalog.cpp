#include "FontCatalog.h"

#include <newui/fontmanager.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <map>

namespace CodeToolsVsix
{
    namespace
    {
        std::string lowered(std::string text)
        {
            for (char& c : text) {
                if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            }
            return text;
        }

        std::string typeOf(const std::string& path)
        {
            const std::string ext = lowered(std::filesystem::path(path).extension().string());
            if (ext == ".ttf") return "TrueType";
            if (ext == ".otf") return "OpenType";
            return "Other";
        }

        // Regular first, then Bold, Italic, Bold Italic, then the rest by name.
        int styleRank(const std::string& style)
        {
            const std::string s = lowered(style);
            if (s.empty() || s == "regular") return 0;
            if (s == "bold") return 1;
            if (s == "italic") return 2;
            if (s == "bold italic") return 3;
            return 4;
        }
    }

    std::uint64_t FontFamily::bytes() const
    {
        std::uint64_t total = 0;
        for (const FontFace& face : faces) total += face.bytes;
        return total;
    }

    FontCatalog FontCatalog::scan()
    {
        std::vector<FontFace> faces;
        for (const newui::SystemFontInfo& info : newui::FontManager::listFonts()) {
            BLFontFace face;
            if (face.create_from_file(info.filePath.c_str(), BL_FILE_READ_MMAP_ENABLED) != BL_SUCCESS) continue;
            FontFace entry;
            entry.fullName = info.name;
            entry.family = face.family_name().data();
            entry.style = face.subfamily_name().data();
            entry.path = info.filePath;
            entry.type = typeOf(info.filePath);
            entry.bundled = info.bundled;
            std::error_code ec;
            const auto size = std::filesystem::file_size(info.filePath, ec);
            entry.bytes = ec ? 0 : static_cast<std::uint64_t>(size);
            if (entry.family.empty()) entry.family = info.name;
            faces.push_back(std::move(entry));
        }
        return fromFaces(std::move(faces));
    }

    FontCatalog FontCatalog::fromFaces(std::vector<FontFace> faces)
    {
        // one entry per (family, source); a file listed twice (the registry names a font more than once) counts once
        std::map<std::pair<bool, std::string>, FontFamily> byFamily;
        for (FontFace& face : faces) {
            FontFamily& family = byFamily[{ face.bundled, lowered(face.family) }];
            if (family.name.empty()) {
                family.name = face.family;
                family.bundled = face.bundled;
            }
            const bool seen = std::any_of(family.faces.begin(), family.faces.end(), [&](const FontFace& other) {
                return lowered(other.path) == lowered(face.path) && other.style == face.style;
            });
            if (!seen) family.faces.push_back(std::move(face));
        }

        FontCatalog catalog;
        for (auto& entry : byFamily) {
            FontFamily& family = entry.second;
            std::stable_sort(family.faces.begin(), family.faces.end(), [](const FontFace& a, const FontFace& b) {
                const int ra = styleRank(a.style);
                const int rb = styleRank(b.style);
                return ra != rb ? ra < rb : lowered(a.style) < lowered(b.style);
            });
            catalog.families_.push_back(std::move(family));
        }
        std::stable_sort(catalog.families_.begin(), catalog.families_.end(), [](const FontFamily& a, const FontFamily& b) {
            if (a.bundled != b.bundled) return !a.bundled;
            return lowered(a.name) < lowered(b.name);
        });
        return catalog;
    }

    std::vector<const FontFamily*> FontCatalog::installed() const
    {
        return matching(std::string(), false);
    }

    std::vector<const FontFamily*> FontCatalog::bundled() const
    {
        return matching(std::string(), true);
    }

    std::size_t FontCatalog::faceCount(bool bundledOnly) const
    {
        std::size_t count = 0;
        for (const FontFamily& family : families_) {
            if (family.bundled == bundledOnly) count += family.faces.size();
        }
        return count;
    }

    std::uint64_t FontCatalog::bytes(bool bundledOnly) const
    {
        std::uint64_t total = 0;
        for (const FontFamily& family : families_) {
            if (family.bundled == bundledOnly) total += family.bytes();
        }
        return total;
    }

    std::vector<const FontFamily*> FontCatalog::matching(const std::string& text, bool bundledOnly) const
    {
        const std::string wanted = lowered(text);
        std::vector<const FontFamily*> found;
        for (const FontFamily& family : families_) {
            if (family.bundled != bundledOnly) continue;
            bool hit = wanted.empty() || lowered(family.name).find(wanted) != std::string::npos;
            for (std::size_t i = 0; !hit && i < family.faces.size(); ++i) {
                hit = lowered(family.faces[i].fullName).find(wanted) != std::string::npos;
            }
            if (hit) found.push_back(&family);
        }
        return found;
    }

    std::string FontCatalog::sizeText(std::uint64_t bytes)
    {
        char buffer[32];
        if (bytes >= 1024ull * 1024ull) {
            std::snprintf(buffer, sizeof buffer, "%.1f MB", double(bytes) / (1024.0 * 1024.0));
        } else if (bytes >= 1024ull) {
            std::snprintf(buffer, sizeof buffer, "%.0f KB", double(bytes) / 1024.0);
        } else {
            std::snprintf(buffer, sizeof buffer, "%llu B", static_cast<unsigned long long>(bytes));
        }
        return buffer;
    }
}
