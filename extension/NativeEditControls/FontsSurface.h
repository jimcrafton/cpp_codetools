#pragma once

#include "FontsModel.h"

#include <newui/controls.h>
#include <newui/subview.h>

namespace CodeToolsVsix
{
    class FontSourcesView;
    class FontCardsView;
    class FontPropertiesView;

    // The Designer's Fonts surface: every font on the machine and every font the application ships, with a
    // specimen of each shipped family and a Keep tick to decide what is worth bundling. Left: the sources
    // (system, bundled, live system UI fonts); centre: one specimen card per bundled family; right: the
    // selection's properties; a footer with the totals. The ticks are only a note for now - nothing is deleted.
    class FontsSurface : public newui::SubView
    {
    public:
        static constexpr float kHeaderHeight = 46.0f;
        static constexpr float kFooterHeight = 40.0f;
        static constexpr float kSourcesWidth = 270.0f;
        static constexpr float kPropertiesWidth = 300.0f;
        static constexpr float kFilterWidth = 280.0f;

        FontsSurface();
        ~FontsSurface() override;

        // Reads the font catalog (opens every font file, so it takes a moment) and fills the lists. The Workspace
        // calls it the first time the surface is shown, not when it is built.
        void refresh();
        bool loaded() const { return loaded_; }

        FontsModel& model() { return model_; }
        const FontsModel& model() const { return model_; }

        newui::TextField* filterField() const { return filter_; }
        newui::Label* totalLabel() const { return total_; }
        newui::Label* keptLabel() const { return kept_; }
        newui::Label* sizeLabel() const { return size_; }

        // After the model changed: repaint the lists and rewrite the labels.
        void modelChanged();

        void updateLayout() override;

    private:
        void fitContent();
        void updateLabels();

        FontsModel model_;
        bool loaded_ = false;
        newui::TextField* filter_ = nullptr;
        newui::Label* total_ = nullptr;
        newui::Label* kept_ = nullptr;
        newui::Label* size_ = nullptr;
        newui::ScrollView* sourcesScroll_ = nullptr;
        newui::ScrollView* cardsScroll_ = nullptr;
        FontSourcesView* sources_ = nullptr;
        FontCardsView* cards_ = nullptr;
        FontPropertiesView* properties_ = nullptr;
    };
}
