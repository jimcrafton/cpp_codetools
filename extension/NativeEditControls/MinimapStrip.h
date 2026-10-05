#pragma once

#include <newui/color.h>
#include <newui/subview.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace CodeToolsVsix
{
    // A thin vertical strip mapping a WHOLE document (not just what's currently visible) onto its
    // own height: a colored tick per mark, a caret-line marker, and click-to-jump - the "overview
    // ruler" idea most editors have beside their scrollbar. Deliberately generic: a mark is just a
    // line and a color, nothing here knows about Find matches, C++, or any particular caller - so
    // it can be reused for another visualization later without changes (see
    // design/find_replace/design-notes.md, "3. Match minimap": "the strip is meant to be reusable
    // ... mirrored on the left"). UI thread only, like every other View.
    class MinimapStrip : public newui::SubView
    {
    public:
        struct Mark
        {
            std::size_t line = 0;   // 0-based
            newui::Color color;
            bool current = false;   // drawn bigger, outlined - at most one of these expected at a time
        };

        MinimapStrip();

        // How many lines the document has - marks and the caret are positioned as a fraction of
        // this (line 0 at the very top, lines()-1 at the very bottom). Clamped to at least 1.
        void setLineCount(std::size_t lines);
        void setMarks(std::vector<Mark> marks);
        void setCaretLine(std::size_t line);

        // For tests and a host that wants to inspect the current state.
        std::size_t lineCount() const { return lineCount_; }
        std::size_t caretLine() const { return caretLine_; }
        const std::vector<Mark>& marks() const { return marks_; }

        // Fired with the 0-based line a click landed on, proportional to where in the strip it was
        // (not snapped to a mark) - the caller decides what that means: jump to the nearest mark,
        // or just move the caret there if there are none.
        typedef newui::Delegate<MinimapStrip, std::size_t> LineClickedDelegate;
        LineClickedDelegate onLineClicked;

        void paint(BLContext& ctx) override;

    private:
        // Dip, not Dlu - tick/caret sizing is chrome, not text-relative (see
        // display-units-plan.md's "Monitor DPI scaling" vs "Font-relative sizing" distinction).
        static constexpr newui::DisplayValue kTickInset{2.0f, newui::DisplayUnit::Dip};          // an ordinary tick, each side
        static constexpr newui::DisplayValue kTickHeight{3.0f, newui::DisplayUnit::Dip};
        static constexpr newui::DisplayValue kCurrentTickInset{0.5f, newui::DisplayUnit::Dip};   // the current match's tick: nearly full width
        static constexpr newui::DisplayValue kCurrentTickHeight{5.0f, newui::DisplayUnit::Dip};
        static constexpr newui::DisplayValue kCaretWidth{3.0f, newui::DisplayUnit::Dip};
        static constexpr newui::DisplayValue kCaretHeight{6.0f, newui::DisplayUnit::Dip};
        static constexpr std::uint32_t kBackgroundAlpha = 128;   // of 255: the strip's wash over the code text

        newui::SyncReturn handleMouseDown(newui::View& sender, const newui::Point& pt, std::uint32_t btnMask,
            std::uint32_t keyMask);
        float yFor(std::size_t line) const;
        std::size_t lineFor(float y) const;

        std::size_t lineCount_ = 1;
        std::size_t caretLine_ = 0;
        std::vector<Mark> marks_;
    };
}
