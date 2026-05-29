#pragma once

#include "../Util/Math/Math.h"

struct PortalInfo_t;

namespace PortalTransform
{
    struct PortalLocalPoint
    {
        float forward = 0.0f;
        float right = 0.0f;
        float up = 0.0f;
    };

    struct PortalAperture
    {
        float halfWidth = 32.0f;
        float halfHeight = 56.0f;
        float tolerance = 4.0f;
    };

    bool BuildPortalMatrix(const PortalInfo_t& portal, matrix3x4_t& out);
    bool BuildEntryToExitMatrix(const PortalInfo_t& entry, const PortalInfo_t& exit, matrix3x4_t& out);

    Vector TransformPoint(const matrix3x4_t& matrix, const Vector& point);
    Vector TransformVector(const matrix3x4_t& matrix, const Vector& vector);
    QAngle TransformAngles(const matrix3x4_t& matrix, const QAngle& angles);

    PortalLocalPoint WorldToPortalLocal(const PortalInfo_t& portal, const Vector& point);
    bool IsPointInsideAperture(const PortalInfo_t& portal, const Vector& point, const PortalAperture& aperture = {});
}
