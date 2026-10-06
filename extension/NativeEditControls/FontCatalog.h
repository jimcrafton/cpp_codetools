#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace CodeToolsVsix
{
    // One font file as FontManager knows it.
    struct FontFace
    {
        std::string fullName;     // "Cascadia Mono Italic"
        std::string family;       // "Cascadia Mono"
        std::string style;        // "Italic"; empty when the file names none
        std::string path;
        std::string type;         // "TrueType", "OpenType" or "Other", from the extension
        bool bundled = false;     // registered from Resources/Fonts, not installed in Windows
        std::uint64_t bytes = 0;  // size of the file
    };

    // The faces of one family from one source, in style order.
    struct FontFamily
    {
        std::string name;
        bool bundled = false;
        std::vector<FontFace> faces;

        std::uint64_t bytes() const;
    };

    // Every font FontManager can load, grouped by family, split by where it came from: what is installed in
    // Windows, and what an application registered (Resources/Fonts). The Fonts surface lists this; it is plain
    // data, so it can be read on any thread and compared in a test.
    class FontCatalog
    {
    public:
        // Reads FontManager::listFonts() and each file's names and size. Opens every file (memory-mapped).
        static FontCatalog scan();

        // Builds a catalog from faces already read (a test, or a filtered copy).
        static FontCatalog fromFaces(std::vector<FontFace> faces);

        // Families sorted by name, system ones first and then bundled.
        const std::vector<FontFamily>& families() const { return families_; }
        std::vector<const FontFamily*> installed() const;
        std::vector<const FontFamily*> bundled() const;

        std::size_t faceCount(bool bundledOnly = false) const;
        std::uint64_t bytes(bool bundledOnly = false) const;

        // Families whose name or any face's full name contains `text` (any case); all of them for empty text.
        std::vector<const FontFamily*> matching(const std::string& text, bool bundledOnly) const;

        // "12 MB", "340 KB".
        static std::string sizeText(std::uint64_t bytes);

    private:
        std::vector<FontFamily> families_;
    };
}
