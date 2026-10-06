#include "CppEditor.h"

#include <algorithm>
#include <filesystem>
#include "CppDiagnostics.h"
#include "CppHighlight.h"
#include "TextEncoding.h"
#include "Logging.h"

#include <cpptools/compileflags.h>
#include <cpptools/parser.h>
#include <cpptools/symbol.h>
#include <cpptools/log.h>

#include <newui/keyboard_constants.h>
#include <newui/layout.h>
#include <newui/rootview.h>
#include <newui/texthistory.h>
#include <newui/uicolormanager.h>

#include <cmath>
#include <vector>

namespace CodeToolsVsix
{
    namespace
    {
        // filePath is never assumed to be null-terminated (see NativeEditor.h) - reads
        // exactly filePathLength wchar_t characters and builds an internally-owned, properly
        // null-terminated std::wstring from them, so every downstream Win32 call (CreateFileW,
        // etc.) gets a safe buffer regardless of what the caller actually passed.
        std::wstring copyPath(const wchar_t* filePath, std::size_t filePathLength)
        {
            if (!filePath || filePathLength == 0)
            {
                return std::wstring();
            }

            return std::wstring(filePath, filePathLength);
        }

        bool readFileUtf8(const std::wstring& path, std::string& outUtf8)
        {
            HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE)
            {
                return false;
            }

            LARGE_INTEGER size{};
            if (!GetFileSizeEx(file, &size) || size.QuadPart < 0)
            {
                CloseHandle(file);
                return false;
            }

            outUtf8.assign(static_cast<std::size_t>(size.QuadPart), '\0');

            bool ok = true;
            if (!outUtf8.empty())
            {
                DWORD bytesRead = 0;
                ok = ReadFile(file, outUtf8.data(), static_cast<DWORD>(outUtf8.size()), &bytesRead, nullptr) != FALSE
                     && bytesRead == outUtf8.size();
            }

            CloseHandle(file);
            return ok;
        }

        bool writeFileUtf8(const std::wstring& path, const std::string& utf8)
        {
            HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE)
            {
                return false;
            }

            bool ok = true;
            if (!utf8.empty())
            {
                DWORD bytesWritten = 0;
                ok = WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &bytesWritten, nullptr) != FALSE
                     && bytesWritten == utf8.size();
            }

            CloseHandle(file);
            return ok;
        }

        const wchar_t* commandName(EditorCommand command)
        {
            switch (command)
            {
            case EditorCommand::Undo: return L"Undo";
            case EditorCommand::Redo: return L"Redo";
            case EditorCommand::Cut: return L"Cut";
            case EditorCommand::Copy: return L"Copy";
            case EditorCommand::Paste: return L"Paste";
            case EditorCommand::Find: return L"Find";
            case EditorCommand::Replace: return L"Replace";
            case EditorCommand::GotoLine: return L"GotoLine";
            default: return L"Unknown";
            }
        }

        // Every place in text a rename of the symbol at offset (a UTF-16 index, like every offset in
        // this DLL's own text APIs) would touch: reparses document with text's current content right
        // now (a rename is a deliberate, occasional click, not typed on every keystroke like the
        // squiggle overlay is) and asks cpptools for the declaration plus every reference to it that
        // is itself in this file, then maps those UTF-8 byte ranges back to UTF-16 ones.
        std::vector<RenameRange> renameOccurrencesAt(const std::wstring& text, std::size_t offset,
            const std::shared_ptr<CppDocument>& document)
        {
            std::vector<RenameRange> result;
            try
            {
                const std::string utf8 = wideToUtf8(text);
                const std::size_t byteOffset = wideToUtf8(text.substr(0, offset)).size();
                const cpptools::CompileFlags flags = document->flags();
                document->session().update(document->path(), utf8, flags.args);
                const std::vector<cpptools::Occurrence> occurrences = document->session().findOccurrences(byteOffset);
                if (occurrences.empty())
                {
                    return result;
                }

                std::vector<std::size_t> byteOffsets;
                byteOffsets.reserve(occurrences.size() * 2);
                for (const cpptools::Occurrence& occurrence : occurrences)
                {
                    byteOffsets.push_back(occurrence.offset);
                    byteOffsets.push_back(occurrence.offset + occurrence.length);
                }
                const std::vector<std::size_t> wideOffsets = utf8ToWideOffsets(utf8, text, byteOffsets);
                result.reserve(occurrences.size());
                for (std::size_t i = 0; i < occurrences.size(); ++i)
                {
                    const std::size_t start = wideOffsets[2 * i];
                    const std::size_t end = wideOffsets[2 * i + 1];
                    if (end > start)
                    {
                        result.push_back(RenameRange{ start, end - start });
                    }
                }
            }
            catch (...)
            {
                // A failed reparse: nothing to rename this time.
            }
            return result;
        }

        // Every other real reference to the symbol at offset (a UTF-16 index), semantic via
        // cpptools::Session::findOccurrences - only real references, never a lookalike-named
        // symbol. Deliberately does NOT reparse (unlike renameOccurrencesAt above): this runs on
        // every caret move, and a full reparse per call (tens of ms, see CCE-17's own measurements)
        // would be far too slow for that. Instead it reads whatever the session's own translation
        // unit currently is - kept reasonably fresh by the existing diagnostics overlay pass
        // (analyzeCppDiagnostics, ~600ms behind typing) - so a highlight can briefly lag a very
        // recent edit, the same staleness the squiggle overlay already accepts. Empty when offset
        // isn't on a renamable symbol, or nothing has been parsed yet.
        std::vector<newui::text::TextStyleRange> symbolOccurrencesAt(const std::wstring& text, std::size_t offset,
            const std::shared_ptr<CppDocument>& document)
        {
            std::vector<newui::text::TextStyleRange> result;
            try
            {
                const std::string utf8 = wideToUtf8(text);
                const std::size_t byteOffset = wideToUtf8(text.substr(0, offset)).size();
                const std::vector<cpptools::Occurrence> occurrences = document->session().findOccurrences(byteOffset);
                if (occurrences.empty())
                {
                    return result;
                }

                std::vector<std::size_t> byteOffsets;
                byteOffsets.reserve(occurrences.size() * 2);
                for (const cpptools::Occurrence& occurrence : occurrences)
                {
                    byteOffsets.push_back(occurrence.offset);
                    byteOffsets.push_back(occurrence.offset + occurrence.length);
                }
                const std::vector<std::size_t> wideOffsets = utf8ToWideOffsets(utf8, text, byteOffsets);
                result.reserve(occurrences.size());
                for (std::size_t i = 0; i < occurrences.size(); ++i)
                {
                    const std::size_t start = wideOffsets[2 * i];
                    const std::size_t end = wideOffsets[2 * i + 1];
                    if (end > start)
                    {
                        result.push_back({ start, end - start, kOccurrenceStyleName });
                    }
                }
            }
            catch (...)
            {
                // Nothing to highlight this time.
            }
            return result;
        }

        // Registers this DLL's log() as cpptools's log sink (see cpptools/log.h) - once, on first
        // use. A C++11 function-local static's initialization is itself thread-safe/exactly-once,
        // so no separate guard/mutex is needed.
        void registerCppToolsLogSink()
        {
            static bool registered = []() {
                cpptools::setLogSink([](cpptools::Severity severity, const std::string& message) {
                    CodeToolsVsix::log(severity, message);
                });
                return true;
            }();
            (void)registered;
        }

    }

    CppEditor::CppEditor(newui::RootView* rootView, newui::SubView* contentHost)
    {
        rootViewOwned_ = true;

        if (!setupUI(rootView, contentHost))
        {
            return;
        }

        setRootView(std::unique_ptr<newui::RootView>(rootView));
    }

    CppEditor::CppEditor(HWND hwndParent, int x, int y, int width, int height)
    {
        logToDebugOut(L"CppEditorControl");

        auto root = std::make_unique<newui::RootView>(
            hwndParent, NativeEditManager::moduleHandle(),
            newui::Rect(static_cast<float>(x), static_cast<float>(y), static_cast<float>(width), static_cast<float>(height)),
            "cppEditorRoot");


        if ( !setupUI(root.get()) )
        {
            return ;
        }

        setRootView(std::move(root));

        

        logToDebugOut(L"CppEditorControl completed");
    }

    bool CppEditor::setupUI(newui::RootView* root, newui::SubView* contentHost)
    {
        // Was previously only reached via the (now-removed, see load()'s own comment) eager
        // outline parse - relocated here so cpptools log messages (parse errors, ...) still reach
        // this DLL's own log() from the first real parse onward, whichever pass runs it first.
        registerCppToolsLogSink();

        root->style().setBackgroundColor(newui::UIColorManager::colorFor(newui::UIColorRole::WindowBackground));

        // contentHost: nullptr (the default - every pre-existing caller) means root itself gets
        // both the layout and the two TextControls below, unchanged from before this parameter
        // existed. A caller that already built other chrome directly onto root (e.g.
        // testharness's own directory tree pane, added as a sibling of contentHost under root's
        // own top-level layout) passes that pane instead, so this editor's own layout/content
        // only ever touches its own subtree, never displacing that other chrome.
        newui::View* host = contentHost != nullptr ? static_cast<newui::View*>(contentHost) : static_cast<newui::View*>(root);
        host_ = host;

        auto rootLayout = std::make_unique<newui::FlexLayout>(newui::Orientation::Vertical);
        rootLayout->setSpacing(0.0f);
        rootLayout->setPadding(0.0f);
        host->setLayout(std::move(rootLayout));

        // Each pane is a text control hosted by a ScrollView, which scrolls it (a TextControl has no
        // scrollbar of its own). The scroll views share the vertical space: most of it to the source.
        auto* scrollView = new newui::ScrollView();
        scrollView->setName("cppEditorScroll");
        scrollView->setVisible(true);
        scrollView->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(3.0f));
        host->addChild(scrollView);

        // A TextFoldingControl: line numbers, folding, colors. Its model remembers edits for undo -
        // installed before anything subscribes to the model.
        auto* textControl = new newui::TextFoldingControl();
        textControl->setName("cppEditorText");
        textControl->setVisible(true);
        textControl->setModel(std::make_unique<newui::text::HistoryTextModel>());
        textControl->setHighlightsCurrentLine(true);
        textControl->setWordWrap(Settings::instance().getBool(Settings::kWordWrap));   // off: one row per line, scrolling sideways
        scrollView->addChild(textControl);

        auto* outlineScroll = new newui::ScrollView();
        outlineScroll->setName("cppEditorOutlineScroll");
        outlineScroll->setVisible(true);
        outlineScroll->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
        host->addChild(outlineScroll);

        auto* outlineControl = new newui::TextControl();
        outlineControl->setName("cppEditorOutline");
        outlineControl->setVisible(true);
        outlineControl->inputTraits().setReadOnly(true);
        // Visually distinct from the editable pane above it, so it doesn't read as "more of the
        // same editable buffer" - same UIColorManager pattern the root's own background uses.
        outlineControl->style().setBackgroundColor(newui::UIColorManager::colorFor(newui::UIColorRole::ControlBackground));
        outlineScroll->addChild(outlineControl);

        // The status row: before the progress bar and the Find overlays, which must stay the last
        // children (see TheOverlaysAreLayoutIgnoredChildrenPaintedOnTopOfThePanes).
        status_ = std::make_unique<EditorStatusBar>(*host, *textControl);
        if (!this->rootViewOwned_) {
            if (!root->initialize())
            {
                // Leave the base's RootView/textControl_/outlineControl_ null - windowHandle()
                // reports failure the same way StandInEditControl::Create() used to (a null HWND),
                // and load/save/execCommand all already guard on textControl_ being null before
                // touching it.
                logToDebugOut(L"!root->initialize()");
                return false;
            }
        }
        

        // Only the editable pane's changes count as "dirty" - outlineControl_'s own setText()
        // calls (load(), below) fire this same delegate too, but nothing should ever mark the
        // document dirty just because the outline was refreshed.
        textControl->model().onChanged.add([this](newui::Model&) {
            ++editVersion_;
            markDirty();
            return newui::SyncReturn::Handled;
            });

        
        scrollView_ = scrollView;
        textControl_ = textControl;
        outlineScroll_ = outlineScroll;
        outlineControl_ = outlineControl;

        // A thin, indeterminate progress bar across the very top of the editor - see its own
        // member comment (CppEditor.h). layoutIgnored, and added BEFORE Find/Replace's own
        // overlays below so those stay the last children (painted on top) if they and this ever
        // did coincide - see TheOverlaysAreLayoutIgnoredChildrenPaintedOnTopOfThePanes, which
        // asserts findBar/goToBar/minimap are exactly the last three children.
        auto* loadingProgress = new newui::Progress();
        loadingProgress->setName("cppEditorLoadingProgress");
        loadingProgress->setVisible(false);
        loadingProgress->setLayoutIgnored(true);
        host->addChild(loadingProgress);
        loadingProgress_ = loadingProgress;

        // Colors and folds follow the text, off the UI thread; a slower pass parses it with libclang
        // for syntax-error squiggles, and keeps the outline current.
        highlight_ = std::make_unique<HighlightController>(*textControl, &analyzeCpp);
        document_ = std::make_shared<CppDocument>();
        applyFileKind(FileKind::Cpp);
        highlight_->setOnOverlayApplied([this](HighlightOverlay& overlay) {
            if (const auto* outline = std::any_cast<std::wstring>(&overlay.extra)) {
                setOutlineText(*outline);
            }
            stopLoadingAnimation();
            if (status_ != nullptr) {
                status_->refresh();   // the squiggles - and so the problems - may have changed
            }
        });
        highlight_->setOnApplied([this](HighlightResult&) {
            if (status_ != nullptr) {
                status_->refresh();   // the colors pass republishes the overlay's squiggles, moved along
            }
        });
        applySettings();
        settingsConnection_ = Settings::instance().onChanged.add(this, &CppEditor::handleSettingChanged);

        // Find / Replace / Go to line: overlays added to the same host as the two panes.
        find_ = std::make_unique<FindReplaceController>(*host, *textControl, highlight_.get());
        find_->setRenameProvider([document = document_](const std::wstring& text, std::size_t offset) {
            return renameOccurrencesAt(text, offset, document);
        });
        find_->setRenameChecker([document = document_](const std::wstring& text, std::size_t offset, const std::wstring& newName) {
            return document->session().renameConflict(wideToUtf8(text.substr(0, offset)).size(), wideToUtf8(newName));
        });
        keyConnection_ = root->onKeyDown.add(this, &CppEditor::handleKeyDown);

        // The status row's arrows move the caret the way Go to line does; its problem ticks go on the
        // minimap.
        status_->setGoTo([this](std::size_t line, std::size_t column) {
            if (find_ != nullptr) {
                find_->goToLine(std::to_wstring(line) + L":" + std::to_wstring(column));
            }
        });
        status_->setMarksSink([this](std::vector<MinimapStrip::Mark> marks) {
            if (find_ != nullptr) {
                find_->setProblemMarks(std::move(marks));
            }
        });
        status_->refresh();

        // Highlights every other real reference to whatever symbol the caret is on (semantic, via
        // libclang - not just same-spelled text) - see symbolOccurrencesAt()'s own comment above.
        caretConnection_ = textControl->caret().onPositionChanged.add(this, &CppEditor::handleCaretMoved);

        return true;
    }

    newui::SyncReturn CppEditor::handleSettingChanged(Settings&, std::string key)
    {
        if (key.rfind("CodeTools.editor.", 0) == 0) {
            applySettings();
        }
        if (key == Settings::kInactiveNotes.key && highlight_ != nullptr) {
            highlight_->rerunOverlay();
        }
        return newui::SyncReturn::Ignored;
    }

    CppEditor::FileKind CppEditor::fileKindFor(const std::wstring& path)
    {
        const std::size_t slash = path.find_last_of(L"\\/");
        std::wstring name = path.substr(slash == std::wstring::npos ? 0 : slash + 1);
        for (wchar_t& c : name) {
            if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c - L'A' + L'a');
        }
        auto endsWith = [&name](const std::wstring& suffix) {
            return name.size() >= suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0;
        };
        if (name == L"cmakelists.txt" || endsWith(L".cmake")) {
            return FileKind::CMake;
        }
        for (const wchar_t* ext : { L".c", L".cc", L".cpp", L".cxx", L".c++", L".h", L".hh", L".hpp", L".hxx", L".h++",
                                    L".inl", L".ipp", L".tpp", L".ixx", L".cppm" }) {
            if (endsWith(ext)) {
                return FileKind::Cpp;
            }
        }
        return FileKind::Plain;
    }

    void CppEditor::applyFileKind(FileKind kind)
    {
        fileKind_ = kind;
        if (highlight_ == nullptr) {
            return;
        }
        switch (kind) {
            case FileKind::Cpp:
                highlight_->setAnalyzer(&analyzeCpp);
                highlight_->setOverlayAnalyzer([document = document_](const std::wstring& text) {
                    return analyzeCppDiagnostics(text, document);
                });
                break;
            case FileKind::CMake:
                highlight_->setAnalyzer(&analyzeCMake);
                highlight_->clearOverlayAnalyzer();
                break;
            case FileKind::Plain:
                highlight_->setAnalyzer(&analyzePlainText);
                highlight_->clearOverlayAnalyzer();
                break;
        }
    }

    void CppEditor::applySettings()
    {
        const Settings& settings = Settings::instance();
        if (textControl_ != nullptr) {
            textControl_->setWordWrap(settings.getBool(Settings::kWordWrap));
        }
        if (highlight_ != nullptr) {
            highlight_->setFadeInactive(settings.getBool(Settings::kFadeInactive));
            highlight_->setFadeStrength(settings.getInt(Settings::kFadeStrength, 0, 100));
            highlight_->setDelay(std::chrono::milliseconds(settings.getInt(Settings::kHighlightDelayMs, 0, 5000)));
            highlight_->setOverlayDelay(std::chrono::milliseconds(settings.getInt(Settings::kDiagnosticsDelayMs, 0, 10000)));
        }
    }

    void CppEditor::startLoadingAnimation()
    {
        if (loadingProgress_ == nullptr || host_ == nullptr)
        {
            return;
        }
        stopLoadingAnimation();   // in case a previous load() is still animating

        loadingProgress_->setBounds(newui::Rect(0.0f, 0.0f, host_->bounds().size().width, 4.0f));
        loadingProgress_->setValue(0.0f);
        loadingProgress_->setVisible(true);
        loadingProgressPhase_ = 0.0f;

        newui::RunLoop& loop = newui::RunLoop::current();
        if (!loop)
        {
            return;   // a test/no-message-pump environment: stays at 0, no animation to drive it
        }
        loadingProgress_->style().markDirty();
        loadingProgressTimer_ = loop.postDelayed(std::chrono::milliseconds(30), [this]() {
            // There's no real percent-complete to report - an opaque libclang parse gives no
            // progress callback - so this sweeps back and forth (0 -> 1 -> 0 -> ...) instead of a
            // one-way fill, the same "at least show it's working" indeterminate convention a
            // marquee progress bar uses elsewhere. A sine wave rather than a linear bounce so it
            // eases at each end instead of sharply reversing direction.
            loadingProgressPhase_ += 0.04f;
            const float value = 0.5f + 0.5f * std::sin(loadingProgressPhase_);
            loadingProgress_->setValue(value);
            return false;   // keep going until stopLoadingAnimation() cancels this
        });
    }

    void CppEditor::stopLoadingAnimation()
    {
        if (loadingProgressTimer_ != newui::RunLoop::kInvalidTimerHandle)
        {
            newui::RunLoop& loop = newui::RunLoop::current();
            if (loop)
            {
                loop.cancelDelayed(loadingProgressTimer_);
            }
            loadingProgressTimer_ = newui::RunLoop::kInvalidTimerHandle;
        }
        if (loadingProgress_ != nullptr)
        {
            loadingProgress_->setVisible(false);
        }
    }

    CppEditor::~CppEditor()
    {
        Settings::instance().onChanged.remove(settingsConnection_);
        if (!registeredPath_.empty())
        {
            documentEditService().unregisterEditor(registeredPath_, this);
        }
        // The root can outlive this editor (a host that gives it its own window): stop listening.
        if (getRootView() != nullptr)
        {
            getRootView()->onKeyDown.remove(keyConnection_);
        }
        if (textControl_ != nullptr)
        {
            textControl_->caret().onPositionChanged.remove(caretConnection_);
        }
        if (loadingProgressTimer_ != newui::RunLoop::kInvalidTimerHandle)
        {
            newui::RunLoop& loop = newui::RunLoop::current();
            if (loop)
            {
                loop.cancelDelayed(loadingProgressTimer_);
            }
        }
    }

    newui::SyncReturn CppEditor::handleCaretMoved(newui::text::Caret& /*sender*/)
    {
        if (status_ != nullptr)
        {
            status_->caretMoved();
        }
        if (highlight_ == nullptr || textControl_ == nullptr || document_ == nullptr)
        {
            return newui::SyncReturn::Ignored;
        }

        // A double-click word-select (TextController::selectWordAt(), controls.cpp) leaves the
        // caret one past the selected word's own last character - outside the identifier's own
        // spelling range, which findOccurrences() requires the offset be inside. Resolve at the
        // selection's own start instead whenever one exists, so a double-clicked symbol still
        // highlights its other references (an ordinary, non-word selection's start is still a
        // reasonable point to ask about, so this isn't double-click-specific).
        const std::vector<newui::text::TextRange>& ranges = textControl_->selection().ranges();
        std::size_t offset;
        if (!ranges.empty())
        {
            offset = ranges.front().start();
        }
        else
        {
            const newui::text::TextPosition position = textControl_->caret().position();
            if (!position.isValid())
            {
                highlight_->setOccurrenceRanges({});
                return newui::SyncReturn::Ignored;
            }
            offset = position.offset();
        }
        highlight_->setOccurrenceRanges(symbolOccurrencesAt(textControl_->text(), offset, document_));
        return newui::SyncReturn::Ignored;
    }

    newui::SyncReturn CppEditor::handleKeyDown(newui::View& /*sender*/, std::uint32_t keyMask,
        int /*keyCharVal*/, int /*repeatCount*/, std::uint32_t VKeyCode)
    {
        // Ctrl alone (Ctrl+Shift+letter and Ctrl+Alt+letter belong to someone else).
        if ((keyMask & (newui::kmShift | newui::kmCtrl | newui::kmAlt)) != newui::kmCtrl)
        {
            return newui::SyncReturn::Ignored;
        }
        switch (VKeyCode)
        {
        case newui::vkLetterF: execCommand(EditorCommand::Find, 0, nullptr); return newui::SyncReturn::Handled;
        case newui::vkLetterH: execCommand(EditorCommand::Replace, 0, nullptr); return newui::SyncReturn::Handled;
        case newui::vkLetterG: execCommand(EditorCommand::GotoLine, 0, nullptr); return newui::SyncReturn::Handled;
        default: return newui::SyncReturn::Ignored;
        }
    }

    bool CppEditor::load(const wchar_t* filePath, std::size_t filePathLength)
    {
        if (!textControl_)
        {
            CodeToolsVsix::log(cpptools::Severity::Error, "CppEditorControl::load: textControl_ is null (construction must have failed)");
            return false;
        }

        std::wstring path = copyPath(filePath, filePathLength);
        if (path.empty())
        {
            CodeToolsVsix::log(cpptools::Severity::Error, "CppEditorControl::load: empty path");
            return false;
        }

        std::string contentUtf8;
        if (!readFileUtf8(path, contentUtf8))
        {
            CodeToolsVsix::log(cpptools::Severity::Error, "CppEditorControl::load: readFileUtf8 failed for " + wideToUtf8(path.c_str(), path.size()));
            return false;
        }

        // What kind of file this is decides what analyzes it: libclang must not parse a CMakeLists.txt.
        const FileKind kind = fileKindFor(path);
        applyFileKind(kind);

        // The parse pretends the text is this file (so its own directory is searched for includes).
        document_->setPath(wideToUtf8(path.c_str(), path.size()));
        registerForEdits(path);

        // A placeholder, not the real thing - a full first parse of a file with heavy includes can
        // take well over a second (CCE-17's own measurements), and this used to run it
        // synchronously right here (buildOutline(), since removed), freezing this editor's own
        // window: it lives on its own dedicated thread (this class's own comment), but that same
        // thread also pumps its own message loop/repaints, so blocking it for a second-plus reads
        // as the whole editor having hung. The real outline (and diagnostics squiggles) now only
        // ever come from the existing async overlay pass (HighlightController::setOverlayAnalyzer,
        // wired in setupUI() - analyzeCppDiagnostics) - already fully async; this eager call was
        // the one place still routing around it instead of just waiting the same short while
        // everything else already does. setText() below replaces this placeholder synchronously in
        // a no-RunLoop environment (unit tests - HighlightController::handleTextChanged() falls
        // back to refresh() when there's nothing to schedule onto), so only a real, message-pumped
        // run (testharness, VS) actually shows it, however briefly.
        if (kind == FileKind::Cpp) {
            setOutlineText(L"--- Outline (cpptools): parsing... ---");
            startLoadingAnimation();   // stops when the first parse lands, which only C++ has
        } else {
            setOutlineText(kind == FileKind::CMake ? L"--- No outline for CMake files ---" : L"--- No outline for this file type ---");
            stopLoadingAnimation();
        }

        textControl_->setText(utf8ToWide(contentUtf8));
        clearDirty();
        if (kind != FileKind::Cpp && status_ != nullptr) {
            status_->refresh();   // no problems to show: the previous file's are gone
        }

        return true;
    }

    void CppEditor::setOutlineText(const std::wstring& outline)
    {
        if (!outlineControl_ || outlineControl_->text() == outline)
        {
            return;
        }
        // TextController::handleModelBeforeRangeChanged() (controls.cpp) vetoes *any* model
        // change - including a programmatic setText(), not just user keystrokes - while
        // isReadOnly() is true. Lift it only for this call, then restore it immediately, so
        // the pane can still be refreshed while staying non-editable to the user the rest of the
        // time.
        outlineControl_->inputTraits().setReadOnly(false);
        outlineControl_->setText(outline);
        outlineControl_->inputTraits().setReadOnly(true);
    }

    bool CppEditor::save(const wchar_t* filePath, std::size_t filePathLength)
    {
        if (!textControl_)
        {
            return false;
        }

        std::wstring path = copyPath(filePath, filePathLength);
        if (path.empty())
        {
            return false;
        }

        if (!writeFileUtf8(path, wideToUtf8(textControl_->text().c_str(), textControl_->text().size())))
        {
            return false;
        }

        clearDirty();
        registerForEdits(path);
        return true;
    }

    void CppEditor::registerForEdits(const std::wstring& path)
    {
        const std::filesystem::path newPath(path);
        if (newPath == registeredPath_)
        {
            return;
        }
        if (!registeredPath_.empty())
        {
            documentEditService().unregisterEditor(registeredPath_, this);
        }
        registeredPath_ = newPath;
        documentEditService().registerEditor(registeredPath_, this);
    }

    DocumentSnapshot CppEditor::snapshot() const
    {
        DocumentSnapshot result;
        if (textControl_ != nullptr)
        {
            result.text = textControl_->text();
        }
        result.version = editVersion_;
        return result;
    }

    EditStatus CppEditor::applyEdits(std::uint64_t expectedVersion, const std::vector<TextEdit>& edits)
    {
        if (textControl_ == nullptr)
        {
            return EditStatus::Rejected;
        }
        if (expectedVersion != editVersion_)
        {
            return EditStatus::VersionMismatch;
        }
        if (edits.empty())
        {
            return EditStatus::Ok;
        }

        // Validate the whole plan (range, overlap, surrogate pairs) on a copy first, so nothing
        // changes on failure.
        std::wstring dryRun = textControl_->text();
        if (!applyTextEdits(dryRun, edits))
        {
            return EditStatus::InvalidEdit;
        }

        std::vector<const TextEdit*> ordered;
        ordered.reserve(edits.size());
        for (const TextEdit& edit : edits)
        {
            ordered.push_back(&edit);
        }
        std::stable_sort(ordered.begin(), ordered.end(),
            [](const TextEdit* a, const TextEdit* b) { return a->offset < b->offset; });

        auto* history = dynamic_cast<newui::text::HistoryTextModel*>(&textControl_->model());
        if (history != nullptr)
        {
            history->beginGroup();
        }
        // Last to first, so the offsets of the ones still to do stay valid.
        for (std::size_t i = ordered.size(); i-- > 0;)
        {
            textControl_->model().replace(newui::text::TextRange(ordered[i]->offset, ordered[i]->length), ordered[i]->text);
        }
        if (history != nullptr)
        {
            history->endGroup();
        }
        return EditStatus::Ok;
    }

    bool CppEditor::execCommand(EditorCommand command, std::uint32_t flags, const EditorCommandArgs* args)
    {
        // The editing commands act on the source pane (what "handled" means to the caller: false
        // when there's nothing to undo / redo / copy / paste).
        if (textControl_ != nullptr)
        {
            switch (command)
            {
            case EditorCommand::Undo: return textControl_->undo();
            case EditorCommand::Redo: return textControl_->redo();
            case EditorCommand::Cut: return textControl_->cut();
            case EditorCommand::Copy: return textControl_->copy();
            case EditorCommand::Paste: return textControl_->paste();
            default: break;
            }
        }

        // Find / Replace / Go to line open the overlays (VS routes Ctrl+F / Ctrl+H / Ctrl+G here). Any
        // text the command carries seeds the search; a GotoLine that names a line goes straight there.
        if (find_ != nullptr && find_->loaded())
        {
            const bool hasText1 = args != nullptr && args->text1 != nullptr && args->text1Length > 0;
            const bool hasText2 = args != nullptr && args->text2 != nullptr && args->text2Length > 0;
            switch (command)
            {
            case EditorCommand::Find:
                find_->showFind();
                if (hasText1)
                {
                    find_->setQuery(std::wstring(args->text1, args->text1Length));
                }
                return true;
            case EditorCommand::Replace:
                find_->showReplace();
                if (hasText1)
                {
                    find_->setQuery(std::wstring(args->text1, args->text1Length));
                }
                if (hasText2)
                {
                    find_->setReplacement(std::wstring(args->text2, args->text2Length));
                }
                return true;
            case EditorCommand::GotoLine:
                // text1 "line:column" (the problems popup's jump), else number (the line alone).
                if (hasText1)
                {
                    return find_->goToLine(std::wstring(args->text1, args->text1Length));
                }
                if (args != nullptr && args->number > 0)
                {
                    return find_->goToLine(std::to_wstring(args->number));
                }
                find_->showGoToLine();
                return true;
            default:
                break;
            }
        }

        // Anything else (or the overlays failed to load): logged, not handled.
        wchar_t buffer[256];
        if (args)
        {
            swprintf_s(buffer,
                       L"CppEditorControl::execCommand: %s stub (flags=%u, text1Length=%zu, text2Length=%zu, number=%lld)\n",
                       commandName(command), flags, args->text1Length, args->text2Length, args->number);
        }
        else
        {
            swprintf_s(buffer, L"CppEditorControl::execCommand: %s stub (flags=%u, no args)\n", commandName(command), flags);
        }

        OutputDebugStringW(buffer);
        return true;
    }
}
