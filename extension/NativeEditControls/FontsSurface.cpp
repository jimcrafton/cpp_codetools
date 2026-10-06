#include "FontsSurface.h"

#include "PaintUtils.h"
#include "TextEncoding.h"

#include <newui/fontmanager.h>
#include <newui/layout.h>
#include <newui/uicolormanager.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>

namespace CodeToolsVsix
{
    namespace
    {
        using newui::UIColorManager;
        using newui::UIColorRole;

        newui::Color roleColor(UIColorRole role) { return UIColorManager::colorFor(role); }

        BLRoundRect roundRect(const newui::Rect& r, double radius)
        {
            return BLRoundRect(r.left(), r.top(), r.width(), r.height(), radius, radius);
        }
    }

    // A list painted by hand inside a ScrollView: the view's own height is its content; the surface sizes it.
    class FontListView : public newui::SubView
    {
    public:
        static constexpr float kPadding = 12.0f;

        explicit FontListView(FontsModel& model) : model_(model) { setVisible(true); }

        std::function<void()> onChanged;   // the selection or a tick changed in the model

        // The height this list needs at `width`.
        virtual float contentHeight(float width) = 0;
        virtual void reload() = 0;

    protected:
        FontsModel& model_;
    };

    // SYSTEM / BUNDLED / LIVE SYSTEM UI headings, each over its families.
    class FontSourcesView : public FontListView
    {
    public:
        static constexpr float kRowHeight = 28.0f;

        explicit FontSourcesView(FontsModel& model) : FontListView(model)
        {
            onMouseDown.add([this](newui::View&, const newui::Point& point, std::uint32_t, std::uint32_t) {
                const std::size_t index = static_cast<std::size_t>(std::max(0.0f, point.y) / kRowHeight);
                if (index >= rows_.size() || rows_[index].kind != FontsModel::Row::Kind::Family) return newui::SyncReturn::Ignored;
                model_.select(rows_[index].source, rows_[index].text);
                if (onChanged) onChanged();
                return newui::SyncReturn::Handled;
            });
        }

        void reload() override
        {
            rows_ = model_.sourceRows();
            style().markDirty();
        }

        float contentHeight(float) override { return float(rows_.size()) * kRowHeight; }

        std::size_t rowCount() const { return rows_.size(); }
        const std::vector<FontsModel::Row>& rows() const { return rows_; }

        void paint(BLContext& ctx) override
        {
            const newui::Rect bounds = getClientBounds();
            newui::Font ui = newui::FontManager::getSystemFont(newui::SystemUIFont::Message);
            BLFont* font = ui.blFont();
            ctx.save();
            ctx.clip_to_rect(BLRect(bounds));
            for (std::size_t i = 0; i < rows_.size(); ++i) {
                const FontsModel::Row& row = rows_[i];
                const newui::Rect line(bounds.left(), bounds.top() + float(i) * kRowHeight, bounds.width(), kRowHeight);
                if (line.top() + line.height() < 0.0f) continue;
                const bool heading = row.kind == FontsModel::Row::Kind::Heading;
                const bool selected = !heading && row.source == model_.selectedSource() && row.text == model_.selectedName();
                if (selected) {
                    ctx.set_fill_style(roleColor(UIColorRole::HighlightBackground).toBLRgba32());
                    ctx.fill_rect(BLRect(line.left(), line.top(), line.width(), line.height()));
                }
                const newui::Color text = roleColor(selected ? UIColorRole::HighlightText : heading ? UIColorRole::DisabledText : UIColorRole::ControlText);
                double detailWidth = 0.0;
                if (!row.detail.empty() && font != nullptr && font->is_valid()) {
                    detailWidth = measureTextWidth(*font, row.detail);
                }
                if (heading) {
                    paintText(ctx, newui::Rect(line.left() + kPadding, line.top() + 4.0f, line.width() - kPadding, line.height()),
                              row.text + "   " + row.detail, text);
                    continue;
                }
                const float detailRoom = float(detailWidth) + kPadding * 2.0f;
                paintText(ctx, newui::Rect(line.left() + kPadding, line.top() + 2.0f, line.width() - kPadding - detailRoom, line.height()), row.text, text);
                if (detailWidth > 0.0) {
                    paintText(ctx, newui::Rect(line.left() + line.width() - kPadding - float(detailWidth), line.top() + 2.0f, float(detailWidth) + 2.0f, line.height()),
                              row.detail, roleColor(selected ? UIColorRole::HighlightText : UIColorRole::DisabledText));
                }
            }
            ctx.restore();
        }

    private:
        std::vector<FontsModel::Row> rows_;
    };

    // One specimen card per bundled family, in a grid that fills the width. A tick on each card is its Keep.
    class FontCardsView : public FontListView
    {
    public:
        static constexpr float kCardHeight = 86.0f;
        static constexpr float kCardMinWidth = 280.0f;
        static constexpr float kGap = 10.0f;
        static constexpr float kTitleHeight = 34.0f;
        static constexpr float kKeepWidth = 64.0f;
        static constexpr float kSpecimenSize = 20.0f;
        static constexpr const char* kSpecimen = "Aa Bg Qy 123 {}[]";

        explicit FontCardsView(FontsModel& model) : FontListView(model)
        {
            onMouseDown.add([this](newui::View&, const newui::Point& point, std::uint32_t, std::uint32_t) {
                for (std::size_t i = 0; i < cards_.size(); ++i) {
                    const newui::Rect card = cardRect(i);
                    if (point.x < card.left() || point.x > card.left() + card.width() || point.y < card.top() || point.y > card.top() + card.height()) continue;
                    const std::string& name = cards_[i]->name;
                    if (point.x >= card.left() + card.width() - kKeepWidth && point.y <= card.top() + 28.0f) {
                        model_.setKept(name, !model_.kept(name));
                    } else {
                        model_.select(FontsModel::Source::Bundled, name);
                    }
                    if (onChanged) onChanged();
                    return newui::SyncReturn::Handled;
                }
                return newui::SyncReturn::Ignored;
            });
        }

        void reload() override
        {
            cards_ = model_.cards();
            style().markDirty();
        }

        float contentHeight(float width) override
        {
            width_ = width;
            const std::size_t rows = (cards_.size() + columns() - 1) / columns();
            return kTitleHeight + kPadding + float(rows) * (kCardHeight + kGap);
        }

        std::size_t cardCount() const { return cards_.size(); }

        // Where card `index` is, in this view's own coordinates.
        newui::Rect cardRect(std::size_t index) const
        {
            const std::size_t cols = columns();
            const float cardWidth = (width_ - kPadding * 2.0f - kGap * float(cols - 1)) / float(cols);
            const std::size_t column = index % cols;
            const std::size_t row = index / cols;
            return newui::Rect(kPadding + float(column) * (cardWidth + kGap), kTitleHeight + float(row) * (kCardHeight + kGap), cardWidth, kCardHeight);
        }

        void paint(BLContext& ctx) override
        {
            const newui::Rect bounds = getClientBounds();
            newui::Font ui = newui::FontManager::getSystemFont(newui::SystemUIFont::Message);
            BLFont* uiFont = ui.blFont();
            ctx.save();
            ctx.clip_to_rect(BLRect(bounds));

            paintText(ctx, newui::Rect(bounds.left() + kPadding, bounds.top() + 8.0f, bounds.width() - kPadding, 20.0f),
                      "Resources\\Fonts   " + std::to_string(cards_.size()) + " families - " + FontCatalog::sizeText(model_.bundledBytes()) +
                      " of font files - tick the ones to keep",
                      roleColor(UIColorRole::DisabledText));

            for (std::size_t i = 0; i < cards_.size(); ++i) {
                const FontFamily& family = *cards_[i];
                const newui::Rect card = cardRect(i);
                const newui::Rect box(bounds.left() + card.left(), bounds.top() + card.top(), card.width(), card.height());
                const bool selected = model_.selectedSource() == FontsModel::Source::Bundled && model_.selectedName() == family.name;
                const bool keep = model_.kept(family.name);

                ctx.save();
                ctx.clip_to_rect(BLRect(box.left(), box.top(), box.width(), box.height()));
                if (!keep) ctx.set_global_alpha(0.55);
                ctx.set_fill_style(roleColor(UIColorRole::ControlBackground).toBLRgba32());
                ctx.fill_round_rect(roundRect(box, 8.0));
                ctx.set_stroke_style(roleColor(selected ? UIColorRole::HighlightBackground : UIColorRole::ControlBorder).toBLRgba32());
                ctx.set_stroke_width(selected ? 2.0 : 1.0);
                ctx.stroke_round_rect(roundRect(box, 8.0));

                std::string detail = std::to_string(family.faces.size()) + (family.faces.size() == 1 ? " file - " : " files - ") +
                                     FontCatalog::sizeText(family.bytes());
                double nameWidth = 0.0;
                if (uiFont != nullptr && uiFont->is_valid()) nameWidth = measureTextWidth(*uiFont, family.name);
                paintText(ctx, newui::Rect(box.left() + 14.0f, box.top() + 8.0f, float(nameWidth) + 4.0f, 20.0f), family.name,
                          roleColor(UIColorRole::ControlText));
                paintText(ctx, newui::Rect(box.left() + 20.0f + float(nameWidth), box.top() + 8.0f, box.width() - kKeepWidth - 26.0f - float(nameWidth), 20.0f),
                          detail, roleColor(UIColorRole::DisabledText));

                const newui::Rect tick(box.left() + box.width() - kKeepWidth, box.top() + 10.0f, kCheckboxSize, kCheckboxSize);
                paintCheckbox(ctx, tick, keep, roleColor(UIColorRole::ControlText));
                paintText(ctx, newui::Rect(tick.left() + kCheckboxSize + 6.0f, box.top() + 8.0f, kKeepWidth - kCheckboxSize - 6.0f, 20.0f), "Keep",
                          roleColor(UIColorRole::ControlText));

                BLFont* specimen = specimenFont(family.name);
                if (specimen != nullptr && specimen->is_valid()) {
                    ctx.set_fill_style(roleColor(UIColorRole::ControlText).toBLRgba32());
                    ctx.fill_utf8_text(BLPoint(box.left() + 14.0, box.top() + 40.0 + specimen->metrics().ascent), *specimen, kSpecimen);
                }
                ctx.restore();
            }
            ctx.restore();
        }

    private:
        std::size_t columns() const
        {
            const float usable = width_ - kPadding * 2.0f + kGap;
            return std::max<std::size_t>(1, static_cast<std::size_t>(std::floor(usable / (kCardMinWidth + kGap))));
        }

        // One font per family, kept for the view's life so a painted BLFont stays valid.
        BLFont* specimenFont(const std::string& family)
        {
            auto found = specimens_.find(family);
            if (found == specimens_.end()) found = specimens_.emplace(family, newui::Font(family, kSpecimenSize)).first;
            return found->second.blFont();
        }

        std::vector<const FontFamily*> cards_;
        std::map<std::string, newui::Font> specimens_;
        float width_ = 600.0f;
    };

    // Label and value pairs for the selection.
    class FontPropertiesView : public FontListView
    {
    public:
        static constexpr float kRowHeight = 28.0f;
        static constexpr float kKeyWidth = 104.0f;

        explicit FontPropertiesView(FontsModel& model) : FontListView(model) {}

        void reload() override { style().markDirty(); }
        float contentHeight(float) override { return 0.0f; }

        void paint(BLContext& ctx) override
        {
            const newui::Rect bounds = getClientBounds();
            ctx.save();
            ctx.clip_to_rect(BLRect(bounds));
            paintText(ctx, newui::Rect(bounds.left() + kPadding, bounds.top() + 10.0f, bounds.width() - kPadding * 2.0f, 20.0f),
                      model_.propertiesTitle(), roleColor(UIColorRole::DisabledText));
            float y = bounds.top() + 38.0f;
            for (const auto& entry : model_.properties()) {
                paintText(ctx, newui::Rect(bounds.left() + kPadding, y, kKeyWidth - 8.0f, kRowHeight), entry.first, roleColor(UIColorRole::DisabledText));
                paintText(ctx, newui::Rect(bounds.left() + kPadding + kKeyWidth, y, bounds.width() - kPadding * 2.0f - kKeyWidth, kRowHeight), entry.second,
                          roleColor(UIColorRole::ControlText));
                y += kRowHeight;
            }

            const std::string family = model_.previewFamily();
            if (!family.empty()) paintPreview(ctx, bounds, y + 8.0f, family);
            ctx.restore();
        }

    private:
        BLFont* fontFor(const std::string& name, float size)
        {
            const std::string key = name + "@" + std::to_string(static_cast<int>(size));
            auto found = fonts_.find(key);
            if (found == fonts_.end()) found = fonts_.emplace(key, newui::Font(name, size)).first;
            BLFont* font = found->second.blFont();
            return font != nullptr && font->is_valid() ? font : nullptr;
        }

        void line(BLContext& ctx, const newui::Rect& bounds, float& y, const std::string& family, float size, const std::string& text)
        {
            BLFont* font = fontFor(family, size);
            y += size * 1.35f;
            if (font == nullptr || y > bounds.top() + bounds.height()) return;
            ctx.set_fill_style(roleColor(UIColorRole::ControlText).toBLRgba32());
            ctx.fill_utf8_text(BLPoint(bounds.left() + kPadding, y), *font, text.c_str(), text.size());
        }

        // The family at a few sizes, then each of its styles in its own face.
        void paintPreview(BLContext& ctx, const newui::Rect& bounds, float y, const std::string& family)
        {
            ctx.set_stroke_style(roleColor(UIColorRole::ControlBorder).toBLRgba32());
            ctx.set_stroke_width(1.0);
            ctx.stroke_line(BLPoint(bounds.left() + kPadding, y), BLPoint(bounds.left() + bounds.width() - kPadding, y));
            y += 6.0f;
            paintText(ctx, newui::Rect(bounds.left() + kPadding, y, bounds.width() - kPadding * 2.0f, 20.0f), "PREVIEW", roleColor(UIColorRole::DisabledText));
            y += 22.0f;
            ctx.save();
            ctx.clip_to_rect(BLRect(bounds.left() + kPadding, y, bounds.width() - kPadding * 2.0f, bounds.height()));
            line(ctx, bounds, y, family, 30.0f, "Aa Bg Qy 123");
            line(ctx, bounds, y, family, 16.0f, "The quick brown fox jumps");
            line(ctx, bounds, y, family, 14.0f, "ABCDEFGHIJKLMNOPQRSTUVWXYZ");
            line(ctx, bounds, y, family, 14.0f, "abcdefghijklmnopqrstuvwxyz");
            line(ctx, bounds, y, family, 14.0f, "0123456789 {}[]()<>;:=+-*/");
            line(ctx, bounds, y, family, 11.0f, "int main() { return 0; }  // 11 pt");
            const std::vector<std::string> faces = model_.previewFaces();
            if (faces.size() > 1) {
                y += 10.0f;
                for (const std::string& face : faces) line(ctx, bounds, y, face, 14.0f, face);
            }
            ctx.restore();
        }

        std::map<std::string, newui::Font> fonts_;
    };

    FontsSurface::FontsSurface()
    {
        setVisible(true);
        setName("fontsSurface");
        style().setBackgroundColor(roleColor(UIColorRole::WindowBackground));
        setLayout(std::make_unique<newui::FlexLayout>(newui::Orientation::Vertical));

        // header: the filter, then the face count at the right
        auto* header = new newui::SubView();
        header->setName("fontsHeader");
        header->setVisible(true);
        header->style().setBackgroundColor(roleColor(UIColorRole::ControlBackground));
        auto headerLayout = std::make_unique<newui::FlexLayout>(newui::Orientation::Horizontal);
        headerLayout->setSpacing(8.0f);
        headerLayout->setPadding(8.0f);
        headerLayout->setCrossAxisAlignment(newui::CrossAxisAlignment::Center);
        header->setLayout(std::move(headerLayout));
        header->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(0.0f));
        header->setDesiredSize(newui::Size(0.0f, kHeaderHeight));

        filter_ = new newui::TextField();
        filter_->setName("fontsFilter");
        filter_->setVisible(true);
        filter_->setPlaceholder("Filter fonts...");
        filter_->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(0.0f));
        filter_->setDesiredSize(newui::Size(kFilterWidth, 28.0f));
        filter_->model().onChanged.add([this](newui::Model&) {
            model_.setFilter(wideToUtf8(filter_->text()));
            modelChanged();
            return newui::SyncReturn::Handled;
        });
        header->addChild(filter_);

        auto* spacer = new newui::SubView();
        spacer->setVisible(true);
        spacer->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
        header->addChild(spacer);

        total_ = new newui::Label();
        total_->setName("fontsTotal");
        total_->setVisible(true);
        total_->setTextAlignment(newui::TextAlignment::Right);
        total_->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(0.0f));
        total_->setDesiredSize(newui::Size(160.0f, 24.0f));
        header->addChild(total_);
        addChild(header);

        // body: sources | specimens | properties
        auto* body = new newui::SubView();
        body->setName("fontsBody");
        body->setVisible(true);
        auto bodyLayout = std::make_unique<newui::FlexLayout>(newui::Orientation::Horizontal);
        bodyLayout->setSpacing(1.0f);
        bodyLayout->setPadding(0.0f);
        body->setLayout(std::move(bodyLayout));
        body->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));

        sourcesScroll_ = new newui::ScrollView();
        sourcesScroll_->setName("fontsSourcesScroll");
        sourcesScroll_->style().setBackgroundColor(roleColor(UIColorRole::ControlBackground));
        sourcesScroll_->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(0.0f));
        sourcesScroll_->setDesiredSize(newui::Size(kSourcesWidth, 0.0f));
        sources_ = new FontSourcesView(model_);
        sources_->setName("fontsSources");
        sourcesScroll_->addChild(sources_);
        body->addChild(sourcesScroll_);

        cardsScroll_ = new newui::ScrollView();
        cardsScroll_->setName("fontsCardsScroll");
        cardsScroll_->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
        cards_ = new FontCardsView(model_);
        cards_->setName("fontsCards");
        cardsScroll_->addChild(cards_);
        body->addChild(cardsScroll_);

        properties_ = new FontPropertiesView(model_);
        properties_->setName("fontsProperties");
        properties_->style().setBackgroundColor(roleColor(UIColorRole::ControlBackground));
        properties_->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(0.0f));
        properties_->setDesiredSize(newui::Size(kPropertiesWidth, 0.0f));
        body->addChild(properties_);
        addChild(body);

        // footer: the totals
        auto* footer = new newui::SubView();
        footer->setName("fontsFooter");
        footer->setVisible(true);
        footer->style().setBackgroundColor(roleColor(UIColorRole::ControlBackground));
        auto footerLayout = std::make_unique<newui::FlexLayout>(newui::Orientation::Horizontal);
        footerLayout->setSpacing(16.0f);
        footerLayout->setPadding(8.0f);
        footerLayout->setCrossAxisAlignment(newui::CrossAxisAlignment::Center);
        footer->setLayout(std::move(footerLayout));
        footer->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(0.0f));
        footer->setDesiredSize(newui::Size(0.0f, kFooterHeight));

        kept_ = new newui::Label();
        kept_->setName("fontsKept");
        kept_->setVisible(true);
        kept_->setTextAlignment(newui::TextAlignment::Left);
        kept_->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(0.0f));
        kept_->setDesiredSize(newui::Size(180.0f, 24.0f));
        footer->addChild(kept_);
        size_ = new newui::Label();
        size_->setName("fontsSize");
        size_->setVisible(true);
        size_->setTextAlignment(newui::TextAlignment::Left);
        size_->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
        footer->addChild(size_);
        addChild(footer);

        auto changed = [this]() { modelChanged(); };
        sources_->onChanged = changed;
        cards_->onChanged = changed;
    }

    void FontsSurface::refresh()
    {
        std::vector<FontsModel::LiveFont> live;
        struct Role { const char* name; newui::SystemUIFont font; };
        const Role roles[] = {
            { "Message", newui::SystemUIFont::Message }, { "Caption", newui::SystemUIFont::Caption },
            { "Small caption", newui::SystemUIFont::SmallCaption }, { "Menu", newui::SystemUIFont::Menu },
            { "Status", newui::SystemUIFont::Status },
        };
        for (const Role& role : roles) {
            newui::Font font = newui::FontManager::getSystemFont(role.font);
            live.push_back({ role.name, font.name() });
        }
        model_.setLiveFonts(std::move(live));
        model_.setCatalog(FontCatalog::scan());
        loaded_ = true;
        modelChanged();
    }

    void FontsSurface::modelChanged()
    {
        sources_->reload();
        cards_->reload();
        properties_->reload();
        updateLabels();
        fitContent();
        redraw();
    }

    void FontsSurface::updateLabels()
    {
        total_->setText(model_.totalText());
        kept_->setText(model_.keptText());
        size_->setText(model_.sizeSummary());
    }

    FontsSurface::~FontsSurface()
    {
        // children are deleted after this body; their removal re-runs updateLayout()
        sourcesScroll_ = nullptr;
        cardsScroll_ = nullptr;
        sources_ = nullptr;
        cards_ = nullptr;
        properties_ = nullptr;
    }

    void FontsSurface::updateLayout()
    {
        newui::SubView::updateLayout();
        if (!isDestroying()) fitContent();
    }

    void FontsSurface::fitContent()
    {
        if (sourcesScroll_ == nullptr || cardsScroll_ == nullptr) return;
        // the room a scroll view leaves its content once its bar is reserved
        auto fit = [](newui::ScrollView* scroll, FontListView* list) {
            const float barWidth = 16.0f;
            const newui::Rect client = scroll->getClientBounds();
            float width = client.width();
            float height = list->contentHeight(width);
            if (height > client.height()) {
                width = std::max(0.0f, width - barWidth);
                height = list->contentHeight(width);
            }
            list->setBounds(newui::Rect(0.0f, 0.0f, width, std::max(height, client.height())));
            scroll->setContentSize(newui::Size(width, height));
        };
        fit(sourcesScroll_, sources_);
        fit(cardsScroll_, cards_);
    }
}
