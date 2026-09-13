#include "GradientEditorDialog.h"
#include "TextEncoding.h"

#include <newui/layout.h>
#include <newui/rootview.h>
#include <newui/uicolormanager.h>
#include <newui/viewbuilder.h>

namespace CodeToolsVsix
{
    namespace
    {
        constexpr float kDialogWidth = 360.0f;
        constexpr float kDialogHeight = 420.0f;
        constexpr float kRowHeight = 24.0f;
        constexpr float kLabelWidth = 70.0f;
    }

    GradientEditorDialog::GradientEditorDialog()
    {
        setTitle("Edit Gradient");
        setBounds(newui::Rect(120.0f, 120.0f, kDialogWidth, kDialogHeight));
        buildChrome();
        rebuildStopRows();
    }

    void GradientEditorDialog::setGradient(const newui::gfx::Gradient& gradient)
    {
        working_ = gradient;
        // A Fill whose backgroundFill was never a gradient before (the common first-click case)
        // seeds a real, empty-stops newui::gfx::Gradient() - correct, but leaves Phase 1 with
        // nothing at all to edit and no add-stop UI yet (that's the real track control, Phase 3).
        // Seeding two reasonable default stops here keeps this phase actually usable on that real
        // first-click case, same spirit as every other "0 isn't useful, give it a real starting
        // point" default elsewhere in this codebase (e.g. Gradient's own linearEnd_/radialRadius_
        // class defaults).
        if (working_.kind() != newui::gfx::GradientKind::Point && working_.stops().empty()) {
            working_.stops().push_back(newui::gfx::GradientStop(0.0f, newui::Color(0, 0, 0)));
            working_.stops().push_back(newui::gfx::GradientStop(1.0f, newui::Color(255, 255, 255)));
        }
        selectedStopIndex_ = 0;
        // A no-op if kindControl_ already shows the right kind; otherwise fires
        // onSelectionChanged, which calls setKind() -> rebuildStopRows() again - harmless (just
        // one extra pass over the same data), not unsafe, since rebuildStopRows() never touches
        // kindControl_ itself (see buildChrome()'s own comment).
        kindControl_->setSelectedIndex(static_cast<std::size_t>(working_.kind()));
        rebuildStopRows();
    }

    void GradientEditorDialog::setKind(newui::gfx::GradientKind kind)
    {
        working_.setKind(kind);
        kindControl_->setSelectedIndex(static_cast<std::size_t>(kind));
        rebuildStopRows();
    }

    void GradientEditorDialog::selectStop(std::size_t index)
    {
        std::size_t count = working_.stops().size();
        selectedStopIndex_ = count == 0 ? 0 : (index >= count ? count - 1 : index);
    }

    void GradientEditorDialog::setSelectedStopColor(const newui::Color& color)
    {
        std::vector<newui::gfx::GradientStop>& stops = working_.stops();
        if (selectedStopIndex_ < stops.size()) {
            stops[selectedStopIndex_].setColor(color);
        }
    }

    void GradientEditorDialog::buildChrome()
    {
        // Real top-level surfaces elsewhere in this codebase (Workspace, DesignerEditor,
        // testharness's own main Frame) all explicitly set WindowBackground - there's no default
        // that isn't black, so this dialog painted solid black with nothing visibly readable
        // against it until this was added.
        rootView().style().setBackgroundColor(newui::UIColorManager::colorFor(newui::UIColorRole::WindowBackground));
        // AnchorLayout on rootView() itself is what makes contentRoot_'s stretch-fill
        // AnchorLayoutParams (below) actually apply - without a Layout attached here,
        // View::updateLayout() has nothing to call arrange() on, so contentRoot_ would just sit at
        // whatever fixed size it was built with even once the real native window (whose actual
        // client size only becomes known once ensureInitialized() creates it) turns out different -
        // exactly why the footer was landing outside the visible area.
        rootView().setLayout(std::make_unique<newui::AnchorLayout>());

        newui::ViewBuilder<newui::SubView> rootBuilder;
        rootBuilder.name("gradientEditorContent")
            .visible(true)
            .bounds(newui::Rect(0.0f, 0.0f, kDialogWidth, kDialogHeight))
            .layoutParams<newui::AnchorLayoutParams>([](newui::AnchorLayoutParams& params) {
                params.anchors = newui::Anchor::Left | newui::Anchor::Top
                                | newui::Anchor::Right | newui::Anchor::Bottom;
            })
            .layout<newui::FlexLayout>([](newui::FlexLayout& layout) {
                layout.setOrientation(newui::Orientation::Vertical);
                layout.setSpacing(8.0f);
                layout.setPadding(12.0f);
            });
        contentRoot_ = rootBuilder.build();
        rootView().addChild(contentRoot_);

        newui::ViewBuilder<newui::SegmentedControl> kindBuilder;
        kindBuilder.name("gradientKindControl")
            .visible(true)
            .configure([](newui::SegmentedControl& control) {
                control.setSegments({"Linear", "Radial", "Conic", "Point"});
            });
        kindControl_ = kindBuilder.build();
        kindControl_->setDesiredSize(kindControl_->naturalSize());
        kindControl_->onSelectionChanged.add([this](newui::SegmentedControl& sender) {
            setKind(static_cast<newui::gfx::GradientKind>(sender.selectedIndex()));
            return newui::SyncReturn::Handled;
        });
        contentRoot_->addChild(kindControl_);

        newui::ViewBuilder<newui::SubView> rowsBuilder;
        rowsBuilder.name("gradientStopRows")
            .visible(true)
            .layoutParams<newui::FlexLayoutParams>([](newui::FlexLayoutParams& params) { params.weight = 1.0f; })
            .layout<newui::FlexLayout>([](newui::FlexLayout& layout) {
                layout.setOrientation(newui::Orientation::Vertical);
                layout.setSpacing(4.0f);
            });
        stopRows_ = rowsBuilder.build();
        contentRoot_->addChild(stopRows_);

        newui::ViewBuilder<newui::SubView> footerBuilder;
        footerBuilder.name("gradientEditorFooter")
            .visible(true)
            .desiredSize(newui::Size(0.0f, 32.0f))
            .layout<newui::FlexLayout>([](newui::FlexLayout& layout) {
                layout.setOrientation(newui::Orientation::Horizontal);
                layout.setSpacing(8.0f);
            });
        newui::SubView* footer = footerBuilder.build();

        newui::ViewBuilder<newui::SubView> footerSpacerBuilder;
        footerSpacerBuilder.name("gradientEditorFooterSpacer")
            .visible(true)
            .layoutParams<newui::FlexLayoutParams>([](newui::FlexLayoutParams& params) { params.weight = 1.0f; });
        footer->addChild(footerSpacerBuilder.build());

        newui::ViewBuilder<newui::Button> cancelBuilder;
        cancelBuilder.name("gradientEditorCancelButton")
            .visible(true)
            .desiredSize(newui::Size(70.0f, 24.0f))
            .configure([](newui::Button& button) { button.setText("Cancel"); });
        newui::Button* cancelButton = cancelBuilder.build();
        cancelButton->onClick.add([this](newui::Control&) {
            close(newui::DialogResult::Cancel);
            return newui::SyncReturn::Handled;
        });
        footer->addChild(cancelButton);

        newui::ViewBuilder<newui::Button> applyBuilder;
        applyBuilder.name("gradientEditorApplyButton")
            .visible(true)
            .desiredSize(newui::Size(70.0f, 24.0f))
            .configure([](newui::Button& button) { button.setText("Apply"); });
        newui::Button* applyButton = applyBuilder.build();
        applyButton->onClick.add([this](newui::Control&) {
            close(newui::DialogResult::Ok);
            return newui::SyncReturn::Handled;
        });
        footer->addChild(applyButton);

        contentRoot_->addChild(footer);
    }

    void GradientEditorDialog::rebuildStopRows()
    {
        // Rebuilt fresh on every kind/seed change (see this method's own declaration comment,
        // GradientEditorDialog.h, for why this never touches kindControl_/the footer).
        // removeChild() only detaches (never deletes) - same "copy the list first, delete each"
        // shape Workspace's own "New" button already uses.
        std::vector<newui::SubView*> old = stopRows_->childViews();
        for (newui::SubView* child : old) {
            stopRows_->removeChild(child);
            delete child;
        }
        stopHexFields_.clear();

        // Point isn't built yet (Phase 4 - see this class's own header comment) - an honest
        // "not built yet" placeholder, matching this project's own convention elsewhere
        // (Workspace's disabled Source/Data Flow segments) rather than pretending capability
        // that doesn't exist.
        if (working_.kind() == newui::gfx::GradientKind::Point) {
            newui::ViewBuilder<newui::Label> placeholderBuilder;
            placeholderBuilder.name("gradientPointPlaceholder")
                .visible(true)
                .desiredSize(newui::Size(0.0f, kRowHeight))
                .configure([](newui::Label& label) { label.setText("Point editing not built yet"); });
            stopRows_->addChild(placeholderBuilder.build());
            return;
        }

        const std::vector<newui::gfx::GradientStop>& stops = working_.stops();
        for (std::size_t i = 0; i < stops.size(); ++i) {
            newui::ViewBuilder<newui::SubView> rowBuilder;
            rowBuilder.name("gradientStopRow")
                .visible(true)
                .desiredSize(newui::Size(0.0f, kRowHeight))
                .layout<newui::FlexLayout>([](newui::FlexLayout& layout) {
                    layout.setOrientation(newui::Orientation::Horizontal);
                    layout.setSpacing(6.0f);
                });
            newui::SubView* row = rowBuilder.build();

            newui::ViewBuilder<newui::Label> labelBuilder;
            labelBuilder.name("gradientStopLabel")
                .visible(true)
                .desiredSize(newui::Size(kLabelWidth, kRowHeight))
                .configure([i](newui::Label& label) { label.setText("Stop " + std::to_string(i + 1)); });
            row->addChild(labelBuilder.build());

            newui::ViewBuilder<newui::TextField> fieldBuilder;
            fieldBuilder.name("gradientStopHexField")
                .visible(true)
                .layoutParams<newui::FlexLayoutParams>([](newui::FlexLayoutParams& params) { params.weight = 1.0f; })
                .configure([&stops, i](newui::TextField& field) {
                    field.setText(utf8ToWide(stops[i].color().toString()));
                });
            newui::TextField* field = fieldBuilder.build();

            auto commit = [this, field, i]() {
                newui::Color color;
                if (newui::Color::fromString(wideToUtf8(field->text()), color)) {
                    selectStop(i);
                    setSelectedStopColor(color);
                }
            };
            field->onLostFocus.add([commit](newui::View&) { commit(); return newui::SyncReturn::Ignored; });
            field->onReturnPressed.add([commit](newui::TextField&) { commit(); return newui::SyncReturn::Handled; });

            row->addChild(field);
            stopHexFields_.push_back(field);
            stopRows_->addChild(row);
        }
    }
}
