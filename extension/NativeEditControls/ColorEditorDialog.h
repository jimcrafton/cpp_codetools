#pragma once

#include "ColorPicker.h"

#include <newui/color.h>
#include <newui/controls.h>
#include <newui/dialogs.h>
#include <newui/subview.h>

#include <cstddef>
#include <string>
#include <vector>

namespace CodeToolsVsix
{
    // Real, second newui::Dialog with fully custom content in this codebase (after
    // GradientEditorDialog) - and the first one whose static chrome is loaded from a real
    // .newui JSON5 resource (Resources/coloreditordialog.newui, hand-converted from
    // design/reference/color_editor_dialog.html via the newui-view-json5 workflow) via
    // newui::Bundle::loadDialog() rather than hand-built in C++ via ViewBuilder<T>
    // (GradientEditorDialog's own approach) - the .newui file already IS the design, this class
    // only needs to find its named nodes (View::findView()) and wire real behavior onto them.
    //
    // The .newui file's own "pickerRow" holds 3 decorative placeholder SubViews (svSquare/
    // hueRail/alphaRail - solid-color/gradient-fill stand-ins, since ColorPicker isn't a
    // reflection-registered loadable type) rather than a real interactive picker - buildChrome()
    // removes those 3 and grafts in one real ColorPicker (ColorPicker.h/.cpp, already built and
    // proven inside GradientEditorDialog's own selectedItemEditor_) sized to fill pickerRow's own
    // client bounds instead. ColorPicker's own FlexLayout (Horizontal, svSquare weighted, hue/
    // alpha rails fixed-width) re-lays-out its 3 real children correctly on that resize with no
    // extra work needed here.
    //
    // Every other named node (hex field, RGB/HSL format tabs, the R/G/B/A or H/S/L/A value grid,
    // the compare/revert swatches, the swatches row, footer) is a real, already-styled node the
    // .newui file already built - this class just finds each one and wires its real interaction
    // to a real public method, same "expose every real child control, drive the real method a
    // click would" testability convention GradientEditorDialog already established.
    //
    // No eyedropper (real screen-color-picking) - out of scope, see this project's own plan
    // notes; eyedropperButton() is exposed but its onClick is left unwired.
    class ColorEditorDialog : public newui::Dialog
    {
    public:
        ColorEditorDialog();

        // Seeds both the working color (pushed straight into colorPicker_ - the one real source
        // of truth every other control here reads back out of via its own onColorChanged, not a
        // second, separately-tracked copy) and the "old" compare color (revertToOld()'s own
        // target) - call before showModal().
        void setColor(const newui::Color& color);

        // Returns committedColor_, a plain member kept in sync with colorPicker_->color() on
        // every colorPicker_->onColorChanged (buildChrome()'s own wiring, same place
        // refreshFromColor() already runs) - NOT a live read through colorPicker_ itself (the
        // original shape). That distinction matters: newui::Dialog's own WM_CLOSE handler
        // (frame.cpp) synchronously calls DestroyWindow(), which synchronously fires WM_DESTROY
        // -> Frame::destroy() -> `delete rootView_` - the *entire* child tree, colorPicker_
        // included, is gone before showModal() ever returns control to whoever called it. Reading
        // colorPicker_->color() here read already-freed memory the whole time (confirmed live in
        // the debugger: hue_/sat_/val_/alpha_ all showing MSVC's 0xdd debug-heap fill pattern) -
        // never caught since showModal() is never called from an automated test (a real blocking
        // native dialog), only from manually clicking Apply in testharness.exe. This is arguably
        // a real newui::Dialog bug (a caller has no safe way to read a child widget's state after
        // showModal() returns at all) worth cleaning up in the user's own D:\code\newui checkout
        // eventually - not fixed here, since this vendored 3rdparty/newui copy tracks that repo
        // via git (see this project's own "no manual newui vendor copy" convention). oldColor_
        // (below) never had this problem, since setColor() already captured it into a plain
        // member eagerly, before the dialog ever closes - committedColor_ just applies that same
        // pattern to the *current* color too.
        newui::Color color() const { return committedColor_; }

        // Which 4-field value grid is showing - R/G/B/A (the default, matching the mockup's own
        // default "rgb" tab) or H/S/L/A. Purely a display/edit-target choice - working_'s own
        // real storage is always RGB(A) (newui::Color itself), converted on the fly via
        // Color::toHSL()/fromHSL() the same way GradientEditorDialog/ColorPicker never hand-roll
        // color math themselves.
        enum class Format { Rgb, Hsl };
        void setFormatTab(Format format);
        Format formatTab() const { return formatTab_; }

        // hexField()'s own onLostFocus/onReturnPressed handler - parses hex via
        // newui::Color::fromString(), preserving working_'s own current alpha (the mockup's own
        // hex field is RGB-only; alpha is the separate "A" value-grid field), commits on success,
        // silently no-ops on invalid text - same contract every other hex-entry point in this
        // codebase already has (see GradientEditorDialog::commitHexField()'s own comment).
        void commitHexField();

        // One of the 4 value-grid TextFields' own onLostFocus/onReturnPressed handler - index is
        // 0-3 (R/G/B/A or H/S/L/A depending on formatTab()). Parses text as a plain number,
        // clamps to that channel's real range, silently no-ops on unparseable text.
        void commitValueGridField(std::size_t index, const std::string& text);

        // Applies swatchViews()[index]'s own already-painted color (read straight off its real
        // ViewStyle::backgroundFill(), not a separate parallel array - the .newui file's 8 seed
        // swatches and any addSwatchFromCurrent()-appended one are both real SubViews, this is
        // the one source of truth for what color each represents). Out-of-range index is a
        // silent no-op. The real path a swatch click calls.
        void selectSwatch(std::size_t index);

        // Appends a new swatch SubView (same size/style as the seed ones) painted with working_'s
        // current color, inserted into swatchesRow() just before addSwatchButton() - not
        // persisted across process restarts, same non-persisted scope GradientEditorDialog's own
        // presetRegistry() already has. The real path addSwatchButton()'s click calls.
        void addSwatchFromCurrent();

        // Restores working_ to whatever setColor() last seeded as the "old" compare color - the
        // real path oldSwatch()'s click calls (matching the mockup's own "Previous -> new" compare
        // swatch, whose left half is a real revert button).
        void revertToOld();

        // Exposed for testability, same convention GradientEditorDialog's own child-control
        // getters already use.
        ColorPicker* colorPicker() const { return colorPicker_; }
        newui::TextField* hexField() const { return hexField_; }
        newui::Button* rgbTab() const { return rgbTab_; }
        newui::Button* hslTab() const { return hslTab_; }
        newui::TextField* valueGridField(std::size_t index) const;
        newui::SubView* oldSwatch() const { return oldSwatch_; }
        newui::SubView* newSwatch() const { return newSwatch_; }
        newui::Label* compareHexLabel() const { return compareHexLabel_; }
        newui::Button* eyedropperButton() const { return eyedropperBtn_; }
        newui::SubView* swatchesRow() const { return swatchesRow_; }
        std::vector<newui::SubView*> swatchViews() const;
        newui::Button* addSwatchButton() const { return addSwatchBtn_; }
        newui::Button* cancelButton() const { return cancelBtn_; }
        newui::Button* applyButton() const { return applyBtn_; }

    private:
        // Loads Resources/coloreditordialog.newui (via setName() + Bundle::loadDialog(), see this
        // class's own header comment), finds every named node this class needs, grafts a real
        // ColorPicker into pickerRow in place of its 3 decorative placeholders, and wires every
        // real interaction to a real public method above. Called once, from the constructor.
        void buildChrome();

        // Repaints newSwatch_ (colorPicker_'s own current color), hexField_'s text (RGB hex, no
        // alpha - see commitHexField()'s own comment), and the 4 value-grid fields/labels for the
        // current formatTab_. The one real refresh colorPicker_'s own onColorChanged calls (see
        // buildChrome()'s wiring) - every commit path in this class (hex/value-grid text, a
        // swatch click, revertToOld()) goes back through colorPicker_->setColor() rather than
        // updating these display widgets directly, so there is exactly one place color() can
        // actually change and exactly one place that reacts to it.
        void refreshFromColor();

        // Repaints just the 4 value-grid fields/labels for the current formatTab_ and colorPicker_'s
        // own current color - called from setFormatTab() (switching tabs alone doesn't change
        // color(), just how it's displayed, so the full refreshFromColor() above would be
        // redundant work, not wrong, but this is the narrower one actually needed there).
        void refreshValueGrid();

        newui::Color oldColor_;
        // See color()'s own comment above for why this exists at all - kept in sync with
        // colorPicker_->color() on every real change, never read lazily through colorPicker_
        // itself once the dialog might have already closed.
        newui::Color committedColor_;
        Format formatTab_ = Format::Rgb;

        ColorPicker* colorPicker_ = nullptr;
        newui::TextField* hexField_ = nullptr;
        newui::Button* rgbTab_ = nullptr;
        newui::Button* hslTab_ = nullptr;
        // rField/gField/bField/aField's own TextFields (H/S/L/A relabels the same 4 widgets
        // rather than swapping in a second set - matches the mockup's own buildValueGrid(),
        // which rebuilds one shared 4-field grid's labels/keys per tab instead of keeping two).
        newui::Label* valueGridLabels_[4] = {};
        newui::TextField* valueGridFields_[4] = {};
        newui::SubView* oldSwatch_ = nullptr;
        newui::SubView* newSwatch_ = nullptr;
        newui::Label* compareHexLabel_ = nullptr;
        newui::Button* eyedropperBtn_ = nullptr;
        newui::SubView* swatchesRow_ = nullptr;
        newui::Button* addSwatchBtn_ = nullptr;
        newui::Button* cancelBtn_ = nullptr;
        newui::Button* applyBtn_ = nullptr;
    };
}
