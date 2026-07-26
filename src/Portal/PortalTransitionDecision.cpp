#include "PortalTransitionDecision.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
    PortalExitClearanceMode g_exitClearanceMode = PortalExitClearanceMode::FullHull;
    bool g_portalNearClipFixEnabled = true;
    bool g_exactExitVisualGuardEnabled = true;
    bool g_entryCameraHandoffEnabled = true;
}

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

bool PortalTransitionDecision::ShouldReplaceOriginalPlayerMove(
    PortalTransitionPhase phase,
    bool movementMutationAllowed)
{
    return movementMutationAllowed && RequiresControlledNoclip(phase);
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
    // ExitingPortal is the spatial rearm lock. It remains authoritative even
    // if the timestamp armed at commit has elapsed while the player is still
    // overlapping the exit portal.
    if (phase == PortalTransitionPhase::CommittingTeleport
        || phase == PortalTransitionPhase::ExitingPortal)
    {
        return false;
    }

    return phase != PortalTransitionPhase::Cooldown
        || currentTime >= cooldownUntil;
}

float PortalTransitionDecision::ComputeExitRearmCooldownUntil(
    float releaseTime,
    float cooldownDuration)
{
    return releaseTime + std::max(0.0f, cooldownDuration);
}

bool PortalTransitionDecision::IsCommittedTeleportPredictionSyncPhase(PortalTransitionPhase phase)
{
    return phase == PortalTransitionPhase::ExitingPortal
        || phase == PortalTransitionPhase::Cooldown;
}

bool PortalTransitionDecision::ShouldSyncCommittedMoveBeforeControlledMove(
    PortalTransitionPhase phase,
    float originDeltaSqr)
{
    constexpr float kCommittedMoveSyncDistance = 48.0f;
    return IsCommittedTeleportPredictionSyncPhase(phase)
        && originDeltaSqr >= kCommittedMoveSyncDistance * kCommittedMoveSyncDistance;
}

bool PortalTransitionDecision::ShouldSyncCommittedViewAnglesWithMove(
    PortalTransitionPhase phase,
    bool committedMoveSynced)
{
    return committedMoveSynced
        && IsCommittedTeleportPredictionSyncPhase(phase);
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

bool PortalTransitionDecision::IsViewNearPortalPlane(
    float forwardDistance,
    float localRight,
    float localUp,
    float maxDepth,
    float maxRight,
    float maxUp)
{
    return std::fabs(forwardDistance) <= std::max(0.0f, maxDepth)
        && std::fabs(localRight) <= std::max(0.0f, maxRight)
        && std::fabs(localUp) <= std::max(0.0f, maxUp);
}

bool PortalTransitionDecision::GetPortalNearClipFixEnabled()
{
    return g_portalNearClipFixEnabled;
}

void PortalTransitionDecision::SetPortalNearClipFixEnabled(bool enabled)
{
    g_portalNearClipFixEnabled = enabled;
}

float PortalTransitionDecision::ComputePortalAwareNearClip(
    bool enabled,
    bool portalContextValid,
    float signedDepth,
    float currentNearClip,
    float activationDepth,
    float targetNearClip)
{
    if (!enabled
        || !portalContextValid
        || !std::isfinite(signedDepth)
        || !std::isfinite(currentNearClip)
        || !std::isfinite(activationDepth)
        || !std::isfinite(targetNearClip)
        || signedDepth < 0.0f
        || signedDepth > std::max(0.0f, activationDepth))
    {
        return currentNearClip;
    }

    return std::min(currentNearClip, std::max(0.1f, targetNearClip));
}

float PortalTransitionDecision::ComputePortalAwareNearClipForPhase(
    PortalTransitionPhase phase,
    bool enabled,
    bool portalContextValid,
    float signedDepth,
    float currentNearClip,
    float activationDepth,
    float targetNearClip)
{
    const bool isExitHandoff = phase == PortalTransitionPhase::ExitingPortal
        || phase == PortalTransitionPhase::Cooldown;
    if (!isExitHandoff)
        return currentNearClip;

    if (!enabled
        || !portalContextValid
        || !std::isfinite(signedDepth)
        || !std::isfinite(currentNearClip)
        || !std::isfinite(activationDepth)
        || !std::isfinite(targetNearClip)
        || std::fabs(signedDepth) > std::max(0.0f, activationDepth))
    {
        return currentNearClip;
    }

    return std::min(currentNearClip, std::max(0.1f, targetNearClip));
}

bool PortalTransitionDecision::GetExactExitVisualGuardEnabled()
{
    return g_exactExitVisualGuardEnabled;
}

void PortalTransitionDecision::SetExactExitVisualGuardEnabled(bool enabled)
{
    g_exactExitVisualGuardEnabled = enabled;
}

bool PortalTransitionDecision::ShouldUseExactExitVisualGuard(
    bool enabled,
    PortalTransitionPhase phase,
    PortalExitClearanceMode clearanceMode,
    bool isMainView,
    bool portalContextValid,
    float signedDepth,
    float activationDepth)
{
    const bool isExitHandoff = phase == PortalTransitionPhase::ExitingPortal
        || phase == PortalTransitionPhase::Cooldown;
    return enabled
        && isExitHandoff
        && clearanceMode == PortalExitClearanceMode::ExactTransform
        && isMainView
        && portalContextValid
        && std::isfinite(signedDepth)
        && std::isfinite(activationDepth)
        && std::fabs(signedDepth) <= std::max(0.0f, activationDepth);
}

bool PortalTransitionDecision::GetEntryCameraHandoffEnabled()
{
    return g_entryCameraHandoffEnabled;
}

void PortalTransitionDecision::SetEntryCameraHandoffEnabled(bool enabled)
{
    g_entryCameraHandoffEnabled = enabled;
}

bool PortalTransitionDecision::ShouldApplyEntryCameraHandoff(
    bool enabled,
    PortalTransitionPhase phase,
    bool isLocalPlayer,
    bool carvingActive,
    bool portalsReady,
    bool insideAperture,
    float signedDepth)
{
    return enabled
        && phase == PortalTransitionPhase::IntersectingPortal
        && isLocalPlayer
        && carvingActive
        && portalsReady
        && insideAperture
        && std::isfinite(signedDepth)
        && signedDepth < 0.0f;
}

float PortalTransitionDecision::ComputeOfficialPortalRemoteClipPlaneDistance(
    float portalPlaneDistance)
{
    // Portal SDK 2013 places the remote clip plane half a unit behind the
    // linked portal so half-in objects remain visible without exposing the
    // carrying wall.
    constexpr float kOfficialRemoteClipOffset = -0.5f;
    return portalPlaneDistance + kOfficialRemoteClipOffset;
}

float PortalTransitionDecision::ComputeOfficialPortalRemoteViewNormalPush()
{
    // Keep the RTT camera at the exact entry-to-exit transform. Visibility
    // recovery is handled by safe PVS origins, not by moving the camera.
    return 0.0f;
}

PortalExitClearanceMode PortalTransitionDecision::GetExitClearanceMode()
{
    return g_exitClearanceMode;
}

void PortalTransitionDecision::SetExitClearanceMode(PortalExitClearanceMode mode)
{
    g_exitClearanceMode = mode;
}

const char* PortalTransitionDecision::ExitClearanceModeName(PortalExitClearanceMode mode)
{
    switch (mode)
    {
    case PortalExitClearanceMode::ExactTransform:
        return "ExactTransform";
    case PortalExitClearanceMode::PlaneEpsilon:
        return "PlaneEpsilon";
    case PortalExitClearanceMode::FullHull:
    default:
        return "FullHull";
    }
}

bool PortalTransitionDecision::TryParseExitClearanceMode(
    const char* text,
    PortalExitClearanceMode* mode)
{
    if (!text || !mode)
        return false;

    if (std::strcmp(text, "full") == 0
        || std::strcmp(text, "FullHull") == 0
        || std::strcmp(text, "0") == 0)
    {
        *mode = PortalExitClearanceMode::FullHull;
        return true;
    }

    if (std::strcmp(text, "plane") == 0
        || std::strcmp(text, "PlaneEpsilon") == 0
        || std::strcmp(text, "1") == 0)
    {
        *mode = PortalExitClearanceMode::PlaneEpsilon;
        return true;
    }

    if (std::strcmp(text, "exact") == 0
        || std::strcmp(text, "ExactTransform") == 0
        || std::strcmp(text, "2") == 0)
    {
        *mode = PortalExitClearanceMode::ExactTransform;
        return true;
    }

    return false;
}

bool PortalTransitionDecision::ShouldCommitPlaneCrossing(
    float previousSignedDepth,
    float currentSignedDepth,
    bool hasPreviousDepth,
    bool insideAperture,
    bool movingIntoPortal)
{
    return insideAperture
        && movingIntoPortal
        && currentSignedDepth < 0.0f
        && (!hasPreviousDepth || previousSignedDepth >= 0.0f);
}

bool PortalTransitionDecision::TryComputePortalPlaneCrossingFraction(
    float previousSignedDepth,
    float currentSignedDepth,
    float* crossingFraction)
{
    if (!crossingFraction
        || !std::isfinite(previousSignedDepth)
        || !std::isfinite(currentSignedDepth)
        || previousSignedDepth < 0.0f
        || currentSignedDepth >= 0.0f)
    {
        return false;
    }

    const float depthDelta = previousSignedDepth - currentSignedDepth;
    if (depthDelta <= 0.0f)
        return false;

    const float fraction = previousSignedDepth / depthDelta;
    if (!std::isfinite(fraction) || fraction < 0.0f || fraction > 1.0f)
        return false;

    *crossingFraction = fraction;
    return true;
}

float PortalTransitionDecision::ComputePredictedPortalCommitLeadTime(
    float currentSignedDepth,
    float velocityAlongNormal,
    float interval,
    float targetSignedDepth)
{
    if (!std::isfinite(currentSignedDepth)
        || !std::isfinite(velocityAlongNormal)
        || !std::isfinite(interval)
        || !std::isfinite(targetSignedDepth)
        || velocityAlongNormal >= 0.0f
        || interval <= 0.0f)
    {
        return 0.0f;
    }

    const float leadTime = (currentSignedDepth - targetSignedDepth) / -velocityAlongNormal;
    return std::min(interval, std::max(0.0f, leadTime));
}

bool PortalTransitionDecision::IsPortalCommitHalfSpaceValid(
    float entrySignedDepth,
    float transformedExitSignedDepth)
{
    return std::isfinite(entrySignedDepth)
        && std::isfinite(transformedExitSignedDepth)
        && entrySignedDepth < 0.0f
        && transformedExitSignedDepth >= 0.0f;
}

float PortalTransitionDecision::ComputeExitClearancePush(
    float transformedCenterDepth,
    float hullHalfExtentAlongNormal,
    float clearanceEpsilon)
{
    return ComputeExitClearancePush(
        PortalExitClearanceMode::FullHull,
        transformedCenterDepth,
        hullHalfExtentAlongNormal,
        clearanceEpsilon);
}

float PortalTransitionDecision::ComputeExitClearancePush(
    PortalExitClearanceMode mode,
    float transformedCenterDepth,
    float hullHalfExtentAlongNormal,
    float clearanceEpsilon)
{
    if (mode == PortalExitClearanceMode::ExactTransform)
        return 0.0f;

    const float requiredCenterDepth = mode == PortalExitClearanceMode::PlaneEpsilon
        ? std::max(0.0f, clearanceEpsilon)
        : std::max(0.0f, hullHalfExtentAlongNormal) + std::max(0.0f, clearanceEpsilon);
    return std::max(0.0f, requiredCenterDepth - transformedCenterDepth);
}
