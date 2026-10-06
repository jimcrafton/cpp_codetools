#include "DesignerSurface.h"

namespace CodeToolsVsix
{
    const std::vector<DesignerSurfaceInfo>& designerSurfaces()
    {
        static const std::vector<DesignerSurfaceInfo> all = {
            { DesignerSurface::Designer, "View Designer", true },
            { DesignerSurface::FrameMap, "Frame Map", false },
            { DesignerSurface::Resources, "Resources", false },
            { DesignerSurface::Fonts, "Fonts", true },
            { DesignerSurface::Templates, "Templates", false },
            { DesignerSurface::Animations, "Animations", false },
            { DesignerSurface::Dependencies, "Dependencies", false },
            { DesignerSurface::DataFlow, "Data Flow", false },
        };
        return all;
    }

    const DesignerSurfaceInfo& designerSurfaceInfo(DesignerSurface surface)
    {
        return designerSurfaces()[static_cast<std::size_t>(surface)];
    }

    std::string designerSurfaceTitle(DesignerSurface surface)
    {
        return std::string("codetools++ - ") + designerSurfaceInfo(surface).name;
    }
}
