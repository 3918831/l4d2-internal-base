#pragma once

namespace PortalTransitionDecision
{
    bool ShouldCommitPlaneCrossing(
        float previousSignedDepth,
        float currentSignedDepth,
        bool hasPreviousDepth,
        bool insideAperture,
        bool movingIntoPortal);

    float ComputeExitClearancePush(
        float transformedCenterDepth,
        float hullHalfExtentAlongNormal,
        float clearanceEpsilon);
}
