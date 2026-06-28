#include "PortalTransitionDecision.h"

#include <algorithm>

bool PortalTransitionDecision::ShouldCommitPlaneCrossing(
    float previousSignedDepth,
    float currentSignedDepth,
    bool hasPreviousDepth,
    bool insideAperture,
    bool movingIntoPortal)
{
    return hasPreviousDepth
        && insideAperture
        && movingIntoPortal
        && previousSignedDepth >= 0.0f
        && currentSignedDepth < 0.0f;
}

float PortalTransitionDecision::ComputeExitClearancePush(
    float transformedCenterDepth,
    float hullHalfExtentAlongNormal,
    float clearanceEpsilon)
{
    const float requiredCenterDepth = std::max(0.0f, hullHalfExtentAlongNormal)
        + std::max(0.0f, clearanceEpsilon);
    return std::max(0.0f, requiredCenterDepth - transformedCenterDepth);
}
