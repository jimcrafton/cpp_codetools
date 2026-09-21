#include "PropertyEditor.h"
#include "CalloutPlacement.h"
#include "ColorChoices.h"
#include "ColorEditorDialog.h"
#include "GradientEditorDialog.h"
#include "PaintUtils.h"
#include "ViewStyleRegistry.h"

#include <newui/application.h>
#include <newui/controls.h>
#include <newui/dialogs.h>
#include <newui/rootview.h>
#include <newui/uicolormanager.h>

#include <algorithm>
#include <cctype>

namespace CodeToolsVsix
{
    namespace
    {
        std::string toLower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        }

        std::string formatFloat(float value)
        {
            std::string text = std::to_string(value);
            // Trims the trailing zeros std::to_string always pads a float
            // with (e.g. "100.000000") down to a shorter, still-round-
            // trippable form ("100") - matches how a hand-typed value
            // looks, not a printf-style fixed width.
            while (!text.empty() && text.back() == '0') {
                text.pop_back();
            }
            if (!text.empty() && text.back() == '.') {
                text.pop_back();
            }
            return text;
        }

        std::string gradientKindName(newui::gfx::GradientKind kind)
        {
            switch (kind) {
            case newui::gfx::GradientKind::Linear: return "Linear";
            case newui::gfx::GradientKind::Radial: return "Radial";
            case newui::gfx::GradientKind::Conic: return "Conic";
            default: return "Point";
            }
        }

        constexpr float kTypePickerCellSize = 84.0f;
        constexpr float kTypePickerGap = 10.0f;
        constexpr float kTypePickerLabelHeight = 16.0f;
        constexpr float kTypePickerTopReserve = 20.0f;  // clears CalloutTool's own top tail+margin

        // One clickable option cell in the Layout/ViewStyle type-swap popup - plain SubView + its
        // own click detection, same "custom control, own onMouseDown" shape PresetButton/
        // AddPresetButton (GradientEditorDialog.cpp) already establish for a custom-painted
        // popup/dialog cell. A real child newui::Label carries the display name (this codebase's
        // "real control over hand-painted approximation" convention); the preview area above it is
        // either drawn directly (paintPreview, Layout's own placeholder glyphs - see
        // ViewStyleRegistry.h's own comment on why no icon assets exist yet) or filled by a real
        // live-painted child SubView the caller adds itself (ViewStyle's own live preview, added as
        // an ordinary child after construction) - paintPreview is only invoked when no such child
        // was added, so the two approaches never fight over the same pixels.
        //
        // onSelected is invoked on click and nothing else - it captures property_/instance_/
        // postCommitSync_ *by value*, never `this` (the PropertyEditor that created this cell,
        // which PropertiesGrid resets right after showing the popup - see LayoutPropertyEditor::
        // editAsync()'s own comment).
        class TypePickerCell : public newui::SubView
        {
        public:
            TypePickerCell(const std::string& label, std::function<void()> onSelected,
                    std::function<void(BLContext&, const newui::Rect&)> paintPreview = nullptr)
                : onSelected_(std::move(onSelected)), paintPreview_(std::move(paintPreview))
            {
                setVisible(true);
                onMouseDown.add(this, &TypePickerCell::handleMouseDown);

                auto* labelView = new newui::Label();
                labelView->setText(label);
                labelView_ = labelView;
                addChild(labelView);
            }

            void setBounds(const newui::Rect& bounds) override
            {
                newui::SubView::setBounds(bounds);
                newui::Rect local = getClientBounds();
                labelView_->setBounds(newui::Rect(0.0f, local.height() - kTypePickerLabelHeight,
                    local.width(), kTypePickerLabelHeight));
            }

            newui::Rect previewRect() const
            {
                newui::Rect local = getClientBounds();
                return newui::Rect(4.0f, 4.0f, local.width() - 8.0f, local.height() - kTypePickerLabelHeight - 8.0f);
            }

            void paint(BLContext& ctx) override
            {
                newui::Rect bounds = getClientBounds();
                if (bounds.width() <= 0.0f || bounds.height() <= 0.0f) {
                    return;
                }
                ctx.save();
                ctx.set_fill_style(newui::UIColorManager::colorFor(newui::UIColorRole::ControlBackground).toBLRgba32());
                ctx.fill_round_rect(BLRect(bounds), 6.0);
                ctx.set_stroke_style(newui::UIColorManager::colorFor(newui::UIColorRole::ControlBorder).toBLRgba32());
                ctx.set_stroke_width(1.0);
                ctx.stroke_round_rect(BLRect(bounds), 6.0);
                ctx.restore();

                if (paintPreview_) {
                    paintPreview_(ctx, previewRect());
                }
            }

        private:
            newui::SyncReturn handleMouseDown(newui::View& /*sender*/, const newui::Point& /*pt*/,
                    std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/)
            {
                if (onSelected_) {
                    onSelected_();
                }
                return newui::SyncReturn::Handled;
            }

            std::function<void()> onSelected_;
            std::function<void(BLContext&, const newui::Rect&)> paintPreview_;
            newui::Label* labelView_ = nullptr;
        };

        // Simple placeholder glyphs for the 4 general-purpose Layout subclasses - no real icon
        // assets exist yet (see ViewStyleRegistry.h's own comment); these are procedurally drawn
        // rather than loaded from Resources/Images/icons/layout/ the way ToolboxRegistry's own
        // table resolves an icon, purely to avoid needing new asset files for this pass. Swapping
        // in real SVG assets later means changing this function's body, not any caller.
        void paintLayoutGlyph(BLContext& ctx, const newui::Rect& r, const std::string& layoutClassName)
        {
            BLRgba32 stroke = newui::UIColorManager::colorFor(newui::UIColorRole::ControlText).toBLRgba32();
            ctx.save();
            ctx.set_stroke_style(stroke);
            ctx.set_fill_style(stroke);
            ctx.set_stroke_width(1.5);

            if (layoutClassName == "FlexLayout") {
                float barW = r.width() / 4.0f;
                for (int i = 0; i < 3; ++i) {
                    float x = r.left() + barW * 0.5f + float(i) * barW * 1.15f;
                    ctx.fill_round_rect(BLRect(x, r.top(), barW * 0.7, r.height()), 2.0);
                }
            } else if (layoutClassName == "GridLayout") {
                float cellW = r.width() / 2.0f;
                float cellH = r.height() / 2.0f;
                float gap = 3.0f;
                for (int row = 0; row < 2; ++row) {
                    for (int col = 0; col < 2; ++col) {
                        ctx.stroke_rect(BLRect(r.left() + float(col) * cellW + gap * 0.5,
                            r.top() + float(row) * cellH + gap * 0.5, cellW - gap, cellH - gap));
                    }
                }
            } else if (layoutClassName == "CardLayout") {
                float w = r.width() * 0.75f;
                float h = r.height() * 0.75f;
                for (int i = 0; i < 3; ++i) {
                    float offset = float(i) * 6.0f;
                    ctx.stroke_round_rect(BLRect(r.left() + offset, r.top() + offset, w, h), 4.0);
                }
            } else {
                // AnchorLayout, and any future Layout with no dedicated glyph yet - a corner-pin
                // mark in each of the 4 corners, evoking "anchored to an edge".
                float len = r.width() * 0.18f;
                for (int cx = 0; cx < 2; ++cx) {
                    for (int cy = 0; cy < 2; ++cy) {
                        float x = cx == 0 ? r.left() : r.right();
                        float y = cy == 0 ? r.top() : r.bottom();
                        float dx = cx == 0 ? len : -len;
                        float dy = cy == 0 ? len : -len;
                        ctx.stroke_line(x, y, x + dx, y);
                        ctx.stroke_line(x, y, x, y + dy);
                    }
                }
            }
            ctx.restore();
        }

        // Lays out n square cells in a single centered row inside a popup of size popupSize,
        // returning each cell's own local (popup-relative) rect - shared by LayoutPropertyEditor/
        // ViewStylePropertyEditor's editAsync() below, which differ only in what each cell shows.
        std::vector<newui::Rect> typePickerCellRects(std::size_t count, newui::Size popupSize)
        {
            std::vector<newui::Rect> rects;
            rects.reserve(count);
            float totalWidth = float(count) * kTypePickerCellSize + float(count > 0 ? count - 1 : 0) * kTypePickerGap;
            float startX = (popupSize.width - totalWidth) * 0.5f;
            for (std::size_t i = 0; i < count; ++i) {
                float x = startX + float(i) * (kTypePickerCellSize + kTypePickerGap);
                rects.emplace_back(x, kTypePickerTopReserve, kTypePickerCellSize, kTypePickerCellSize);
            }
            return rects;
        }

        // kTypePickerTopReserve reserved symmetrically on *every* edge (not just top) - this
        // popup's own tail can now land on any of the 4 sides (placeCallout(), above), so its
        // content can't assume the tail's own clearance only ever eats into the top margin the
        // way it used to when the popup was always placed below its anchor.
        newui::Size typePickerPopupSize(std::size_t optionCount)
        {
            return newui::Size(
                float(optionCount) * (kTypePickerCellSize + kTypePickerGap) - kTypePickerGap + kTypePickerTopReserve * 2.0f,
                kTypePickerCellSize + kTypePickerTopReserve * 2.0f);
        }

        // Whether a candidate Font would actually resolve to a real, loadable BLFont -
        // FontManager::getFont() (called by Font::blFont()) matches name/bold/italic's combined
        // lookup name case-insensitively against FontManager::listFonts() and caches nothing on a
        // failed lookup, so this is a cheap, side-effect-free way to reject an edit *before*
        // committing it, rather than only discovering the failure later at paint time (a real,
        // live-reported crash: picking an already-suffixed face name like "Trebuchet MS Bold" as
        // the font *name* and also ticking "bold" composes "Trebuchet MS Bold Bold", which isn't a
        // real installed face). Only name/size/bold/italic feed into that lookup at all -
        // underlined/strikeThrough are plain storage Font::blFont() never consults (see Font's own
        // class comment, font.h), so those two are never gated by this.
        bool fontResolves(const newui::Font& font)
        {
            return font.blFont() != nullptr;
        }

        // Parses exactly `count` comma-separated floats - std::nullopt if
        // the count doesn't match or any token fails to parse (including
        // trailing garbage after a valid number), same "no partial
        // commits" contract every other PropertyEditor::parseValue()
        // already follows.
        std::optional<std::vector<float>> parseFloatList(const std::string& text, std::size_t count)
        {
            std::vector<float> values;
            std::size_t start = 0;
            while (start <= text.size()) {
                std::size_t comma = text.find(',', start);
                std::string token = text.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
                std::size_t first = token.find_first_not_of(" \t");
                std::size_t last = token.find_last_not_of(" \t");
                if (first == std::string::npos) {
                    return std::nullopt;
                }
                token = token.substr(first, last - first + 1);
                try {
                    std::size_t consumed = 0;
                    float value = std::stof(token, &consumed);
                    if (consumed != token.size()) {
                        return std::nullopt;
                    }
                    values.push_back(value);
                } catch (const std::exception&) {
                    return std::nullopt;
                }
                if (comma == std::string::npos) {
                    break;
                }
                start = comma + 1;
            }
            if (values.size() != count) {
                return std::nullopt;
            }
            return values;
        }
    }

    std::string BoolPropertyEditor::valueAsString() const
    {
        return std::any_cast<bool>(rawValue()) ? "true" : "false";
    }

    std::optional<std::any> BoolPropertyEditor::parseValue(const std::string& text) const
    {
        std::string lower = toLower(text);
        if (lower == "true" || lower == "1" || lower == "yes") {
            return std::any(true);
        }
        if (lower == "false" || lower == "0" || lower == "no") {
            return std::any(false);
        }
        return std::nullopt;
    }

    void BoolPropertyEditor::paintValue(BLContext& ctx, const newui::Rect& rect, const newui::Color& textColor) const
    {
        newui::Rect box(rect.left(), rect.top() + (rect.size().height - kCheckboxSize) * 0.5f,
            kCheckboxSize, kCheckboxSize);
        paintCheckbox(ctx, box, valueAsString() == "true", textColor);
    }

    std::string IntPropertyEditor::valueAsString() const
    {
        return std::to_string(std::any_cast<int>(rawValue()));
    }

    std::optional<std::any> IntPropertyEditor::parseValue(const std::string& text) const
    {
        try {
            std::size_t consumed = 0;
            int value = std::stoi(text, &consumed);
            if (consumed == text.size()) {
                return std::any(value);
            }
        } catch (const std::exception&) {
            // invalid text: falls through to nullopt below
        }
        return std::nullopt;
    }

    std::string SizeTPropertyEditor::valueAsString() const
    {
        return std::to_string(std::any_cast<std::size_t>(rawValue()));
    }

    std::optional<std::any> SizeTPropertyEditor::parseValue(const std::string& text) const
    {
        // std::stoull() accepts "-1" and wraps it, so a sign is refused up front.
        if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos) {
            return std::nullopt;
        }
        try {
            std::size_t consumed = 0;
            unsigned long long value = std::stoull(text, &consumed);
            if (consumed == text.size()) {
                return std::any(static_cast<std::size_t>(value));
            }
        } catch (const std::exception&) {
            // out of range: falls through to nullopt below
        }
        return std::nullopt;
    }

    std::string GridTracksPropertyEditor::format(const std::vector<newui::GridTrack>& tracks)
    {
        std::string text;
        for (const newui::GridTrack& track : tracks) {
            if (!text.empty()) {
                text += ", ";
            }
            switch (track.kind) {
            case newui::GridTrackKind::Auto: text += "Auto"; break;
            case newui::GridTrackKind::Star:
                text += track.value == 1.0f ? std::string("*") : formatFloat(track.value) + "*";
                break;
            case newui::GridTrackKind::Fixed: text += formatFloat(track.value); break;
            }
        }
        return text;
    }

    std::optional<std::vector<newui::GridTrack>> GridTracksPropertyEditor::parse(const std::string& text)
    {
        std::vector<newui::GridTrack> tracks;
        bool blank = true;
        for (char c : text) {
            if (c != ' ' && c != '\t') {
                blank = false;
            }
        }
        if (blank) {
            return tracks;
        }

        std::size_t start = 0;
        while (start <= text.size()) {
            std::size_t end = text.find(',', start);
            if (end == std::string::npos) {
                end = text.size();
            }
            std::string token = toLower(text.substr(start, end - start));
            const std::size_t first = token.find_first_not_of(" \t");
            const std::size_t last = token.find_last_not_of(" \t");
            std::string trimmedToken = first == std::string::npos ? std::string() : token.substr(first, last - first + 1);
            if (trimmedToken.empty()) {
                return std::nullopt;   // "40,,*" - a track is missing
            }

            newui::GridTrack track;
            if (trimmedToken == "auto") {
                track = newui::GridTrack{ newui::GridTrackKind::Auto, 0.0f };
            } else {
                const bool star = trimmedToken.back() == '*';
                std::string number = trimmedToken;
                if (star) {
                    number.pop_back();
                } else if (number.size() > 2 && number.compare(number.size() - 2, 2, "px") == 0) {
                    number.resize(number.size() - 2);
                }
                float value = 1.0f;
                if (!number.empty()) {
                    try {
                        std::size_t consumed = 0;
                        value = std::stof(number, &consumed);
                        if (consumed != number.size()) {
                            return std::nullopt;
                        }
                    } catch (const std::exception&) {
                        return std::nullopt;
                    }
                } else if (!star) {
                    return std::nullopt;
                }
                if (!(value >= 0.0f) || (star && value == 0.0f)) {
                    return std::nullopt;   // negative sizes and a zero-weight star make no sense
                }
                track = newui::GridTrack{ star ? newui::GridTrackKind::Star : newui::GridTrackKind::Fixed, value };
            }
            tracks.push_back(track);

            if (end == text.size()) {
                break;
            }
            start = end + 1;
        }
        return tracks;
    }

    std::string GridTracksPropertyEditor::valueAsString() const
    {
        return format(std::any_cast<std::vector<newui::GridTrack>>(rawValue()));
    }

    std::optional<std::any> GridTracksPropertyEditor::parseValue(const std::string& text) const
    {
        std::optional<std::vector<newui::GridTrack>> tracks = parse(text);
        if (!tracks.has_value()) {
            return std::nullopt;
        }
        return std::any(*tracks);
    }

    std::string FloatPropertyEditor::valueAsString() const
    {
        return std::to_string(std::any_cast<float>(rawValue()));
    }

    std::optional<std::any> FloatPropertyEditor::parseValue(const std::string& text) const
    {
        try {
            std::size_t consumed = 0;
            float value = std::stof(text, &consumed);
            if (consumed == text.size()) {
                return std::any(value);
            }
        } catch (const std::exception&) {
            // invalid text: falls through to nullopt below
        }
        return std::nullopt;
    }

    std::string StringPropertyEditor::valueAsString() const
    {
        return std::any_cast<std::string>(rawValue());
    }

    std::optional<std::any> StringPropertyEditor::parseValue(const std::string& text) const
    {
        return std::any(text);
    }

    std::string FilePathPropertyEditor::valueAsString() const
    {
        return std::any_cast<std::string>(rawValue());
    }

    std::optional<std::any> FilePathPropertyEditor::parseValue(const std::string& text) const
    {
        return std::any(text);
    }

    std::vector<newui::FileDialogFilter> FilePathPropertyEditor::filtersFor(const newui::reflection::Property* property)
    {
        std::vector<newui::FileDialogFilter> filters;
        if (property == nullptr) {
            return filters;
        }
        for (const std::string& tag : property->tags()) {
            if (tag == "image") {
                filters.push_back({ "Images (*.png, *.svg)", "*.png;*.svg" });
                filters.push_back({ "All Files (*.*)", "*.*" });
                break;
            }
        }
        return filters;
    }

    void FilePathPropertyEditor::edit(newui::View* owner)
    {
        newui::FileDialogOptions options;
        options.title = "Select File";
        options.filters = filtersFor(property_);
        std::string outPath;
        if (newui::Dialog::showOpenFile(owner->rootView()->windowHandle(), options, outPath)) {
            commitValue(std::any(outPath));
        }
    }

    std::string ColorPropertyEditor::valueAsString() const
    {
        return colorDisplayText(std::any_cast<newui::Color>(rawValue()));
    }

    std::vector<std::string> ColorPropertyEditor::dropdownValues() const
    {
        std::vector<std::string> names;
        for (const ColorChoice& choice : colorChoices()) {
            names.push_back(choice.name);
        }
        return names;
    }

    std::string ColorPropertyEditor::dropdownCurrentValue() const
    {
        return colorChoiceNameFor(std::any_cast<newui::Color>(rawValue())).value_or(std::string());
    }

    void ColorPropertyEditor::customizeDropdown(newui::DropDownList& dropdown) const
    {
        dropdown.setController(std::make_shared<ColorListController>());
    }

    void ColorPropertyEditor::edit(newui::View* owner)
    {
        newui::Color current = std::any_cast<newui::Color>(rawValue());
        ColorEditorDialog dialog;
        dialog.setColor(current);
        if (dialog.showModal(owner) == newui::DialogResult::Ok) {
            commitValue(std::any(dialog.color()));
        }
    }

    std::optional<std::any> ColorPropertyEditor::parseValue(const std::string& text) const
    {
        newui::Color color;
        if (parseColorText(text, color)) {
            return std::any(color);
        }
        return std::nullopt;
    }

    void ColorPropertyEditor::paintValue(BLContext& ctx, const newui::Rect& rect, const newui::Color& textColor) const
    {
        newui::Rect box(rect.left(), rect.top() + (rect.size().height - kSwatchSize) * 0.5f, kSwatchSize, kSwatchSize);
        paintSwatch(ctx, box, std::any_cast<newui::Color>(rawValue()), textColor);
        newui::Rect textRect(rect.left() + kSwatchSize + 6.0f, rect.top(),
            rect.size().width - kSwatchSize - 6.0f, rect.size().height);
        paintText(ctx, textRect, valueAsString(), textColor);
    }

    std::string PointPropertyEditor::valueAsString() const
    {
        newui::Point p = std::any_cast<newui::Point>(rawValue());
        return formatFloat(p.x) + ", " + formatFloat(p.y);
    }

    std::optional<std::any> PointPropertyEditor::parseValue(const std::string& text) const
    {
        auto values = parseFloatList(text, 2);
        if (!values.has_value()) {
            return std::nullopt;
        }
        return std::any(newui::Point((*values)[0], (*values)[1]));
    }

    std::string PointPropertyEditor::subPropertyValueAsString(std::size_t index) const
    {
        newui::Point p = std::any_cast<newui::Point>(rawValue());
        return formatFloat(index == 0 ? p.x : p.y);
    }

    void PointPropertyEditor::setSubPropertyValueFromString(std::size_t index, const std::string& text)
    {
        auto values = parseFloatList(text, 1);
        if (!values.has_value()) {
            return;
        }
        newui::Point p = std::any_cast<newui::Point>(rawValue());
        (index == 0 ? p.x : p.y) = (*values)[0];
        commitValue(std::any(p));
    }

    std::string SizePropertyEditor::valueAsString() const
    {
        newui::Size s = std::any_cast<newui::Size>(rawValue());
        return formatFloat(s.width) + ", " + formatFloat(s.height);
    }

    std::optional<std::any> SizePropertyEditor::parseValue(const std::string& text) const
    {
        auto values = parseFloatList(text, 2);
        if (!values.has_value()) {
            return std::nullopt;
        }
        return std::any(newui::Size((*values)[0], (*values)[1]));
    }

    std::string SizePropertyEditor::subPropertyValueAsString(std::size_t index) const
    {
        newui::Size s = std::any_cast<newui::Size>(rawValue());
        return formatFloat(index == 0 ? s.width : s.height);
    }

    void SizePropertyEditor::setSubPropertyValueFromString(std::size_t index, const std::string& text)
    {
        auto values = parseFloatList(text, 1);
        if (!values.has_value()) {
            return;
        }
        newui::Size s = std::any_cast<newui::Size>(rawValue());
        (index == 0 ? s.width : s.height) = (*values)[0];
        commitValue(std::any(s));
    }

    std::string RectPropertyEditor::valueAsString() const
    {
        newui::Rect r = std::any_cast<newui::Rect>(rawValue());
        return formatFloat(r.left()) + ", " + formatFloat(r.top()) + ", "
            + formatFloat(r.width()) + ", " + formatFloat(r.height());
    }

    std::optional<std::any> RectPropertyEditor::parseValue(const std::string& text) const
    {
        auto values = parseFloatList(text, 4);
        if (!values.has_value()) {
            return std::nullopt;
        }
        return std::any(newui::Rect((*values)[0], (*values)[1], (*values)[2], (*values)[3]));
    }

    std::string RectPropertyEditor::subPropertyValueAsString(std::size_t index) const
    {
        newui::Rect r = std::any_cast<newui::Rect>(rawValue());
        switch (index) {
        case 0: return formatFloat(r.left());
        case 1: return formatFloat(r.top());
        case 2: return formatFloat(r.width());
        default: return formatFloat(r.height());
        }
    }

    void RectPropertyEditor::setSubPropertyValueFromString(std::size_t index, const std::string& text)
    {
        auto values = parseFloatList(text, 1);
        if (!values.has_value()) {
            return;
        }
        newui::Rect r = std::any_cast<newui::Rect>(rawValue());
        float value = (*values)[0];
        newui::Rect updated;
        switch (index) {
        case 0: updated = newui::Rect(value, r.top(), r.width(), r.height()); break;
        case 1: updated = newui::Rect(r.left(), value, r.width(), r.height()); break;
        case 2: updated = newui::Rect(r.left(), r.top(), value, r.height()); break;
        default: updated = newui::Rect(r.left(), r.top(), r.width(), value); break;
        }
        commitValue(std::any(updated));
    }

    std::string FontPropertyEditor::valueAsString() const
    {
        newui::Font f = std::any_cast<newui::Font>(rawValue());
        return f.name() + ", " + formatFloat(f.size());
    }

    std::optional<std::any> FontPropertyEditor::parseValue(const std::string& text) const
    {
        std::size_t comma = text.find(',');
        if (comma == std::string::npos) {
            return std::nullopt;
        }
        std::string name = text.substr(0, comma);
        std::size_t nameLast = name.find_last_not_of(" \t");
        name = nameLast == std::string::npos ? std::string() : name.substr(0, nameLast + 1);

        std::string sizeToken = text.substr(comma + 1);
        std::size_t first = sizeToken.find_first_not_of(" \t");
        std::size_t last = sizeToken.find_last_not_of(" \t");
        if (first == std::string::npos) {
            return std::nullopt;
        }
        sizeToken = sizeToken.substr(first, last - first + 1);

        try {
            std::size_t consumed = 0;
            float size = std::stof(sizeToken, &consumed);
            if (consumed != sizeToken.size()) {
                return std::nullopt;
            }
            newui::Font f = std::any_cast<newui::Font>(rawValue());
            f.setName(name);
            f.setSize(size);
            if (!fontResolves(f)) {
                return std::nullopt;
            }
            return std::any(f);
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }

    std::vector<std::string> FontPropertyEditor::subPropertyDropdownValues(std::size_t index) const
    {
        if (index != 0) {
            return {};
        }
        std::vector<std::string> names;
        for (const newui::SystemFontInfo& info : newui::FontManager::listFonts()) {
            names.push_back(info.name);
        }
        return names;
    }

    std::string FontPropertyEditor::subPropertyValueAsString(std::size_t index) const
    {
        newui::Font f = std::any_cast<newui::Font>(rawValue());
        switch (index) {
        case 0: return f.name();
        case 1: return formatFloat(f.size());
        case 2: return f.bold() ? "true" : "false";
        case 3: return f.italic() ? "true" : "false";
        case 4: return f.underlined() ? "true" : "false";
        default: return f.strikeThrough() ? "true" : "false";
        }
    }

    void FontPropertyEditor::setSubPropertyValueFromString(std::size_t index, const std::string& text)
    {
        newui::Font f = std::any_cast<newui::Font>(rawValue());
        switch (index) {
        case 0:
            f.setName(text);
            break;
        case 1: {
            try {
                std::size_t consumed = 0;
                float size = std::stof(text, &consumed);
                if (consumed != text.size()) {
                    return;
                }
                f.setSize(size);
            } catch (const std::exception&) {
                return;
            }
            break;
        }
        default: {
            std::string lower = toLower(text);
            bool checked = lower == "true" || lower == "1" || lower == "yes";
            switch (index) {
            case 2: f.setBold(checked); break;
            case 3: f.setItalic(checked); break;
            case 4: f.setUnderlined(checked); break;
            default: f.setStrikeThrough(checked); break;
            }
            break;
        }
        }
        // Only name/size/bold/italic (indices 0-3) feed into Font::blFont()'s own lookup at all -
        // see fontResolves()'s own comment above for why this rejects, rather than accepts, an
        // edit that would leave the font unable to resolve (e.g. ticking "bold" on a face already
        // named "... Bold"). underlined/strikeThrough (4/5) always commit - blFont() never
        // consults them, so they can never be the reason a font fails to resolve.
        if (index <= 3 && !fontResolves(f)) {
            return;
        }
        commitValue(std::any(f));
    }

    std::string GradientPropertyEditor::valueAsString() const
    {
        newui::gfx::Gradient g = std::any_cast<newui::gfx::Gradient>(rawValue());
        std::string summary = gradientKindName(g.kind());
        if (g.kind() != newui::gfx::GradientKind::Point) {
            summary += " \xC2\xB7 " + std::to_string(g.stops().size()) + " stops";
        } else {
            summary += " \xC2\xB7 " + std::to_string(g.points().size()) + " points";
        }
        return summary;
    }

    std::optional<std::any> GradientPropertyEditor::parseValue(const std::string& /*text*/) const
    {
        // Dialog-only editing - see this class's own declaration comment (PropertyEditor.h) for
        // why there's no sensible single-line text form to accept here.
        return std::nullopt;
    }

    void GradientPropertyEditor::edit(newui::View* owner)
    {
        newui::gfx::Gradient current = std::any_cast<newui::gfx::Gradient>(rawValue());

        GradientEditorDialog dialog;
        // owner->bounds() is parent-relative (its own x/y position within owner's parent) - but
        // Gradient's own Linear/Radial/Conic geometry (linearStart_/linearEnd_ etc., graphics.h)
        // is painted in the owning view's *local* space (View::paintStyle() passes only
        // bounds_.size() through to ViewStyle::paint(), never bounds_'s own position - the parent
        // already translated the context before calling in). Only owner's *size* belongs here;
        // its position would otherwise get baked into the committed gradient's geometry as a
        // spurious offset.
        dialog.setShapeBounds(newui::Rect(0.0f, 0.0f, owner->bounds().width(), owner->bounds().height()));
        dialog.setGradient(current);

        // newui::Dialog::showModal(View*) resolves the real owning HWND via
        // owner->rootView()->windowHandle() - works whether that RootView is hosted by a real
        // newui::Frame (testharness.exe) or lives directly inside a native HWND with no Frame at
        // all (NativeEditControls' VSIX-hosted RootView), unlike the Frame*-only overload. owner
        // is always a real, attached View here - PropertiesGrid (this method's only caller) never
        // reaches EditStyle::Dialog dispatch without a live model_.selected() backing the property
        // tree a PropertyLeaf row came from.
        if (dialog.showModal(owner) == newui::DialogResult::Ok) {
            commitValue(std::any(dialog.gradient()));
        }
    }

    std::string LayoutPropertyEditor::valueAsString() const
    {
        // property_->addressableValue(instance_) - the raw pointer itself, nullptr when no
        // Layout is attached - checked first: getClass() (below) never returns nullptr for this
        // property even when the pointer is null, it falls back to classinfo(type()) (the
        // declared Layout base, which is itself a real registered class - reflection.h's own
        // TypedProperty::getClass() comment), which would otherwise misreport an unset Layout as
        // "Layout" instead of "(none)".
        if (property_->addressableValue(instance_) == nullptr) {
            return "(none)";
        }
        // property_->getClass(instance_) - NOT rawValue() - see this class's own declaration
        // comment (PropertyEditor.h) for why rawValue() would return an empty/sliced std::any for
        // a PtrGetter property instead of the true runtime class name.
        const newui::reflection::Class* clazz = property_->getClass(instance_);
        return clazz != nullptr ? clazz->name() : std::string("(none)");
    }

    newui::PopupTool* LayoutPropertyEditor::editAsync(newui::View* owner, const newui::Rect& anchorScreenRect)
    {
        struct LayoutOption { std::string displayName; std::function<newui::Layout*()> factory; };
        static const std::vector<LayoutOption> options = {
            { "Anchor", [] { return new newui::AnchorLayout(); } },
            { "Flex",   [] { return new newui::FlexLayout(newui::Orientation::Vertical); } },
            { "Card",   [] { return new newui::CardLayout(); } },
            { "Grid",   [] { return new newui::GridLayout(); } },
        };

        newui::Size popupSize = typePickerPopupSize(options.size());
        CalloutPlacement placement = placeCallout(anchorScreenRect, popupSize, designerWindowScreenRect(owner));

        auto* popup = new newui::CalloutTool(owner->rootView()->windowHandle(),
            newui::Application::instance().instanceHandle(), placement.bounds, "layoutTypePicker");
        if (!popup->initialize()) {
            delete popup;
            return nullptr;
        }
        popup->setTailSide(placement.tailSide);
        popup->setTailPosition(placement.tailPosition);

        // property_/instance_/postCommitSync_ captured by value in each cell's own onSelected
        // lambda below - never `this`, which PropertiesGrid destroys (liveEditor_.reset()) right
        // after this call returns, well before the user actually clicks a cell.
        const newui::reflection::Property* property = property_;
        void* instance = instance_;
        PostCommitSync sync = postCommitSync_;

        std::vector<newui::Rect> cellRects = typePickerCellRects(options.size(), placement.bounds.size());
        for (std::size_t i = 0; i < options.size(); ++i) {
            std::function<newui::Layout*()> factory = options[i].factory;
            auto onSelected = [popup, property, instance, sync, factory]() {
                property->set(instance, std::any(factory()));
                if (sync) {
                    sync();
                }
                popup->dismiss();
            };
            std::string className = options[i].displayName == "Anchor" ? "AnchorLayout"
                : options[i].displayName == "Flex" ? "FlexLayout"
                : options[i].displayName == "Card" ? "CardLayout" : "GridLayout";
            auto* cell = new TypePickerCell(options[i].displayName, std::move(onSelected),
                [className](BLContext& ctx, const newui::Rect& r) { paintLayoutGlyph(ctx, r, className); });
            cell->setBounds(cellRects[i]);
            popup->addChild(cell);
        }

        popup->present();
        return popup;
    }

    std::string ViewStylePropertyEditor::valueAsString() const
    {
        // Unlike LayoutPropertyEditor's own layout() (a real, sometimes-null View::layout()),
        // View::style() always returns a live reference (ClassBuilder::property()'s
        // isOwningRefGetter case wraps it as `&std::invoke(getter, self)` - reflection.h - never
        // null) - this null check is defensive only, kept for the same "(none)" fallback shape.
        if (property_->addressableValue(instance_) == nullptr) {
            return "(none)";
        }
        const newui::reflection::Class* clazz = property_->getClass(instance_);
        return clazz != nullptr ? clazz->name() : std::string("(none)");
    }

    newui::PopupTool* ViewStylePropertyEditor::editAsync(newui::View* owner, const newui::Rect& anchorScreenRect)
    {
        std::vector<ViewStyleOption> options = ViewStyleRegistry::optionsFor(owner);

        newui::Size popupSize = typePickerPopupSize(options.size());
        CalloutPlacement placement = placeCallout(anchorScreenRect, popupSize, designerWindowScreenRect(owner));

        auto* popup = new newui::CalloutTool(owner->rootView()->windowHandle(),
            newui::Application::instance().instanceHandle(), placement.bounds, "viewStyleTypePicker");
        if (!popup->initialize()) {
            delete popup;
            return nullptr;
        }
        popup->setTailSide(placement.tailSide);
        popup->setTailPosition(placement.tailPosition);

        const newui::reflection::Property* property = property_;
        void* instance = instance_;
        PostCommitSync sync = postCommitSync_;

        std::vector<newui::Rect> cellRects = typePickerCellRects(options.size(), placement.bounds.size());
        for (std::size_t i = 0; i < options.size(); ++i) {
            std::function<newui::ViewStyle*()> factory = options[i].factory;
            auto onSelected = [popup, property, instance, sync, factory]() {
                property->set(instance, std::any(factory()));
                if (sync) {
                    sync();
                }
                popup->dismiss();
            };
            auto* cell = new TypePickerCell(options[i].displayName, onSelected);
            cell->setBounds(cellRects[i]);

            // A real, live-painted preview - a throwaway inert SubView with the candidate style
            // installed, added as an ordinary popup child (matches this codebase's "real control
            // over hand-painted approximation" convention - see this class's own header comment).
            // Added as a *sibling* of cell, not a child of it - View::hitTestChildren() (view.cpp)
            // walks to the single deepest/topmost SubView under the click point with no bubbling
            // to whatever's underneath, so preview needs its own onMouseDown wired to the exact
            // same onSelected (still a valid copy - never moved out of above) or a click landing
            // on the live preview itself (most of the cell's area) would silently do nothing.
            auto* preview = new newui::SubView();
            preview->setVisible(true);
            preview->setStyle(std::unique_ptr<newui::ViewStyle>(factory()));
            newui::Rect previewLocal = cell->previewRect();
            preview->setBounds(newui::Rect(cellRects[i].left() + previewLocal.left(),
                cellRects[i].top() + previewLocal.top(), previewLocal.width(), previewLocal.height()));
            preview->onMouseDown.add(std::function<newui::SyncReturn(newui::View&, const newui::Point&, std::uint32_t, std::uint32_t)>(
                [onSelected](newui::View&, const newui::Point&, std::uint32_t, std::uint32_t) -> newui::SyncReturn {
                    onSelected();
                    return newui::SyncReturn::Handled;
                }));
            popup->addChild(cell);
            popup->addChild(preview);
        }

        popup->present();
        return popup;
    }

    std::string EnumPropertyEditor::valueAsString() const
    {
        std::uint64_t value = enum_->toUInt64(rawValue());
        std::string name;
        if (enum_->tryToString(value, name)) {
            return name;
        }
        return std::to_string(value);
    }

    std::optional<std::any> EnumPropertyEditor::parseValue(const std::string& text) const
    {
        std::uint64_t value = 0;
        if (!enum_->tryParse(text, value)) {
            return std::nullopt;
        }
        std::any result = enum_->fromUInt64(value);
        if (!result.has_value()) {
            return std::nullopt;
        }
        return result;
    }

    std::vector<std::string> EnumPropertyEditor::dropdownValues() const
    {
        std::vector<std::string> names;
        for (const auto& v : enum_->values()) {
            names.push_back(v.name);
        }
        return names;
    }

    std::string FlagsEnumPropertyEditor::valueAsString() const
    {
        std::uint64_t value = enum_->toUInt64(rawValue());
        std::vector<std::string> names = enum_->decompose(value);
        std::string joined;
        for (std::size_t i = 0; i < names.size(); ++i) {
            if (i > 0) {
                joined += " | ";
            }
            joined += names[i];
        }
        return joined;
    }

    std::optional<std::any> FlagsEnumPropertyEditor::parseValue(const std::string& text) const
    {
        // Mirrors valueAsString()'s own " | "-joined format - not reachable from the grid itself
        // (a flags row is always edited one checkbox at a time, via setSubPropertyValueFromString()
        // below), but kept real/correct rather than a stub, matching every other PropertyEditor's
        // own parseValue() contract.
        std::uint64_t combined = 0;
        std::size_t start = 0;
        while (start <= text.size()) {
            std::size_t bar = text.find('|', start);
            std::string token = text.substr(start, bar == std::string::npos ? std::string::npos : bar - start);
            std::size_t first = token.find_first_not_of(" \t");
            std::size_t last = token.find_last_not_of(" \t");
            if (first != std::string::npos) {
                std::uint64_t bit = 0;
                if (!enum_->tryParse(token.substr(first, last - first + 1), bit)) {
                    return std::nullopt;
                }
                combined |= bit;
            }
            if (bar == std::string::npos) {
                break;
            }
            start = bar + 1;
        }
        std::any result = enum_->fromUInt64(combined);
        if (!result.has_value()) {
            return std::nullopt;
        }
        return result;
    }

    std::vector<std::string> FlagsEnumPropertyEditor::subPropertyNames() const
    {
        std::vector<std::string> names;
        for (const auto& v : enum_->values()) {
            // A zero-value entry (e.g. "None") isn't a real bit to toggle - Enum::decompose()
            // itself skips these the same way (reflection.h) when picking candidate flag names.
            if (v.value != 0) {
                names.push_back(v.name);
            }
        }
        return names;
    }

    std::string FlagsEnumPropertyEditor::subPropertyValueAsString(std::size_t index) const
    {
        std::vector<std::string> names = subPropertyNames();
        if (index >= names.size()) {
            return "false";
        }
        std::uint64_t flagValue = 0;
        enum_->tryParse(names[index], flagValue);
        std::uint64_t current = enum_->toUInt64(rawValue());
        return (flagValue != 0 && (current & flagValue) == flagValue) ? "true" : "false";
    }

    void FlagsEnumPropertyEditor::setSubPropertyValueFromString(std::size_t index, const std::string& text)
    {
        std::vector<std::string> names = subPropertyNames();
        if (index >= names.size()) {
            return;
        }
        std::uint64_t flagValue = 0;
        if (!enum_->tryParse(names[index], flagValue) || flagValue == 0) {
            return;
        }

        std::string lower = toLower(text);
        bool checked = lower == "true" || lower == "1" || lower == "yes";

        std::uint64_t current = enum_->toUInt64(rawValue());
        std::uint64_t updated = checked ? (current | flagValue) : (current & ~flagValue);
        std::any newValue = enum_->fromUInt64(updated);
        if (newValue.has_value()) {
            commitValue(newValue);
        }
    }

    void PropertyEditor::paintValue(BLContext& ctx, const newui::Rect& rect, const newui::Color& textColor) const
    {
        paintText(ctx, rect, valueAsString(), textColor);
    }

    void PropertyEditor::paintSubPropertyValue(BLContext& ctx, const newui::Rect& rect, std::size_t index,
        const newui::Color& textColor) const
    {
        // subPropertyIsBool() already exists precisely so a generic caller (this default, now the
        // only caller PropertyItem::paint() itself used to be) can tell a flags-enum bit
        // (FlagsEnumPropertyEditor, always true) or a Font bold/italic/underlined/strikeThrough
        // row (FontPropertyEditor, true for index >= 2) apart from an ordinary text sub-property
        // (Rect/Point/Size's own float components) without needing to know either concrete type -
        // neither one needs its own paintSubPropertyValue() override at all as a result.
        if (subPropertyIsBool(index)) {
            newui::Rect box(rect.left(), rect.top() + (rect.size().height - kCheckboxSize) * 0.5f,
                kCheckboxSize, kCheckboxSize);
            paintCheckbox(ctx, box, subPropertyValueAsString(index) == "true", textColor);
            return;
        }
        paintText(ctx, rect, subPropertyValueAsString(index), textColor);
    }

    std::function<void(const std::any&)> PropertyEditor::valueWriter() const
    {
        const newui::reflection::Property* property = property_;
        void* instance = instance_;
        return [property, instance](const std::any& value) { property->set(instance, value); };
    }

    std::function<void(const std::any&)> GridTracksPropertyEditor::valueWriter() const
    {
        const newui::reflection::Property* property = property_;
        void* instance = instance_;
        return [property, instance](const std::any& value) {
            *static_cast<std::vector<newui::GridTrack>*>(property->address(instance)) =
                std::any_cast<std::vector<newui::GridTrack>>(value);
        };
    }

    void PropertyEditor::commitValue(const std::any& newValue) const
    {
        if (undoStack_ == nullptr) {
            setRawValue(newValue);
            if (postCommitSync_) {
                postCommitSync_();
            }
            return;
        }

        std::any oldValue = rawValue();
        PostCommitSync sync = postCommitSync_;
        std::function<void(const std::any&)> write = valueWriter();

        newui::UndoableAction action;
        action.description = "Change " + property_->name();
        // sync() (if any) runs after the write in *both* directions, reading whatever the
        // property's own value now is - see setPostCommitSync()'s own comment for why this lives
        // here rather than in a per-instance side call outside the undo system.
        action.doIt = [write, newValue, sync] {
            write(newValue);
            if (sync) { sync(); }
        };
        action.undoIt = [write, oldValue, sync] {
            write(oldValue);
            if (sync) { sync(); }
        };
        undoStack_->push(std::move(action));  // push() calls doIt() immediately
    }

    void PropertyEditor::setValueFromString(const std::string& text)
    {
        std::optional<std::any> parsed = parseValue(text);
        if (!parsed.has_value()) {
            return;
        }
        commitValue(*parsed);
    }

    PropertyEditorRegistry& PropertyEditorRegistry::instance()
    {
        static PropertyEditorRegistry registry;
        return registry;
    }

    void PropertyEditorRegistry::registerEditor(std::type_index propertyType, Factory factory,
                                                 const newui::reflection::Class* owningClass,
                                                 const std::string& propertyName)
    {
        entries_.push_back(Entry{ propertyType, owningClass, propertyName, std::move(factory) });
    }

    void PropertyEditorRegistry::registerEditor(const std::string& tag, Factory factory)
    {
        tagEntries_[tag] = std::move(factory);
    }

    std::unique_ptr<PropertyEditor> PropertyEditorRegistry::createEditor(
        const newui::reflection::Property* property,
        const newui::reflection::Class* owningClass,
        void* instance) const
    {
        for (const std::string& tag : property->tags()) {
            auto it = tagEntries_.find(tag);
            if (it != tagEntries_.end()) {
                return it->second(property, instance);
            }
        }

        const Entry* best = nullptr;
        int bestScore = -1;

        for (const Entry& entry : entries_) {
            if (entry.propertyType != property->type()) {
                continue;
            }
            if (entry.owningClass != nullptr && entry.owningClass != owningClass) {
                continue;
            }
            if (!entry.propertyName.empty() && entry.propertyName != property->name()) {
                continue;
            }

            int score = (entry.owningClass != nullptr ? 2 : 0) + (!entry.propertyName.empty() ? 1 : 0);
            if (score >= bestScore) {
                bestScore = score;
                best = &entry;
            }
        }

        if (best == nullptr) {
            // No tag/type-specific registration - fall back to a generic
            // enum dropdown if this property's type happens to be a
            // registered Enum (see EnumPropertyEditor's own comment for
            // why this can't just be another registerEditor() entry).
            if (const newui::reflection::Enum* enumInfo =
                    newui::reflection::ReflectionRegistry::getEnum(property->type())) {
                if (enumInfo->isFlags()) {
                    return std::make_unique<FlagsEnumPropertyEditor>(property, instance, enumInfo);
                }
                return std::make_unique<EnumPropertyEditor>(property, instance, enumInfo);
            }
            return nullptr;
        }
        return best->factory(property, instance);
    }

    bool PropertyEditorRegistry::hasTypeEditor(std::type_index type) const
    {
        for (const Entry& entry : entries_) {
            if (entry.propertyType == type) {
                return true;
            }
        }
        return false;
    }

    void PropertyEditorRegistry::registerBuiltinEditors()
    {
        if (builtinsRegistered_) {
            return;
        }
        builtinsRegistered_ = true;

        registerEditor(std::type_index(typeid(bool)),
            [](const newui::reflection::Property* p, void* instance) { return std::make_unique<BoolPropertyEditor>(p, instance); });
        registerEditor(std::type_index(typeid(int)),
            [](const newui::reflection::Property* p, void* instance) { return std::make_unique<IntPropertyEditor>(p, instance); });
        registerEditor(std::type_index(typeid(std::size_t)),
            [](const newui::reflection::Property* p, void* instance) { return std::make_unique<SizeTPropertyEditor>(p, instance); });
        registerEditor(std::type_index(typeid(std::vector<newui::GridTrack>)),
            [](const newui::reflection::Property* p, void* instance) { return std::make_unique<GridTracksPropertyEditor>(p, instance); });
        registerEditor(std::type_index(typeid(float)),
            [](const newui::reflection::Property* p, void* instance) { return std::make_unique<FloatPropertyEditor>(p, instance); });
        registerEditor(std::type_index(typeid(std::string)),
            [](const newui::reflection::Property* p, void* instance) { return std::make_unique<StringPropertyEditor>(p, instance); });
        registerEditor(std::string("filepath"),
            [](const newui::reflection::Property* p, void* instance) { return std::make_unique<FilePathPropertyEditor>(p, instance); });
        registerEditor(std::type_index(typeid(newui::Color)),
            [](const newui::reflection::Property* p, void* instance) { return std::make_unique<ColorPropertyEditor>(p, instance); });
        registerEditor(std::type_index(typeid(newui::Point)),
            [](const newui::reflection::Property* p, void* instance) { return std::make_unique<PointPropertyEditor>(p, instance); });
        registerEditor(std::type_index(typeid(newui::Size)),
            [](const newui::reflection::Property* p, void* instance) { return std::make_unique<SizePropertyEditor>(p, instance); });
        registerEditor(std::type_index(typeid(newui::Rect)),
            [](const newui::reflection::Property* p, void* instance) { return std::make_unique<RectPropertyEditor>(p, instance); });
        registerEditor(std::type_index(typeid(newui::Font)),
            [](const newui::reflection::Property* p, void* instance) { return std::make_unique<FontPropertyEditor>(p, instance); });
        registerEditor(std::type_index(typeid(newui::gfx::Gradient)),
            [](const newui::reflection::Property* p, void* instance) { return std::make_unique<GradientPropertyEditor>(p, instance); });
        registerEditor(std::type_index(typeid(newui::Layout)),
            [](const newui::reflection::Property* p, void* instance) { return std::make_unique<LayoutPropertyEditor>(p, instance); });
        registerEditor(std::type_index(typeid(newui::ViewStyle)),
            [](const newui::reflection::Property* p, void* instance) { return std::make_unique<ViewStylePropertyEditor>(p, instance); });
    }
}
