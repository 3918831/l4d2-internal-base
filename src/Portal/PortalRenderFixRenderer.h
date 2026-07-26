#pragma once

#include <cstddef>

#include "../Util/Math/Math.h"

class IMaterial;
class IMatRenderContext;

namespace PortalRenderFixRenderer
{
    struct DrawResult
    {
        bool applied = false;
        std::size_t vertexCount = 0;
        float eyeDepth = 0.0f;
    };

    DrawResult DrawMainViewStencilProxy(
        IMatRenderContext* renderContext,
        IMaterial* stencilMaterial,
        const CViewSetup& view,
        const Vector& portalOrigin,
        const QAngle& portalAngles,
        float portalScale,
        const char* portalName);
}
