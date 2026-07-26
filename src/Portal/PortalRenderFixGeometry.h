#pragma once

#include <array>
#include <cstddef>

#include "../Util/Math/Math.h"

namespace PortalRenderFixGeometry
{
    inline constexpr float kPi = 3.14159265358979323846f;
    inline constexpr int kAperturePlaneCount = 12;
    inline constexpr std::size_t kMaxPolygonVertices = 64;

    struct BuildInput
    {
        Vector cameraOrigin;
        Vector cameraForward;
        Vector cameraRight;
        Vector cameraUp;
        float zNear = 1.0f;

        Vector portalOrigin;
        Vector portalForward;
        Vector portalRight;
        Vector portalUp;
        float halfWidth = 32.0f;
        float halfHeight = 54.0f;

        float apertureScale = 1.1f;
        float frontClipDistance = 0.3f;
        float nearPlaneBias = 0.05f;
        float nearQuadHalfExtent = 40.0f;
        float clipEpsilon = 0.01f;
    };

    struct Polygon
    {
        std::array<Vector, kMaxPolygonVertices> vertices{};
        std::size_t count = 0;
    };

    // Builds the official-style main-view proxy on camera zNear + 0.05, then
    // clips it against twelve expanded portal-aperture tangent planes and the
    // portal front plane. The output remains in world space for the renderer
    // to project with the engine's active view/projection matrices.
    bool BuildNearPlaneAperturePolygon(const BuildInput& input, Polygon& output);
}
