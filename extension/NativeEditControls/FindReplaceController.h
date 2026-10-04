#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <newui/controls.h>
#include <newui/displayunit.h>
#include <newui/subview.h>
#include <newui/textfolding.h>

#include "FindEngine.h"
#include "MinimapStrip.h"

namespace CodeToolsVsix
{
    class HighlightController;

    // Where an overlay of size overlay goes next to a caret line inside a host of size host, in the
    // host's coordinates: just below the line (caretTopLeft is the caret's top left, caretHeight the
    // line's height), aligned a little left of the caret's column; above the line if it doesn't fit
    // below; and always kept inside the host (a small edge inset). Never on the caret's own line.
    //
    // metrics resolves the edge/gap insets (DisplayValues in Dip - see FindReplaceController.cpp) to
    // real pixels; defaults to 96 DPI so every existing caller/test that doesn't have a real View's
    // displayMetrics() handy keeps seeing exactly the same numbers as before this was DPI-aware.
    newui::Point overlayPositionBesideCaret(const newui::Point& caretTopLeft, float caretHeight,
        const newui::Size& overlay, const newui::Size& host,
        const newui::DisplayMetrics& metrics = newui::DisplayMetrics::forDpi(newui::kBaselineDpi));

    // Whether overlay hides the point at (x, y) of a line height tall (the caret / the end of a match).
    bool overlayCoversPoint(const newui::Rect& overlay, const newui::Point& point, float lineHeight);

    // Whether overlay hides any part of a width x lineHeight rect at topLeft - a match's whole span,
    // not just one point on it (checking only the caret, which sits at one *end* of a match, can
    // miss overlap with the rest of it).
    bool overlayCoversRange(const newui::Rect& overlay, const newui::Point& topLeft, float width, float lineHeight);

    // A [offset, offset + length) range into the editor's text (the same convention as FindMatch).
    struct RenameRange
    {
        std::size_t offset = 0;
        std::size_t length = 0;
    };

    // What Rename asks: every place in text a rename of the symbol at offset would touch - the real
    // declaration and its references (found with libclang), unlike Replace all's plain text search.
    // Empty when offset isn't on a renamable symbol. May reparse (block) - called only from the
    // Rename button, never on every keystroke like the highlight passes are.
    using RenameProvider = std::function<std::vector<RenameRange>(const std::wstring& text, std::size_t offset)>;

    // Find / Replace and Go to line for a code editor: two overlay views (Resources/findbar.newui and
    // gotoline.newui) added to host, over the editor. See design/find_replace/design-notes.md for the
    // design; what this first version does:
    //   - opens near the caret, seeded from the selection or the identifier under the caret (which also
    //     turns on whole-word, until the user types in the Find field or clicks the lit button);
    //   - highlights every match (HighlightController's "match" layer, the current one stronger) and
    //     selects the current one in the editor; next / previous wrap around;
    //   - Aa (match case), ab (whole word) and the All / Code / Comments / Strings chips narrow matches;
    //   - Replace and Replace all (one undo step), Go to line (line or line:column).
    // The controls for what isn't built yet (Rename, the Function region, the per-function counts) are
    // hidden. Esc closes and returns focus to the editor. UI thread only.
    //
    // The overlays are added to host as layout-ignored children (View::setLayoutIgnored), so the host's
    // own layout never moves them - they are placed by explicit bounds and painted last, on top. text is
    // the editor, highlight (may be null) shows the matches.
    class FindReplaceController
    {
    public:
        // Dip, not Dlu - these are chrome sizing (an overlay's own inset/gap, a fixed-width strip),
        // not text-relative, so they should scale with monitor DPI but not with the theme font - see
        // display-units-plan.md's "Monitor DPI scaling" vs "Font-relative sizing" distinction.
        static constexpr newui::DisplayValue kEdge{6.0f, newui::DisplayUnit::Dip};       // keep an overlay this far inside the host
        static constexpr newui::DisplayValue kCaretGap{6.0f, newui::DisplayUnit::Dip};   // between the caret's line and an overlay next to it
        static constexpr newui::DisplayValue kMinimapWidth{28.0f, newui::DisplayUnit::Dip};

        FindReplaceController(newui::View& host, newui::TextFoldingControl& text, HighlightController* highlight);
        ~FindReplaceController();
        FindReplaceController(const FindReplaceController&) = delete;
        FindReplaceController& operator=(const FindReplaceController&) = delete;

        // Both overlay views loaded (their .newui files were found and are what this expects).
        bool loaded() const { return findBar_ != nullptr && goToBar_ != nullptr; }

        // Ctrl+F / Ctrl+H / Ctrl+G. Opening one closes the other kind.
        void showFind() { openFind(false); }
        void showReplace() { openFind(true); }
        void showGoToLine();
        void close();   // both; Find's highlights go away, the current match stays selected

        bool isFindOpen() const;
        bool isReplaceShown() const { return isFindOpen() && showReplace_; }
        bool isGoToLineOpen() const;
        newui::SubView* findBar() const { return findBar_; }
        newui::SubView* goToBar() const { return goToBar_; }
        // The overview-ruler strip shown alongside Find/Replace (right of the editor's own
        // scrollbar), for tests - null only if construction failed the same way findBar()/goToBar()
        // would be.
        MinimapStrip* minimap() const { return minimap_; }

        // Ticks that are always on the minimap - the editor's problem lines - whether or not Find is open.
        // While there are any the strip stays visible (its own click just moves the caret there); Find's
        // matches are drawn over them while it is open. UI thread.
        void setProblemMarks(std::vector<MinimapStrip::Mark> marks);

        // The search state (also what the controls drive; public for tests and for a host that wants to
        // start a search itself).
        const std::wstring& query() const { return query_; }
        void setQuery(const std::wstring& query);
        void setReplacement(const std::wstring& replacement);
        void setMatchCase(bool value);
        void setWholeWord(bool value);
        void setKind(KindFilter kind);
        const std::vector<FindMatch>& matches() const { return matches_; }
        std::size_t currentIndex() const { return current_; }   // kNoMatch if none
        void next();
        void previous();
        // Replaces the current match with the Replace field's text and moves to the next one; false if
        // there is no current match.
        bool replaceCurrent();
        // Replaces every match as one undo step; how many.
        std::size_t replaceAll();
        // "128" or "128:15" (1-based). False, with the message shown in the bar, for anything else or a
        // line that doesn't exist; true closes the bar and puts the caret there.
        bool goToLine(const std::wstring& input);

        // Enables the Rename button (hidden until this is called) and what it asks when clicked.
        void setRenameProvider(RenameProvider provider);
        // Renames the symbol at the current match (or the caret, if none) to the Replace field's
        // text: every occurrence the provider finds, as one undo step. 0 (and a message in the bar)
        // when there is no provider or no renamable symbol there.
        std::size_t renameCurrent();

    private:
        void openFind(bool replace);
        void closeFind();
        void closeGoTo();
        void wire();
        void search(std::size_t fromOffset);
        void publish(bool select);
        std::size_t caretOffset() const;
        bool wordOn() const { return wholeWord_ || autoWord_; }
        void selectMatch(std::size_t index);
        // The ScrollView the editor is in (nullptr if none).
        newui::ScrollView* scroller() const;
        // Scrolls the editor so the caret's line has room around it: nothing if it is already comfortably
        // inside the view (a few lines of margin either side), else the line is centered - so a jump lands
        // in the middle of the page, not on its very edge (the editor's own scroll-into-view only moves
        // the minimum). Does nothing until the editor has been laid out.
        void revealCaret();
        void applyReplaceVisibility();
        void syncToggles();
        void fit(newui::SubView& bar);
        void place(newui::SubView& bar);
        // While Find is open: if the bar is now on top of the match being shown (the caret follows the
        // matches), move it to the other side of that line. It otherwise stays where it was put.
        void moveClearOfCurrentMatch();
        void focus(newui::SubView* view);
        void setFieldText(newui::TextField* field, const std::wstring& text);
        void selectAllIn(newui::TextField* field);
        void setGoToMessage(const std::string& message);
        void showCurrentLineHint();
        void onKindChip(std::size_t index);
        // Repositions minimap_ (right of the editor's own scrollbar) and refreshes its marks/caret
        // from the current matches_/current_/caret. No-op if construction gave up on it.
        void updateMinimap();

        newui::View* host_;
        newui::TextFoldingControl* text_;
        HighlightController* highlight_;
        newui::SubView* findBar_ = nullptr;   // both owned by host_
        newui::SubView* goToBar_ = nullptr;
        MinimapStrip* minimap_ = nullptr;     // also owned by host_; shown with findBar_, or while problemMarks_ has any
        std::vector<MinimapStrip::Mark> problemMarks_;   // setProblemMarks()

        newui::TextField* findInput_ = nullptr;
        newui::TextField* replaceInput_ = nullptr;
        newui::SubView* replaceRow_ = nullptr;
        newui::ToolbarButton* expandBtn_ = nullptr;
        newui::ToolbarButton* caseBtn_ = nullptr;
        newui::ToolbarButton* wordBtn_ = nullptr;
        newui::ToolbarButton* prevBtn_ = nullptr;
        newui::ToolbarButton* nextBtn_ = nullptr;
        newui::ToolbarButton* kindChips_[4] = {};   // All, Code, Comments, Strings
        newui::Button* replaceBtn_ = nullptr;
        newui::Button* replaceAllBtn_ = nullptr;
        newui::Button* renameBtn_ = nullptr;
        newui::Label* count_ = nullptr;
        newui::TextField* goToInput_ = nullptr;
        newui::Label* goToMessage_ = nullptr;

        std::wstring query_;
        FindOptions options_;
        bool wholeWord_ = false;   // the user's own setting
        bool autoWord_ = false;    // on because the query came from a token under the caret
        bool showReplace_ = false;
        std::vector<FindMatch> matches_;
        std::size_t current_ = kNoMatch;
        int suppress_ = 0;   // >0 while this class sets a control itself, so its handlers ignore the echo
        int busy_ = 0;       // >0 while this class edits the text, so the edit isn't answered with a re-search
        bool moving_ = false;   // moveClearOfCurrentMatch() is running (a move can cause a scroll event)
        RenameProvider renameProvider_;
        std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    };
}
