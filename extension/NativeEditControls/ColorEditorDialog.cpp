#include "ColorEditorDialog.h"
#include "TextEncoding.h"

#include <newui/bundle.h>
#include <newui/layout.h>
#include <newui/rootview.h>
#include <newui/viewstyle.h>

#include <cstdio>
#include <cstdlib>

namespace CodeToolsVsix
{
    namespace
    {
        constexpr std::size_t kValueGridSize = 4;
        constexpr float kSwatchSize = 32.0f;
        constexpr float kSwatchRadius = 6.0f;

        float clampf(float value, float lo, float hi)
        {
            return value < lo ? lo : (value > hi ? hi : value);
        }

        std::string toUpperAscii(std::string text)
        {
            for (char& c : text) {
                if (c >= 'a' && c <= 'z') {
                    c = static_cast<char>(c - 'a' + 'A');
                }
            }
            return text;
        }

        // color().toString() is always "#RRGGBBAA" (9 chars, color.h's own fixed format) - the
        // mockup's own hex field/compare label are RGB-only (alpha lives in the separate "A"
        // value-grid field), so every real display point here wants just the middle 6 digits.
        std::string rgbHexDigits(const newui::Color& color)
        {
            return color.toString().substr(1, 6);
        }
    }

    ColorEditorDialog::ColorEditorDialog()
    {
        setName("coloreditordialog");
        newui::Bundle::instance().loadDialog(*this);
        buildChrome();
    }

    void ColorEditorDialog::buildChrome()
    {
        if (auto* pickerRow = dynamic_cast<newui::SubView*>(rootView().findView("pickerRow"))) {
            // The 3 placeholder children (svSquare/hueRail/alphaRail) are decorative-only - see
            // this class's own header comment - replaced wholesale with one real ColorPicker.
            std::vector<newui::SubView*> placeholders = pickerRow->childViews();
            for (newui::SubView* child : placeholders) {
                pickerRow->removeChild(child);
                delete child;
            }
            colorPicker_ = new ColorPicker();
            colorPicker_->setName("colorPickerReal");
            colorPicker_->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
            pickerRow->addChild(colorPicker_);
            colorPicker_->onColorChanged.add([this](ColorPicker&) {
                // See color()'s own comment (ColorEditorDialog.h) for why this can't just be a
                // live colorPicker_->color() read at Apply time instead.
                committedColor_ = colorPicker_->color();
                refreshFromColor();
                return newui::SyncReturn::Handled;
            });
        }

        hexField_ = dynamic_cast<newui::TextField*>(rootView().findView("hexInput"));
        if (hexField_ != nullptr) {
            hexField_->onLostFocus.add([this](newui::View&) { commitHexField(); return newui::SyncReturn::Ignored; });
            hexField_->onReturnPressed.add([this](newui::TextField&) { commitHexField(); return newui::SyncReturn::Handled; });
        }

        rgbTab_ = dynamic_cast<newui::Button*>(rootView().findView("rgbTab"));
        if (rgbTab_ != nullptr) {
            rgbTab_->onClick.add([this](newui::Control&) { setFormatTab(Format::Rgb); return newui::SyncReturn::Handled; });
        }
        hslTab_ = dynamic_cast<newui::Button*>(rootView().findView("hslTab"));
        if (hslTab_ != nullptr) {
            hslTab_->onClick.add([this](newui::Control&) { setFormatTab(Format::Hsl); return newui::SyncReturn::Handled; });
        }

        static const char* kLabelNames[kValueGridSize] = {"rFieldLabel", "gFieldLabel", "bFieldLabel", "aFieldLabel"};
        static const char* kValueNames[kValueGridSize] = {"rFieldValue", "gFieldValue", "bFieldValue", "aFieldValue"};
        for (std::size_t i = 0; i < kValueGridSize; ++i) {
            valueGridLabels_[i] = dynamic_cast<newui::Label*>(rootView().findView(kLabelNames[i]));
            valueGridFields_[i] = dynamic_cast<newui::TextField*>(rootView().findView(kValueNames[i]));
            if (valueGridFields_[i] != nullptr) {
                std::size_t index = i;
                newui::TextField* field = valueGridFields_[i];
                valueGridFields_[i]->onLostFocus.add([this, index, field](newui::View&) {
                    commitValueGridField(index, wideToUtf8(field->text()));
                    return newui::SyncReturn::Ignored;
                });
                valueGridFields_[i]->onReturnPressed.add([this, index, field](newui::TextField&) {
                    commitValueGridField(index, wideToUtf8(field->text()));
                    return newui::SyncReturn::Handled;
                });
            }
        }

        oldSwatch_ = dynamic_cast<newui::SubView*>(rootView().findView("oldSwatch"));
        if (oldSwatch_ != nullptr) {
            oldSwatch_->onMouseDown.add([this](newui::View&, const newui::Point&, std::uint32_t, std::uint32_t) {
                revertToOld();
                return newui::SyncReturn::Handled;
            });
        }
        newSwatch_ = dynamic_cast<newui::SubView*>(rootView().findView("newSwatch"));
        compareHexLabel_ = dynamic_cast<newui::Label*>(rootView().findView("compareHex"));
        // Real screen-color-picking is out of scope - see this class's own header comment.
        eyedropperBtn_ = dynamic_cast<newui::Button*>(rootView().findView("eyedropperBtn"));

        swatchesRow_ = dynamic_cast<newui::SubView*>(rootView().findView("swatchesRow"));
        addSwatchBtn_ = dynamic_cast<newui::Button*>(rootView().findView("addSwatchBtn"));
        std::vector<newui::SubView*> swatches = swatchViews();
        for (std::size_t i = 0; i < swatches.size(); ++i) {
            std::size_t index = i;
            swatches[i]->onMouseDown.add([this, index](newui::View&, const newui::Point&, std::uint32_t, std::uint32_t) {
                selectSwatch(index);
                return newui::SyncReturn::Handled;
            });
        }
        if (addSwatchBtn_ != nullptr) {
            addSwatchBtn_->onClick.add([this](newui::Control&) { addSwatchFromCurrent(); return newui::SyncReturn::Handled; });
        }

        cancelBtn_ = dynamic_cast<newui::Button*>(rootView().findView("cancelBtn"));
        if (cancelBtn_ != nullptr) {
            cancelBtn_->onClick.add([this](newui::Control&) { close(newui::DialogResult::Cancel); return newui::SyncReturn::Handled; });
        }
        applyBtn_ = dynamic_cast<newui::Button*>(rootView().findView("applyBtn"));
        if (applyBtn_ != nullptr) {
            applyBtn_->onClick.add([this](newui::Control&) { close(newui::DialogResult::Ok); return newui::SyncReturn::Handled; });
        }
        // The header's own "x" close button - same as Cancel.
        if (auto* closeBtn = dynamic_cast<newui::Button*>(rootView().findView("closeBtn"))) {
            closeBtn->onClick.add([this](newui::Control&) { close(newui::DialogResult::Cancel); return newui::SyncReturn::Handled; });
        }
    }

    void ColorEditorDialog::setColor(const newui::Color& color)
    {
        oldColor_ = color;
        // Seeded here too, not just inside colorPicker_'s own onColorChanged (buildChrome()'s
        // wiring) - setColor() below no-ops (fires no onColorChanged at all) if color already
        // equals colorPicker_'s own current default state, which would otherwise leave
        // committedColor_ at its own default-constructed value instead of this real seed.
        committedColor_ = color;
        colorPicker_->setColor(color);
        // setColor() above no-ops (no onColorChanged) if color already equals colorPicker_'s own
        // current one - force the refresh unconditionally anyway, since this is a fresh seed and
        // oldSwatch_ in particular must repaint regardless.
        refreshFromColor();
        if (oldSwatch_ != nullptr) {
            oldSwatch_->style().backgroundFill().setKind(newui::gfx::PaintKind::Color);
            oldSwatch_->style().backgroundFill().setColor(oldColor_);
            oldSwatch_->redraw();
        }
    }

    void ColorEditorDialog::setFormatTab(Format format)
    {
        if (formatTab_ == format) {
            return;
        }
        formatTab_ = format;
        refreshValueGrid();
    }

    void ColorEditorDialog::commitHexField()
    {
        if (hexField_ == nullptr || colorPicker_ == nullptr) {
            return;
        }
        newui::Color parsed;
        if (!newui::Color::fromString(wideToUtf8(hexField_->text()), parsed)) {
            return;
        }
        colorPicker_->setColor(newui::Color(parsed.r, parsed.g, parsed.b, colorPicker_->alpha()));
    }

    void ColorEditorDialog::commitValueGridField(std::size_t index, const std::string& text)
    {
        if (index >= kValueGridSize || colorPicker_ == nullptr) {
            return;
        }
        char* end = nullptr;
        float value = std::strtof(text.c_str(), &end);
        if (end == text.c_str()) {
            return;
        }

        if (index == 3) {
            colorPicker_->setAlpha(clampf(value, 0.0f, 100.0f) / 100.0f);
            return;
        }

        if (formatTab_ == Format::Rgb) {
            newui::Color c = color();
            float channel = clampf(value, 0.0f, 255.0f) / 255.0f;
            if (index == 0) c.r = channel;
            else if (index == 1) c.g = channel;
            else c.b = channel;
            colorPicker_->setColor(c);
        } else {
            newui::HSLColor hsl = color().toHSL();
            if (index == 0) hsl.h = clampf(value, 0.0f, 360.0f);
            else if (index == 1) hsl.s = clampf(value, 0.0f, 100.0f) / 100.0f;
            else hsl.l = clampf(value, 0.0f, 100.0f) / 100.0f;
            colorPicker_->setColor(newui::Color::fromHSL(hsl));
        }
    }

    void ColorEditorDialog::selectSwatch(std::size_t index)
    {
        std::vector<newui::SubView*> swatches = swatchViews();
        if (index >= swatches.size() || colorPicker_ == nullptr) {
            return;
        }
        colorPicker_->setColor(swatches[index]->style().backgroundFill().color());
    }

    void ColorEditorDialog::addSwatchFromCurrent()
    {
        if (swatchesRow_ == nullptr) {
            return;
        }
        std::size_t newIndex = swatchViews().size();

        auto* swatch = new newui::SubView();
        swatch->setName("swatch" + std::to_string(newIndex));
        swatch->setVisible(true);
        swatch->setDesiredSize(newui::Size(kSwatchSize, kSwatchSize));
        auto style = std::make_unique<newui::ViewStyle>();
        style->setRectRadius(kSwatchRadius);
        style->backgroundFill().setKind(newui::gfx::PaintKind::Color);
        style->backgroundFill().setColor(color());
        swatch->setStyle(std::move(style));
        swatch->onMouseDown.add([this, newIndex](newui::View&, const newui::Point&, std::uint32_t, std::uint32_t) {
            selectSwatch(newIndex);
            return newui::SyncReturn::Handled;
        });

        // "+" stays last regardless of addChild()'s own append-only order - detach it, add the
        // new swatch, then re-attach it (removeChild() only detaches, never deletes - same "copy
        // the list first, delete each" shape GradientEditorDialog's own rebuildPresetsRow() uses,
        // just without the delete since addSwatchBtn_ is reused, not replaced).
        if (addSwatchBtn_ != nullptr) {
            swatchesRow_->removeChild(addSwatchBtn_);
        }
        swatchesRow_->addChild(swatch);
        if (addSwatchBtn_ != nullptr) {
            swatchesRow_->addChild(addSwatchBtn_);
        }
    }

    void ColorEditorDialog::revertToOld()
    {
        if (colorPicker_ == nullptr) {
            return;
        }
        colorPicker_->setColor(oldColor_);
        refreshFromColor();
    }

    newui::TextField* ColorEditorDialog::valueGridField(std::size_t index) const
    {
        return index < kValueGridSize ? valueGridFields_[index] : nullptr;
    }

    std::vector<newui::SubView*> ColorEditorDialog::swatchViews() const
    {
        std::vector<newui::SubView*> result;
        if (swatchesRow_ == nullptr) {
            return result;
        }
        for (newui::SubView* child : swatchesRow_->childViews()) {
            if (child != addSwatchBtn_) {
                result.push_back(child);
            }
        }
        return result;
    }

    void ColorEditorDialog::refreshFromColor()
    {
        newui::Color c = color();
        if (newSwatch_ != nullptr) {
            newSwatch_->style().backgroundFill().setKind(newui::gfx::PaintKind::Color);
            newSwatch_->style().backgroundFill().setColor(c);
            newSwatch_->redraw();
        }
        if (hexField_ != nullptr) {
            hexField_->setText(utf8ToWide(rgbHexDigits(c)));
        }
        if (compareHexLabel_ != nullptr) {
            compareHexLabel_->setText("#" + toUpperAscii(rgbHexDigits(c)));
        }
        refreshValueGrid();
    }

    void ColorEditorDialog::refreshValueGrid()
    {
        newui::Color c = color();
        static const char* kRgbLabels[kValueGridSize] = {"R", "G", "B", "A"};
        static const char* kHslLabels[kValueGridSize] = {"H", "S", "L", "A"};
        const char* const* labels = (formatTab_ == Format::Rgb) ? kRgbLabels : kHslLabels;

        float values[kValueGridSize] = {};
        if (formatTab_ == Format::Rgb) {
            values[0] = c.r * 255.0f;
            values[1] = c.g * 255.0f;
            values[2] = c.b * 255.0f;
        } else {
            newui::HSLColor hsl = c.toHSL();
            values[0] = hsl.h;
            values[1] = hsl.s * 100.0f;
            values[2] = hsl.l * 100.0f;
        }
        values[3] = c.a * 100.0f;

        for (std::size_t i = 0; i < kValueGridSize; ++i) {
            if (valueGridLabels_[i] != nullptr) {
                valueGridLabels_[i]->setText(labels[i]);
            }
            if (valueGridFields_[i] != nullptr) {
                char buf[16];
                std::snprintf(buf, sizeof(buf), "%.0f", values[i]);
                valueGridFields_[i]->setText(utf8ToWide(buf));
            }
        }
    }
}
