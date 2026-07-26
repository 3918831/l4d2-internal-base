#include "PortalRenderFixGeometry.h"

#include <algorithm>
#include <cmath>

namespace
{
    bool IsFiniteVector(const Vector& value)
    {
        return std::isfinite(value.x)
            && std::isfinite(value.y)
            && std::isfinite(value.z);
    }

    bool NormalizeChecked(const Vector& value, Vector& normalized)
    {
        if (!IsFiniteVector(value))
            return false;

        const float lengthSquared = value.LenghtSqr();
        if (!std::isfinite(lengthSquared) || lengthSquared < 0.25f)
            return false;

        normalized = value / std::sqrt(lengthSquared);
        return IsFiniteVector(normalized);
    }

    bool IsFinitePositive(float value)
    {
        return std::isfinite(value) && value > 0.0f;
    }

    bool ClipToPlane(
        const PortalRenderFixGeometry::Polygon& input,
        const Vector& normal,
        float distance,
        float epsilon,
        PortalRenderFixGeometry::Polygon& output)
    {
        output.count = 0;
        if (input.count < 3 || !IsFiniteVector(normal) || !std::isfinite(distance))
            return false;

        for (std::size_t i = 0; i < input.count; ++i)
        {
            const Vector& current = input.vertices[i];
            const Vector& next = input.vertices[(i + 1) % input.count];
            const float currentDistance = current.Dot(normal) - distance;
            const float nextDistance = next.Dot(normal) - distance;
            const bool currentInside = currentDistance >= -epsilon;
            const bool nextInside = nextDistance >= -epsilon;

            if (currentInside)
            {
                if (output.count >= output.vertices.size())
                    return false;
                output.vertices[output.count++] = current;
            }

            if (currentInside == nextInside)
                continue;

            const float denominator = currentDistance - nextDistance;
            if (!std::isfinite(denominator) || std::fabs(denominator) < 1.0e-6f)
                continue;

            const float fraction = currentDistance / denominator;
            const Vector intersection = current + (next - current) * fraction;
            if (!IsFiniteVector(intersection) || output.count >= output.vertices.size())
                return false;

            output.vertices[output.count++] = intersection;
        }

        return output.count >= 3;
    }
}

bool PortalRenderFixGeometry::BuildNearPlaneAperturePolygon(
    const BuildInput& input,
    Polygon& output)
{
    output.count = 0;

    if (!IsFiniteVector(input.cameraOrigin)
        || !IsFiniteVector(input.portalOrigin)
        || !IsFinitePositive(input.zNear)
        || !IsFinitePositive(input.halfWidth)
        || !IsFinitePositive(input.halfHeight)
        || !IsFinitePositive(input.apertureScale)
        || !IsFinitePositive(input.nearQuadHalfExtent)
        || !std::isfinite(input.frontClipDistance)
        || input.frontClipDistance < 0.0f
        || !std::isfinite(input.nearPlaneBias)
        || input.nearPlaneBias < 0.0f
        || !std::isfinite(input.clipEpsilon)
        || input.clipEpsilon < 0.0f)
    {
        return false;
    }

    Vector cameraForward;
    Vector cameraRight;
    Vector cameraUp;
    Vector portalForward;
    Vector portalRight;
    Vector portalUp;
    if (!NormalizeChecked(input.cameraForward, cameraForward)
        || !NormalizeChecked(input.cameraRight, cameraRight)
        || !NormalizeChecked(input.cameraUp, cameraUp)
        || !NormalizeChecked(input.portalForward, portalForward)
        || !NormalizeChecked(input.portalRight, portalRight)
        || !NormalizeChecked(input.portalUp, portalUp))
    {
        return false;
    }

    const Vector portalToCamera = input.cameraOrigin - input.portalOrigin;
    const float eyeDepth = portalToCamera.Dot(portalForward);
    if (!std::isfinite(eyeDepth)
        || eyeDepth < -1.0f
        || portalToCamera.LenghtSqr() >= input.halfHeight * input.halfHeight)
    {
        return false;
    }

    const Vector nearOrigin =
        input.cameraOrigin + cameraForward * (input.zNear + input.nearPlaneBias);
    const Vector horizontal = cameraRight * input.nearQuadHalfExtent;
    const Vector vertical = cameraUp * input.nearQuadHalfExtent;

    Polygon current;
    current.count = 4;
    current.vertices[0] = nearOrigin + horizontal - vertical;
    current.vertices[1] = nearOrigin + horizontal + vertical;
    current.vertices[2] = nearOrigin - horizontal + vertical;
    current.vertices[3] = nearOrigin - horizontal - vertical;

    Polygon clipped;
    for (int i = 0; i < kAperturePlaneCount; ++i)
    {
        const float circlePosition =
            static_cast<float>(i) * (2.0f * kPi / static_cast<float>(kAperturePlaneCount));
        const float upBlend = std::cos(circlePosition);
        const float rightBlend = std::sin(circlePosition);
        const Vector normal =
            portalUp * -upBlend + portalRight * -rightBlend;
        const Vector pointOnPlane =
            input.portalOrigin
            + portalUp * (upBlend * input.halfHeight * input.apertureScale)
            + portalRight * (rightBlend * input.halfWidth * input.apertureScale);

        if (!ClipToPlane(
                current,
                normal,
                normal.Dot(pointOnPlane),
                input.clipEpsilon,
                clipped))
        {
            output.count = 0;
            return false;
        }

        current = clipped;
    }

    const float effectiveFrontClip =
        std::min(input.frontClipDistance, std::max(0.0f, eyeDepth));
    const Vector frontNormal = portalForward * -1.0f;
    const Vector frontPoint =
        input.portalOrigin + portalForward * effectiveFrontClip;
    if (!ClipToPlane(
            current,
            frontNormal,
            frontNormal.Dot(frontPoint),
            input.clipEpsilon,
            clipped))
    {
        output.count = 0;
        return false;
    }

    output = clipped;
    return output.count >= 3;
}
