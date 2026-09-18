#include "CalloutPlacement.h"

#include <newui/rootview.h>

namespace CodeToolsVsix
{
    namespace
    {
        constexpr float kCalloutGap = 6.0f;  // clearance between the anchor and the popup's own edge

        // Kept off the rounded corners (CalloutRoundRect::tailPosition()'s own doc comment - "not
        // clamped - a value outside 0-1 places the tip past the corresponding rounded corner,
        // which buildPath() doesn't account for"), not just off 0/1 exactly.
        float clampedTailFraction(float rawFraction)
        {
            return rawFraction < 0.12f ? 0.12f : rawFraction > 0.88f ? 0.88f : rawFraction;
        }
    }

    CalloutPlacement placeCallout(const newui::Rect& anchorScreenRect, const newui::Size& popupSize,
        const newui::Rect& containerScreenRect)
    {
        auto clampToRange = [](float value, float lo, float hi) {
            return hi < lo ? lo : value < lo ? lo : value > hi ? hi : value;
        };
        float anchorCenterX = anchorScreenRect.left() + anchorScreenRect.width() * 0.5f;
        float anchorCenterY = anchorScreenRect.top() + anchorScreenRect.height() * 0.5f;

        // Below/above anchorScreenRect, horizontally centered on it (cross axis) then clamped
        // into containerScreenRect - tailOnTop selects which of the two (a popup below has its
        // own tail on its top edge, pointing up at the anchor, and vice versa).
        auto vertical = [&](bool tailOnTop) {
            float top = tailOnTop ? anchorScreenRect.bottom() + kCalloutGap
                                   : anchorScreenRect.top() - kCalloutGap - popupSize.height;
            float left = clampToRange(anchorCenterX - popupSize.width * 0.5f,
                containerScreenRect.left(), containerScreenRect.right() - popupSize.width);
            CalloutPlacement p;
            p.bounds = newui::Rect(left, top, popupSize.width, popupSize.height);
            p.tailSide = tailOnTop ? newui::shapes::TailSide::Top : newui::shapes::TailSide::Bottom;
            p.tailPosition = clampedTailFraction(popupSize.width > 0.0f ? (anchorCenterX - left) / popupSize.width : 0.5f);
            return p;
        };
        // Right/left of anchorScreenRect, vertically centered (cross axis) then clamped -
        // tailOnLeft selects which of the two (a popup to the right has its own tail on its left
        // edge, pointing left at the anchor, and vice versa).
        auto horizontal = [&](bool tailOnLeft) {
            float left = tailOnLeft ? anchorScreenRect.right() + kCalloutGap
                                     : anchorScreenRect.left() - kCalloutGap - popupSize.width;
            float top = clampToRange(anchorCenterY - popupSize.height * 0.5f,
                containerScreenRect.top(), containerScreenRect.bottom() - popupSize.height);
            CalloutPlacement p;
            p.bounds = newui::Rect(left, top, popupSize.width, popupSize.height);
            p.tailSide = tailOnLeft ? newui::shapes::TailSide::Left : newui::shapes::TailSide::Right;
            p.tailPosition = clampedTailFraction(popupSize.height > 0.0f ? (anchorCenterY - top) / popupSize.height : 0.5f);
            return p;
        };

        bool fitsBelow = anchorScreenRect.bottom() + kCalloutGap + popupSize.height <= containerScreenRect.bottom();
        bool fitsAbove = anchorScreenRect.top() - kCalloutGap - popupSize.height >= containerScreenRect.top();
        bool fitsRight = anchorScreenRect.right() + kCalloutGap + popupSize.width <= containerScreenRect.right();
        bool fitsLeft = anchorScreenRect.left() - kCalloutGap - popupSize.width >= containerScreenRect.left();

        if (fitsBelow) return vertical(true);
        if (fitsAbove) return vertical(false);
        if (fitsRight) return horizontal(true);
        if (fitsLeft) return horizontal(false);
        return vertical(true);
    }

    newui::Rect designerWindowScreenRect(newui::View* owner)
    {
        newui::RootView* root = owner != nullptr ? owner->rootView() : nullptr;
        if (root == nullptr) {
            return newui::Rect();
        }
        return root->screenBounds();
    }
}
