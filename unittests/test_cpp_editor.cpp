// Tests for the C++ side of the editors: cppFoldsFor() / analyzeCpp(), the shared HighlightController
// working on a TextFoldingControl, and CppEditor hosting it all in a ScrollView.

#include "../extension/NativeEditControls/CppDiagnostics.h"
#include "../extension/NativeEditControls/CppEditor.h"
#include "../extension/NativeEditControls/CppHighlight.h"
#include "../extension/NativeEditControls/HighlightController.h"
#include "../extension/NativeEditControls/TextEncoding.h"

#include <newui/bundle.h>
#include <newui/clipboardmgr.h>
#include <newui/text.h>
#include <newui/utils.h>
#include <newui/fontmanager.h>
#include <newui/rootview.h>
#include <newui/texthistory.h>
#include <newui/uicolormanager.h>

#include <lex/highlight.h>

#include <dwrite.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

using CodeToolsVsix::analyzeCpp;
using CodeToolsVsix::cppFoldsFor;
using CodeToolsVsix::HighlightController;
using CodeToolsVsix::HighlightResult;
using newui::text::TextFold;

namespace
{
    // What each fold hides, in start order.
    std::vector<std::wstring> hidden(const std::wstring& text, std::vector<TextFold> folds)
    {
        std::sort(folds.begin(), folds.end(), [](const TextFold& a, const TextFold& b) { return a.start < b.start; });
        std::vector<std::wstring> out;
        for (const TextFold& fold : folds) {
            out.push_back(text.substr(fold.start, fold.length));
        }
        return out;
    }

    std::vector<std::wstring> hidden(const std::wstring& text)
    {
        return hidden(text, cppFoldsFor(text));
    }

    newui::Color themeColor(lex::StyleId style)
    {
        const lex::Theme theme = newui::UIColorManager::isDarkMode() ? lex::Theme::dark() : lex::Theme::light();
        const std::uint32_t argb = theme.style(style).foreground;
        return newui::Color(((argb >> 16) & 0xFF) / 255.0f, ((argb >> 8) & 0xFF) / 255.0f,
            (argb & 0xFF) / 255.0f, ((argb >> 24) & 0xFF) / 255.0f);
    }

    const newui::text::TextColorRun* runAt(const std::vector<newui::text::TextColorRun>& runs, std::size_t at)
    {
        for (const newui::text::TextColorRun& run : runs) {
            if (at >= run.start && at < run.start + run.length) {
                return &run;
            }
        }
        return nullptr;
    }
}

// ---- folds -------------------------------------------------------------------------------------

TEST(CppFolds, BracesSpanningLinesFoldWhatIsBetweenThem)
{
    const std::wstring text = L"int main() {\n  if (x) {\n    y();\n  }\n}\n";
    const std::vector<TextFold> folds = cppFoldsFor(text);
    ASSERT_EQ(folds.size(), 2u);
    EXPECT_EQ(hidden(text, folds), (std::vector<std::wstring>{ L"\n  if (x) {\n    y();\n  }\n", L"\n    y();\n  " }));
    for (const TextFold& fold : folds) {
        EXPECT_EQ(fold.placeholder, L"...");
        EXPECT_FALSE(fold.collapsed);
    }
}

TEST(CppFolds, BracesOnOneLineDoNotFold)
{
    EXPECT_TRUE(cppFoldsFor(L"int a[] = {1, 2};\nstruct S { int x; };\nauto f = [] { return 1; };\n").empty());
    EXPECT_TRUE(cppFoldsFor(L"").empty());
}

TEST(CppFolds, MultiLineBlockCommentsFold)
{
    EXPECT_EQ(hidden(L"/* one\n two */\nint x;"), (std::vector<std::wstring>{ L" one\n two " }));
    EXPECT_TRUE(cppFoldsFor(L"/* one line */ int x;").empty());
    EXPECT_EQ(hidden(L"/* open\n never closed"), (std::vector<std::wstring>{ L" open\n never closed" })) << "an unterminated one hides to the end";
}

TEST(CppFolds, PreprocessorGroupsFoldBetweenTheirDirectives)
{
    const std::wstring text = L"#if A\nint a;\n#else\nint b;\n#endif\n";
    const std::vector<TextFold> folds = cppFoldsFor(text);
    EXPECT_EQ(hidden(text, folds), (std::vector<std::wstring>{ L"\nint a;\n", L"\nint b;\n" }));
    for (const TextFold& fold : folds) {
        EXPECT_EQ(fold.placeholder, L" ... ");
    }

    EXPECT_EQ(hidden(L"#if A\na\n#elif B\nb\n#else\nc\n#endif\n").size(), 3u);
    EXPECT_EQ(hidden(L"#if A\n#if B\nx\n#endif\n#endif\n").size(), 2u) << "nested groups";
    EXPECT_EQ(hidden(L"#ifdef X\n#endif\n"), (std::vector<std::wstring>{ L"\n" })) << "an empty group still folds its line break";
    EXPECT_EQ(hidden(L"  #  ifndef X\nint x;\n  # endif\n"), (std::vector<std::wstring>{ L"\nint x;\n  " })) << "indented and spaced directives";
}

TEST(CppFolds, UnmatchedAndHiddenBracesAndDirectivesAreIgnored)
{
    EXPECT_TRUE(cppFoldsFor(L"} {\n").empty());
    EXPECT_TRUE(cppFoldsFor(L"{\n\n").empty()) << "an unclosed brace has nothing to fold";
    EXPECT_TRUE(cppFoldsFor(L"#endif\n#else\n").empty());
    // Braces inside a raw string or a comment aren't braces.
    EXPECT_TRUE(cppFoldsFor(L"const char* s = R\"(\n{\n)\";\n// {\n").empty());
    EXPECT_TRUE(cppFoldsFor(L"\"{\" \"}\"\n'{' '}'\n").empty());
}

TEST(CppFolds, ATextThatIsMostlyGarbageStillYieldsSaneFolds)
{
    // Whatever the lexer makes of it, every fold must lie inside the text.
    const std::wstring text = L"{\n\"open\n{ /*\n}\n#if\n'\n}\nR\"x(\n{\n";
    for (const TextFold& fold : cppFoldsFor(text)) {
        EXPECT_LE(fold.start + fold.length, text.size());
        EXPECT_GT(fold.length, 0u);
    }
}

// ---- the analyzer ------------------------------------------------------------------------------

TEST(CppAnalyze, ColorsSquigglesAndFoldsInOnePass)
{
    const std::wstring text = L"int a = 08;\nvoid f() {\n  return;\n}\n";
    const HighlightResult result = analyzeCpp(text);

    bool keyword = false, problem = false;
    for (const newui::text::TextStyleRange& range : result.ranges) {
        if (range.style == "keyword" && range.start == 0 && range.length == 3) keyword = true;
        if (range.style == CodeToolsVsix::kProblemStyleName && range.start == 8) problem = true;   // the malformed "08"
    }
    EXPECT_TRUE(keyword);
    EXPECT_TRUE(problem);
    EXPECT_TRUE(result.foldsValid);
    ASSERT_EQ(result.folds.size(), 1u);
    EXPECT_EQ(text.substr(result.folds[0].start, result.folds[0].length), L"\n  return;\n");
}

// ---- HighlightController on a TextFoldingControl ------------------------------------------------

TEST(HighlightController, ColorsAndFoldsAppearAndCollapsedFoldsSurviveAnEdit)
{
    newui::TextFoldingControl control;
    control.setModel(std::make_unique<newui::text::HistoryTextModel>());
    HighlightController highlight(control, &analyzeCpp);
    int passes = 0;
    highlight.setOnApplied([&passes](HighlightResult&) { ++passes; });

    const std::wstring text = L"void a() {\n  x();\n}\nvoid b() {\n  y();\n}\n";
    control.setText(text);   // no run loop: analyzed and applied on the spot
    EXPECT_EQ(passes, 1);
    ASSERT_EQ(control.folds().size(), 2u);
    ASSERT_NE(runAt(control.colorRuns(), 0), nullptr);
    EXPECT_EQ(runAt(control.colorRuns(), 0)->color, themeColor(lex::StyleId::Keyword));   // void

    // Collapse the second one; an edit above it re-analyzes, and it is still collapsed.
    std::vector<TextFold> folds = control.folds();
    folds[1].collapsed = true;
    const std::size_t collapsedStart = folds[1].start;
    control.setFolds(folds);
    control.model().insert(0, L"// comment\n");
    EXPECT_EQ(passes, 2);
    ASSERT_EQ(control.folds().size(), 2u);
    EXPECT_FALSE(control.folds()[0].collapsed);
    EXPECT_TRUE(control.folds()[1].collapsed);
    EXPECT_EQ(control.folds()[1].start, collapsedStart + 11);
}

TEST(HighlightController, ATextThatCantBeAnalyzedKeepsTheFoldsItHas)
{
    newui::TextFoldingControl control;
    control.setModel(std::make_unique<newui::text::HistoryTextModel>());
    bool valid = true;
    HighlightController highlight(control, [&valid](const std::wstring& text) {
        HighlightResult result = analyzeCpp(text);
        result.foldsValid = valid;
        if (!valid) {
            result.folds.clear();
        }
        return result;
    });

    control.setText(L"void a() {\n  x();\n}\n");
    ASSERT_EQ(control.folds().size(), 1u);
    valid = false;
    control.model().insert(0, L"//\n");
    ASSERT_EQ(control.folds().size(), 1u) << "kept, and moved along with the edit";
    EXPECT_EQ(control.folds()[0].start, 10u + 3u);
}

// ---- CppEditor ---------------------------------------------------------------------------------

namespace
{
    struct CppFile
    {
        explicit CppFile(const std::string& contents)
            : path(std::filesystem::temp_directory_path() / "cpp_editor_test_sample.cpp")
        {
            std::ofstream out(path, std::ios::binary);
            out << contents;
        }
        ~CppFile() { std::error_code ignored; std::filesystem::remove(path, ignored); }
        std::wstring wide() const { return path.wstring(); }
        std::string read() const
        {
            std::ifstream in(path, std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
        std::filesystem::path path;
    };

    constexpr const char* kSample =
        "#include <vector>\n"
        "int main() {\n"
        "    /* note\n"
        "       more */\n"
        "    return 0;\n"
        "}\n";
}

TEST(CppEditor, HostsTheSourceInAScrollViewAndTheOutlineInAnother)
{
    auto* root = new newui::RootView(nullptr, newui::Rect(0, 0, 900, 700), "cppTestRoot");
    CodeToolsVsix::CppEditor editor(root);
    ASSERT_NE(editor.textControl(), nullptr);
    ASSERT_NE(editor.scrollView(), nullptr);
    ASSERT_NE(editor.outlineControl(), nullptr);

    // The text control sits inside a ScrollView (which may hold it in an inner viewport view), and
    // so does the outline - each in its own.
    auto scrolledBy = [](newui::View* view) {
        for (newui::View* up = view != nullptr ? view->parent() : nullptr; up != nullptr; up = up->parent()) {
            if (auto* scroll = dynamic_cast<newui::ScrollView*>(up)) {
                return scroll;
            }
        }
        return static_cast<newui::ScrollView*>(nullptr);
    };
    EXPECT_EQ(scrolledBy(editor.textControl()), editor.scrollView());
    EXPECT_NE(scrolledBy(editor.outlineControl()), nullptr);
    EXPECT_NE(scrolledBy(editor.outlineControl()), editor.scrollView());
    EXPECT_TRUE(editor.outlineControl()->inputTraits().isReadOnly());
    // Edits are remembered.
    editor.textControl()->setText(L"x");
    editor.textControl()->model().insert(1, L"y");
    EXPECT_TRUE(editor.textControl()->canUndo());
}

namespace {
    // The editor holding a four-line text with squiggles on lines 2 and 4 (1-based), as a finished highlight
    // pass would leave it - the ranges are set by hand so no parse decides what the problems are.
    struct EditorWithProblems
    {
        newui::RootView* root = new newui::RootView(nullptr, newui::Rect(0, 0, 900, 700), "cppTestRoot");
        CodeToolsVsix::CppEditor editor{ root };

        EditorWithProblems()
        {
            editor.textControl()->setText(L"int a;\nint b = ;\nint c;\nint d = e;\n");
            editor.textControl()->setStyledRanges({
                { 15, 1, CodeToolsVsix::kProblemStyleName, "expected expression" },
                { 32, 1, CodeToolsVsix::kWarningStyleName, "unused" } });
            editor.statusBar()->refresh();
        }
        void caretAt(std::size_t offset)
        {
            editor.textControl()->selection().clear();
            editor.textControl()->caret().setPosition(newui::text::TextPosition(offset));
        }
    };
}

TEST(CppEditor, AStatusRowShowsTheCaretsLineAndColumn)
{
    auto* root = new newui::RootView(nullptr, newui::Rect(0, 0, 900, 700), "cppTestRoot");
    CodeToolsVsix::CppEditor editor(root);
    ASSERT_NE(editor.statusBar(), nullptr);
    editor.textControl()->setText(L"int a;\nint b = ;\n");

    editor.textControl()->caret().setPosition(newui::text::TextPosition(0));
    EXPECT_EQ(editor.statusBar()->positionText(), "  Ln 1, Col 1");
    editor.textControl()->caret().setPosition(newui::text::TextPosition(11));   // the ' ' before '='
    EXPECT_EQ(editor.statusBar()->positionText(), "  Ln 2, Col 5");
}

TEST(CppEditor, WithNoProblemsThereIsNoNavigatorAndNoProblemTicks)
{
    auto* root = new newui::RootView(nullptr, newui::Rect(0, 0, 900, 700), "cppTestRoot");
    CodeToolsVsix::CppEditor editor(root);
    editor.textControl()->setText(L"int a;\n");
    editor.statusBar()->refresh();

    EXPECT_FALSE(editor.statusBar()->navigatorVisible());
    EXPECT_EQ(editor.statusBar()->summaryText(), "");
    EXPECT_FALSE(editor.findReplace()->minimap()->isVisible());
}

TEST(CppEditor, TheStatusRowCountsTheProblemsAndSaysWhichOneTheCaretIsOn)
{
    EditorWithProblems fixture;
    CodeToolsVsix::EditorStatusBar& status = *fixture.editor.statusBar();

    ASSERT_EQ(status.problems().size(), 2u);
    EXPECT_TRUE(status.navigatorVisible());
    fixture.caretAt(0);   // line 1: not on a problem
    EXPECT_EQ(status.summaryText(), "2 problems");
    fixture.caretAt(15);  // line 2: the first
    EXPECT_EQ(status.summaryText(), "1 of 2 problems");
    fixture.caretAt(32);  // line 4: the second
    EXPECT_EQ(status.summaryText(), "2 of 2 problems");
}

TEST(CppEditor, NextAndPreviousProblemMoveTheCaretToTheLineWithOneWrapping)
{
    EditorWithProblems fixture;
    CodeToolsVsix::EditorStatusBar& status = *fixture.editor.statusBar();
    auto caretOffset = [&] { return fixture.editor.textControl()->caret().position().offset(); };

    fixture.caretAt(0);
    status.goToNextProblem();
    EXPECT_EQ(caretOffset(), 15u) << "line 2, at the squiggle";
    status.goToNextProblem();
    EXPECT_EQ(caretOffset(), 32u) << "line 4";
    status.goToNextProblem();
    EXPECT_EQ(caretOffset(), 15u) << "wrapped to the first";
    status.goToPreviousProblem();
    EXPECT_EQ(caretOffset(), 32u) << "wrapped back to the last";
    status.goToPreviousProblem();
    EXPECT_EQ(caretOffset(), 15u);
}

TEST(CppEditor, TheMinimapShowsATickPerProblemLineEvenWithFindClosed)
{
    EditorWithProblems fixture;
    CodeToolsVsix::MinimapStrip* minimap = fixture.editor.findReplace()->minimap();
    ASSERT_NE(minimap, nullptr);
    fixture.caretAt(15);

    EXPECT_FALSE(fixture.editor.findReplace()->isFindOpen());
    EXPECT_TRUE(minimap->isVisible());
    ASSERT_EQ(minimap->marks().size(), 2u);
    EXPECT_EQ(minimap->marks()[0].line, 1u);
    EXPECT_EQ(minimap->marks()[1].line, 3u);
    EXPECT_TRUE(minimap->marks()[0].current) << "the caret is on the first";
    EXPECT_FALSE(minimap->marks()[1].current);
}

TEST(CppEditor, FindingKeepsTheProblemTicksAndClosingFindKeepsTheStrip)
{
    EditorWithProblems fixture;
    CodeToolsVsix::FindReplaceController& find = *fixture.editor.findReplace();
    CodeToolsVsix::MinimapStrip* minimap = find.minimap();

    find.showFind();
    find.setQuery(L"int");
    EXPECT_GT(minimap->marks().size(), 2u) << "the matches too, over the problem ticks";
    EXPECT_EQ(minimap->marks()[0].line, 1u) << "the problem ticks come first";

    find.close();
    EXPECT_TRUE(minimap->isVisible()) << "still two problems";
    EXPECT_EQ(minimap->marks().size(), 2u);
}

TEST(CppEditor, TheProblemTicksGoWhenTheProblemsDo)
{
    EditorWithProblems fixture;
    ASSERT_TRUE(fixture.editor.findReplace()->minimap()->isVisible());

    fixture.editor.textControl()->setStyledRanges({});
    fixture.editor.statusBar()->refresh();
    EXPECT_FALSE(fixture.editor.statusBar()->navigatorVisible());
    EXPECT_FALSE(fixture.editor.findReplace()->minimap()->isVisible());
}

TEST(CppEditor, TheSourceDoesNotWrapAndAVeryLongLineMakesItScrollSideways)
{
    auto* root = new newui::RootView(nullptr, newui::Rect(0, 0, 900, 700), "cppTestRoot");
    CodeToolsVsix::CppEditor editor(root);
    ASSERT_NE(editor.textControl(), nullptr);
    ASSERT_NE(editor.scrollView(), nullptr);
    EXPECT_FALSE(editor.textControl()->wordWrap());

    // Short lines fit: nothing to scroll sideways.
    editor.textControl()->setText(L"int x = 1;\nint y = 2;\n");
    editor.scrollView()->updateLayout();
    EXPECT_FALSE(editor.scrollView()->hBar()->isVisible());
    const float oneRow = editor.textControl()->contentSize().height;

    // A line far wider than the pane: still one row, and now there is a horizontal scrollbar.
    editor.textControl()->setText(L"int x = " + std::wstring(600, L'1') + L";\nint y = 2;\n");
    editor.scrollView()->updateLayout();
    EXPECT_TRUE(editor.scrollView()->hBar()->isVisible());
    EXPECT_FLOAT_EQ(editor.textControl()->contentSize().height, oneRow) << "wrapping would have made it taller";
}

TEST(CppEditor, LoadingAFileShowsItHighlightedAndFoldableWithAnOutline)
{
    CppFile file(kSample);
    auto* root = new newui::RootView(nullptr, newui::Rect(0, 0, 900, 700), "cppTestRoot");
    CodeToolsVsix::CppEditor editor(root);
    ASSERT_TRUE(editor.load(file.wide().c_str(), file.wide().size()));

    EXPECT_EQ(CodeToolsVsix::wideToUtf8(editor.textControl()->text()), std::string(kSample));
    EXPECT_FALSE(editor.isDirty());
    EXPECT_FALSE(editor.textControl()->canUndo()) << "loading isn't an edit";

    const std::wstring text = editor.textControl()->text();
    const newui::text::TextColorRun* directive = runAt(editor.textControl()->colorRuns(), 0);
    ASSERT_NE(directive, nullptr);
    EXPECT_EQ(directive->color, themeColor(lex::StyleId::Preprocessor));
    const newui::text::TextColorRun* keyword = runAt(editor.textControl()->colorRuns(), text.find(L"int"));
    ASSERT_NE(keyword, nullptr);
    EXPECT_EQ(keyword->color, themeColor(lex::StyleId::Keyword));
    const newui::text::TextColorRun* comment = runAt(editor.textControl()->colorRuns(), text.find(L"more"));
    ASSERT_NE(comment, nullptr);
    EXPECT_EQ(comment->color, themeColor(lex::StyleId::Comment));

    // The function body and the two-line comment.
    EXPECT_EQ(editor.textControl()->folds().size(), 2u);
    EXPECT_NE(editor.outlineControl()->text().find(L"main"), std::wstring::npos);
}

TEST(CppEditor, EditsMarkItDirtyUndoAndRedoWorkAndSaveWritesTheText)
{
    CppFile file(kSample);
    auto* root = new newui::RootView(nullptr, newui::Rect(0, 0, 900, 700), "cppTestRoot");
    CodeToolsVsix::CppEditor editor(root);
    ASSERT_TRUE(editor.load(file.wide().c_str(), file.wide().size()));

    editor.textControl()->model().insert(0, L"// header\n");
    EXPECT_TRUE(editor.isDirty());
    EXPECT_EQ(editor.textControl()->text().rfind(L"// header\n", 0), 0u);

    EXPECT_TRUE(editor.execCommand(EditorCommand::Undo, 0, nullptr));
    EXPECT_EQ(CodeToolsVsix::wideToUtf8(editor.textControl()->text()), std::string(kSample));
    EXPECT_FALSE(editor.execCommand(EditorCommand::Undo, 0, nullptr)) << "nothing left to undo";
    EXPECT_TRUE(editor.execCommand(EditorCommand::Redo, 0, nullptr));
    EXPECT_EQ(editor.textControl()->text().rfind(L"// header\n", 0), 0u);

    ASSERT_TRUE(editor.save(file.wide().c_str(), file.wide().size()));
    EXPECT_FALSE(editor.isDirty());
    EXPECT_EQ(file.read(), "// header\n" + std::string(kSample));
}

TEST(CppEditor, AnOpenEditorIsTheLiveTextForDocumentEditServiceNotTheDiskCopy)
{
    using namespace CodeToolsVsix;
    CppFile file(kSample);
    auto* root = new newui::RootView(nullptr, newui::Rect(0, 0, 900, 700), "cppTestRoot");
    CppEditor editor(root);
    ASSERT_TRUE(editor.load(file.wide().c_str(), file.wide().size()));

    editor.textControl()->model().insert(0, L"// unsaved\n");  // not on disk

    DocumentSnapshot snap;
    documentEditService().getText(file.path, [&](EditStatus, DocumentSnapshot s) { snap = std::move(s); });
    EXPECT_EQ(snap.text, L"// unsaved\n" + utf8ToWide(kSample));
    EXPECT_EQ(file.read(), std::string(kSample));

    EditStatus status = EditStatus::Rejected;
    documentEditService().applyEdits(file.path, snap.version, { { 0, 0, L"// planned\n" } },
        [&](EditStatus s) { status = s; });
    EXPECT_EQ(status, EditStatus::Ok);
    EXPECT_EQ(editor.textControl()->text(), L"// planned\n// unsaved\n" + utf8ToWide(kSample));
    EXPECT_EQ(file.read(), std::string(kSample)) << "the disk copy is never touched";
}

TEST(CppEditor, EditsFromTheServiceAreOneUndoStepAndBumpTheVersion)
{
    using namespace CodeToolsVsix;
    CppFile file("abc\ndef\n");
    auto* root = new newui::RootView(nullptr, newui::Rect(0, 0, 900, 700), "cppTestRoot");
    CppEditor editor(root);
    ASSERT_TRUE(editor.load(file.wide().c_str(), file.wide().size()));

    const DocumentSnapshot before = editor.snapshot();
    ASSERT_EQ(editor.applyEdits(before.version, { { 0, 0, L"1" }, { 4, 0, L"2" }, { 8, 0, L"3" } }), EditStatus::Ok);
    EXPECT_EQ(editor.textControl()->text(), L"1abc\n2def\n3");
    EXPECT_NE(editor.snapshot().version, before.version);

    EXPECT_TRUE(editor.execCommand(EditorCommand::Undo, 0, nullptr));
    EXPECT_EQ(editor.textControl()->text(), L"abc\ndef\n") << "all three edits undo together";
    EXPECT_FALSE(editor.execCommand(EditorCommand::Undo, 0, nullptr));
}

TEST(CppEditor, ServiceEditOffsetsAreUtf16UnitsSoAnEmojiCountsAsTwo)
{
    using namespace CodeToolsVsix;
    // "é" is one unit, the emoji a surrogate pair (two).
    CppFile file("// \xC3\xA9 \xF0\x9F\x98\x80 end\n");
    auto* root = new newui::RootView(nullptr, newui::Rect(0, 0, 900, 700), "cppTestRoot");
    CppEditor editor(root);
    ASSERT_TRUE(editor.load(file.wide().c_str(), file.wide().size()));

    const DocumentSnapshot snap = editor.snapshot();
    const std::size_t end = snap.text.find(L"end");
    ASSERT_EQ(end, 8u);
    ASSERT_EQ(editor.applyEdits(snap.version, { { end, 3, L"END" } }), EditStatus::Ok);
    EXPECT_EQ(editor.textControl()->text(), L"// \u00E9 \U0001F600 END\n");

    // An offset between the two halves of the emoji is refused and changes nothing.
    const std::wstring current = editor.textControl()->text();
    EXPECT_EQ(editor.applyEdits(editor.snapshot().version, { { 6, 0, L"x" } }), EditStatus::InvalidEdit);
    EXPECT_EQ(editor.textControl()->text(), current);
}

TEST(CppEditor, AStaleVersionAfterTypingIsRefusedAndChangesNothing)
{
    using namespace CodeToolsVsix;
    CppFile file("abc");
    auto* root = new newui::RootView(nullptr, newui::Rect(0, 0, 900, 700), "cppTestRoot");
    CppEditor editor(root);
    ASSERT_TRUE(editor.load(file.wide().c_str(), file.wide().size()));

    const std::uint64_t planned = editor.snapshot().version;
    editor.textControl()->model().insert(0, L"typed ");  // the user types after the plan was made
    EXPECT_EQ(editor.applyEdits(planned, { { 0, 0, L"X" } }), EditStatus::VersionMismatch);
    EXPECT_EQ(editor.textControl()->text(), L"typed abc");
}

TEST(CppEditor, ItStopsBeingReachableThroughTheServiceOnceDestroyed)
{
    using namespace CodeToolsVsix;
    CppFile file("on disk");
    {
        auto* root = new newui::RootView(nullptr, newui::Rect(0, 0, 900, 700), "cppTestRoot");
        CppEditor editor(root);
        ASSERT_TRUE(editor.load(file.wide().c_str(), file.wide().size()));
        editor.textControl()->model().insert(0, L"edited ");

        DocumentSnapshot open;
        documentEditService().getText(file.path, [&](EditStatus, DocumentSnapshot s) { open = std::move(s); });
        EXPECT_EQ(open.text, L"edited on disk");
    }

    DocumentSnapshot afterClose;
    documentEditService().getText(file.path, [&](EditStatus, DocumentSnapshot s) { afterClose = std::move(s); });
    EXPECT_EQ(afterClose.text, L"on disk");
}

TEST(CppEditor, CopyCutAndPasteActOnTheSourceText)
{
    auto* root = new newui::RootView(nullptr, newui::Rect(0, 0, 900, 700), "cppTestRoot");
    CodeToolsVsix::CppEditor editor(root);
    editor.textControl()->setText(L"int value = 1;");

    editor.textControl()->selection().setRange(newui::text::TextRange(4, 5));   // "value"
    ASSERT_TRUE(editor.execCommand(EditorCommand::Copy, 0, nullptr));
    std::wstring onClipboard;
    ASSERT_TRUE(newui::ClipboardManager::getText(onClipboard));
    EXPECT_EQ(onClipboard, L"value");

    ASSERT_TRUE(editor.execCommand(EditorCommand::Cut, 0, nullptr));
    EXPECT_EQ(editor.textControl()->text(), L"int  = 1;");
    ASSERT_TRUE(editor.execCommand(EditorCommand::Paste, 0, nullptr));
    EXPECT_EQ(editor.textControl()->text(), L"int value = 1;");
}

// ---- syntax-error squiggles ----------------------------------------------------------------------

namespace
{
    cpptools::Diagnostic diagnostic(const char* message, const char* category, std::size_t offset, std::size_t rangeEnd = 0,
        cpptools::Severity severity = cpptools::Severity::Error, bool fromMainFile = true)
    {
        cpptools::Diagnostic d;
        d.message = message;
        d.category = category;
        d.location.offset = offset;
        d.rangeEndOffset = rangeEnd;
        d.severity = severity;
        d.fromMainFile = fromMainFile;
        return d;
    }

    std::vector<std::pair<std::size_t, std::size_t>> spans(const std::vector<newui::text::TextStyleRange>& ranges)
    {
        std::vector<std::pair<std::size_t, std::size_t>> out;
        for (const newui::text::TextStyleRange& range : ranges) {
            EXPECT_EQ(range.style, CodeToolsVsix::kProblemStyleName);
            out.emplace_back(range.start, range.length);
        }
        return out;
    }
}

TEST(CppDiagnostics, ByteOffsetsMapToUtf16Offsets)
{
    using CodeToolsVsix::utf8ToWideOffsets;
    const std::wstring ascii = L"int x;";
    EXPECT_EQ(utf8ToWideOffsets("int x;", ascii, { 0, 4, 6, 99 }), (std::vector<std::size_t>{ 0, 4, 6, 6 }));

    // "\u00e9" is two UTF-8 bytes, one UTF-16 unit; U+1F600 is four bytes and a surrogate pair.
    const std::wstring wide = L"\u00e9=\U0001F600;";
    const std::string utf8 = CodeToolsVsix::wideToUtf8(wide);
    ASSERT_EQ(utf8.size(), 2u + 1u + 4u + 1u);
    EXPECT_EQ(utf8ToWideOffsets(utf8, wide, { 0, 2, 3, 7, 8 }), (std::vector<std::size_t>{ 0, 1, 2, 4, 5 }));
    // Given out of order and repeated.
    EXPECT_EQ(utf8ToWideOffsets(utf8, wide, { 7, 2, 7, 0 }), (std::vector<std::size_t>{ 4, 1, 4, 0 }));
}

TEST(CppDiagnostics, ASquigglePointsAtTheTokenOrTheTokenBeforeAGap)
{
    using CodeToolsVsix::diagnosticRanges;
    // "expected ';'" is reported in the gap right after `1`.
    const std::wstring text = L"int x = 1 // note\nint y;\n";
    const std::string utf8 = CodeToolsVsix::wideToUtf8(text);
    EXPECT_EQ(spans(diagnosticRanges(text, utf8, { diagnostic("expected ';'", "Parse Issue", 9) })),
        (std::vector<std::pair<std::size_t, std::size_t>>{ { 8, 1 } })) << "the `1`, not the whitespace";

    // At a token: that token.
    const std::wstring broken = L"int x = ;\n";
    EXPECT_EQ(spans(diagnosticRanges(broken, CodeToolsVsix::wideToUtf8(broken), { diagnostic("expected expression", "Parse Issue", 8) })),
        (std::vector<std::pair<std::size_t, std::size_t>>{ { 8, 1 } }));

    // A diagnostic with a range covers it.
    EXPECT_EQ(spans(diagnosticRanges(text, utf8, { diagnostic("bad", "Lexical or Preprocessor Issue", 4, 9) })),
        (std::vector<std::pair<std::size_t, std::size_t>>{ { 4, 5 } }));

    // At the very end of the text: the last significant token.
    const std::wstring open = L"void f() {\n";
    EXPECT_EQ(spans(diagnosticRanges(open, CodeToolsVsix::wideToUtf8(open), { diagnostic("expected '}'", "Parse Issue", 11) })),
        (std::vector<std::pair<std::size_t, std::size_t>>{ { 9, 1 } }));

    // Two diagnostics on the same spot are one squiggle.
    EXPECT_EQ(diagnosticRanges(open, CodeToolsVsix::wideToUtf8(open), { diagnostic("a", "Parse Issue", 11), diagnostic("b", "Parse Issue", 11) }).size(), 1u);
}

TEST(CppDiagnostics, ADiagnosticsMessageRidesOnItsRangeToBeShownAfterTheLine)
{
    using CodeToolsVsix::diagnosticRanges;
    const std::wstring text = L"int x = ;\n";
    const auto ranges = diagnosticRanges(text, CodeToolsVsix::wideToUtf8(text), { diagnostic("expected expression", "Parse Issue", 8) });

    ASSERT_EQ(ranges.size(), 1u);
    EXPECT_EQ(ranges[0].annotation, "expected expression");
}

TEST(CppDiagnostics, WhenAnErrorAndAWarningShareASpotTheErrorsMessageIsTheOneShown)
{
    using CodeToolsVsix::diagnosticRanges;
    const std::wstring text = L"int x = ;\n";
    const std::string utf8 = CodeToolsVsix::wideToUtf8(text);
    const auto ranges = diagnosticRanges(text, utf8, {
        diagnostic("a warning", "Parse Issue", 8, 0, cpptools::Severity::Warning),
        diagnostic("an error", "Parse Issue", 8) });

    ASSERT_EQ(ranges.size(), 1u);
    EXPECT_EQ(ranges[0].style, CodeToolsVsix::kProblemStyleName);
    EXPECT_EQ(ranges[0].annotation, "an error");
}

TEST(CppDiagnostics, TheProblemAndWarningStylesTintTheirWholeLine)
{
    const auto sheet = CodeToolsVsix::highlightStyleSheet(lex::Theme::light());
    ASSERT_NE(sheet->style(CodeToolsVsix::kProblemStyleName), nullptr);
    ASSERT_NE(sheet->style(CodeToolsVsix::kWarningStyleName), nullptr);
    EXPECT_FALSE(sheet->style(CodeToolsVsix::kProblemStyleName)->lineBackgroundColor().isNull());
    EXPECT_FALSE(sheet->style(CodeToolsVsix::kWarningStyleName)->lineBackgroundColor().isNull());
    // Translucent, so the text and the current-line highlight still read through it.
    EXPECT_LT(sheet->style(CodeToolsVsix::kProblemStyleName)->lineBackgroundColor().a, 0.5f);
}

TEST(CppDiagnostics, OnlySyntaxLevelErrorsInTheMainFileAreShownByDefault)
{
    using CodeToolsVsix::DiagnosticFilter;
    using CodeToolsVsix::diagnosticRanges;
    const std::wstring text = L"#include <x.h>\nint a = b;\nint c = ;\n";
    const std::string utf8 = CodeToolsVsix::wideToUtf8(text);
    const std::vector<cpptools::Diagnostic> all = {
        diagnostic("'x.h' file not found", "Lexical or Preprocessor Issue", 9, 14),
        diagnostic("use of undeclared identifier 'b'", "Semantic Issue", 24),
        diagnostic("expected expression", "Parse Issue", 34),
        diagnostic("unused variable", "Parse Issue", 20, 0, cpptools::Severity::Warning),
        diagnostic("in an include", "Parse Issue", 20, 0, cpptools::Severity::Error, false),
        diagnostic("previous declaration is here", "Parse Issue", 20, 0, cpptools::Severity::Note),
    };
    DiagnosticFilter errorsOnly;
    errorsOnly.warnings = false;
    EXPECT_EQ(spans(diagnosticRanges(text, utf8, all, errorsOnly)), (std::vector<std::pair<std::size_t, std::size_t>>{ { 34, 1 } }));

    DiagnosticFilter everything;
    everything.syntaxOnly = false;
    everything.warnings = false;
    EXPECT_EQ(diagnosticRanges(text, utf8, all, everything).size(), 3u) << "the include, the undeclared name and the syntax error";
    everything.warnings = true;
    EXPECT_EQ(diagnosticRanges(text, utf8, all, everything).size(), 4u) << "and the warning";
}

TEST(CppDiagnostics, ARealParseSquigglesTheSyntaxErrorAndReportsTheOutline)
{
    auto document = std::make_shared<CodeToolsVsix::CppDocument>();
    const std::wstring text = L"int main() {\n  int x = ;\n  return 0;\n}\n";
    const CodeToolsVsix::HighlightOverlay overlay = CodeToolsVsix::analyzeCppDiagnostics(text, document);

    ASSERT_EQ(overlay.ranges.size(), 1u);
    EXPECT_EQ(overlay.ranges[0].style, CodeToolsVsix::kProblemStyleName);
    EXPECT_EQ(overlay.ranges[0].start, text.find(L';'));
    EXPECT_EQ(overlay.ranges[0].length, 1u);
    const auto* outline = std::any_cast<std::wstring>(&overlay.extra);
    ASSERT_NE(outline, nullptr);
    EXPECT_NE(outline->find(L"main"), std::wstring::npos);

    EXPECT_TRUE(CodeToolsVsix::analyzeCppDiagnostics(L"int main() { return 0; }\n", document).ranges.empty());
}

TEST(CppDiagnostics, NonAsciiTextBeforeTheErrorDoesNotShiftTheSquiggle)
{
    auto document = std::make_shared<CodeToolsVsix::CppDocument>();
    const std::wstring text = L"// caf\u00e9 \U0001F600 na\u00efve\nint x = ;\n";
    const CodeToolsVsix::HighlightOverlay overlay = CodeToolsVsix::analyzeCppDiagnostics(text, document);
    ASSERT_EQ(overlay.ranges.size(), 1u);
    EXPECT_EQ(overlay.ranges[0].start, text.rfind(L';'));
}

TEST(CppDiagnostics, AMissingIncludeNeitherHidesSyntaxErrorsNorIsReportedItself)
{
    auto document = std::make_shared<CodeToolsVsix::CppDocument>();
    const std::wstring text = L"#include <no_such_header_anywhere.h>\nstd::vector<int> v;\nint x = ;\n";
    const CodeToolsVsix::HighlightOverlay overlay = CodeToolsVsix::analyzeCppDiagnostics(text, document);
    // Only the real syntax error: not the include, and not "undeclared identifier std".
    ASSERT_EQ(overlay.ranges.size(), 1u);
    EXPECT_EQ(overlay.ranges[0].start, text.rfind(L';'));

    CodeToolsVsix::DiagnosticFilter everything;
    everything.syntaxOnly = false;
    EXPECT_GT(CodeToolsVsix::analyzeCppDiagnostics(text, document, everything).ranges.size(), 1u);
}

TEST(CppDiagnostics, ThePapersOwnExampleIsFlagged)
{
    // What was typed into the editor without any squiggle appearing.
    auto document = std::make_shared<CodeToolsVsix::CppDocument>();
    const std::wstring text = L"namespace lex {fsd fs fsdf\n\nnamespace cpp {\n}\n}\n";
    const CodeToolsVsix::HighlightOverlay overlay = CodeToolsVsix::analyzeCppDiagnostics(text, document);
    EXPECT_FALSE(overlay.ranges.empty());
}

TEST(CppDiagnostics, WarningsGetTheirOwnStyleAndAnErrorOnTheSameSpotWins)
{
    using CodeToolsVsix::diagnosticRanges;
    const std::wstring text = L"#include <x.h> junk more\nint a;\n";
    const std::string utf8 = CodeToolsVsix::wideToUtf8(text);

    // Junk after an #include is reported at the gap before it: the warning covers the junk, to the end of the line.
    auto ranges = diagnosticRanges(text, utf8, { diagnostic("extra tokens at end of #include directive", "Lexical or Preprocessor Issue", 14, 0, cpptools::Severity::Warning) });
    ASSERT_EQ(ranges.size(), 1u);
    EXPECT_EQ(ranges[0].style, CodeToolsVsix::kWarningStyleName);
    EXPECT_EQ(ranges[0].start, 15u);
    EXPECT_EQ(ranges[0].length, 9u);   // "junk more"

    // A warning and an error on the same token: one squiggle, red.
    ranges = diagnosticRanges(text, utf8, {
        diagnostic("a warning", "Parse Issue", 4, 0, cpptools::Severity::Warning),
        diagnostic("an error", "Parse Issue", 4) });
    ASSERT_EQ(ranges.size(), 1u);
    EXPECT_EQ(ranges[0].style, CodeToolsVsix::kProblemStyleName);

    // Semantic warnings (unused variables and the like) stay hidden with the semantic errors.
    EXPECT_TRUE(diagnosticRanges(text, utf8, { diagnostic("unused variable 'a'", "Semantic Issue", 30, 0, cpptools::Severity::Warning) }).empty());
}

TEST(CppDiagnostics, JunkAfterAnIncludeIsAGreenSquiggleOnTheJunkAndTheMissingHeaderIsNotReported)
{
    auto document = std::make_shared<CodeToolsVsix::CppDocument>();
    const std::wstring text = L"#include <no_such_header_at_all.h> zsd;klfjsl;dl\nint a;\n";
    const CodeToolsVsix::HighlightOverlay overlay = CodeToolsVsix::analyzeCppDiagnostics(text, document);
    ASSERT_EQ(overlay.ranges.size(), 1u);
    EXPECT_EQ(overlay.ranges[0].style, CodeToolsVsix::kWarningStyleName);
    EXPECT_EQ(overlay.ranges[0].start, text.find(L"zsd"));
    EXPECT_EQ(overlay.ranges[0].length, std::wstring(L"zsd;klfjsl;dl").size());
}

// ---- shifting ranges through an edit ------------------------------------------------------------

namespace
{
    using Ranges = std::vector<newui::text::TextStyleRange>;

    Ranges shifted(const std::wstring& before, const std::wstring& after, const Ranges& ranges)
    {
        return CodeToolsVsix::shiftRangesThroughEdit(ranges, newui::text::PieceTree(before), newui::text::PieceTree(after));
    }

    newui::text::TextStyleRange range(std::size_t start, std::size_t length)
    {
        return { start, length, "problem" };
    }
}

TEST(ShiftRanges, RangesBeforeAnEditStayAndRangesAfterItMove)
{
    const std::wstring before = L"aaa bbb ccc";
    // Insert 3 characters in the middle.
    Ranges result = shifted(before, L"aaa bbbXYZ ccc", { range(0, 3), range(8, 3) });
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0].start, 0u);
    EXPECT_EQ(result[1].start, 11u);

    // Delete 4 at the start.
    result = shifted(before, L"bbb ccc", { range(4, 3), range(8, 3) });
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0].start, 0u);
    EXPECT_EQ(result[1].start, 4u);
}

TEST(ShiftRanges, ARangeTouchingTheEditIsDropped)
{
    const std::wstring before = L"aaa bbb ccc";
    EXPECT_TRUE(shifted(before, L"aaa bXb ccc", { range(4, 3) }).empty()) << "an edit inside it";
    EXPECT_TRUE(shifted(before, L"aaa  ccc", { range(4, 3) }).empty()) << "an edit that removes it";
    EXPECT_TRUE(shifted(before, L"aaa bbbXX ccc", { range(4, 6) }).empty()) << "an edit in its middle-to-end";
    EXPECT_EQ(shifted(before, before, { range(4, 3) }).size(), 1u) << "no edit, no change";
}

TEST(ShiftRanges, AnInsertRightBeforeOrRightAfterARangeKeepsIt)
{
    const std::wstring before = L"aaa bbb ccc";
    Ranges result = shifted(before, L"// x\naaa bbb ccc", { range(4, 3) });
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].start, 9u);
    result = shifted(before, L"aaa bbb ccc!", { range(4, 3) });
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].start, 4u);
    EXPECT_EQ(result[0].length, 3u);
}

// ---- the two passes together ---------------------------------------------------------------------

namespace
{
    template <typename Fn>
    void onLoop(newui::RunLoop& loop, Fn fn)
    {
        std::promise<void> done;
        loop.post([&]() { fn(); done.set_value(); });
        done.get_future().wait();
    }
}

TEST(HighlightController, ASlowOverlayIsKeptAndMovedAlongWhileTheFastPassRunsAgain)
{
    newui::RunLoop::RunLoopThread loopThread = newui::RunLoop::runThreaded();
    newui::RunLoop& loop = *loopThread.loop;
    loop.waitForStart();

    std::atomic<int> overlayRuns{ 0 };
    std::unique_ptr<newui::TextFoldingControl> control;
    std::unique_ptr<HighlightController> highlight;
    std::atomic<int> colorPasses{ 0 };
    onLoop(loop, [&]() {
        control = std::make_unique<newui::TextFoldingControl>();
        control->setModel(std::make_unique<newui::text::HistoryTextModel>());
        highlight = std::make_unique<HighlightController>(*control, &analyzeCpp);
        highlight->setDelay(std::chrono::milliseconds(5));
        // The "slow" pass: squiggles wherever the text says BAD, after a pause and a slow analysis.
        highlight->setOverlayAnalyzer([&overlayRuns](const std::wstring& text) {
            ++overlayRuns;
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            CodeToolsVsix::HighlightOverlay overlay;
            const std::size_t at = text.find(L"BAD");
            if (at != std::wstring::npos) {
                overlay.ranges.push_back({ at, 3, CodeToolsVsix::kProblemStyleName });
            }
            return overlay;
        }, std::chrono::milliseconds(20));
        highlight->setOnApplied([&colorPasses](HighlightResult&) { ++colorPasses; });
        control->setText(L"int BAD = 1;\n");
    });

    auto squiggleStart = [&]() {
        std::size_t start = static_cast<std::size_t>(-1);
        onLoop(loop, [&]() {
            if (!control->decorations().empty()) {
                start = control->decorations()[0].start;
            }
        });
        return start;
    };

    // Wait for the slow overlay to land.
    std::size_t start = static_cast<std::size_t>(-1);
    for (int i = 0; i < 200 && start == static_cast<std::size_t>(-1); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        start = squiggleStart();
    }
    ASSERT_EQ(start, 4u);

    // Now an edit above it: the fast pass lands well before the slow one, and must not lose the
    // squiggle - it comes back moved along by the length of what was inserted.
    int passesBefore = 0;
    onLoop(loop, [&]() {
        passesBefore = colorPasses;
        control->model().insert(0, L"// note\n");
    });
    bool fastPassLanded = false;
    std::size_t movedStart = static_cast<std::size_t>(-1);
    for (int i = 0; i < 100 && !fastPassLanded; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        onLoop(loop, [&]() { fastPassLanded = colorPasses > passesBefore; });
    }
    ASSERT_TRUE(fastPassLanded);
    movedStart = squiggleStart();
    EXPECT_EQ(movedStart, 4u + 8u) << "still there, shifted by the inserted comment line";

    // (and the slow pass, when it lands, agrees)
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    EXPECT_EQ(squiggleStart(), 4u + 8u);
    EXPECT_GE(overlayRuns.load(), 2);

    onLoop(loop, [&]() { highlight.reset(); control.reset(); });
    loop.quit();
    loopThread.thread.join();
}

// ---- project compile flags ---------------------------------------------------------------------

namespace
{
    // proj/inc/foo.h, proj/src/a.cpp and a proj/build/compile_commands.json giving a.cpp "-I../inc".
    struct Project
    {
        Project()
            : root(std::filesystem::temp_directory_path() / ("cpp_editor_project_" + std::to_string(reinterpret_cast<std::uintptr_t>(this))))
        {
            std::filesystem::remove_all(root);
            write("proj/inc/foo.h", "struct Foo { int x; };\n");
            write("proj/src/a.cpp", "int placeholder;\n");
            std::string dir = (root / "proj" / "build").generic_string();
            std::string source = (root / "proj" / "src" / "a.cpp").generic_string();
            write("proj/build/compile_commands.json",
                "[{\"directory\": \"" + dir + "\", \"file\": \"" + source + "\", "
                "\"arguments\": [\"clang++\", \"-I../inc\", \"-std=c++17\", \"-c\", \"" + source + "\"]}]\n");
        }
        ~Project() { std::error_code ignored; std::filesystem::remove_all(root, ignored); }
        void write(const std::string& relative, const std::string& contents) const
        {
            const std::filesystem::path file = root / relative;
            std::filesystem::create_directories(file.parent_path());
            std::ofstream out(file, std::ios::binary);
            out << contents;
        }
        std::string source() const { return (root / "proj" / "src" / "a.cpp").string(); }
        std::filesystem::path root;
    };

    // Whether some squiggle starts within [at, at + length).
    bool squiggleAt(const std::vector<newui::text::TextStyleRange>& ranges, std::size_t at, std::size_t length = 1)
    {
        return std::any_of(ranges.begin(), ranges.end(), [&](const newui::text::TextStyleRange& range) {
            return range.start >= at && range.start < at + length;
        });
    }
}

TEST(CppProjectFlags, WithAProjectDatabaseSemanticErrorsAndMissingIncludesAreShownAndResolvedIncludesAreNot)
{
    Project project;
    auto document = std::make_shared<CodeToolsVsix::CppDocument>();
    document->setPath(project.source());
    ASSERT_TRUE(document->flags().fromProject());

    const std::wstring text = L"#include \"foo.h\"\n#include \"missing.h\"\nint main() { undeclared_name(); Foo f; return f.x; }\n";
    const CodeToolsVsix::HighlightOverlay overlay = CodeToolsVsix::analyzeCppDiagnostics(text, document);

    EXPECT_TRUE(squiggleAt(overlay.ranges, text.find(L"missing.h") - 1, 11)) << "a real missing include (from the opening quote)";
    EXPECT_TRUE(squiggleAt(overlay.ranges, text.find(L"undeclared_name"), 15)) << "a semantic error";
    EXPECT_FALSE(squiggleAt(overlay.ranges, text.find(L"foo.h"), 5)) << "found through -I../inc";
    EXPECT_FALSE(squiggleAt(overlay.ranges, text.find(L"Foo f"), 3)) << "Foo is declared in foo.h";
    EXPECT_FALSE(squiggleAt(overlay.ranges, text.find(L"f.x"), 3));

    const auto* outline = std::any_cast<std::wstring>(&overlay.extra);
    ASSERT_NE(outline, nullptr);
    EXPECT_NE(outline->find(L"compile_commands.json"), std::wstring::npos) << "the outline says which flags were used";
}

TEST(CppProjectFlags, WithoutOneTheSameTextShowsOnlySyntaxErrors)
{
    auto document = std::make_shared<CodeToolsVsix::CppDocument>();   // no path: the defaults
    const std::wstring text = L"#include \"foo.h\"\n#include \"missing.h\"\nint main() { undeclared_name(); Foo f; return f.x; }\n";
    const CodeToolsVsix::HighlightOverlay overlay = CodeToolsVsix::analyzeCppDiagnostics(text, document);
    EXPECT_TRUE(overlay.ranges.empty()) << "the missing includes and the cascade after them are all filtered";
    const auto* outline = std::any_cast<std::wstring>(&overlay.extra);
    ASSERT_NE(outline, nullptr);
    EXPECT_NE(outline->find(L"defaults"), std::wstring::npos);

    // ...unless asked not to follow project flags and to see everything.
    CodeToolsVsix::DiagnosticFilter everything;
    everything.syntaxOnly = false;
    EXPECT_FALSE(CodeToolsVsix::analyzeCppDiagnostics(text, document, everything).ranges.empty());
}

TEST(CppProjectFlags, SyntaxErrorsStillShowWithProjectFlags)
{
    Project project;
    auto document = std::make_shared<CodeToolsVsix::CppDocument>();
    document->setPath(project.source());
    const std::wstring text = L"#include \"foo.h\"\nint x = ;\n";
    const CodeToolsVsix::HighlightOverlay overlay = CodeToolsVsix::analyzeCppDiagnostics(text, document);
    ASSERT_EQ(overlay.ranges.size(), 1u);
    EXPECT_EQ(overlay.ranges[0].start, text.rfind(L';'));
}

TEST(CppProjectFlags, ABorrowedHeaderGetsTheNearestSourcesFlags)
{
    Project project;
    project.write("proj/src/b.h", "#include \"foo.h\"\ninline Foo make() { return Foo{ 1 }; }\n");
    auto document = std::make_shared<CodeToolsVsix::CppDocument>();
    document->setPath((project.root / "proj" / "src" / "b.h").string());
    const std::wstring text = L"#include \"foo.h\"\ninline Foo make() { return Foo{ 1 }; }\n";
    EXPECT_TRUE(CodeToolsVsix::analyzeCppDiagnostics(text, document).ranges.empty());
}

TEST(CppEditor, LoadingAProjectFileShowsWhichCompileFlagsItUsed)
{
    Project project;
    auto* root = new newui::RootView(nullptr, newui::Rect(0, 0, 900, 700), "cppTestRoot");
    CodeToolsVsix::CppEditor editor(root);
    const std::wstring path = std::filesystem::path(project.source()).wstring();
    ASSERT_TRUE(editor.load(path.c_str(), path.size()));
    EXPECT_NE(editor.outlineControl()->text().find(L"compile_commands.json"), std::wstring::npos);
}

TEST(CppProjectFlags, AHeaderWithPragmaOnceGetsNoSquiggleWithOrWithoutAProject)
{
    const std::wstring text = L"#pragma once\nstruct A { int x; };\n";
    {
        Project project;
        project.write("proj/src/c.h", "#pragma once\n");
        auto document = std::make_shared<CodeToolsVsix::CppDocument>();
        document->setPath((project.root / "proj" / "src" / "c.h").string());
        EXPECT_TRUE(CodeToolsVsix::analyzeCppDiagnostics(text, document).ranges.empty()) << "with the project's flags";
    }
    auto document = std::make_shared<CodeToolsVsix::CppDocument>();
    EXPECT_TRUE(CodeToolsVsix::analyzeCppDiagnostics(text, document).ranges.empty()) << "with the defaults";
}

// ---- the kept translation unit ------------------------------------------------------------------

TEST(CppProjectFlags, EachPassAfterTheFirstReparsesTheKeptTranslationUnit)
{
    Project project;
    auto document = std::make_shared<CodeToolsVsix::CppDocument>();
    document->setPath(project.source());
    const std::wstring text = L"#include \"foo.h\"\nint main() { return 0; }\n";

    CodeToolsVsix::analyzeCppDiagnostics(text, document);
    CodeToolsVsix::analyzeCppDiagnostics(text + L"int more;\n", document);
    const CodeToolsVsix::HighlightOverlay third = CodeToolsVsix::analyzeCppDiagnostics(text + L"int more = ;\n", document);
    EXPECT_EQ(document->session().parseCount(), 1u);
    EXPECT_EQ(document->session().reparseCount(), 2u);
    // ...and the reparse still sees the new text.
    ASSERT_EQ(third.ranges.size(), 1u);
    EXPECT_EQ(third.ranges[0].start, (text + L"int more = ;\n").rfind(L';'));
}

TEST(CppProjectFlags, AnUnsavedBufferAlsoReparsesAndItsStandInFileIsRemoved)
{
    std::string standIn;
    {
        auto document = std::make_shared<CodeToolsVsix::CppDocument>();
        EXPECT_FALSE(document->hasPath());
        CodeToolsVsix::analyzeCppDiagnostics(L"int a;\n", document);
        CodeToolsVsix::analyzeCppDiagnostics(L"int a; int b;\n", document);
        EXPECT_EQ(document->session().parseCount(), 1u);
        EXPECT_EQ(document->session().reparseCount(), 1u);
        standIn = document->path();
        EXPECT_TRUE(std::filesystem::exists(standIn)) << "libclang needs the main file to exist to reuse a preamble";
        EXPECT_EQ(std::filesystem::path(standIn).extension(), ".cpp");
    }
    EXPECT_FALSE(std::filesystem::exists(standIn));
}

TEST(CppProjectFlags, SavingUnderANameStartsTheParseOverForThatFile)
{
    Project project;
    auto document = std::make_shared<CodeToolsVsix::CppDocument>();
    CodeToolsVsix::analyzeCppDiagnostics(L"int a;\n", document);
    document->setPath(project.source());
    CodeToolsVsix::analyzeCppDiagnostics(L"int a;\n", document);
    EXPECT_EQ(document->session().parseCount(), 2u) << "a different file";
    EXPECT_TRUE(document->flags().fromProject());
}

// ---- Find / Replace / Go to line hosted by CppEditor ------------------------------------------------------

TEST(CppEditorFind, TheOverlaysAreLayoutIgnoredChildrenPaintedOnTopOfThePanes)
{
    auto* root = new newui::RootView(nullptr, newui::Rect(0, 0, 900, 700), "cppFindRoot");
    CodeToolsVsix::CppEditor editor(root);
    ASSERT_NE(editor.findReplace(), nullptr);
    ASSERT_TRUE(editor.findReplace()->loaded());

    newui::SubView* findBar = editor.findReplace()->findBar();
    newui::SubView* goToBar = editor.findReplace()->goToBar();
    newui::SubView* minimap = editor.findReplace()->minimap();
    EXPECT_TRUE(findBar->isLayoutIgnored());
    EXPECT_TRUE(goToBar->isLayoutIgnored());
    ASSERT_NE(minimap, nullptr);
    EXPECT_TRUE(minimap->isLayoutIgnored());
    EXPECT_EQ(findBar->parent(), goToBar->parent());
    EXPECT_EQ(findBar->parent(), minimap->parent());

    // Last in the child order = drawn last = on top - of the source/outline panes for all three,
    // and of the minimap specifically for findBar/goToBar (minimap is added first among these
    // three so an open bar always reads on top where the two happen to overlap - a real, reported
    // bug when the minimap was added last and drew over the bar instead).
    const auto& children = findBar->parent()->childViews();
    ASSERT_GE(children.size(), 5u);
    EXPECT_EQ(children[children.size() - 3], minimap);
    EXPECT_EQ(children[children.size() - 2], findBar);
    EXPECT_EQ(children[children.size() - 1], goToBar);
}

TEST(CppEditorFind, AResizeRepositionsAnOpenOverlayAndMinimapAgainstTheNewBounds)
{
    auto* root = new newui::RootView(nullptr, newui::Rect(0, 0, 900, 700), "cppFindRoot");
    CodeToolsVsix::CppEditor editor(root);
    editor.textControl()->setText(L"int path = 1;\nreturn path;\n");
    editor.textControl()->caret().setPosition(newui::text::TextPosition(6));

    ASSERT_TRUE(editor.execCommand(EditorCommand::Find, 0, nullptr));
    newui::SubView* findBar = editor.findReplace()->findBar();
    ASSERT_TRUE(findBar->isVisible());
    const newui::Rect placed = findBar->bounds();
    EXPECT_GT(placed.size().width, 0.0f);

    CodeToolsVsix::MinimapStrip* minimap = editor.findReplace()->minimap();
    ASSERT_TRUE(minimap->isVisible());
    const float placedMinimapRight = minimap->bounds().right();

    // The host re-runs its flex layout on a resize; the overlay/minimap aren't tracked children of
    // that layout themselves, but FindReplaceController's own onSizeChanged handler (on the
    // editor's ScrollView, not the host directly - see wire()'s own comment for why: a handler on
    // the host's onSizeChanged fires too early, before the host's own updateLayout() has resized
    // that ScrollView, leaving a stale viewport - a real, live-found bug this test now guards
    // against) re-places them against the new, much wider bounds.
    root->setBounds(newui::Rect(0, 0, 1900, 800));
    EXPECT_TRUE(findBar->isVisible());
    EXPECT_GT(minimap->bounds().right(), placedMinimapRight);
}

TEST(CppEditorFind, TheFindReplaceAndGotoLineCommandsOpenTheirOverlays)
{
    auto* root = new newui::RootView(nullptr, newui::Rect(0, 0, 900, 700), "cppFindRoot");
    CodeToolsVsix::CppEditor editor(root);
    editor.textControl()->setText(L"int path = 1;\nreturn path;\n");
    editor.textControl()->caret().setPosition(newui::text::TextPosition(6));   // inside "path"
    CodeToolsVsix::FindReplaceController* find = editor.findReplace();

    EXPECT_FALSE(find->isFindOpen());
    EXPECT_TRUE(editor.execCommand(EditorCommand::Find, 0, nullptr));
    EXPECT_TRUE(find->isFindOpen());
    EXPECT_FALSE(find->isReplaceShown());
    EXPECT_EQ(find->query(), L"path");     // seeded from the token under the caret
    EXPECT_EQ(find->matches().size(), 2u);

    EXPECT_TRUE(editor.execCommand(EditorCommand::Replace, 0, nullptr));
    EXPECT_TRUE(find->isReplaceShown());

    EXPECT_TRUE(editor.execCommand(EditorCommand::GotoLine, 0, nullptr));   // no line named: opens the bar
    EXPECT_TRUE(find->isGoToLineOpen());
    EXPECT_FALSE(find->isFindOpen());
}

TEST(CppEditorFind, CommandArgumentsSeedTheSearchAndGoToALineDirectly)
{
    auto* root = new newui::RootView(nullptr, newui::Rect(0, 0, 900, 700), "cppFindRoot");
    CodeToolsVsix::CppEditor editor(root);
    editor.textControl()->setText(L"int path = 1;\nreturn path;\n");
    CodeToolsVsix::FindReplaceController* find = editor.findReplace();

    const wchar_t search[] = L"return";
    const wchar_t replacement[] = L"yield";
    EditorCommandArgs args{ search, 6, replacement, 5, 0 };
    EXPECT_TRUE(editor.execCommand(EditorCommand::Replace, 0, &args));
    EXPECT_EQ(find->query(), L"return");
    EXPECT_EQ(find->matches().size(), 1u);
    EXPECT_TRUE(find->replaceCurrent());
    EXPECT_EQ(editor.textControl()->text(), L"int path = 1;\nyield path;\n");

    EditorCommandArgs line{ nullptr, 0, nullptr, 0, 2 };
    EXPECT_TRUE(editor.execCommand(EditorCommand::GotoLine, 0, &line));
    EXPECT_EQ(editor.textControl()->caret().position().offset(), 14u);   // the start of line 2
    EXPECT_FALSE(find->isGoToLineOpen());

    EditorCommandArgs past{ nullptr, 0, nullptr, 0, 99 };
    EXPECT_FALSE(editor.execCommand(EditorCommand::GotoLine, 0, &past));   // no such line: reported, not moved
}

TEST(CppEditorFind, GoToLineLeavesRoomAroundTheLineInsteadOfLandingOnTheEdgeOfThePage)
{
    auto* root = new newui::RootView(nullptr, newui::Rect(0, 0, 900, 400), "cppFindRoot");
    CodeToolsVsix::CppEditor editor(root);
    std::wstring document;
    for (int i = 1; i <= 300; ++i) {
        document += L"line " + std::to_wstring(i) + L"\n";
    }
    editor.textControl()->setText(document);
    root->setBounds(newui::Rect(0, 0, 900, 401));   // re-runs the layout with the text in place
    (void)editor.textControl()->contentSize();      // builds the text layout (what a paint would)
    editor.scrollView()->updateLayout();
    newui::ScrollBar* bar = editor.scrollView()->vBar();
    ASSERT_TRUE(bar->isVisible());
    ASSERT_GT(bar->pageSize(), 0.0f);

    auto lineMiddle = [&]() {
        const newui::Rect caret = editor.textControl()->controller().caretDocumentRect();
        return caret.top() + caret.size().height * 0.5f;
    };

    // A far jump puts the line in the middle of the page, not on its edge.
    ASSERT_TRUE(editor.findReplace()->goToLine(L"150"));
    const newui::Rect caret = editor.textControl()->controller().caretDocumentRect();
    ASSERT_GT(caret.size().height, 0.0f);
    EXPECT_NEAR(lineMiddle(), bar->value() + bar->pageSize() * 0.5f, caret.size().height * 1.5f);

    // A line already comfortably in view leaves the page alone.
    const float before = bar->value();
    ASSERT_TRUE(editor.findReplace()->goToLine(L"152"));
    EXPECT_FLOAT_EQ(bar->value(), before);

    // A line right at the bottom edge is not comfortable: it is brought toward the middle.
    bar->setValue(lineMiddle() + caret.size().height - bar->pageSize());   // line 152's bottom now at the very edge
    ASSERT_TRUE(editor.findReplace()->goToLine(L"152"));
    EXPECT_NEAR(lineMiddle(), bar->value() + bar->pageSize() * 0.5f, caret.size().height * 1.5f);
}

TEST(CppEditorFind, RenameUsesLibclangToRenameTheDeclarationAndEveryRealReferenceToIt)
{
    auto* root = new newui::RootView(nullptr, newui::Rect(0, 0, 900, 700), "cppFindRoot");
    CodeToolsVsix::CppEditor editor(root);
    ASSERT_NE(editor.findReplace(), nullptr);
    const std::wstring text = L"int count = 0;\nvoid f() { count = count + 1; }\n";
    editor.textControl()->setText(text);
    editor.textControl()->caret().setPosition(newui::text::TextPosition(text.find(L"count")));   // the declaration
    CodeToolsVsix::FindReplaceController* find = editor.findReplace();

    find->showReplace();   // seeds Find and Replace from the token under the caret
    find->setReplacement(L"total");

    EXPECT_EQ(find->renameCurrent(), 3u);
    EXPECT_EQ(editor.textControl()->text(), L"int total = 0;\nvoid f() { total = total + 1; }\n");
    EXPECT_TRUE(editor.textControl()->undo());   // one step
    EXPECT_EQ(editor.textControl()->text(), text);
}

TEST(CppEditorFind, CaretOnASymbolHighlightsEveryOtherRealReferenceToIt)
{
    auto* root = new newui::RootView(nullptr, newui::Rect(0, 0, 900, 700), "cppFindRoot");
    CodeToolsVsix::CppEditor editor(root);
    const std::wstring text = L"int count = 0;\nvoid f() { count = count + 1; }\n";
    editor.textControl()->setText(text);   // no RunLoop in tests: HighlightController parses synchronously

    editor.textControl()->caret().setPosition(newui::text::TextPosition(text.find(L"count")));   // the declaration

    std::size_t occurrenceRanges = 0;
    for (const auto& range : editor.textControl()->styledRanges()) {
        if (range.style == CodeToolsVsix::kOccurrenceStyleName) {
            ++occurrenceRanges;
        }
    }
    EXPECT_EQ(occurrenceRanges, 3u);   // the declaration and both uses - semantic, not textual (see below)
}

TEST(CppEditorFind, OccurrenceHighlightIsSemanticNotJustSameSpelledText)
{
    auto* root = new newui::RootView(nullptr, newui::Rect(0, 0, 900, 700), "cppFindRoot");
    CodeToolsVsix::CppEditor editor(root);
    // Two unrelated locals named "count" in different, non-overlapping scopes.
    const std::wstring text = L"void f() { int count = 1; count = 2; }\nvoid g() { int count = 3; }\n";
    editor.textControl()->setText(text);

    editor.textControl()->caret().setPosition(newui::text::TextPosition(text.find(L"count")));   // f's count

    std::size_t occurrenceRanges = 0;
    for (const auto& range : editor.textControl()->styledRanges()) {
        if (range.style == CodeToolsVsix::kOccurrenceStyleName) {
            ++occurrenceRanges;
        }
    }
    EXPECT_EQ(occurrenceRanges, 2u);   // f's declaration + assignment only, not g's unrelated "count"
}

TEST(CppEditorFind, DoubleClickWordSelectStillHighlightsOccurrencesDespiteTheCaretLandingPastTheWord)
{
    auto* root = new newui::RootView(nullptr, newui::Rect(0, 0, 900, 700), "cppFindRoot");
    CodeToolsVsix::CppEditor editor(root);
    const std::wstring text = L"int count = 0;\nvoid f() { count = count + 1; }\n";
    editor.textControl()->setText(text);

    const std::size_t start = text.find(L"count");
    const std::size_t end = start + 5;   // wcslen(L"count")
    // Mirrors TextController::selectWordAt()'s own double-click sequence (controls.cpp): select
    // the word, then land the caret one past its last character - outside the identifier's own
    // spelling range, which findOccurrences() alone would reject.
    editor.textControl()->selection().setRange(newui::text::TextRange(start, end - start));
    editor.textControl()->caret().setPosition(newui::text::TextPosition(end));

    std::size_t occurrenceRanges = 0;
    for (const auto& range : editor.textControl()->styledRanges()) {
        if (range.style == CodeToolsVsix::kOccurrenceStyleName) {
            ++occurrenceRanges;
        }
    }
    EXPECT_EQ(occurrenceRanges, 3u);
}

TEST(CppEditorFind, MovingTheCaretOffARenamableSymbolClearsTheOccurrenceHighlight)
{
    auto* root = new newui::RootView(nullptr, newui::Rect(0, 0, 900, 700), "cppFindRoot");
    CodeToolsVsix::CppEditor editor(root);
    const std::wstring text = L"int count = 0;\n";
    editor.textControl()->setText(text);

    editor.textControl()->caret().setPosition(newui::text::TextPosition(text.find(L"count")));
    const auto hasOccurrence = [&]() {
        return std::any_of(editor.textControl()->styledRanges().begin(), editor.textControl()->styledRanges().end(),
            [](const newui::text::TextStyleRange& r) { return r.style == CodeToolsVsix::kOccurrenceStyleName; });
    };
    ASSERT_TRUE(hasOccurrence());

    editor.textControl()->caret().setPosition(newui::text::TextPosition(0));   // "i" of "int" - a keyword
    EXPECT_FALSE(hasOccurrence());
}

TEST(CppEditorFind, EditingCommandsStillActOnTheSourcePane)
{
    auto* root = new newui::RootView(nullptr, newui::Rect(0, 0, 900, 700), "cppFindRoot");
    CodeToolsVsix::CppEditor editor(root);
    editor.textControl()->setText(L"x");
    editor.textControl()->model().insert(1, L"y");
    EXPECT_TRUE(editor.execCommand(EditorCommand::Undo, 0, nullptr));
    EXPECT_EQ(editor.textControl()->text(), L"x");
}

TEST(CppEditor, LinesTheCompilerSkipsAreTintedAndSayWhyOnTheFirst) {
    const std::wstring text = L"#define F 1\n#if F\nint on;\n#else\nint off;\n#endif\n#ifdef NEVER\nint a;\n\nint b;\n#endif\n";
    const auto ranges = CodeToolsVsix::inactiveRanges(text, "inactive_test.cpp", CodeToolsVsix::wideToUtf8(text), { "-std=c++17" });

    ASSERT_EQ(ranges.size(), 4u) << "int off; and the three lines under #ifdef NEVER (one empty)";
    const std::size_t offLine = text.find(L"int off;");
    EXPECT_EQ(ranges[0].start, offLine);
    EXPECT_EQ(ranges[0].length, 8u);
    for (const auto& range : ranges) EXPECT_EQ(range.style, std::string(CodeToolsVsix::kInactiveStyleName)) << "one tint for all";
    EXPECT_EQ(ranges[0].annotation, "inactive: depends on F") << "the #else is the other side of #if F";
    EXPECT_EQ(ranges[1].annotation, "inactive: depends on NEVER");
    EXPECT_TRUE(ranges[2].annotation.empty()) << "said once per region";
    EXPECT_EQ(ranges[2].length, 1u) << "an empty line still carries the tint";
}

TEST(CppEditor, TurningTheNotesOffKeepsTheTintAndDropsTheText) {
    const std::wstring text = L"#ifdef NEVER\nint a;\n#endif\n";
    const auto ranges = CodeToolsVsix::inactiveRanges(text, "inactive_notes.cpp", CodeToolsVsix::wideToUtf8(text), { "-std=c++17" }, false);

    ASSERT_EQ(ranges.size(), 1u);
    EXPECT_EQ(ranges[0].style, std::string(CodeToolsVsix::kInactiveStyleName));
    EXPECT_TRUE(ranges[0].annotation.empty());
}

TEST(CppEditor, FadingSwitchesTheSkippedLinesToTheFadedStyleAndLeavesTheRest) {
    using newui::text::TextStyleRange;
    const std::vector<TextStyleRange> ranges = { { 0, 6, CodeToolsVsix::kInactiveStyleName }, { 7, 3, CodeToolsVsix::kProblemStyleName } };

    const auto faded = CodeToolsVsix::withInactiveLook(ranges, true);
    ASSERT_EQ(faded.size(), 2u);
    EXPECT_EQ(faded[0].style, std::string(CodeToolsVsix::kInactiveFadedStyleName));
    EXPECT_EQ(faded[1].style, std::string(CodeToolsVsix::kProblemStyleName));
    EXPECT_EQ(CodeToolsVsix::withInactiveLook(ranges, false)[0].style, std::string(CodeToolsVsix::kInactiveStyleName));
}

TEST(CppEditor, TheFadedStyleOverlaysTheThemeBackgroundAndThePlainOneDoesNot) {
    lex::Theme theme = lex::Theme::light();
    auto sheet = CodeToolsVsix::highlightStyleSheet(theme);
    const newui::TextStyle* plain = sheet->style(CodeToolsVsix::kInactiveStyleName);
    const newui::TextStyle* faded = sheet->style(CodeToolsVsix::kInactiveFadedStyleName);
    ASSERT_NE(plain, nullptr);
    ASSERT_NE(faded, nullptr);
    EXPECT_TRUE(plain->overlayColor().isNull());
    EXPECT_FALSE(faded->overlayColor().isNull());
    EXPECT_EQ(faded->overlayComposite(), newui::CompositeMode::SrcOver);
    EXPECT_FLOAT_EQ(faded->overlayColor().a, CodeToolsVsix::kDefaultFadeStrength / 100.0f);
    EXPECT_FLOAT_EQ(CodeToolsVsix::highlightStyleSheet(theme, 40)->style(CodeToolsVsix::kInactiveFadedStyleName)->overlayColor().a, 0.4f)
        << "the strength is the overlay's alpha";
    EXPECT_FLOAT_EQ(CodeToolsVsix::highlightStyleSheet(theme, 250)->style(CodeToolsVsix::kInactiveFadedStyleName)->overlayColor().a, 1.0f)
        << "clamped to 100";
    EXPECT_EQ(faded->lineBackgroundColor(), plain->lineBackgroundColor()) << "the same faint tint";
}

TEST(CppEditor, TheFontsShippedWithTheEditorGiveItsAnnotationAnItalicFace) {
    newui::Bundle::instance().loadFonts();
    BLFont italic;
    EXPECT_TRUE(newui::FontManager::createFont("Cascadia Mono Italic", 14.0f, italic));
    BLFont boldItalic;
    EXPECT_TRUE(newui::FontManager::createFont("Cascadia Mono Bold Italic", 14.0f, boldItalic));
    newui::Font family("Cascadia Mono", 14.0f);
    family.setItalic(true);
    EXPECT_NE(family.blFont(), nullptr) << "what the editor's annotation asks for";
}

TEST(CppEditor, TheEditorsDirectWriteTextCanUseTheFontsShippedWithIt) {
    newui::Bundle::instance().loadFonts();
    const newui::SystemFontInfo* shipped = nullptr;
    for (const newui::SystemFontInfo& info : newui::FontManager::listFonts()) {
        if (info.bundled) {
            shipped = &info;
            break;
        }
    }
    ASSERT_NE(shipped, nullptr) << "Resources/Fonts has fonts in it";
    BLFontFace face;
    ASSERT_EQ(face.create_from_file(shipped->filePath.c_str()), BL_SUCCESS);

    IDWriteFontCollection* collection = newui::text::DirectWriteResources::fontCollection();
    ASSERT_NE(collection, nullptr);
    UINT32 index = 0;
    BOOL exists = FALSE;
    ASSERT_TRUE(SUCCEEDED(collection->FindFamilyName(newui::utf8ToWide(face.family_name().data()).c_str(), &index, &exists)));
    EXPECT_TRUE(exists) << face.family_name().data();
}
