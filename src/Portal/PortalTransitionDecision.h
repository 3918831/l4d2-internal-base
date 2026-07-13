#pragma once

enum class PortalTransitionPhase
{
    Idle,
    ApproachingPortal,
    IntersectingPortal,
    CommittingTeleport,
    ExitingPortal,
    Cooldown,
};

namespace PortalTransitionDecision
{
    bool RequiresControlledNoclip(PortalTransitionPhase phase);
    bool ShouldApplyLegacyBridgeMovement(PortalTransitionPhase phase);
    bool ShouldRunOriginalPlayerMove(PortalTransitionPhase phase);
    bool ShouldRestoreWalkOnProbeLoss(PortalTransitionPhase phase);
    bool ShouldReleaseExitControlledMove(float frontDistance, bool insideAperture);
    bool ShouldAllowTraversalEntry(PortalTransitionPhase phase, float currentTime, float cooldownUntil);
    bool ShouldSyncCommittedMoveBeforeControlledMove(PortalTransitionPhase phase, float originDeltaSqr);
    bool ShouldSyncCommittedViewAnglesWithMove(PortalTransitionPhase phase, bool committedMoveSynced);
    bool IsWithinGModWallPortalBounds(float frontDistance, float feetLocalRight, float feetLocalUp);

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
