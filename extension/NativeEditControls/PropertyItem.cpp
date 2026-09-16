#include "PropertyItem.h"
#include "PaintUtils.h"

#include <newui/color.h>
#include <newui/uicolormanager.h>

#include <algorithm>
#include <cctype>
#include <typeindex>

namespace CodeToolsVsix
{
    newui::TreeItem* PropertiesTreeController::createItem(const std::vector<std::size_t>& /*path*/)
    {
        return new PropertyItem();
    }

    void PropertiesTreeController::setKeyColumnFraction(float fraction)
    {
        float clamped = fraction < kMinKeyColumnFraction ? kMinKeyColumnFraction
            : fraction > kMaxKeyColumnFraction ? kMaxKeyColumnFraction : fraction;
        if (clamped == keyColumnFraction_) {
            return;
        }
        keyColumnFraction_ = clamped;
        onDataChanged(*this);
    }

    namespace
    {
        // Same priority (disabled beats selected beats normal) items.cpp's
        // own file-local itemTextColor() uses - reimplemented here since
        // that one isn't exported.
        newui::Color rowTextColor(const newui::Item& item)
        {
            newui::UIColorRole role = !item.isEnabled() ? newui::UIColorRole::DisabledText
                : item.isSelected() ? newui::UIColorRole::HighlightText
                : newui::UIColorRole::ControlText;
            return newui::UIColorManager::colorFor(role);
        }

        // The key column and group/section labels (Main.dc.html's own
        // ".prop-row .k"/".prop-cat"/".prop-group-head", all a dimmer
        // shade than the value column's own full-strength text) - same
        // priority as rowTextColor() otherwise (selection/disabled still
        // win). DisabledText is the closest *real* Windows-backed role to
        // the mockup's own arbitrary "--text-dim" token - same "no
        // fabricated system color" discipline CanvasWell's own background
        // comment already follows, and the same role ToolboxItem/
        // PropertyRow's own header treatment already reused for exactly
        // this "muted label" purpose.
        newui::Color dimTextColor(const newui::Item& item)
        {
            newui::UIColorRole role = !item.isEnabled() ? newui::UIColorRole::DisabledText
                : item.isSelected() ? newui::UIColorRole::HighlightText
                : newui::UIColorRole::DisabledText;
            return newui::UIColorManager::colorFor(role);
        }

        // Same hand-drawn triangle items.cpp's own file-local
        // paintExpandGlyph() uses - reimplemented here, not exported.
        void paintExpandGlyph(BLContext& ctx, double centerX, double centerY, double size, bool expanded, const newui::Color& color)
        {
            double half = size * 0.5;
            BLPath path;
            if (expanded) {
                path.move_to(centerX - half, centerY - half * 0.6);
                path.line_to(centerX + half, centerY - half * 0.6);
                path.line_to(centerX, centerY + half * 0.6);
            } else {
                path.move_to(centerX - half * 0.6, centerY - half);
                path.line_to(centerX - half * 0.6, centerY + half);
                path.line_to(centerX + half * 0.6, centerY);
            }
            path.close();

            ctx.save();
            ctx.set_fill_style(color.toBLRgba32());
            ctx.fill_path(path);
            ctx.restore();
        }

        // The small "..." affordance a Dialog-style editor's row shows - see PropertyItem::
        // ellipsisButtonRectFor()'s own comment (PropertyItem.h) for why this exists at all and
        // what opens it. A plain bordered square (matches PaintUtils' own paintCheckbox()
        // "hand-drawn control chrome" weight) with 3 small dots, not real text - avoids a second
        // BLFont/glyph-buffer round trip just for three periods.
        void paintEllipsisButton(BLContext& ctx, const newui::Rect& box, const newui::Color& color)
        {
            BLRgba32 rgba = color.toBLRgba32();
            ctx.save();
            ctx.set_stroke_style(rgba);
            ctx.set_stroke_width(1.0);
            ctx.stroke_round_rect(BLRect(box.left(), box.top(), box.size().width, box.size().height), 3.0);

            ctx.set_fill_style(rgba);
            double cy = double(box.top() + box.size().height * 0.5f);
            double spacing = double(box.size().width) * 0.22;
            double cx = double(box.left() + box.size().width * 0.5f);
            for (int i = -1; i <= 1; ++i) {
                ctx.fill_circle(cx + double(i) * spacing, cy, 1.3);
            }
            ctx.restore();
        }
    }

    void PropertyItem::paint(BLContext& ctx, const newui::Rect& rect, const std::vector<std::size_t>& path,
        newui::TreeController& controller)
    {
        auto* model = dynamic_cast<PropertiesModel*>(controller.model());
        if (model == nullptr) {
            newui::TreeItem::paint(ctx, rect, path, controller);
            return;
        }

        auto* propsController = dynamic_cast<PropertiesTreeController*>(&controller);
        float keyColumnFraction = propsController != nullptr
            ? propsController->keyColumnFraction() : PropertiesTreeController::kDefaultKeyColumnFraction;

        PropertiesModel::Node node = model->nodeAt(path);
        bool isGroupLike = node.kind == PropertiesModel::Kind::PropertyGroup
            || node.kind == PropertiesModel::Kind::PropertySubGroup
            || node.kind == PropertiesModel::Kind::DelegatesHeader;

        // Group-like rows never show the row-selection highlight fill -
        // they're expand/section nodes, not an editable value (same
        // "forced false, still call Item::paint()" reasoning ToolboxItem's
        // own header treatment already established - clientBounds() is
        // only ever computed as paint()'s own side effect, items.h).
        if (isGroupLike) {
            setSelected(false);
        }
        Item::paint(ctx, rect);

        if (isGroupLike) {
            // A second, slightly different panel shade (Main.dc.html's own
            // ".prop-cat"/".prop-group-head" background) sets a group/
            // section header row visually apart from the plain rows
            // around it - no real Windows system color maps to that
            // specific "alt panel" concept, so this derives one from a
            // real system color at low alpha instead of fabricating an
            // arbitrary hex value, the same technique Item::paint()'s own
            // hover fill already uses (a reduced-alpha HighlightBackground).
            ctx.save();
            ctx.set_fill_style(newui::UIColorManager::colorFor(newui::UIColorRole::ControlBorder).toBLRgba32());
            ctx.set_fill_alpha(0.35);
            ctx.fill_rect(BLRect(rect.left(), rect.top(), rect.size().width, rect.size().height));
            ctx.restore();
        }

        bool hasChildren = model->hasChildren(path);
        double indent = double(newui::treeDepthOf(path)) * newui::kTreeIndentWidth;
        double glyphCenterX = clientBounds().left() + indent + newui::kTreeGlyphWidth * 0.5;
        double glyphCenterY = clientBounds().top() + clientBounds().size().height * 0.5;
        if (hasChildren) {
            paintExpandGlyph(ctx, glyphCenterX, glyphCenterY, newui::kTreeGlyphWidth * 0.8,
                controller.isExpanded(path), rowTextColor(*this));
        }
        double textLeft = clientBounds().left() + indent + newui::kTreeGlyphWidth;

        if (isGroupLike) {
            newui::Rect labelRect(float(textLeft), clientBounds().top(),
                clientBounds().size().width - float(textLeft - clientBounds().left()), clientBounds().size().height);

            // Uppercase, dim, smaller-font label (Main.dc.html's own
            // ".prop-cat"/".prop-group-head" - uppercase, --text-faint,
            // 10.5px vs a regular row's 12px) - SystemUIFont::Status is
            // the closest real, Windows-backed smaller UI font role
            // (NONCLIENTMETRICS::lfStatusFont), not a fabricated size.
            // Only the name itself is uppercased - the "(TypeName)" suffix
            // stays as-is, matching ".prop-group-head .type
            // { text-transform: none }" exactly (a real, caught bug: an
            // earlier version uppercased the whole label, turning e.g.
            // "style (ViewStyle)" into "STYLE (VIEWSTYLE)").
            std::string name;
            std::string typeSuffix;
            if (node.kind == PropertiesModel::Kind::DelegatesHeader) {
                name = "Delegates";
            } else {
                name = node.property->name();

                // A Kind::PropertyGroup property (Layout/LayoutParams-
                // shaped: a polymorphic, addressable pointer with no data
                // of its own) whose live pointer is currently null (e.g.
                // a leaf Button's layout()) has nothing real to name -
                // "(Layout)"/"(LayoutParams)" would just repeat the
                // always-empty declared base, so this shows "(none)"
                // instead, same convention as a real "nothing attached"
                // value. Kind::PropertySubGroup (Rect/Size/Point) never
                // takes this branch - it always has real synthetic
                // children regardless of address() (which it doesn't even
                // use), so getClass()'s own resolved name is always
                // meaningful there.
                bool attachedPointerIsNull = node.kind == PropertiesModel::Kind::PropertyGroup
                    && node.property->isAddressable() && node.property->address(node.ownerInstance) == nullptr;
                if (attachedPointerIsNull) {
                    typeSuffix = " (none)";
                } else {
                    // getClass(), not classinfo(type()) - so a Layout/
                    // LayoutParams-typed group header names the real
                    // attached subclass ("layout (FlexLayout)"), not the
                    // useless declared base ("layout (Layout)") - see
                    // PropertiesModel::classifyProperty()'s own comment.
                    const newui::reflection::Class* nested = node.property->getClass(node.ownerInstance);
                    typeSuffix = " (" + (nested != nullptr ? nested->name() : std::string("?")) + ")";
                }

                // "bounds" specifically, once PropertiesModel has determined its owning View's
                // real parent Layout doesn't afford free positioning (see Node::readOnly's own
                // comment) - shown right on this group header line since that's the one place with
                // room for a full sentence; the individual x/y/width/height rows below just dim
                // their value text instead (see the SubPropertyEntry paint below).
                if (node.readOnly && !node.readOnlyReason.empty()) {
                    typeSuffix += " - " + node.readOnlyReason;
                }
            }
            for (char& c : name) {
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            }

            // A Layout/ViewStyle-shaped PropertyGroup (a registered EditStyle::Dialog editor
            // that's *also* an addressable nested Class - see PropertiesModel::
            // classifyProperty()'s own comment) gets the same "..." affordance a Dialog leaf row
            // does, at the header row's own right edge - the type-swap popup, not the drilldown
            // into this group's own current fields, which single-clicking/expanding the row
            // itself still does exactly as before. Every other group-like row (DelegatesHeader,
            // an ordinary nested-Class PropertyGroup with no registered editor at all) never has
            // one, so this is a no-op for those.
            newui::Rect labelValueRect = labelRect;
            if (node.kind == PropertiesModel::Kind::PropertyGroup) {
                auto groupEditor = PropertyEditorRegistry::instance()
                    .createEditor(node.property, node.ownerClass, node.ownerInstance);
                if (groupEditor != nullptr && groupEditor->editStyle() == PropertyEditor::EditStyle::Dialog) {
                    newui::Rect ellipsisRect = ellipsisButtonRectFor(labelRect);
                    paintEllipsisButton(ctx, ellipsisRect, dimTextColor(*this));
                    labelValueRect = newui::Rect(labelRect.left(), labelRect.top(),
                        ellipsisRect.left() - labelRect.left() - 4.0f, labelRect.size().height);
                }
            }
            paintText(ctx, labelValueRect, name + typeSuffix, dimTextColor(*this), newui::SystemUIFont::Status);
            return;
        }

        newui::Rect keyRect = keyRectFor(clientBounds(), path, keyColumnFraction);
        newui::Rect valueRect = valueRectFor(clientBounds(), path, keyColumnFraction);

        // A thin divider line at the shared column split, matching real
        // property grids (VS's own Properties window) - PropertiesGrid's
        // own drag handling (bluesky/property-grid-design.md's "resizable
        // key/value column divider" section) hit-tests against this exact
        // same X (clientBounds().left() + width * keyColumnFraction), so
        // this is the one visible cue for where that grab region is. Leaf
        // rows only - drawing it through group/section header rows too
        // (tried, then reverted per direct user feedback) looked wrong
        // crossing the expand glyph/full-width label; those rows get the
        // alt-panel background fill above as their own visual separation
        // instead.
        double dividerX = clientBounds().left() + double(clientBounds().size().width) * double(keyColumnFraction);
        BLRgba32 borderColor = newui::UIColorManager::colorFor(newui::UIColorRole::ControlBorder).toBLRgba32();
        ctx.save();
        ctx.set_stroke_style(borderColor);
        ctx.set_stroke_width(1.0);
        ctx.stroke_line(BLPoint(dividerX, clientBounds().top()), BLPoint(dividerX, clientBounds().bottom()));
        // Row separator (Main.dc.html's own ".prop-row { border-bottom }").
        ctx.stroke_line(BLPoint(clientBounds().left(), clientBounds().bottom()), BLPoint(clientBounds().right(), clientBounds().bottom()));
        ctx.restore();

        std::string keyText;
        if (node.kind == PropertiesModel::Kind::ParentPicker) {
            keyText = "Parent";
        } else if (node.kind == PropertiesModel::Kind::DelegateEntry) {
            keyText = node.delegate->name();
        } else if (node.kind == PropertiesModel::Kind::SubPropertyEntry) {
            // Filled in below, once the parent compound property's own
            // PropertyEditor (needed for subPropertyNames() anyway) is
            // built - left blank here just avoids a second, unnecessary
            // PropertyEditorRegistry lookup (paintText() below no-ops on
            // an empty string).
        } else if (node.property != nullptr) {
            keyText = node.property->name();
        }
        paintText(ctx, keyRect, keyText, dimTextColor(*this));

        if (node.kind == PropertiesModel::Kind::ParentPicker) {
            // Not backed by a real Property - reads selected_'s own live parent() directly
            // (node.ownerInstance is the selected View itself, same instance the Root node
            // carries). "(root)" for the design surface's own RootViewProxy (a real parent(),
            // just not a SubView so it has no name() of its own worth showing).
            auto* view = static_cast<newui::SubView*>(node.ownerInstance);
            newui::View* parent = view != nullptr ? view->parent() : nullptr;
            auto* parentSub = dynamic_cast<newui::SubView*>(parent);
            std::string label = parentSub != nullptr ? parentSub->name() : (parent != nullptr ? "(root)" : "(none)");
            paintText(ctx, valueRect, label, rowTextColor(*this));
            return;
        }

        if (node.kind == PropertiesModel::Kind::PropertyUnsupported) {
            paintText(ctx, valueRect, "(unsupported)", rowTextColor(*this));
            return;
        }

        if (node.kind == PropertiesModel::Kind::DelegateEntry) {
            std::vector<std::string> listeners = node.delegate->describedListeners(node.ownerInstance);
            std::string joined;
            for (std::size_t i = 0; i < listeners.size(); ++i) {
                if (i > 0) {
                    joined += ", ";
                }
                joined += listeners[i];
            }
            paintText(ctx, valueRect, joined.empty() ? "(no listeners)" : joined, rowTextColor(*this));
            return;
        }

        // Kind::PropertyLeaf/SubPropertyEntry - same registered
        // PropertyEditor (PropertyEditorRegistry, built 2026-09-03)
        // PropertiesModel used to classify this node in the first place -
        // for SubPropertyEntry, node.property/ownerClass/ownerInstance
        // still describe the *parent* compound property (e.g. "bounds"),
        // exactly what its own PropertySubGroup node used too.
        std::unique_ptr<PropertyEditor> editor = PropertyEditorRegistry::instance()
            .createEditor(node.property, node.ownerClass, node.ownerInstance);
        if (editor == nullptr) {
            return;
        }

        // The "..." affordance for a Dialog-style leaf (Color/Gradient/FilePath/...) - see
        // PropertyItem::ellipsisButtonRectFor()'s own comment (PropertyItem.h) for why a plain
        // click on the rest of valueRect no longer opens it. Never true for SubPropertyEntry
        // (editor here is still the *parent* compound property's own editor, e.g. "bounds" -
        // SubProperties editors are never EditStyle::Dialog by construction) or for bool
        // (BoolPropertyEditor is EditStyle::Dropdown), so both those branches below keep using
        // the full, unshrunk valueRect exactly as before.
        if (node.kind != PropertiesModel::Kind::SubPropertyEntry
                && editor->editStyle() == PropertyEditor::EditStyle::Dialog) {
            newui::Rect ellipsisRect = ellipsisButtonRectFor(valueRect);
            paintEllipsisButton(ctx, ellipsisRect, dimTextColor(*this));
            valueRect = newui::Rect(valueRect.left(), valueRect.top(),
                ellipsisRect.left() - valueRect.left() - 4.0f, valueRect.size().height);
        }

        if (node.kind == PropertiesModel::Kind::SubPropertyEntry) {
            std::vector<std::string> subNames = editor->subPropertyNames();
            std::string subName = node.subPropertyIndex < subNames.size() ? subNames[node.subPropertyIndex] : std::string();
            paintText(ctx, keyRect, subName, dimTextColor(*this));
            // Read-only (governed by a Layout other than FreePosition, see Node::readOnly's own
            // comment) shows dimmed, same visual language as a disabled control elsewhere in this
            // codebase - the group header line above already spells out why.
            newui::Color valueColor = node.readOnly ? dimTextColor(*this) : rowTextColor(*this);
            // The editor itself now owns how this sub-property's value looks (a checkbox for a
            // flags-enum bit, plain text for everything else - PropertyEditor::
            // paintSubPropertyValue()/PropertyEditor.h) rather than this method hardcoding an
            // isBool()-then-checkbox-else-text dispatch.
            editor->paintSubPropertyValue(ctx, valueRect, node.subPropertyIndex, valueColor);
            return;
        }

        // The editor itself now owns how its own value looks (BoolPropertyEditor paints a
        // checkbox, ColorPropertyEditor a swatch+text, everything else plain text -
        // PropertyEditor::paintValue()/PropertyEditor.h) rather than this method hardcoding a
        // growing property->type()-based if/else chain.
        editor->paintValue(ctx, valueRect, rowTextColor(*this));
    }

    newui::Rect PropertyItem::keyRectFor(const newui::Rect& rowRect, const std::vector<std::size_t>& path, float keyColumnFraction)
    {
        double indent = double(newui::treeDepthOf(path)) * newui::kTreeIndentWidth;
        double textLeft = rowRect.left() + indent + newui::kTreeGlyphWidth;
        float keyWidth = rowRect.size().width * keyColumnFraction;
        return newui::Rect(float(textLeft), rowRect.top(),
            keyWidth - float(textLeft - rowRect.left()), rowRect.size().height);
    }

    newui::Rect PropertyItem::valueRectFor(const newui::Rect& rowRect, const std::vector<std::size_t>& /*path*/, float keyColumnFraction)
    {
        // Doesn't itself depend on indent - the value column starts at a
        // fixed fraction of the row's own full width regardless of
        // nesting depth (only the key column's left edge shifts with
        // indent, see keyRectFor()) - path kept in the signature purely so
        // both halves of this key/value split share one call shape.
        float keyWidth = rowRect.size().width * keyColumnFraction;
        return newui::Rect(rowRect.left() + keyWidth + kRowPadding, rowRect.top(),
            rowRect.size().width - keyWidth - kRowPadding, rowRect.size().height);
    }

    newui::Rect PropertyItem::ellipsisButtonRectFor(const newui::Rect& contentRect)
    {
        float margin = 3.0f;
        float size = kEllipsisButtonSize;
        return newui::Rect(contentRect.right() - size - margin,
            contentRect.top() + (contentRect.size().height - size) * 0.5f, size, size);
    }
}
