#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace CodeToolsVsix
{
    // What the Designer can show under its toolbar. Designer is the canvas and its docks; the rest each replace
    // the whole area. `available` is false for one not built yet - it is listed, greyed, so the list says what is coming.
    enum class DesignerSurface
    {
        Designer = 0,
        FrameMap,
        Resources,
        Fonts,
        Templates,
        Animations,
        Dependencies,
        DataFlow,
    };

    struct DesignerSurfaceInfo
    {
        DesignerSurface id;
        const char* name;
        bool available;
    };

    // In menu order.
    const std::vector<DesignerSurfaceInfo>& designerSurfaces();
    const DesignerSurfaceInfo& designerSurfaceInfo(DesignerSurface surface);

    // What the switcher button shows: "codetools++ - Fonts".
    std::string designerSurfaceTitle(DesignerSurface surface);
}
