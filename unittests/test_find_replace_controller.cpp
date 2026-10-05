// Tests for FindReplaceController: Find / Replace / Go to line driving the real overlay views
// (Resources/findbar.newui, gotoline.newui) over a real TextFoldingControl. No window: the controller's
// own API and the controls' delegates are exercised, not synthetic mouse/keyboard input.

#include "../extension/NativeEditControls/CppHighlight.h"
#include "../extension/NativeEditControls/FindReplaceController.h"
#include "../extension/NativeEditControls/HighlightController.h"
#include "../extension/NativeEditControls/MinimapStrip.h"

#include <newui/controls.h>
#include <newui/rootview.h>
#include <newui/subview.h>
#include <newui/textfolding.h>
#include <newui/texthistory.h>

#include <gtest/gtest.h>

#include <memory>
#include <string>

using CodeToolsVsix::FindReplaceController;
using CodeToolsVsix::HighlightController;
using CodeToolsVsix::KindFilter;
using CodeToolsVsix::kNoMatch;
using CodeToolsVsix::MinimapStrip;
using CodeToolsVsix::RenameRange;

namespace
{
    // A root with a layout-free host holding the editor, the way CppEditor will hold it.
    struct Fixture
    {
        newui::RootView root{ nullptr, newui::Rect(0, 0, 900, 600), "findTest" };
        newui::SubView* host = new newui::SubView();
        newui::TextFoldingControl* text = new newui::TextFoldingControl();
        std::unique_ptr<HighlightController> highlight;
        std::unique_ptr<FindReplaceController> find;

        explicit Fixture(const std::wstring& document)
        {
            root.addChild(host);
            host->setVisible(true);
            host->setBounds(newui::Rect(0, 0, 900, 600));
            text->setModel(std::make_unique<newui::text::HistoryTextModel>());
            text->setVisible(true);
            text->setBounds(newui::Rect(0, 0, 900, 600));
            host->addChild(text);
            text->setText(document);
            highlight = std::make_unique<HighlightController>(*text, &CodeToolsVsix::analyzeCpp);
            find = std::make_unique<FindReplaceController>(*host, *text, highlight.get());
        }

        ~Fixture()
        {
            find.reset();
            highlight.reset();
            root.destroy();
        }

        void caretAt(std::size_t offset)
        {
            text->selection().clear();
            text->caret().setPosition(newui::text::TextPosition(offset));
        }

        template <typename T>
        T* control(newui::SubView* bar, const char* name)
        {
            return dynamic_cast<T*>(bar->findView(name));
        }

        std::string countLabel()
        {
            return control<newui::Label>(find->findBar(), "count")->text();
        }

        std::size_t styledCount(const char* style)
        {
            std::size_t n = 0;
            for (const auto& range : text->styledRanges()) {
                if (range.style == style) {
                    ++n;
                }
            }
            return n;
        }
    };
}

TEST(FindReplaceController, LoadsBothOverlaysHiddenAndAddsThemToTheHost)
{
    Fixture f(L"int a;");
    ASSERT_TRUE(f.find->loaded());
    EXPECT_FALSE(f.find->isFindOpen());
    EXPECT_FALSE(f.find->isGoToLineOpen());
    EXPECT_EQ(f.find->findBar()->parent(), f.host);
    EXPECT_EQ(f.find->goToBar()->parent(), f.host);
    // What isn't built yet is hidden, not shown doing nothing.
    EXPECT_FALSE(f.find->findBar()->findView("renameBtn")->isVisible());
    EXPECT_FALSE(f.find->findBar()->findView("regionSeg")->isVisible());
}

TEST(FindReplaceController, FindSeedsFromTheTokenUnderTheCaretWithWholeWord)
{
    Fixture f(L"int path = 1;\nreturn path + pathway;\n");
    f.caretAt(6);   // inside the first "path"
    f.find->showFind();

    EXPECT_TRUE(f.find->isFindOpen());
    EXPECT_EQ(f.find->query(), L"path");
    ASSERT_EQ(f.find->matches().size(), 2u);   // whole word: not "pathway"
    EXPECT_EQ(f.find->currentIndex(), 0u);      // the very token the caret was in
    EXPECT_EQ(f.countLabel(), "1 of 2");
    EXPECT_EQ(f.control<newui::TextField>(f.find->findBar(), "findInput")->text(), L"path");
    ASSERT_EQ(f.text->selection().ranges().size(), 1u);
    EXPECT_EQ(f.text->selection().ranges()[0].start(), 4u);
    EXPECT_EQ(f.text->selection().ranges()[0].length(), 4u);
    EXPECT_TRUE(f.control<newui::ToolbarButton>(f.find->findBar(), "wordBtn")->isChecked());
}

TEST(FindReplaceController, FindSeedsFromASingleLineSelectionWithoutWholeWord)
{
    Fixture f(L"path pathway path");
    f.text->selection().setRange(newui::text::TextRange(0, 4));
    f.find->showFind();

    EXPECT_EQ(f.find->query(), L"path");
    EXPECT_EQ(f.find->matches().size(), 3u);   // a selection is searched as is: "pathway" counts
    EXPECT_FALSE(f.control<newui::ToolbarButton>(f.find->findBar(), "wordBtn")->isChecked());
}

TEST(FindReplaceController, NoTokenUnderTheCaretKeepsTheLastQuery)
{
    Fixture f(L"foo   bar foo");
    f.find->setQuery(L"foo");
    f.find->showFind();
    f.find->close();
    f.caretAt(4);   // in the blank run
    f.find->showFind();
    EXPECT_EQ(f.find->query(), L"foo");
    EXPECT_EQ(f.find->matches().size(), 2u);
}

TEST(FindReplaceController, NextAndPreviousStepThroughMatchesAndWrap)
{
    Fixture f(L"ab ab ab");
    f.find->setQuery(L"ab");
    ASSERT_EQ(f.find->matches().size(), 3u);
    EXPECT_EQ(f.find->currentIndex(), 0u);
    f.find->next();
    EXPECT_EQ(f.find->currentIndex(), 1u);
    f.find->next();
    f.find->next();
    EXPECT_EQ(f.find->currentIndex(), 0u);   // wrapped
    f.find->previous();
    EXPECT_EQ(f.find->currentIndex(), 2u);   // wrapped back
    ASSERT_EQ(f.text->selection().ranges().size(), 1u);
    EXPECT_EQ(f.text->selection().ranges()[0].start(), 6u);   // the editor follows
}

TEST(FindReplaceController, NoMatchesShowsNoResultsAndDisablesTheButtons)
{
    Fixture f(L"abc");
    f.find->showFind();
    f.find->setQuery(L"zzz");
    EXPECT_EQ(f.find->currentIndex(), kNoMatch);
    EXPECT_EQ(f.countLabel(), "No results");
    EXPECT_FALSE(f.control<newui::ToolbarButton>(f.find->findBar(), "nextBtn")->isEnabled());
    EXPECT_FALSE(f.control<newui::Button>(f.find->findBar(), "replaceAllBtn")->isEnabled());
    f.find->setQuery(L"");
    EXPECT_EQ(f.countLabel(), "");
}

TEST(FindReplaceController, MatchCaseWholeWordAndKindNarrowTheMatches)
{
    Fixture f(L"int path; // path\nchar* s = \"Path\"; path");
    f.find->setQuery(L"path");
    EXPECT_EQ(f.find->matches().size(), 4u);
    f.find->setMatchCase(true);
    EXPECT_EQ(f.find->matches().size(), 3u);
    f.find->setMatchCase(false);
    f.find->setKind(KindFilter::Comment);
    EXPECT_EQ(f.find->matches().size(), 1u);
    f.find->setKind(KindFilter::String);
    EXPECT_EQ(f.find->matches().size(), 1u);
    f.find->setKind(KindFilter::Code);
    EXPECT_EQ(f.find->matches().size(), 2u);
    // The chips follow: only the active one is checked.
    EXPECT_TRUE(f.control<newui::ToolbarButton>(f.find->findBar(), "kindCode")->isChecked());
    EXPECT_FALSE(f.control<newui::ToolbarButton>(f.find->findBar(), "kindAll")->isChecked());
    f.find->setKind(KindFilter::All);
    f.find->setWholeWord(true);
    EXPECT_EQ(f.find->matches().size(), 4u);
}

TEST(FindReplaceController, TypingInTheFindFieldChangesTheQueryAndDropsTheAutomaticWholeWord)
{
    Fixture f(L"int path = pathway;");
    f.caretAt(6);
    f.find->showFind();
    ASSERT_EQ(f.find->matches().size(), 1u);   // whole word

    f.control<newui::TextField>(f.find->findBar(), "findInput")->setText(L"path");   // as if typed
    EXPECT_EQ(f.find->matches().size(), 2u);   // "pathway" now counts
    EXPECT_FALSE(f.control<newui::ToolbarButton>(f.find->findBar(), "wordBtn")->isChecked());
}

TEST(FindReplaceController, ReplaceShowsTheReplaceRowPrefilledWithTheToken)
{
    Fixture f(L"int path = 1;");
    f.caretAt(6);
    f.find->showReplace();
    EXPECT_TRUE(f.find->isReplaceShown());
    EXPECT_TRUE(f.find->findBar()->findView("replaceRow")->isVisible());
    EXPECT_EQ(f.control<newui::TextField>(f.find->findBar(), "replaceInput")->text(), L"path");
}

TEST(FindReplaceController, OnlyTheFieldTheUserTypesIntoNextHoldsASelection)
{
    Fixture f(L"int path = 1;");
    f.caretAt(6);
    auto* findField = f.control<newui::TextField>(f.find->findBar(), "findInput");
    auto* replaceField = f.control<newui::TextField>(f.find->findBar(), "replaceInput");

    f.find->showReplace();   // Ctrl+H: the Replace field is the one to type into
    EXPECT_TRUE(findField->selection().isEmpty());
    EXPECT_FALSE(replaceField->selection().isEmpty());

    f.find->showFind();      // Ctrl+F while open: now the Find field, and the Replace one lets go
    EXPECT_FALSE(findField->selection().isEmpty());
    EXPECT_TRUE(replaceField->selection().isEmpty());

    f.find->showReplace();   // and back again
    EXPECT_TRUE(findField->selection().isEmpty());
    EXPECT_FALSE(replaceField->selection().isEmpty());
}

TEST(FindReplaceController, TheReplaceRowGrowsTheBarAndTheChevronTogglesIt)
{
    Fixture f(L"int path = 1;");
    f.caretAt(6);
    f.find->showFind();
    const float without = f.find->findBar()->bounds().size().height;
    EXPECT_FALSE(f.find->isReplaceShown());

    auto* chevron = f.control<newui::ToolbarButton>(f.find->findBar(), "expandBtn");
    chevron->onClick(*chevron);
    EXPECT_TRUE(f.find->isReplaceShown());
    EXPECT_GT(f.find->findBar()->bounds().size().height, without);   // 24px row plus its gap
}

TEST(FindReplaceController, ReplaceCurrentReplacesOneAndMovesOn)
{
    Fixture f(L"a b a b a");
    f.find->showFind();
    f.find->setQuery(L"a");
    f.control<newui::TextField>(f.find->findBar(), "replaceInput")->setText(L"XY");

    EXPECT_TRUE(f.find->replaceCurrent());
    EXPECT_EQ(f.text->text(), L"XY b a b a");
    ASSERT_EQ(f.find->matches().size(), 2u);
    EXPECT_EQ(f.find->currentIndex(), 0u);
    EXPECT_EQ(f.find->matches()[0].start, 5u);   // on to the next one
}

TEST(FindReplaceController, ReplacingWithTextContainingTheQueryDoesNotMatchItselfAgain)
{
    Fixture f(L"a b");
    f.find->showFind();
    f.find->setQuery(L"a");
    f.control<newui::TextField>(f.find->findBar(), "replaceInput")->setText(L"aa");
    EXPECT_TRUE(f.find->replaceCurrent());
    EXPECT_EQ(f.text->text(), L"aa b");
    // The new "aa" still matches, but the search carries on after it (and wraps), it doesn't re-replace it.
    EXPECT_EQ(f.find->matches().size(), 2u);
    EXPECT_EQ(f.find->currentIndex(), 0u);
}

TEST(FindReplaceController, ReplaceAllReplacesEveryMatchAsOneUndoStep)
{
    Fixture f(L"a b a b a");
    f.find->showFind();
    f.find->setQuery(L"a");
    f.control<newui::TextField>(f.find->findBar(), "replaceInput")->setText(L"-");

    EXPECT_EQ(f.find->replaceAll(), 3u);
    EXPECT_EQ(f.text->text(), L"- b - b -");
    EXPECT_TRUE(f.find->matches().empty());

    EXPECT_TRUE(f.text->undo());   // one step undoes them all
    EXPECT_EQ(f.text->text(), L"a b a b a");
}

TEST(FindReplaceController, ReplaceOnlyTouchesMatchesThatPassTheFilters)
{
    Fixture f(L"int path; // path");
    f.find->showFind();
    f.find->setQuery(L"path");
    f.find->setKind(KindFilter::Code);
    f.control<newui::TextField>(f.find->findBar(), "replaceInput")->setText(L"p");
    EXPECT_EQ(f.find->replaceAll(), 1u);
    EXPECT_EQ(f.text->text(), L"int p; // path");   // the comment is left alone
}

TEST(FindReplaceController, RenameButtonIsHiddenUntilAProviderIsSet)
{
    Fixture f(L"int count;");
    EXPECT_FALSE(f.control<newui::Button>(f.find->findBar(), "renameBtn")->isVisible());
    f.find->setRenameProvider([](const std::wstring&, std::size_t) { return std::vector<RenameRange>(); });
    EXPECT_TRUE(f.control<newui::Button>(f.find->findBar(), "renameBtn")->isVisible());
}

TEST(FindReplaceController, RenameCurrentReplacesEveryOccurrenceTheProviderFindsAsOneUndoStep)
{
    Fixture f(L"int count = 0; int total = count + count;");
    f.find->setRenameProvider([](const std::wstring& text, std::size_t) {
        std::vector<RenameRange> ranges;
        std::size_t at = 0;
        while ((at = text.find(L"count", at)) != std::wstring::npos) {
            ranges.push_back({ at, 5u });
            at += 5;
        }
        return ranges;
    });
    f.find->showReplace();
    f.control<newui::TextField>(f.find->findBar(), "replaceInput")->setText(L"n");

    EXPECT_EQ(f.find->renameCurrent(), 3u);
    EXPECT_EQ(f.text->text(), L"int n = 0; int total = n + n;");
    EXPECT_TRUE(f.text->undo());   // one step undoes them all
    EXPECT_EQ(f.text->text(), L"int count = 0; int total = count + count;");
}

TEST(FindReplaceController, RenameCurrentAsksAboutTheCurrentMatchNotJustTheCaret)
{
    Fixture f(L"a a a");
    f.find->showFind();
    f.find->setQuery(L"a");
    f.find->next();   // the current match is now the second "a", at offset 2
    std::size_t seenOffset = kNoMatch;
    f.find->setRenameProvider([&](const std::wstring&, std::size_t offset) {
        seenOffset = offset;
        return std::vector<RenameRange>();
    });
    f.find->renameCurrent();
    EXPECT_EQ(seenOffset, 2u);
}

TEST(FindReplaceController, RenameHighlightsTheNewTextAtItsOwnLengthNotAStaleSubstringOfTheOldName)
{
    // "recount" embeds "count" - a plain re-search for the old query would wrongly re-match that
    // embedded copy, highlighted at the OLD (shorter) name's length, sitting oddly inside the new one.
    Fixture f(L"int count = 0; int total = count + count;");
    f.find->setRenameProvider([](const std::wstring& text, std::size_t) {
        std::vector<RenameRange> ranges;
        std::size_t at = 0;
        while ((at = text.find(L"count", at)) != std::wstring::npos) {
            ranges.push_back({ at, 5u });
            at += 5;
        }
        return ranges;
    });
    f.find->showReplace();
    f.find->setQuery(L"count");
    f.control<newui::TextField>(f.find->findBar(), "replaceInput")->setText(L"recount");

    ASSERT_EQ(f.find->renameCurrent(), 3u);
    EXPECT_EQ(f.text->text(), L"int recount = 0; int total = recount + recount;");
    ASSERT_EQ(f.find->matches().size(), 3u);
    for (const auto& match : f.find->matches()) {
        EXPECT_EQ(match.length, 7u) << "the new name's length, not a leftover 5-char \"count\" inside it";
        EXPECT_EQ(f.text->text().substr(match.start, match.length), L"recount");
    }
    EXPECT_EQ(f.find->query(), L"recount") << "Find now looks for what things were renamed to";
}

TEST(FindReplaceController, RenameCurrentWithNoRenamableSymbolShowsAMessageAndChangesNothing)
{
    Fixture f(L"int count;");
    f.find->showReplace();
    f.find->setRenameProvider([](const std::wstring&, std::size_t) { return std::vector<RenameRange>(); });

    EXPECT_EQ(f.find->renameCurrent(), 0u);
    EXPECT_EQ(f.text->text(), L"int count;");
    EXPECT_EQ(f.countLabel(), "No renamable symbol there");
}

TEST(FindReplaceController, RenameCurrentChangesNothingAndShowsTheCheckersMessageWhenTheNameClashes)
{
    Fixture f(L"int count = 0;");
    f.find->setRenameProvider([](const std::wstring&, std::size_t) { return std::vector<RenameRange>{ { 4u, 5u } }; });
    f.find->setRenameChecker([](const std::wstring&, std::size_t, const std::wstring& name) {
        return name == L"taken" ? std::string("\"taken\" already exists in C") : std::string();
    });
    f.find->showReplace();
    f.control<newui::TextField>(f.find->findBar(), "replaceInput")->setText(L"taken");

    EXPECT_EQ(f.find->renameCurrent(), 0u);
    EXPECT_EQ(f.text->text(), L"int count = 0;");
    EXPECT_EQ(f.countLabel(), "\"taken\" already exists in C");

    f.control<newui::TextField>(f.find->findBar(), "replaceInput")->setText(L"free");
    EXPECT_EQ(f.find->renameCurrent(), 1u);
    EXPECT_EQ(f.text->text(), L"int free = 0;");
}

TEST(FindReplaceController, MatchesAreHighlightedAndTheCurrentOneIsMarked)
{
    Fixture f(L"ab ab ab");
    f.find->showFind();
    f.find->setQuery(L"ab");
    EXPECT_EQ(f.styledCount(CodeToolsVsix::kMatchStyleName), 2u);
    EXPECT_EQ(f.styledCount(CodeToolsVsix::kCurrentMatchStyleName), 1u);

    f.find->close();
    EXPECT_EQ(f.styledCount(CodeToolsVsix::kMatchStyleName), 0u);
    EXPECT_EQ(f.styledCount(CodeToolsVsix::kCurrentMatchStyleName), 0u);
    EXPECT_FALSE(f.find->isFindOpen());
}

TEST(FindReplaceController, EditingTheDocumentUnderAnOpenBarRefreshesTheMatches)
{
    Fixture f(L"ab ab");
    f.find->showFind();
    f.find->setQuery(L"ab");
    ASSERT_EQ(f.find->matches().size(), 2u);
    f.text->model().insert(0, L"ab ");
    EXPECT_EQ(f.find->matches().size(), 3u);
}

TEST(FindReplaceController, GoToLineMovesTheCaretAndClosesTheBar)
{
    Fixture f(L"one\ntwo\nthree");
    f.find->showGoToLine();
    EXPECT_TRUE(f.find->isGoToLineOpen());

    EXPECT_TRUE(f.find->goToLine(L"2"));
    EXPECT_EQ(f.text->caret().position().offset(), 4u);
    EXPECT_FALSE(f.find->isGoToLineOpen());

    EXPECT_TRUE(f.find->goToLine(L"3:2"));
    EXPECT_EQ(f.text->caret().position().offset(), 9u);
    EXPECT_TRUE(f.text->selection().isEmpty());
}

TEST(FindReplaceController, GoToLineRejectsBadInputAndOutOfRangeAndStaysOpen)
{
    Fixture f(L"one\ntwo\nthree");
    f.find->showGoToLine();
    auto* message = f.control<newui::Label>(f.find->goToBar(), "gotoMsg");

    EXPECT_FALSE(f.find->goToLine(L"abc"));
    EXPECT_NE(message->text().find("line number"), std::string::npos);
    EXPECT_FALSE(f.find->goToLine(L"9"));
    EXPECT_NE(message->text().find("out of range"), std::string::npos);
    EXPECT_NE(message->text().find("1-3"), std::string::npos);
    EXPECT_FALSE(f.find->goToLine(L"0"));
    EXPECT_FALSE(f.find->goToLine(L"2:"));
    EXPECT_TRUE(f.find->isGoToLineOpen());
    EXPECT_TRUE(f.find->goToLine(L" 3 : 1 "));   // spaces are fine
}

TEST(FindReplaceController, OpeningOneKindClosesTheOther)
{
    Fixture f(L"one\ntwo");
    f.find->showFind();
    f.find->showGoToLine();
    EXPECT_FALSE(f.find->isFindOpen());
    EXPECT_TRUE(f.find->isGoToLineOpen());
    f.find->showFind();
    EXPECT_TRUE(f.find->isFindOpen());
    EXPECT_FALSE(f.find->isGoToLineOpen());
}

TEST(FindReplaceController, TheBarMovesClearWhenItWouldHideTheCurrentMatchsFullWidth)
{
    std::wstring document;
    for (int i = 0; i < 10; ++i) {
        document += L"// filler line " + std::to_wstring(i) + L"\n";
    }
    document += L"int needleVariable = 1;\n";
    Fixture f(document);
    (void)f.text->contentSize();   // builds the layout hitTestRange() needs

    f.find->showFind();
    f.find->setQuery(L"needleVariable");
    ASSERT_EQ(f.find->matches().size(), 1u);
    const CodeToolsVsix::FindMatch match = f.find->matches()[0];
    const auto rects = f.text->controller().layoutEngine().hitTestRange(newui::text::TextRange(match.start, match.length));
    ASSERT_FALSE(rects.empty());

    // Put the bar squarely on top of the match (as if it had been left there from an earlier,
    // nearby match) and ask it to get out of the way again.
    f.find->findBar()->setBounds(newui::Rect(rects[0].left() - 20.0f, rects[0].top() - 10.0f, 300.0f, 120.0f));
    f.find->next();   // re-selects the (only) match, running moveClearOfCurrentMatch() again

    const newui::Rect after = f.find->findBar()->bounds();
    EXPECT_FALSE(CodeToolsVsix::overlayCoversRange(after, rects[0].pos(), rects[0].size().width, rects[0].size().height))
        << "bar now at " << after.left() << "," << after.top();
}

// ---- placement (pure): where an overlay goes next to the caret ----------------------------------------------

TEST(OverlayPlacement, GoesJustBelowTheCaretLineAlignedToTheCaretColumn)
{
    const newui::Point at = CodeToolsVsix::overlayPositionBesideCaret(
        newui::Point(200, 100), 20, newui::Size(420, 100), newui::Size(900, 600));
    EXPECT_FLOAT_EQ(at.x, 176.0f);   // 24 left of the caret
    EXPECT_FLOAT_EQ(at.y, 126.0f);   // line bottom (120) + 6
}

TEST(OverlayPlacement, GoesAboveTheLineWhenThereIsNoRoomBelow)
{
    const newui::Point at = CodeToolsVsix::overlayPositionBesideCaret(
        newui::Point(200, 560), 20, newui::Size(420, 100), newui::Size(900, 600));
    EXPECT_FLOAT_EQ(at.y, 454.0f);   // caret top (560) - height (100) - 6
    EXPECT_LE(at.y + 100.0f, 560.0f);   // clear of the caret's line
}

TEST(OverlayPlacement, StaysInsideTheHostHorizontally)
{
    const newui::Size overlay(420, 100), host(900, 600);
    EXPECT_FLOAT_EQ(CodeToolsVsix::overlayPositionBesideCaret(newui::Point(10, 100), 20, overlay, host).x, 6.0f);
    EXPECT_FLOAT_EQ(CodeToolsVsix::overlayPositionBesideCaret(newui::Point(880, 100), 20, overlay, host).x, 900.0f - 420.0f - 6.0f);
}

TEST(OverlayPlacement, AHostSmallerThanTheOverlayPutsItAtTheEdgeInsetInsteadOfOffScreen)
{
    const newui::Point at = CodeToolsVsix::overlayPositionBesideCaret(
        newui::Point(50, 50), 20, newui::Size(420, 100), newui::Size(300, 80));
    EXPECT_FLOAT_EQ(at.x, 6.0f);
    EXPECT_FLOAT_EQ(at.y, 6.0f);
}

TEST(OverlayPlacement, CoversPointSaysWhetherTheBarHidesTheCaretsLine)
{
    const newui::Rect bar(100, 100, 420, 100);   // x 100..520, y 100..200
    EXPECT_TRUE(CodeToolsVsix::overlayCoversPoint(bar, newui::Point(150, 150), 20));
    EXPECT_FALSE(CodeToolsVsix::overlayCoversPoint(bar, newui::Point(50, 150), 20));    // left of it
    EXPECT_FALSE(CodeToolsVsix::overlayCoversPoint(bar, newui::Point(600, 150), 20));   // right of it
    EXPECT_TRUE(CodeToolsVsix::overlayCoversPoint(bar, newui::Point(150, 90), 20));     // the line overlaps its top edge
    EXPECT_FALSE(CodeToolsVsix::overlayCoversPoint(bar, newui::Point(150, 60), 20));    // wholly above
    EXPECT_FALSE(CodeToolsVsix::overlayCoversPoint(bar, newui::Point(150, 200), 20));   // starts at its bottom edge
}

TEST(OverlayPlacement, CoversRangeCatchesOverlapTheCaretAlonePointWouldMiss)
{
    const newui::Rect bar(100, 100, 420, 100);   // x 100..520, y 100..200
    // A match starting well inside the bar but ending past its right edge (the caret sits at that
    // far end): checking only the caret point (moveClearOfCurrentMatch() used to) would miss this.
    EXPECT_TRUE(CodeToolsVsix::overlayCoversRange(bar, newui::Point(480, 150), 140, 20));
    EXPECT_FALSE(CodeToolsVsix::overlayCoversPoint(bar, newui::Point(620, 150), 20)) << "the caret alone looks clear";

    EXPECT_FALSE(CodeToolsVsix::overlayCoversRange(bar, newui::Point(600, 150), 50, 20)) << "wholly to the right";
    EXPECT_FALSE(CodeToolsVsix::overlayCoversRange(bar, newui::Point(0, 150), 50, 20)) << "wholly to the left";
    EXPECT_TRUE(CodeToolsVsix::overlayCoversRange(bar, newui::Point(150, 90), 30, 20)) << "overlaps the top edge";
    EXPECT_FALSE(CodeToolsVsix::overlayCoversRange(bar, newui::Point(150, 60), 20, 20)) << "wholly above";
}

TEST(FindReplaceController, TheOverlaysOpenInsideTheHost)
{
    Fixture f(L"one\ntwo");
    f.find->showFind();
    const newui::Rect bar = f.find->findBar()->bounds();
    EXPECT_GE(bar.left(), 0.0f);
    EXPECT_GE(bar.top(), 0.0f);
    EXPECT_LE(bar.left() + bar.size().width, 900.0f);
    EXPECT_LE(bar.top() + bar.size().height, 600.0f);
    EXPECT_FLOAT_EQ(bar.size().width, 420.0f);

    f.find->showGoToLine();
    const newui::Rect goTo = f.find->goToBar()->bounds();
    EXPECT_LE(goTo.left() + goTo.size().width, 900.0f);
    EXPECT_GT(goTo.size().height, 40.0f);
}

// ---- the minimap strip: shown alongside Find/Replace, hidden with it ---------------------------

TEST(FindReplaceController, MinimapShowsWithFindAndReplaceAndHidesWhenClosed)
{
    Fixture f(L"one\ntwo\nthree\n");
    ASSERT_NE(f.find->minimap(), nullptr);
    EXPECT_FALSE(f.find->minimap()->isVisible());

    f.find->showFind();
    EXPECT_TRUE(f.find->minimap()->isVisible());
    f.find->close();
    EXPECT_FALSE(f.find->minimap()->isVisible());

    f.find->showReplace();
    EXPECT_TRUE(f.find->minimap()->isVisible());

    // Go to line is mutually exclusive with Find - the minimap goes with it.
    f.find->showGoToLine();
    EXPECT_FALSE(f.find->minimap()->isVisible());
}

TEST(FindReplaceController, MinimapReflectsLineCountMatchLinesKindColorsAndTheCurrentOne)
{
    Fixture f(L"int path = 1;\n// path\n\"path\"\nreturn path;\n");
    f.find->showFind();
    f.find->setQuery(L"path");
    ASSERT_EQ(f.find->matches().size(), 4u);

    ASSERT_NE(f.find->minimap(), nullptr);
    EXPECT_EQ(f.find->minimap()->lineCount(), 5u);   // 4 lines of text plus the trailing blank one
    const auto& marks = f.find->minimap()->marks();
    ASSERT_EQ(marks.size(), 4u);
    EXPECT_EQ(marks[0].line, 0u);   // "int path = 1;"
    EXPECT_EQ(marks[1].line, 1u);   // "// path"
    EXPECT_EQ(marks[2].line, 2u);   // "\"path\""
    EXPECT_EQ(marks[3].line, 3u);   // "return path;"
    EXPECT_TRUE(marks[0].current);   // the search starts from the caret, landing on the first match
    EXPECT_FALSE(marks[1].current);
    EXPECT_FALSE(marks[2].current);
    EXPECT_FALSE(marks[3].current);
    // Comment and string matches get their own colors, distinct from a plain code match's.
    EXPECT_FALSE(marks[0].color.toBLRgba32() == marks[1].color.toBLRgba32()) << "code vs. comment";
    EXPECT_FALSE(marks[0].color.toBLRgba32() == marks[2].color.toBLRgba32()) << "code vs. string";
    EXPECT_TRUE(marks[0].color.toBLRgba32() == marks[3].color.toBLRgba32()) << "code vs. code";

    f.find->next();
    EXPECT_TRUE(f.find->minimap()->marks()[1].current);
    EXPECT_FALSE(f.find->minimap()->marks()[0].current);
}

TEST(FindReplaceController, MinimapCaretLineFollowsTheCaretEvenWithNoMatches)
{
    Fixture f(L"line one\nline two\nline three\n");
    f.find->showFind();
    f.find->setQuery(L"nothing matches this");
    EXPECT_TRUE(f.find->matches().empty());
    EXPECT_EQ(f.find->minimap()->caretLine(), 0u);

    ASSERT_TRUE(f.find->goToLine(L"3"));
    f.find->showFind();   // re-open: goToLine() closed it
    f.find->setQuery(L"nothing matches this");
    EXPECT_EQ(f.find->minimap()->caretLine(), 2u);
}

TEST(FindReplaceController, ClickingTheMinimapJumpsToTheNearestMatchOrMovesTheCaretWithNone)
{
    Fixture f(L"a\nb\na\nb\na\n");
    f.find->showFind();
    f.find->setQuery(L"a");
    ASSERT_EQ(f.find->matches().size(), 3u);   // lines 0, 2, 4

    f.find->minimap()->onLineClicked(*f.find->minimap(), 4);   // exactly on the last match's line
    EXPECT_EQ(f.find->currentIndex(), 2u);
    f.find->minimap()->onLineClicked(*f.find->minimap(), 0);   // exactly on the first match's line
    EXPECT_EQ(f.find->currentIndex(), 0u);

    f.find->setQuery(L"nothing matches this");
    ASSERT_TRUE(f.find->matches().empty());
    f.find->minimap()->onLineClicked(*f.find->minimap(), 1);   // no matches: just moves the caret there
    EXPECT_EQ(f.text->caret().position().offset(), 2u);   // start of line 2 ("b\n...")
}
