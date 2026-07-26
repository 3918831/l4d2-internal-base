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

enum class PortalExitClearanceMode
{
    FullHull,
    PlaneEpsilon,
    ExactTransform,
};

namespace PortalTransitionDecision
{
    bool RequiresControlledNoclip(PortalTransitionPhase phase);
    bool ShouldApplyLegacyBridgeMovement(PortalTransitionPhase phase);
    bool ShouldRunOriginalPlayerMove(PortalTransitionPhase phase);
    bool ShouldReplaceOriginalPlayerMove(PortalTransitionPhase phase, bool movementMutationAllowed);
    bool ShouldRestoreWalkOnProbeLoss(PortalTransitionPhase phase);
    bool ShouldReleaseExitControlledMove(float frontDistance, bool insideAperture);
    bool ShouldAllowTraversalEntry(PortalTransitionPhase phase, float currentTime, float cooldownUntil);
    float ComputeExitRearmCooldownUntil(float releaseTime, float cooldownDuration);
    bool IsCommittedTeleportPredictionSyncPhase(PortalTransitionPhase phase);
    bool ShouldSyncCommittedMoveBeforeControlledMove(PortalTransitionPhase phase, float originDeltaSqr);
    bool ShouldSyncCommittedViewAnglesWithMove(PortalTransitionPhase phase, bool committedMoveSynced);
    bool IsWithinGModWallPortalBounds(float frontDistance, float feetLocalRight, float feetLocalUp);
    bool IsViewNearPortalPlane(
        float forwardDistance,
        float localRight,
        float localUp,
        float maxDepth,
        float maxRight,
        float maxUp);

    bool GetPortalNearClipFixEnabled();
    void SetPortalNearClipFixEnabled(bool enabled);
    float ComputePortalAwareNearClip(
        bool enabled,
        bool portalContextValid,
        float signedDepth,
        float currentNearClip,
        float activationDepth,
        float targetNearClip);
    float ComputePortalAwareNearClipForPhase(
        PortalTransitionPhase phase,
        bool enabled,
        bool portalContextValid,
        float signedDepth,
        float currentNearClip,
        float activationDepth,
        float targetNearClip);

    bool GetExactExitVisualGuardEnabled();
    void SetExactExitVisualGuardEnabled(bool enabled);
    bool ShouldUseExactExitVisualGuard(
        bool enabled,
        PortalTransitionPhase phase,
        PortalExitClearanceMode clearanceMode,
        bool isMainView,
        bool portalContextValid,
        float signedDepth,
        float activationDepth);

    bool GetEntryCameraHandoffEnabled();
    void SetEntryCameraHandoffEnabled(bool enabled);
    bool ShouldApplyEntryCameraHandoff(
        bool enabled,
        PortalTransitionPhase phase,
        bool isLocalPlayer,
        bool carvingActive,
        bool portalsReady,
        bool insideAperture,
        float signedDepth);

    float ComputeOfficialPortalRemoteClipPlaneDistance(float portalPlaneDistance);
    float ComputeOfficialPortalRemoteViewNormalPush();

    PortalExitClearanceMode GetExitClearanceMode();
    void SetExitClearanceMode(PortalExitClearanceMode mode);
    const char* ExitClearanceModeName(PortalExitClearanceMode mode);
    bool TryParseExitClearanceMode(const char* text, PortalExitClearanceMode* mode);

    bool ShouldCommitPlaneCrossing(
        float previousSignedDepth,
        float currentSignedDepth,
        bool hasPreviousDepth,
        bool insideAperture,
        bool movingIntoPortal);

    bool TryComputePortalPlaneCrossingFraction(
        float previousSignedDepth,
        float currentSignedDepth,
        float* crossingFraction);

    float ComputePredictedPortalCommitLeadTime(
        float currentSignedDepth,
        float velocityAlongNormal,
        float interval,
        float targetSignedDepth);

    bool IsPortalCommitHalfSpaceValid(
        float entrySignedDepth,
        float transformedExitSignedDepth);

    float ComputeExitClearancePush(
        float transformedCenterDepth,
        float hullHalfExtentAlongNormal,
        float clearanceEpsilon);

    float ComputeExitClearancePush(
        PortalExitClearanceMode mode,
        float transformedCenterDepth,
        float hullHalfExtentAlongNormal,
        float clearanceEpsilon);
}
