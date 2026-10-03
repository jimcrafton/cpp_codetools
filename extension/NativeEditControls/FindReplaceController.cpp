#include "FindReplaceController.h"
#include "HighlightController.h"
#include "Logging.h"

#include <newui/bundle.h>
#include <newui/layout.h>
#include <newui/rootview.h>
#include <newui/texthistory.h>
#include <newui/uicolormanager.h>

#include <lex/highlight.h>

#include <Windows.h>

#include <limits>

namespace CodeToolsVsix
{
    namespace
    {
        // Dip, not Dlu - these are chrome sizing (an overlay's own inset/gap, a fixed-width strip),
        // not text-relative, so they should scale with monitor DPI but not with the theme font - see
        // display-units-plan.md's "Monitor DPI scaling" vs "Font-relative sizing" distinction.
        constexpr newui::DisplayValue kEdge(6.0f, newui::DisplayUnit::Dip);       // keep an overlay this far inside the host
        constexpr newui::DisplayValue kCaretGap(6.0f, newui::DisplayUnit::Dip);   // between the caret's line and an overlay next to it
        constexpr newui::DisplayValue kMinimapWidth(28.0f, newui::DisplayUnit::Dip);

        // The lex theme's own color for style, converted from its packed 0xAARRGGBB to newui::Color -
        // the minimap's ticks are content colors (they match the syntax highlight hues), not chrome.
        newui::Color themeColorFor(lex::StyleId style)
        {
            const lex::Theme theme = newui::UIColorManager::isDarkMode() ? lex::Theme::dark() : lex::Theme::light();
            const std::uint32_t argb = theme.style(style).foreground;
            return newui::Color(((argb >> 16) & 0xFF) / 255.0f, ((argb >> 8) & 0xFF) / 255.0f,
                (argb & 0xFF) / 255.0f, ((argb >> 24) & 0xFF) / 255.0f);
        }

        newui::Color minimapColorFor(MatchKind kind)
        {
            switch (kind) {
                case MatchKind::Comment: return themeColorFor(lex::StyleId::Comment);
                case MatchKind::String: return themeColorFor(lex::StyleId::String);
                default: return themeColorFor(lex::StyleId::Default);
            }
        }

        float clampTo(float value, float low, float high)
        {
            if (high < low) {
                return low;
            }
            return value < low ? low : (value > high ? high : value);
        }

        bool shiftHeld()
        {
            return (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
        }

        template <typename T>
        T* named(newui::View& root, const char* name)
        {
            return dynamic_cast<T*>(root.findView(name));
        }

        // Loads "<name>.newui" as a view, hidden, added to host (which owns it); nullptr if it can't be.
        newui::SubView* loadOverlay(newui::View& host, const char* name)
        {
            newui::View* view = newui::Bundle::instance().loadView(name);
            auto* sub = dynamic_cast<newui::SubView*>(view);
            if (sub == nullptr) {
                if (view != nullptr) {
                    view->destroy();
                    delete view;
                }
                logToDebugOut(L"FindReplaceController: could not load an overlay .newui");
                return nullptr;
            }
            sub->setVisible(false);
            // Floats over the host: its layout must never move or size it, or count it in its arrangement.
            // Flagged before it is added, so the layout never touches its bounds or visibility at all.
            sub->setLayoutIgnored(true);
            host.addChild(sub);
            return sub;
        }

        bool parseNumber(const std::wstring& text, std::size_t& at, std::size_t& value)
        {
            std::size_t digits = 0;
            value = 0;
            while (at < text.size() && text[at] >= L'0' && text[at] <= L'9' && digits < 9) {
                value = value * 10 + static_cast<std::size_t>(text[at] - L'0');
                ++at;
                ++digits;
            }
            return digits != 0;
        }

        void skipSpaces(const std::wstring& text, std::size_t& at)
        {
            while (at < text.size() && text[at] == L' ') {
                ++at;
            }
        }
    }

    newui::Point overlayPositionBesideCaret(const newui::Point& caretTopLeft, float caretHeight,
        const newui::Size& overlay, const newui::Size& host, const newui::DisplayMetrics& metrics)
    {
        const float edgeX = kEdge.toPixelsX(metrics);
        const float edgeY = kEdge.toPixelsY(metrics);
        const float caretGapY = kCaretGap.toPixelsY(metrics);

        const float left = clampTo(caretTopLeft.x - 24.0f, edgeX, host.width - overlay.width - edgeX);
        float top = caretTopLeft.y + caretHeight + caretGapY;   // below the caret's line
        if (top + overlay.height > host.height - edgeY) {
            top = caretTopLeft.y - overlay.height - caretGapY;   // no room: above it
        }
        top = clampTo(top, edgeY, host.height - overlay.height - edgeY);
        return newui::Point(left, top);
    }

    bool overlayCoversPoint(const newui::Rect& overlay, const newui::Point& point, float lineHeight)
    {
        return point.x >= overlay.left() && point.x <= overlay.left() + overlay.size().width
            && point.y + lineHeight > overlay.top() && point.y < overlay.top() + overlay.size().height;
    }

    bool overlayCoversRange(const newui::Rect& overlay, const newui::Point& topLeft, float width, float lineHeight)
    {
        return topLeft.x < overlay.right() && overlay.left() < topLeft.x + width
            && topLeft.y < overlay.bottom() && overlay.top() < topLeft.y + lineHeight;
    }

    FindReplaceController::FindReplaceController(newui::View& host, newui::TextFoldingControl& text, HighlightController* highlight)
        : host_(&host), text_(&text), highlight_(highlight)
    {
        // The overview-ruler strip - not a .newui view (custom-painted), so built directly rather
        // than loaded; shown/hidden alongside findBar_ (see openFind()/closeFind()). Added to
        // host_ BEFORE findBar_/goToBar_ below (added last = painted last = on top) so an open bar
        // always reads on top of the minimap where the two happen to overlap - a real, reported bug
        // when they didn't (the minimap drew over the bar).
        minimap_ = new MinimapStrip();
        minimap_->setName("minimap");
        minimap_->setVisible(false);
        minimap_->setLayoutIgnored(true);
        host_->addChild(minimap_);

        findBar_ = loadOverlay(host, "findbar");
        goToBar_ = loadOverlay(host, "gotoline");
        if (!loaded()) {
            return;
        }
        wire();

        std::shared_ptr<bool> alive = alive_;
        minimap_->onLineClicked.add([this, alive](MinimapStrip&, std::size_t line) {
            if (!*alive) {
                return newui::SyncReturn::Ignored;
            }
            if (!matches_.empty()) {
                const std::wstring& document = text_->text();
                std::size_t best = 0;
                std::size_t bestDiff = std::numeric_limits<std::size_t>::max();
                for (std::size_t i = 0; i < matches_.size(); ++i) {
                    const std::size_t matchLine = lineOfOffset(document, matches_[i].start) - 1;
                    const std::size_t diff = matchLine > line ? matchLine - line : line - matchLine;
                    if (diff < bestDiff) {
                        bestDiff = diff;
                        best = i;
                    }
                }
                current_ = best;
                publish(true);
            } else {
                text_->selection().clear();
                text_->caret().setPosition(newui::text::TextPosition(offsetOfLine(text_->text(), line + 1)));
                revealCaret();
            }
            return newui::SyncReturn::Handled;
        });
    }

    FindReplaceController::~FindReplaceController()
    {
        *alive_ = false;
    }

    void FindReplaceController::wire()
    {
        std::shared_ptr<bool> alive = alive_;
        newui::SubView& bar = *findBar_;

        findInput_ = named<newui::TextField>(bar, "findInput");
        replaceInput_ = named<newui::TextField>(bar, "replaceInput");
        replaceRow_ = named<newui::SubView>(bar, "replaceRow");
        expandBtn_ = named<newui::ToolbarButton>(bar, "expandBtn");
        caseBtn_ = named<newui::ToolbarButton>(bar, "caseBtn");
        wordBtn_ = named<newui::ToolbarButton>(bar, "wordBtn");
        prevBtn_ = named<newui::ToolbarButton>(bar, "prevBtn");
        nextBtn_ = named<newui::ToolbarButton>(bar, "nextBtn");
        auto* closeBtn = named<newui::ToolbarButton>(bar, "closeBtn");
        const char* const kindNames[4] = { "kindAll", "kindCode", "kindComment", "kindString" };
        for (std::size_t i = 0; i < 4; ++i) {
            kindChips_[i] = named<newui::ToolbarButton>(bar, kindNames[i]);
        }
        replaceBtn_ = named<newui::Button>(bar, "replaceBtn");
        replaceAllBtn_ = named<newui::Button>(bar, "replaceAllBtn");
        renameBtn_ = named<newui::Button>(bar, "renameBtn");
        count_ = named<newui::Label>(bar, "count");
        goToInput_ = named<newui::TextField>(*goToBar_, "gotoInput");
        goToMessage_ = named<newui::Label>(*goToBar_, "gotoMsg");

        // Not built yet: hidden rather than shown doing nothing. renameBtn_ is shown once a
        // RenameProvider is set (setRenameProvider), so it's covered separately here.
        for (const char* name : { "regionSeg", "breakdownRow" }) {
            if (newui::View* view = bar.findView(name)) {
                view->setVisible(false);
            }
        }
        if (renameBtn_ != nullptr) {
            renameBtn_->setVisible(false);
            // Rename does more than its plain siblings (every real reference to the symbol, found
            // with libclang - not just text matches), so its label is tinted to stand out.
            // HighlightBackground is the closest thing this framework has to an "accent" role.
            renameBtn_->setTextColor(newui::UIColorManager::colorFor(newui::UIColorRole::HighlightBackground));
            renameBtn_->onClick.add([this, alive](newui::Control&) {
                if (*alive) {
                    renameCurrent();
                }
                return newui::SyncReturn::Handled;
            });
        }

        // The Find field: typing changes the query (and drops the automatic whole-word).
        findInput_->model().onChanged.add([this, alive](newui::Model&) {
            if (!*alive || suppress_ > 0) {
                return newui::SyncReturn::Ignored;
            }
            query_ = findInput_->text();
            autoWord_ = false;
            search(caretOffset());
            return newui::SyncReturn::Handled;
        });
        findInput_->onReturnPressed.add([this, alive](newui::TextField&) {
            if (*alive) {
                if (shiftHeld()) {   // Shift+Enter: the other direction
                    previous();
                } else {
                    next();
                }
            }
            return newui::SyncReturn::Handled;
        });
        replaceInput_->onReturnPressed.add([this, alive](newui::TextField&) {
            if (*alive) {
                replaceCurrent();
            }
            return newui::SyncReturn::Handled;
        });
        goToInput_->onReturnPressed.add([this, alive](newui::TextField&) {
            if (*alive) {
                goToLine(goToInput_->text());
            }
            return newui::SyncReturn::Handled;
        });
        goToInput_->model().onChanged.add([this, alive](newui::Model&) {
            if (*alive && suppress_ == 0) {
                showCurrentLineHint();
            }
            return newui::SyncReturn::Handled;
        });

        // Esc closes, from any of the three fields.
        for (newui::TextField* field : { findInput_, replaceInput_, goToInput_ }) {
            field->onKeyDown.add([this, alive](newui::View&, std::uint32_t, int, int, std::uint32_t vk) {
                if (*alive && vk == VK_ESCAPE) {
                    close();
                    return newui::SyncReturn::Handled;
                }
                return newui::SyncReturn::Ignored;
            });
            // A field keeps drawing its selection after it loses focus, so only the one being edited
            // may hold one: clear it when focus moves on (else two fields look selected at once).
            field->onLostFocus.add([field, alive](newui::View&) {
                if (*alive && !field->selection().isEmpty()) {
                    field->selection().clear();
                }
                return newui::SyncReturn::Ignored;
            });
        }

        caseBtn_->onCheckedChanged.add([this, alive](newui::ToolbarButton& sender) {
            if (*alive && suppress_ == 0) {
                setMatchCase(sender.isChecked());
            }
            return newui::SyncReturn::Handled;
        });
        wordBtn_->onCheckedChanged.add([this, alive](newui::ToolbarButton& sender) {
            if (!*alive || suppress_ > 0) {
                return newui::SyncReturn::Ignored;
            }
            if (autoWord_) {   // the lit button was clicked: switch the automatic whole-word off
                autoWord_ = false;
                wholeWord_ = false;
                setWholeWord(false);
            } else {
                setWholeWord(sender.isChecked());
            }
            return newui::SyncReturn::Handled;
        });
        for (std::size_t i = 0; i < 4; ++i) {
            kindChips_[i]->onCheckedChanged.add([this, alive, i](newui::ToolbarButton&) {
                if (*alive && suppress_ == 0) {
                    onKindChip(i);
                }
                return newui::SyncReturn::Handled;
            });
        }

        prevBtn_->onClick.add([this, alive](newui::Control&) {
            if (*alive) {
                previous();
            }
            return newui::SyncReturn::Handled;
        });
        nextBtn_->onClick.add([this, alive](newui::Control&) {
            if (*alive) {
                next();
            }
            return newui::SyncReturn::Handled;
        });
        closeBtn->onClick.add([this, alive](newui::Control&) {
            if (*alive) {
                close();
            }
            return newui::SyncReturn::Handled;
        });
        expandBtn_->onClick.add([this, alive](newui::Control&) {
            if (*alive) {
                showReplace_ = !showReplace_;
                applyReplaceVisibility();
            }
            return newui::SyncReturn::Handled;
        });
        replaceBtn_->onClick.add([this, alive](newui::Control&) {
            if (*alive) {
                replaceCurrent();
            }
            return newui::SyncReturn::Handled;
        });
        replaceAllBtn_->onClick.add([this, alive](newui::Control&) {
            if (*alive) {
                replaceAll();
            }
            return newui::SyncReturn::Handled;
        });

        // Scrolling can bring the current match up under the bar (the editor scrolls to a match only when
        // it is next painted), so check again then.
        text_->onScrollOffsetChanged.add([this, alive](newui::View&, const newui::Point&) {
            if (*alive) {
                moveClearOfCurrentMatch();
            }
            return newui::SyncReturn::Ignored;
        });

        // A resize (an ordinary window resize, or the resize that comes with a DPI change) can
        // leave an open bar positioned/sized for the OLD bounds, and the minimap's viewport-
        // relative position stale, until something else happens to open/search/replace again -
        // re-place and re-measure right away instead of waiting for that.
        //
        // Deliberately the editor's own ScrollView (scroller()), not host_ - confirmed live in
        // the VS debugger that host_->onSizeChanged fires too early for this: SubView::setBounds()
        // calls onSizeChanged() BEFORE its own updateLayout() (subview.cpp), and it's THAT
        // updateLayout() call that resizes host_'s children - including the ScrollView
        // updateMinimap() reads via scroller()->bounds() - so a handler on host_'s own
        // onSizeChanged always sees a stale, pre-resize viewport (the minimap's DisplayValue-based
        // WIDTH still came out right, since that doesn't depend on any descendant's bounds - only
        // its POSITION was wrong, which is exactly the bug this fixes). scroller()'s own
        // onSizeChanged, by contrast, only fires once its own bounds - and host_'s, set earlier in
        // the same cascade - are both already final, so place()'s use of host_->bounds() is safe
        // here too.
        if (newui::ScrollView* scroll = scroller()) {
            scroll->onSizeChanged.add([this, alive](newui::View&, const newui::Size&) {
                if (!*alive) {
                    return newui::SyncReturn::Ignored;
                }
                if (isFindOpen()) {
                    place(*findBar_);
                    updateMinimap();
                } else if (isGoToLineOpen()) {
                    place(*goToBar_);
                }
                if (!isFindOpen() && minimap_ != nullptr && minimap_->isVisible()) {
                    updateMinimap();   // just the problem ticks showing: still pinned to the resized viewport
                }
                return newui::SyncReturn::Ignored;
            });
        }

        // The document changing under an open bar (anything but this class's own edits): matches are stale.
        text_->model().onChanged.add([this, alive](newui::Model&) {
            if (*alive && busy_ == 0 && isFindOpen()) {
                search(caretOffset());
            }
            return newui::SyncReturn::Handled;
        });
    }

    bool FindReplaceController::isFindOpen() const
    {
        return findBar_ != nullptr && findBar_->isVisible();
    }

    bool FindReplaceController::isGoToLineOpen() const
    {
        return goToBar_ != nullptr && goToBar_->isVisible();
    }

    std::size_t FindReplaceController::caretOffset() const
    {
        const newui::text::TextPosition position = text_->caret().position();
        return position.isValid() ? position.offset() : 0;
    }

    void FindReplaceController::focus(newui::SubView* view)
    {
        if (newui::RootView* root = host_->rootView()) {
            root->setFocusedSubView(view);
        }
    }

    void FindReplaceController::setFieldText(newui::TextField* field, const std::wstring& text)
    {
        ++suppress_;
        field->setText(text);
        --suppress_;
    }

    void FindReplaceController::selectAllIn(newui::TextField* field)
    {
        const std::size_t length = field->text().size();
        if (length != 0) {
            field->selection().setRange(newui::text::TextRange(0, length));
        }
    }

    // ---- opening and closing ------------------------------------------------------------------------------

    void FindReplaceController::openFind(bool replace)
    {
        if (!loaded()) {
            return;
        }
        closeGoTo();
        const bool wasOpen = isFindOpen();

        // Seed: a single-line selection, else the identifier the caret is in.
        std::wstring seed;
        bool fromToken = false;
        const std::wstring& document = text_->text();
        const auto& ranges = text_->selection().ranges();
        if (!ranges.empty() && ranges.front().length() > 0 && ranges.front().end() <= document.size()) {
            const std::wstring selected = document.substr(ranges.front().start(), ranges.front().length());
            if (selected.find_first_of(L"\r\n") == std::wstring::npos && selected.size() <= 200) {
                seed = selected;
            }
        }
        if (seed.empty()) {
            seed = tokenAt(document, caretOffset());
            fromToken = !seed.empty();
        }
        if (!seed.empty()) {
            query_ = seed;
            autoWord_ = fromToken;
            setFieldText(findInput_, seed);
            if (replace) {
                setFieldText(replaceInput_, seed);   // a rename starts from the current name
            }
        } else if (!wasOpen) {
            setFieldText(findInput_, query_);
        }

        showReplace_ = replace || (wasOpen && showReplace_);
        applyReplaceVisibility();
        findBar_->setVisible(true);
        if (minimap_ != nullptr) {
            minimap_->setVisible(true);
        }
        if (!wasOpen) {
            place(*findBar_);   // once, at the caret; it stays put while the caret follows the matches
        }
        search(caretOffset());

        // Exactly one field is selected: the one the user types into next. Clear both first, so a selection
        // left over from an earlier open (or from setText above) doesn't stay lit in the other.
        newui::TextField* target = (replace && !seed.empty()) ? replaceInput_ : findInput_;
        for (newui::TextField* field : { findInput_, replaceInput_ }) {
            if (!field->selection().isEmpty()) {
                field->selection().clear();
            }
        }
        focus(target);
        selectAllIn(target);
    }

    void FindReplaceController::showGoToLine()
    {
        if (!loaded()) {
            return;
        }
        closeFind();
        showCurrentLineHint();
        setFieldText(goToInput_, std::wstring());
        goToBar_->setVisible(true);
        fit(*goToBar_);
        place(*goToBar_);
        focus(goToInput_);
    }

    void FindReplaceController::closeFind()
    {
        if (findBar_ == nullptr || !findBar_->isVisible()) {
            return;
        }
        findBar_->setVisible(false);
        matches_.clear();
        current_ = kNoMatch;
        if (highlight_ != nullptr) {
            highlight_->setExtraRanges({});
        }
        if (minimap_ != nullptr) {
            minimap_->setVisible(!problemMarks_.empty());   // the problem ticks stay
            if (!problemMarks_.empty()) {
                updateMinimap();
            }
        }
    }

    void FindReplaceController::closeGoTo()
    {
        if (goToBar_ != nullptr) {
            goToBar_->setVisible(false);
        }
    }

    void FindReplaceController::close()
    {
        const bool wasOpen = isFindOpen() || isGoToLineOpen();
        closeFind();
        closeGoTo();
        if (wasOpen) {
            focus(text_);
        }
    }

    // ---- placement ---------------------------------------------------------------------------------------

    // Height from what is showing (the bar's own FlexLayout: padding, spacing and each visible child's
    // desired height) - a plain view doesn't size itself to its children.
    void FindReplaceController::fit(newui::SubView& bar)
    {
        float spacing = 4.0f;
        float padding = 6.0f;
        if (const auto* layout = dynamic_cast<const newui::FlexLayout*>(bar.layout())) {
            spacing = layout->spacing();
            padding = layout->padding();
        }
        float height = 2.0f * padding;
        std::size_t shown = 0;
        for (const newui::SubView* child : bar.childViews()) {
            if (child->isVisible()) {
                height += child->desiredSize().height;
                ++shown;
            }
        }
        if (shown > 1) {
            height += spacing * static_cast<float>(shown - 1);
        }
        const newui::Rect old = bar.bounds();
        bar.setBounds(newui::Rect(old.left(), old.top(), bar.desiredSize().width, height));
    }

    // Just below the caret's line, aligned with the caret (above it if there's no room below), and
    // inside the host. With no caret rect yet (nothing laid out), the top right corner.
    void FindReplaceController::place(newui::SubView& bar)
    {
        fit(bar);
        const float width = bar.bounds().size().width;
        const float height = bar.bounds().size().height;
        const float hostWidth = host_->bounds().size().width;
        const float hostHeight = host_->bounds().size().height;
        const newui::DisplayMetrics metrics = host_->displayMetrics();
        const float edgeX = kEdge.toPixelsX(metrics);

        float left = clampTo(hostWidth - width - 10.0f, edgeX, hostWidth - width - edgeX);
        float top = 8.0f;
        const newui::Rect caret = text_->controller().caretDocumentRect();
        if (caret.size().height > 0.0f) {
            // caretDocumentRect() is in the layout engine's raw document space (unscrolled) - the
            // same space TextController::drawCaret() itself subtracts scrollOffsetY() from before
            // painting. mapTo() expects text_'s own LOCAL (scrolled-into-view) space instead, so
            // this needs the same subtraction first - without it this only happened to look right
            // at scroll position 0 (both spaces coincide there), landing the bar at the host's
            // bottom edge once scrolled any real distance down (a real, reported bug).
            newui::Point caretTextLocal = caret.pos();
            caretTextLocal.y -= text_->controller().scrollOffsetY();
            const newui::Point at = overlayPositionBesideCaret(text_->mapTo(*host_, caretTextLocal), caret.size().height,
                newui::Size(width, height), newui::Size(hostWidth, hostHeight), metrics);
            left = at.x;
            top = at.y;
        }
        bar.setBounds(newui::Rect(left, top, width, height));
    }

    void FindReplaceController::moveClearOfCurrentMatch()
    {
        if (moving_ || !isFindOpen() || current_ == kNoMatch || current_ >= matches_.size()) {
            return;
        }
        // The match's own on-screen span, not just the caret (which sits at one *end* of it) - a
        // narrow bar could clear the caret's single point while still covering the rest of a long
        // match's word.
        const FindMatch& match = matches_[current_];
        const std::vector<newui::Rect> rects = text_->controller().layoutEngine().hitTestRange(
            newui::text::TextRange(match.start, match.length));
        if (rects.empty()) {
            return;   // not laid out yet
        }
        newui::Rect matchRect = rects.front();
        for (std::size_t i = 1; i < rects.size(); ++i) {   // a match split across visual lines: its bounding box
            const newui::Rect& r = rects[i];
            const float left = matchRect.left() < r.left() ? matchRect.left() : r.left();
            const float top = matchRect.top() < r.top() ? matchRect.top() : r.top();
            const float right = matchRect.right() > r.right() ? matchRect.right() : r.right();
            const float bottom = matchRect.bottom() > r.bottom() ? matchRect.bottom() : r.bottom();
            matchRect = newui::Rect(left, top, right - left, bottom - top);
        }
        // Same document-space-to-text-local fix as place() above - hitTestRange() is also raw,
        // unscrolled document space.
        newui::Point matchTextLocal = matchRect.pos();
        matchTextLocal.y -= text_->controller().scrollOffsetY();
        const newui::Point at = text_->mapTo(*host_, matchTextLocal);
        const newui::Rect bar = findBar_->bounds();
        if (!overlayCoversRange(bar, at, matchRect.size().width, matchRect.size().height)) {
            return;
        }
        moving_ = true;
        const newui::Point moved = overlayPositionBesideCaret(at, matchRect.size().height, bar.size(), host_->bounds().size(),
            host_->displayMetrics());
        findBar_->setBounds(newui::Rect(moved.x, moved.y, bar.size().width, bar.size().height));
        moving_ = false;
    }

    void FindReplaceController::applyReplaceVisibility()
    {
        replaceRow_->setVisible(showReplace_);
        expandBtn_->setIcon(showReplace_ ? "Images/icons/find/chevron-down.svg" : "Images/icons/find/chevron-right.svg");
        if (isFindOpen()) {
            fit(*findBar_);
        }
    }

    // ---- searching ---------------------------------------------------------------------------------------

    void FindReplaceController::setQuery(const std::wstring& query)
    {
        query_ = query;
        autoWord_ = false;
        setFieldText(findInput_, query);
        search(caretOffset());
    }

    void FindReplaceController::setReplacement(const std::wstring& replacement)
    {
        setFieldText(replaceInput_, replacement);
    }

    void FindReplaceController::setMatchCase(bool value)
    {
        options_.matchCase = value;
        syncToggles();
        search(caretOffset());
    }

    void FindReplaceController::setWholeWord(bool value)
    {
        wholeWord_ = value;
        if (!value) {
            autoWord_ = false;
        }
        syncToggles();
        search(caretOffset());
    }

    void FindReplaceController::setKind(KindFilter kind)
    {
        options_.kind = kind;
        ++suppress_;
        for (std::size_t i = 0; i < 4; ++i) {
            kindChips_[i]->setChecked(i == static_cast<std::size_t>(kind));
        }
        --suppress_;
        search(caretOffset());
    }

    void FindReplaceController::onKindChip(std::size_t index)
    {
        if (!kindChips_[index]->isChecked()) {
            // Clicking the active chip again would leave none: keep it.
            ++suppress_;
            kindChips_[index]->setChecked(true);
            --suppress_;
            return;
        }
        setKind(static_cast<KindFilter>(index));
    }

    void FindReplaceController::syncToggles()
    {
        ++suppress_;
        caseBtn_->setChecked(options_.matchCase);
        wordBtn_->setChecked(wordOn());
        --suppress_;
    }

    void FindReplaceController::search(std::size_t fromOffset)
    {
        options_.wholeWord = wordOn();
        matches_ = findAll(text_->text(), query_, options_);
        current_ = firstMatchAtOrAfter(matches_, fromOffset);
        syncToggles();
        publish(true);
    }

    // Shows the state: highlights, the count, which buttons make sense, and (select) the current match
    // selected in the editor.
    void FindReplaceController::publish(bool select)
    {
        if (highlight_ != nullptr) {
            std::vector<newui::text::TextStyleRange> ranges;
            ranges.reserve(matches_.size());
            for (std::size_t i = 0; i < matches_.size(); ++i) {
                ranges.push_back({ matches_[i].start, matches_[i].length,
                    i == current_ ? kCurrentMatchStyleName : kMatchStyleName });
            }
            highlight_->setExtraRanges(std::move(ranges));
        }

        std::string label;
        if (!query_.empty()) {
            label = matches_.empty() ? "No results"
                : std::to_string(current_ + 1) + " of " + std::to_string(matches_.size());
        }
        count_->setText(label);

        const bool any = !matches_.empty();
        prevBtn_->setEnabled(any);
        nextBtn_->setEnabled(any);
        replaceBtn_->setEnabled(any);
        replaceAllBtn_->setEnabled(any);

        if (select && current_ != kNoMatch) {
            selectMatch(current_);
            moveClearOfCurrentMatch();
        }
        updateMinimap();
    }

    void FindReplaceController::selectMatch(std::size_t index)
    {
        const FindMatch& match = matches_[index];
        text_->selection().setRange(newui::text::TextRange(match.start, match.length));
        text_->caret().setPosition(newui::text::TextPosition(match.start + match.length));
        revealCaret();
    }

    newui::ScrollView* FindReplaceController::scroller() const
    {
        for (newui::View* up = text_->parent(); up != nullptr; up = up->parent()) {
            if (auto* scroll = dynamic_cast<newui::ScrollView*>(up)) {
                return scroll;
            }
        }
        return nullptr;
    }

    void FindReplaceController::revealCaret()
    {
        newui::ScrollView* scroll = scroller();
        if (scroll == nullptr || !scroll->vBar()->isVisible()) {
            return;   // nothing scrolls (the text fits, or there is no ScrollView)
        }
        const newui::Rect caret = text_->controller().caretDocumentRect();
        if (caret.size().height <= 0.0f) {
            return;   // not laid out yet: the editor's own scroll-into-view will do what it can
        }
        newui::ScrollBar* bar = scroll->vBar();
        const float page = bar->pageSize();   // how much of the document the view shows at once
        const float top = bar->value();
        float margin = caret.size().height * 3.0f;
        if (margin > page * 0.25f) {
            margin = page * 0.25f;
        }
        if (caret.top() - margin >= top && caret.bottom() + margin <= top + page) {
            return;   // already comfortably in view: leave the page where it is
        }
        bar->setValue(caret.top() + caret.size().height * 0.5f - page * 0.5f);   // the line in the middle
    }

    void FindReplaceController::updateMinimap()
    {
        if (minimap_ == nullptr) {
            return;
        }

        // Pinned to the right edge of the editor's own scrolled viewport, just left of its vertical
        // scrollbar (when there is one) so it doesn't sit on top of it.
        newui::ScrollView* scroll = scroller();
        const newui::Rect viewport = scroll != nullptr ? scroll->bounds() : text_->bounds();
        float scrollBarWidth = 0.0f;
        if (scroll != nullptr && scroll->vBar() != nullptr && scroll->vBar()->isVisible()) {
            scrollBarWidth = scroll->vBar()->bounds().width();
        }
        const float minimapWidth = kMinimapWidth.toPixelsX(host_->displayMetrics());
        minimap_->setBounds(newui::Rect(viewport.right() - scrollBarWidth - minimapWidth, viewport.top(),
            minimapWidth, viewport.height()));

        const std::wstring& document = text_->text();
        minimap_->setLineCount(lineCount(document));
        std::vector<MinimapStrip::Mark> marks = problemMarks_;   // under Find's matches, which come after
        marks.reserve(marks.size() + matches_.size());
        for (std::size_t i = 0; i < matches_.size(); ++i) {
            MinimapStrip::Mark mark;
            mark.line = lineOfOffset(document, matches_[i].start) - 1;   // 1-based -> 0-based
            mark.color = minimapColorFor(matches_[i].kind);
            mark.current = (i == current_);
            marks.push_back(mark);
        }
        minimap_->setMarks(std::move(marks));
        minimap_->setCaretLine(lineOfOffset(document, caretOffset()) - 1);
    }

    void FindReplaceController::setProblemMarks(std::vector<MinimapStrip::Mark> marks)
    {
        problemMarks_ = std::move(marks);
        if (minimap_ == nullptr) {
            return;
        }
        minimap_->setVisible(isFindOpen() || !problemMarks_.empty());
        if (minimap_->isVisible()) {
            updateMinimap();
        }
    }

    void FindReplaceController::next()
    {
        if (matches_.empty()) {
            return;
        }
        current_ = (current_ + 1) % matches_.size();
        publish(true);
    }

    void FindReplaceController::previous()
    {
        if (matches_.empty()) {
            return;
        }
        current_ = (current_ + matches_.size() - 1) % matches_.size();
        publish(true);
    }

    // ---- replacing ---------------------------------------------------------------------------------------

    bool FindReplaceController::replaceCurrent()
    {
        if (current_ == kNoMatch || current_ >= matches_.size()) {
            return false;
        }
        const FindMatch match = matches_[current_];
        const std::wstring replacement = replaceInput_->text();
        ++busy_;
        text_->model().replace(newui::text::TextRange(match.start, match.length), replacement);
        --busy_;

        // Carry on from just after what was put in (which may itself match the query).
        const std::size_t resume = match.start + replacement.size();
        options_.wholeWord = wordOn();
        matches_ = findAll(text_->text(), query_, options_);
        current_ = matches_.empty() ? kNoMatch : 0;
        for (std::size_t i = 0; i < matches_.size(); ++i) {
            if (matches_[i].start >= resume) {
                current_ = i;
                break;
            }
        }
        publish(true);
        return true;
    }

    std::size_t FindReplaceController::replaceAll()
    {
        if (matches_.empty()) {
            return 0;
        }
        const std::size_t count = matches_.size();
        const std::wstring replacement = replaceInput_->text();
        auto* history = dynamic_cast<newui::text::HistoryTextModel*>(&text_->model());
        if (history != nullptr) {
            history->beginGroup();
        }
        ++busy_;
        // Last to first, so the offsets of the ones still to do stay valid.
        for (std::size_t i = matches_.size(); i-- > 0;) {
            text_->model().replace(newui::text::TextRange(matches_[i].start, matches_[i].length), replacement);
        }
        --busy_;
        if (history != nullptr) {
            history->endGroup();
        }
        search(0);
        return count;
    }

    void FindReplaceController::setRenameProvider(RenameProvider provider)
    {
        renameProvider_ = std::move(provider);
        if (renameBtn_ != nullptr) {
            renameBtn_->setVisible(renameProvider_ != nullptr);
            if (isFindOpen()) {
                fit(*findBar_);
            }
        }
    }

    std::size_t FindReplaceController::renameCurrent()
    {
        if (!renameProvider_) {
            return 0;
        }
        const std::size_t offset = (current_ != kNoMatch && current_ < matches_.size()) ? matches_[current_].start : caretOffset();
        std::vector<RenameRange> occurrences = renameProvider_(text_->text(), offset);
        if (occurrences.empty()) {
            count_->setText("No renamable symbol there");
            return 0;
        }

        const std::wstring replacement = replaceInput_->text();
        auto* history = dynamic_cast<newui::text::HistoryTextModel*>(&text_->model());
        if (history != nullptr) {
            history->beginGroup();
        }
        ++busy_;
        // Last to first, so the offsets of the ones still to do stay valid.
        for (std::size_t i = occurrences.size(); i-- > 0;) {
            text_->model().replace(newui::text::TextRange(occurrences[i].offset, occurrences[i].length), replacement);
        }
        --busy_;
        if (history != nullptr) {
            history->endGroup();
        }

        // Every renamed occurrence, at the replacement's own length - NOT a fresh textual search for
        // the old name (query_), which can wrongly re-match a copy of the old name left embedded in
        // the new one (e.g. renaming "count" to "recount" would otherwise highlight a stray "count"
        // inside it, sized for the old, shorter name). occurrences[i].offset is only where it WAS:
        // every occurrence to its left that has since been replaced (they all have, having been
        // done after it in the last-to-first loop above) shifted it by that replacement's own
        // length change, so the running `shift` accumulates those as occurrences are walked in their
        // original (ascending) order.
        matches_.clear();
        matches_.reserve(occurrences.size());
        std::ptrdiff_t shift = 0;
        std::size_t newCurrent = 0;
        for (std::size_t i = 0; i < occurrences.size(); ++i) {
            const RenameRange& occurrence = occurrences[i];
            if (offset >= occurrence.offset && offset < occurrence.offset + occurrence.length) {
                newCurrent = i;
            }
            const std::size_t at = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(occurrence.offset) + shift);
            matches_.push_back(FindMatch{ at, replacement.size(), MatchKind::Code });
            shift += static_cast<std::ptrdiff_t>(replacement.size()) - static_cast<std::ptrdiff_t>(occurrence.length);
        }
        current_ = newCurrent;
        query_ = replacement;
        setFieldText(findInput_, replacement);   // Find now looks for what things were renamed to
        autoWord_ = false;
        publish(true);
        return occurrences.size();
    }

    // ---- Go to line --------------------------------------------------------------------------------------

    void FindReplaceController::setGoToMessage(const std::string& message)
    {
        goToMessage_->setText(message);
    }

    void FindReplaceController::showCurrentLineHint()
    {
        const std::wstring& document = text_->text();
        setGoToMessage("Current line " + std::to_string(lineOfOffset(document, caretOffset()))
            + ". Valid: 1-" + std::to_string(lineCount(document)));
    }

    bool FindReplaceController::goToLine(const std::wstring& input)
    {
        std::size_t at = 0;
        std::size_t line = 0;
        std::size_t column = 1;
        skipSpaces(input, at);
        bool ok = parseNumber(input, at, line);
        if (ok) {
            skipSpaces(input, at);
            if (at < input.size() && input[at] == L':') {
                ++at;
                skipSpaces(input, at);
                ok = parseNumber(input, at, column);
                skipSpaces(input, at);
            }
            ok = ok && at == input.size();
        }
        if (!ok) {
            setGoToMessage("Enter a line number, or line:column");
            return false;
        }
        const std::wstring& document = text_->text();
        const std::size_t lines = lineCount(document);
        if (line < 1 || line > lines) {
            setGoToMessage("Line out of range. Valid: 1-" + std::to_string(lines));
            return false;
        }
        text_->selection().clear();
        text_->caret().setPosition(newui::text::TextPosition(offsetOfLine(document, line, column)));
        revealCaret();
        closeGoTo();
        focus(text_);
        return true;
    }
}
