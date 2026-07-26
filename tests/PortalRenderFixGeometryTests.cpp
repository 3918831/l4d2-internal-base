#include <cmath>
#include <cstdlib>
#include <iostream>

#include "../src/Portal/PortalRenderFixGeometry.h"

namespace
{
    void Expect(bool condition, const char* message)
    {
        if (condition)
            return;

        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }

    void ExpectNear(float actual, float expected, const char* message)
    {
        Expect(std::fabs(actual - expected) < 0.001f, message);
    }

    PortalRenderFixGeometry::BuildInput MakeHeadOnInput(float portalDistance)
    {
        PortalRenderFixGeometry::BuildInput input;
        input.cameraOrigin = Vector(0.0f, 0.0f, 0.0f);
        input.cameraForward = Vector(1.0f, 0.0f, 0.0f);
        input.cameraRight = Vector(0.0f, 1.0f, 0.0f);
        input.cameraUp = Vector(0.0f, 0.0f, 1.0f);
        input.zNear = 1.0f;
        input.portalOrigin = Vector(portalDistance, 0.0f, 0.0f);
        input.portalForward = Vector(-1.0f, 0.0f, 0.0f);
        input.portalRight = Vector(0.0f, 1.0f, 0.0f);
        input.portalUp = Vector(0.0f, 0.0f, 1.0f);
        input.halfWidth = 32.0f;
        input.halfHeight = 54.0f;
        return input;
    }
}

int main()
{
    using namespace PortalRenderFixGeometry;

    {
        const BuildInput input = MakeHeadOnInput(0.634f);
        PortalRenderFixGeometry::Polygon polygon;
        Expect(BuildNearPlaneAperturePolygon(input, polygon),
            "a portal model closer than zNear must receive a render-fix polygon");
        Expect(polygon.count >= 3,
            "the render-fix polygon must remain drawable after portal clipping");

        for (std::size_t i = 0; i < polygon.count; ++i)
        {
            const Vector& point = polygon.vertices[i];
            ExpectNear(point.x, 1.05f,
                "every proxy vertex must lie on camera zNear plus the official bias");

            const Vector local = point - input.portalOrigin;
            const float right = local.Dot(input.portalRight);
            const float up = local.Dot(input.portalUp);
            const float forward = local.Dot(input.portalForward);
            Expect(forward <= input.frontClipDistance + 0.011f,
                "proxy vertices must not extend beyond the official front clip plane");

            for (int plane = 0; plane < kAperturePlaneCount; ++plane)
            {
                const float angle =
                    static_cast<float>(plane) * (2.0f * kPi / static_cast<float>(kAperturePlaneCount));
                const float rightBlend = std::sin(angle);
                const float upBlend = std::cos(angle);
                const float tangent = rightBlend * right + upBlend * up;
                const float officialBoundary =
                    input.apertureScale
                    * (rightBlend * rightBlend * input.halfWidth
                        + upBlend * upBlend * input.halfHeight);
                Expect(tangent <= officialBoundary + 0.011f,
                    "proxy vertices must stay inside every expanded aperture tangent plane");
            }
        }
    }

    {
        const BuildInput input = MakeHeadOnInput(2.0f);
        PortalRenderFixGeometry::Polygon polygon;
        Expect(!BuildNearPlaneAperturePolygon(input, polygon),
            "a portal beyond the camera near plane must not create an occluding proxy");
        Expect(polygon.count == 0,
            "an eliminated proxy must report zero vertices");
    }

    {
        BuildInput input = MakeHeadOnInput(0.634f);
        input.cameraForward = Vector();
        PortalRenderFixGeometry::Polygon polygon;
        Expect(!BuildNearPlaneAperturePolygon(input, polygon),
            "invalid camera basis vectors must fail closed");
    }

    std::cout << "Portal render-fix geometry tests passed\n";
    return 0;
}
