#include "PortalTransform.h"

#include <cmath>

#pragma warning(push)
#pragma warning(disable: 4819)
#include "L4D2_Portal.h"
#pragma warning(pop)

namespace
{
    bool IsFiniteVector(const Vector& value)
    {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    }

    bool IsFiniteAngles(const QAngle& value)
    {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    }

    Vector PortalLocalToWorld(const PortalInfo_t& portal, const PortalTransform::PortalLocalPoint& local)
    {
        Vector forward;
        Vector right;
        Vector up;
        U::Math.AngleVectors(portal.angles, &forward, &right, &up);

        return portal.origin
            + forward * local.forward
            + right * local.right
            + up * local.up;
    }
}

bool PortalTransform::BuildPortalMatrix(const PortalInfo_t& portal, matrix3x4_t& out)
{
    if (!IsFiniteVector(portal.origin) || !IsFiniteAngles(portal.angles))
        return false;

    U::Math.AngleMatrix(portal.angles, portal.origin, out);
    return true;
}

bool PortalTransform::BuildEntryToExitMatrix(const PortalInfo_t& entry, const PortalInfo_t& exit, matrix3x4_t& out)
{
    matrix3x4_t entryWorld;
    matrix3x4_t exitWorld;

    if (!BuildPortalMatrix(entry, entryWorld) || !BuildPortalMatrix(exit, exitWorld))
        return false;

    matrix3x4_t entryWorldInverse;
    if (!U::Math.MatrixInverse(entryWorld, entryWorldInverse))
        return false;

    matrix3x4_t rotate180;
    U::Math.SetIdentityMatrix(rotate180);
    rotate180[0][0] = -1.0f;
    rotate180[1][1] = -1.0f;

    matrix3x4_t rotatedEntryInverse;
    U::Math.ConcatTransforms(rotate180, entryWorldInverse, rotatedEntryInverse);
    U::Math.ConcatTransforms(exitWorld, rotatedEntryInverse, out);
    return true;
}

Vector PortalTransform::TransformPoint(const matrix3x4_t& matrix, const Vector& point)
{
    Vector transformed;
    U::Math.VectorTransform(point, matrix, transformed);
    return transformed;
}

Vector PortalTransform::TransformVector(const matrix3x4_t& matrix, const Vector& vector)
{
    return Vector(
        matrix[0][0] * vector.x + matrix[0][1] * vector.y + matrix[0][2] * vector.z,
        matrix[1][0] * vector.x + matrix[1][1] * vector.y + matrix[1][2] * vector.z,
        matrix[2][0] * vector.x + matrix[2][1] * vector.y + matrix[2][2] * vector.z);
}

QAngle PortalTransform::TransformAngles(const matrix3x4_t& matrix, const QAngle& angles)
{
    matrix3x4_t anglesMatrix;
    matrix3x4_t transformedMatrix;
    QAngle transformed;

    U::Math.AngleMatrix(angles, anglesMatrix);
    U::Math.ConcatTransforms(matrix, anglesMatrix, transformedMatrix);
    U::Math.MatrixAngles(transformedMatrix, transformed);

    if (!IsFiniteAngles(transformed))
    {
        transformed.x = 0.0f;
        transformed.y = 0.0f;
        transformed.z = 0.0f;
    }

    return transformed;
}

Vector PortalTransform::ComputeGModPortalHeadPosition(const PortalInfo_t& entry, const PortalInfo_t& exit, const Vector& headPosition)
{
    PortalLocalPoint local = WorldToPortalLocal(entry, headPosition);
    local.forward = -local.forward;
    local.right = -local.right;
    return PortalLocalToWorld(exit, local);
}

Vector PortalTransform::ComputeOriginForGModHeadPosition(const Vector& gmodHeadPosition, const Vector& eyeFromOrigin)
{
    return gmodHeadPosition - eyeFromOrigin;
}

PortalTransform::PortalLocalPoint PortalTransform::WorldToPortalLocal(const PortalInfo_t& portal, const Vector& point)
{
    Vector forward;
    Vector right;
    Vector up;
    U::Math.AngleVectors(portal.angles, &forward, &right, &up);

    const Vector delta = point - portal.origin;

    PortalLocalPoint local;
    local.forward = delta.Dot(forward);
    local.right = delta.Dot(right);
    local.up = delta.Dot(up);
    return local;
}

bool PortalTransform::IsPointInsideAperture(const PortalInfo_t& portal, const Vector& point, const PortalAperture& aperture)
{
    const PortalLocalPoint local = WorldToPortalLocal(portal, point);
    return std::fabs(local.right) <= aperture.halfWidth + aperture.tolerance
        && std::fabs(local.up) <= aperture.halfHeight + aperture.tolerance;
}
