#include "PortalTransitionDecision.h"

#include <algorithm>
#include <cmath>

bool PortalTransitionDecision::RequiresControlledNoclip(PortalTransitionPhase phase)
{
    return phase == PortalTransitionPhase::IntersectingPortal
        || phase == PortalTransitionPhase::CommittingTeleport
        || phase == PortalTransitionPhase::ExitingPortal;
}

bool PortalTransitionDecision::ShouldApplyLegacyBridgeMovement(PortalTransitionPhase phase)
{
    return phase == PortalTransitionPhase::Idle
        || phase == PortalTransitionPhase::Cooldown;
}

bool PortalTransitionDecision::ShouldRunOriginalPlayerMove(PortalTransitionPhase phase)
{
    return !RequiresControlledNoclip(phase);
}

bool PortalTransitionDecision::ShouldRestoreWalkOnProbeLoss(PortalTransitionPhase phase)
{
    return !RequiresControlledNoclip(phase);
}

bool PortalTransitionDecision::ShouldReleaseExitControlledMove(float frontDistance, bool insideAperture)
{
    constexpr float kGModExitReleaseFrontDistance = 16.0f;
    return frontDistance >= kGModExitReleaseFrontDistance
        || !insideAperture;
}

bool PortalTransitionDecision::ShouldAllowTraversalEntry(
    PortalTransitionPhase phase,
    float currentTime,
    float cooldownUntil)
{
    return phase != PortalTransitionPhase::Cooldown
        || currentTime >= cooldownUntil;
}

bool PortalTransitionDecision::ShouldSyncCommittedMoveBeforeControlledMove(
    PortalTransitionPhase phase,
    float originDeltaSqr)
{
    constexpr float kCommittedMoveSyncDistance = 48.0f;
    return RequiresControlledNoclip(phase)
        && originDeltaSqr >= kCommittedMoveSyncDistance * kCommittedMoveSyncDistance;
}

bool PortalTransitionDecision::ShouldSyncCommittedViewAnglesWithMove(
    PortalTransitionPhase phase,
    bool committedMoveSynced)
{
    return committedMoveSynced
        && RequiresControlledNoclip(phase);
}

bool PortalTransitionDecision::IsWithinGModWallPortalBounds(
    float frontDistance,
    float feetLocalRight,
    float feetLocalUp)
{
    constexpr float kGModWallEnterFrontDistance = 17.0f;
    constexpr float kGModWallHalfWidth = 20.0f;
    constexpr float kGModWallUpperHeight = 44.0f;
    constexpr float kL4D2WallLowerHeight = -60.0f;

    return frontDistance <= kGModWallEnterFrontDistance
        && std::fabs(feetLocalRight) <= kGModWallHalfWidth
        && feetLocalUp <= kGModWallUpperHeight
        && feetLocalUp >= kL4D2WallLowerHeight;
}

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
