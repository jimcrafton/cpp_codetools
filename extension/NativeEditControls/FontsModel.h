#pragma once

#include "FontCatalog.h"

#include <cstdint>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace CodeToolsVsix
{
    // What the Fonts surface shows and the choices made on it: the filter, the selected family and which bundled
    // families are ticked "Keep". Plain data over a FontCatalog, so the surface itself only paints it.
    class FontsModel
    {
    public:
        enum class Source { System, Bundled, Live };

        // One row of the sources list: a heading, or a family under it.
        struct Row
        {
            enum class Kind { Heading, Family };
            Kind kind = Kind::Family;
            Source source = Source::System;
            std::string text;
            std::string detail;   // the right-aligned note: a size, "default UI font"
        };

        // A system UI font: what it is called in Windows' settings and the family it resolves to now.
        struct LiveFont
        {
            std::string role;
            std::string family;
        };

        // What the editor and the Designer use today, by family; the rest are not referenced yet.
        static constexpr const char* kUsedByCascadiaMono = "Editor text, inactive-code note";
        static constexpr const char* kUsedByNothing = "nothing yet";

        void setCatalog(FontCatalog catalog);
        const FontCatalog& catalog() const { return catalog_; }
        void setLiveFonts(std::vector<LiveFont> fonts) { live_ = std::move(fonts); }

        // Narrows every list to families matching `text` (name or any face's name, any case).
        void setFilter(std::string text) { filter_ = std::move(text); }
        const std::string& filter() const { return filter_; }

        // Headings and families for the left list: SYSTEM, BUNDLED, LIVE SYSTEM UI.
        std::vector<Row> sourceRows() const;
        // The bundled families that pass the filter, one specimen card each.
        std::vector<const FontFamily*> cards() const;

        void select(Source source, const std::string& name);
        Source selectedSource() const { return selectedSource_; }
        const std::string& selectedName() const { return selectedName_; }
        // The selected family, or nullptr for none or a live font.
        const FontFamily* selectedFamily() const;

        // Keep ticks (bundled families). Cascadia Mono and Fira Code start ticked, the rest not.
        bool kept(const std::string& family) const;
        void setKept(const std::string& family, bool keep);
        std::size_t keptCount() const;
        std::size_t bundledFamilyCount() const;
        std::uint64_t keptBytes() const;
        std::uint64_t bundledBytes() const;

        static const char* usedBy(const std::string& family);
        // The families chosen to keep: Cascadia Mono (the editor's font) and Fira Code.
        static bool keptByDefault(const std::string& family);

        // "N of M families kept" and "X of Y MB - trimming saves Z".
        std::string keptText() const;
        std::string sizeSummary() const;
        // "92 bundled faces" for the header.
        std::string totalText() const;

        // The properties pane: label and value pairs for the selection; empty for none.
        std::vector<std::pair<std::string, std::string>> properties() const;
        // The heading above them: "PROPERTIES - Cascadia Mono".
        std::string propertiesTitle() const;

        // The font to preview: the selected family, or what the selected live font resolves to. Empty for none.
        std::string previewFamily() const;
        // Full names of the selected family's faces ("Cascadia Mono Italic"), each previewed in its own style.
        static constexpr std::size_t kMostPreviewFaces = 8;
        std::vector<std::string> previewFaces() const;

    private:
        FontCatalog catalog_;
        std::vector<LiveFont> live_;
        std::string filter_;
        Source selectedSource_ = Source::Bundled;
        std::string selectedName_;
        std::set<std::string> dropped_;   // bundled families the user unticked
        std::set<std::string> picked_;    // ... and ones ticked that nothing uses
    };
}
