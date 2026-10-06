#include "FontsModel.h"

namespace CodeToolsVsix
{
    void FontsModel::setCatalog(FontCatalog catalog)
    {
        catalog_ = std::move(catalog);
        if (selectedName_.empty()) {
            const std::vector<const FontFamily*> bundled = catalog_.bundled();
            if (!bundled.empty()) {
                selectedSource_ = Source::Bundled;
                selectedName_ = bundled.front()->name;
            }
        }
    }

    std::vector<FontsModel::Row> FontsModel::sourceRows() const
    {
        std::vector<Row> rows;
        auto heading = [&rows](Source source, const char* text, const char* note) {
            Row row;
            row.kind = Row::Kind::Heading;
            row.source = source;
            row.text = text;
            row.detail = note;
            rows.push_back(row);
        };
        auto families = [&](Source source, bool bundled) {
            for (const FontFamily* family : catalog_.matching(filter_, bundled)) {
                Row row;
                row.source = source;
                row.text = family->name;
                row.detail = FontCatalog::sizeText(family->bytes());
                rows.push_back(row);
            }
        };

        heading(Source::System, "SYSTEM", "installed in Windows");
        families(Source::System, false);
        heading(Source::Bundled, "BUNDLED", "Resources\\Fonts");
        families(Source::Bundled, true);
        heading(Source::Live, "LIVE SYSTEM UI", "tracks the theme");
        for (const LiveFont& font : live_) {
            Row row;
            row.source = Source::Live;
            row.text = font.role;
            row.detail = font.family;
            rows.push_back(row);
        }
        return rows;
    }

    std::vector<const FontFamily*> FontsModel::cards() const
    {
        return catalog_.matching(filter_, true);
    }

    void FontsModel::select(Source source, const std::string& name)
    {
        selectedSource_ = source;
        selectedName_ = name;
    }

    const FontFamily* FontsModel::selectedFamily() const
    {
        if (selectedSource_ == Source::Live) return nullptr;
        for (const FontFamily& family : catalog_.families()) {
            if (family.bundled == (selectedSource_ == Source::Bundled) && family.name == selectedName_) return &family;
        }
        return nullptr;
    }

    const char* FontsModel::usedBy(const std::string& family)
    {
        return family == "Cascadia Mono" ? kUsedByCascadiaMono : kUsedByNothing;
    }

    bool FontsModel::keptByDefault(const std::string& family)
    {
        return family == "Cascadia Mono" || family == "Fira Code";
    }

    bool FontsModel::kept(const std::string& family) const
    {
        if (dropped_.count(family) != 0) return false;
        if (picked_.count(family) != 0) return true;
        return keptByDefault(family);
    }

    void FontsModel::setKept(const std::string& family, bool keep)
    {
        dropped_.erase(family);
        picked_.erase(family);
        const bool byDefault = keptByDefault(family);
        if (keep && !byDefault) picked_.insert(family);
        if (!keep && byDefault) dropped_.insert(family);
    }

    std::size_t FontsModel::bundledFamilyCount() const
    {
        return catalog_.bundled().size();
    }

    std::size_t FontsModel::keptCount() const
    {
        std::size_t count = 0;
        for (const FontFamily* family : catalog_.bundled()) count += kept(family->name) ? 1 : 0;
        return count;
    }

    std::uint64_t FontsModel::bundledBytes() const
    {
        return catalog_.bytes(true);
    }

    std::uint64_t FontsModel::keptBytes() const
    {
        std::uint64_t total = 0;
        for (const FontFamily* family : catalog_.bundled()) {
            if (kept(family->name)) total += family->bytes();
        }
        return total;
    }

    std::string FontsModel::keptText() const
    {
        return std::to_string(keptCount()) + " of " + std::to_string(bundledFamilyCount()) + " families kept";
    }

    std::string FontsModel::sizeSummary() const
    {
        return FontCatalog::sizeText(keptBytes()) + " of " + FontCatalog::sizeText(bundledBytes()) +
               " - trimming saves " + FontCatalog::sizeText(bundledBytes() - keptBytes());
    }

    std::string FontsModel::totalText() const
    {
        return std::to_string(catalog_.faceCount(true)) + " bundled faces";
    }

    std::vector<std::pair<std::string, std::string>> FontsModel::properties() const
    {
        std::vector<std::pair<std::string, std::string>> props;
        if (selectedName_.empty()) return props;

        if (selectedSource_ == Source::Live) {
            for (const LiveFont& font : live_) {
                if (font.role != selectedName_) continue;
                props.emplace_back("Source", "system UI font");
                props.emplace_back("Resolves to", font.family);
            }
            return props;
        }

        const FontFamily* family = selectedFamily();
        if (family == nullptr) return props;
        const bool bundled = selectedSource_ == Source::Bundled;
        props.emplace_back("Source", bundled ? "bundled" : "installed in Windows");
        props.emplace_back("Files", std::to_string(family->faces.size()));
        props.emplace_back("Size on disk", FontCatalog::sizeText(family->bytes()));
        if (bundled) {
            props.emplace_back("Used by", usedBy(family->name));
            props.emplace_back("Registered as", family->name);
        } else if (!family->faces.empty()) {
            props.emplace_back("Type", family->faces.front().type);
        }
        return props;
    }

    std::string FontsModel::previewFamily() const
    {
        if (selectedSource_ == Source::Live) {
            for (const LiveFont& font : live_) {
                if (font.role == selectedName_) return font.family;
            }
            return std::string();
        }
        return selectedFamily() != nullptr ? selectedName_ : std::string();
    }

    std::vector<std::string> FontsModel::previewFaces() const
    {
        std::vector<std::string> names;
        const FontFamily* family = selectedFamily();
        if (family == nullptr) return names;
        for (const FontFace& face : family->faces) {
            if (names.size() >= kMostPreviewFaces) break;
            names.push_back(face.fullName);
        }
        return names;
    }

    std::string FontsModel::propertiesTitle() const
    {
        return selectedName_.empty() ? std::string("PROPERTIES") : "PROPERTIES - " + selectedName_;
    }
}
