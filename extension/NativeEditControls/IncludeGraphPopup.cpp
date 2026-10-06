#include "IncludeGraphPopup.h"
#include "CalloutPlacement.h"
#include "IncludeGraphLayout.h"
#include "PaintUtils.h"
#include "PickerRow.h"

#include <cpptools/includeanalysis.h>

#include <newui/application.h>
#include <newui/controls.h>
#include <newui/fontmanager.h>
#include <newui/rootview.h>
#include <newui/runloop.h>
#include <newui/uicolormanager.h>

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <map>

namespace CodeToolsVsix
{
    // The boxes and the arrows between them, painted by hand. Holds one layout; a click on a box asks its owner to
    // centre on that file.
    class IncludeGraphView : public newui::SubView
    {
    public:
        using PathHandler = std::function<void(const std::string&)>;

        IncludeGraphView()
        {
            setVisible(true);
            onMouseDown.add([this](newui::View&, const newui::Point& point, std::uint32_t, std::uint32_t) {
                const std::string path = pathAt(point);
                if (!path.empty() && onCentre) onCentre(path);
                return path.empty() ? newui::SyncReturn::Ignored : newui::SyncReturn::Handled;
            });
            onMouseDblClick.add([this](newui::View&, const newui::Point& point, std::uint32_t, std::uint32_t) {
                const std::string path = pathAt(point);
                if (!path.empty() && onOpen) onOpen(path);
                return path.empty() ? newui::SyncReturn::Ignored : newui::SyncReturn::Handled;
            });
        }

        PathHandler onCentre;
        PathHandler onOpen;

        void setLayout(IncludeGraphLayout layout)
        {
            layout_ = std::move(layout);
            most_ = 1;
            for (const IncludeGraphNode& node : layout_.nodes) most_ = std::max(most_, node.units);
            style().markDirty();
        }

        void paint(BLContext& ctx) override
        {
            const newui::Rect bounds = getClientBounds();
            if (bounds.width() <= 0.0f || bounds.height() <= 0.0f || layout_.empty()) return;

            using newui::UIColorManager;
            using newui::UIColorRole;
            const BLRgba32 line = UIColorManager::colorFor(UIColorRole::DisabledText).toBLRgba32();
            const BLRgba32 text = UIColorManager::colorFor(UIColorRole::ControlText).toBLRgba32();
            const BLRgba32 muted = UIColorManager::colorFor(UIColorRole::DisabledText).toBLRgba32();
            const BLRgba32 fill = UIColorManager::colorFor(UIColorRole::ControlBackground).toBLRgba32();
            const BLRgba32 border = UIColorManager::colorFor(UIColorRole::ControlBorder).toBLRgba32();
            const BLRgba32 focusFill = UIColorManager::colorFor(UIColorRole::HighlightBackground).toBLRgba32();
            const BLRgba32 focusText = UIColorManager::colorFor(UIColorRole::HighlightText).toBLRgba32();
            const BLRgba32 heat = UIColorManager::colorFor(UIColorRole::LinkText).toBLRgba32();

            ctx.save();
            ctx.clip_to_rect(BLRect(bounds));

            // arrows first, under the boxes
            ctx.set_stroke_style(line);
            ctx.set_stroke_width(1.25);
            for (const IncludeGraphEdge& edge : layout_.edges) {
                const newui::Rect from = boxRect(layout_.nodes[edge.from]);
                const newui::Rect to = boxRect(layout_.nodes[edge.to]);
                const double x0 = from.left() + from.width();
                const double y0 = from.top() + from.height() * 0.5;
                const double x1 = to.left();
                const double y1 = to.top() + to.height() * 0.5;
                const double mid = (x0 + x1) * 0.5;
                BLPath curve;
                curve.move_to(x0, y0);
                curve.cubic_to(mid, y0, mid, y1, x1 - 1.0, y1);
                ctx.stroke_path(curve);
                BLPath head;
                head.move_to(x1, y1);
                head.line_to(x1 - 6.0, y1 - 3.5);
                head.line_to(x1 - 6.0, y1 + 3.5);
                head.close();
                ctx.set_fill_style(line);
                ctx.fill_path(head);
            }

            newui::Font systemFont = newui::FontManager::getSystemFont(newui::SystemUIFont::Message);
            BLFont* font = systemFont.blFont();
            for (const IncludeGraphNode& node : layout_.nodes) {
                const newui::Rect box = boxRect(node);
                const bool focus = node.column == 0;
                const bool more = node.path.empty();
                ctx.set_fill_style(focus ? focusFill : fill);
                ctx.fill_round_rect(BLRoundRect(box.left(), box.top(), box.width(), box.height(), 5.0, 5.0));
                ctx.set_stroke_style(more ? line : border);
                ctx.set_stroke_width(1.0);
                ctx.stroke_round_rect(BLRoundRect(box.left(), box.top(), box.width(), box.height(), 5.0, 5.0));
                if (!more && !focus && node.units > 0) {   // a thin bar along the bottom: how many translation units it reaches
                    const double barWidth = (box.width() - 12.0) * double(node.units) / double(most_);
                    ctx.set_fill_style(heat);
                    ctx.fill_round_rect(BLRoundRect(box.left() + 6.0, box.top() + box.height() - 5.0, std::max(barWidth, 3.0), 2.0, 1.0, 1.0));
                }
                if (font == nullptr || !font->is_valid()) continue;
                const std::string count = more || node.units == 0 ? std::string() : std::to_string(node.units);
                const double countWidth = count.empty() ? 0.0 : measureTextWidth(*font, count);
                const double room = box.width() - 14.0 - (count.empty() ? 0.0 : countWidth + 6.0);
                const std::string label = fitted(*font, node.label, room);
                const double baseline = box.top() + box.height() * 0.5 + (font->metrics().ascent - font->metrics().descent) * 0.5 - 1.0;
                ctx.set_fill_style(focus ? focusText : more ? muted : text);
                ctx.fill_utf8_text(BLPoint(box.left() + 7.0, baseline), *font, label.c_str(), label.size());
                if (!count.empty()) {
                    ctx.set_fill_style(focus ? focusText : muted);
                    ctx.fill_utf8_text(BLPoint(box.left() + box.width() - 7.0 - countWidth, baseline), *font, count.c_str(), count.size());
                }
            }
            ctx.restore();
        }

        // Where a box is, in this view's own coordinates. Each column is centred on the view's height.
        newui::Rect boxRect(const IncludeGraphNode& node) const
        {
            const newui::Rect bounds = getClientBounds();
            const int columns = layout_.lastColumn - layout_.firstColumn + 1;
            const float contentWidth = float(columns) * IncludeGraphPopup::kBoxWidth + float(columns - 1) * IncludeGraphPopup::kColumnGap;
            const float left = bounds.left() + (bounds.width() - contentWidth) * 0.5f;
            std::size_t inColumn = 0;
            for (const IncludeGraphNode& other : layout_.nodes) inColumn += other.column == node.column ? 1 : 0;
            const float step = IncludeGraphPopup::kBoxHeight + IncludeGraphPopup::kRowGap;
            const float columnHeight = float(inColumn) * step - IncludeGraphPopup::kRowGap;
            const float top = bounds.top() + (bounds.height() - columnHeight) * 0.5f;
            return newui::Rect(left + float(node.column - layout_.firstColumn) * (IncludeGraphPopup::kBoxWidth + IncludeGraphPopup::kColumnGap),
                               top + float(node.row) * step, IncludeGraphPopup::kBoxWidth, IncludeGraphPopup::kBoxHeight);
        }

        // The file whose box holds `point`; empty for none or for a "+N more" box.
        std::string pathAt(const newui::Point& point) const
        {
            for (const IncludeGraphNode& node : layout_.nodes) {
                const newui::Rect box = boxRect(node);
                if (point.x >= box.left() && point.x <= box.left() + box.width() && point.y >= box.top() && point.y <= box.top() + box.height()) {
                    return node.path;
                }
            }
            return std::string();
        }

        const IncludeGraphLayout& layout() const { return layout_; }

    private:
        // `text` cut at a character with an ellipsis so it fits in `room` pixels.
        static std::string fitted(BLFont& font, const std::string& text, double room)
        {
            if (measureTextWidth(font, text) <= room) return text;
            std::string cut = text;
            while (!cut.empty() && measureTextWidth(font, cut + "...") > room) cut.pop_back();
            return cut + "...";
        }

        IncludeGraphLayout layout_;
        std::size_t most_ = 1;
    };

    namespace
    {
        struct GraphState
        {
            std::shared_ptr<const cpptools::IncludeGraph> graph;
            std::function<void(const std::string&)> onOpen;
            newui::PopupTool* popup = nullptr;
            IncludeGraphView* view = nullptr;
            newui::Label* heading = nullptr;

            void centreOn(const std::string& path)
            {
                IncludeGraphLayout layout = layoutIncludeGraph(*graph, path);
                if (layout.empty()) return;
                const cpptools::HeaderImpact impact = graph->impactOf(path);
                heading->setText(layout.nodes[0].label + "  -  " + std::to_string(impact.transitiveSources) + " translation units reach it");
                view->setLayout(std::move(layout));
                popup->markDirty();
                popup->repaintNow();
            }

            void open(const std::string& path)
            {
                std::function<void(const std::string&)> callback = onOpen;
                popup->dismiss();
                auto run = [callback, path]() {
                    if (callback) callback(path);
                };
                // After the popup is gone: this runs inside the popup's own click dispatch.
                if (newui::RunLoop::current()) newui::RunLoop::current().post(std::move(run));
                else run();
            }
        };

        // The usable area of the monitor the anchor is on: a graph this wide does not fit a narrow tool window.
        newui::Rect workAreaNear(const newui::Rect& anchor)
        {
            RECT rect = { LONG(anchor.left()), LONG(anchor.top()), LONG(anchor.left() + anchor.width()), LONG(anchor.top() + anchor.height()) };
            MONITORINFO info = {};
            info.cbSize = sizeof(info);
            if (!::GetMonitorInfoW(::MonitorFromRect(&rect, MONITOR_DEFAULTTONEAREST), &info)) {
                return newui::Rect(0.0f, 0.0f, 1920.0f, 1080.0f);
            }
            return newui::Rect(float(info.rcWork.left), float(info.rcWork.top), float(info.rcWork.right - info.rcWork.left),
                               float(info.rcWork.bottom - info.rcWork.top));
        }
    }

    newui::PopupTool* IncludeGraphPopup::show(newui::View& owner, const newui::Rect& anchorScreenRect,
                                              std::shared_ptr<const cpptools::IncludeGraph> graph, const std::string& focus,
                                              std::function<void(const std::string&)> onOpen)
    {
        newui::RootView* rootView = owner.rootView();
        if (rootView == nullptr || rootView->windowHandle() == nullptr || graph == nullptr) return nullptr;
        IncludeGraphLayout first = layoutIncludeGraph(*graph, focus);
        if (first.empty()) return nullptr;

        const float graphHeight = float(kMostRows) * (kBoxHeight + kRowGap) - kRowGap;
        const float innerHeight = kHeaderHeight + kGap + graphHeight + kGap + kFooterHeight;
        const float height = PickerRow::kTopReserve + kPadding * 2.0f + innerHeight;
        const CalloutPlacement placement = placeCallout(anchorScreenRect, newui::Size(kWidth, height), workAreaNear(anchorScreenRect));

        auto* popup = new newui::CalloutTool(rootView->windowHandle(), newui::Application::instance().instanceHandle(),
                                             placement.bounds, "includeGraphPopup");
        if (!popup->initialize()) {
            delete popup;
            return nullptr;
        }
        popup->setTailSide(placement.tailSide);
        popup->setTailPosition(placement.tailPosition);

        auto state = std::make_shared<GraphState>();
        state->popup = popup;
        state->graph = std::move(graph);
        state->onOpen = std::move(onOpen);

        const newui::Size actual = placement.bounds.size();
        const float contentWidth = actual.width - kPadding * 2.0f;
        float y = placement.tailSide == newui::shapes::TailSide::Bottom ? kPadding : PickerRow::kTopReserve + kPadding;

        state->heading = new newui::Label();
        state->heading->setVisible(true);
        state->heading->setTextAlignment(newui::TextAlignment::Left);
        state->heading->setBounds(newui::Rect(kPadding, y, contentWidth, kHeaderHeight));
        popup->addChild(state->heading);
        y += kHeaderHeight + kGap;

        state->view = new IncludeGraphView();
        state->view->setBounds(newui::Rect(kPadding, y, contentWidth, graphHeight));
        popup->addChild(state->view);
        y += graphHeight + kGap;

        auto* hint = new newui::Label();
        hint->setVisible(true);
        hint->setTextAlignment(newui::TextAlignment::Left);
        hint->setText("Includers on the left, includes on the right; the number is the translation units a change reaches. "
                      "Click a file to centre on it, double-click to open it.");
        hint->setBounds(newui::Rect(kPadding, y, contentWidth, kFooterHeight));
        popup->addChild(hint);

        state->view->onCentre = [state](const std::string& path) { state->centreOn(path); };
        state->view->onOpen = [state](const std::string& path) { state->open(path); };

        state->centreOn(focus);
        popup->present();
        return popup;
    }
}
