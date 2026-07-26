#include <cstdlib>
#include <cmath>
#include <iostream>

#include "../src/Portal/PortalTransitionDecision.h"

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
}

int main()
{
    Expect(
        !PortalTransitionDecision::RequiresControlledNoclip(PortalTransitionPhase::Idle),
        "idle movement must use the normal walk solver");
    Expect(
        !PortalTransitionDecision::RequiresControlledNoclip(PortalTransitionPhase::ApproachingPortal),
        "approaching the portal must not enter noclip early");
    Expect(
        PortalTransitionDecision::RequiresControlledNoclip(PortalTransitionPhase::IntersectingPortal),
        "portal intersection must use controlled noclip");
    Expect(
        PortalTransitionDecision::RequiresControlledNoclip(PortalTransitionPhase::CommittingTeleport),
        "teleport commit must retain controlled noclip");
    Expect(
        PortalTransitionDecision::RequiresControlledNoclip(PortalTransitionPhase::ExitingPortal),
        "exit movement must retain controlled noclip");
    Expect(
        !PortalTransitionDecision::RequiresControlledNoclip(PortalTransitionPhase::Cooldown),
        "cooldown must restore the normal walk solver");

    Expect(
        PortalTransitionDecision::ShouldApplyLegacyBridgeMovement(PortalTransitionPhase::Idle),
        "idle movement may still use legacy bridge helpers when no GMod traversal is active");
    Expect(
        !PortalTransitionDecision::ShouldApplyLegacyBridgeMovement(PortalTransitionPhase::ApproachingPortal),
        "GMod-style traversal must not let legacy bridge helpers alter approach movement");
    Expect(
        !PortalTransitionDecision::ShouldApplyLegacyBridgeMovement(PortalTransitionPhase::IntersectingPortal),
        "controlled noclip must disable legacy bridge movement writers");
    Expect(
        !PortalTransitionDecision::ShouldApplyLegacyBridgeMovement(PortalTransitionPhase::CommittingTeleport),
        "teleport commit must disable legacy bridge movement writers");
    Expect(
        !PortalTransitionDecision::ShouldApplyLegacyBridgeMovement(PortalTransitionPhase::ExitingPortal),
        "exit movement must disable legacy bridge movement writers");
    Expect(
        PortalTransitionDecision::ShouldApplyLegacyBridgeMovement(PortalTransitionPhase::Cooldown),
        "cooldown has restored walk movement so legacy bridge helpers are not blocked by this policy");

    Expect(
        PortalTransitionDecision::ShouldRunOriginalPlayerMove(PortalTransitionPhase::Idle),
        "idle must run the original player movement");
    Expect(
        PortalTransitionDecision::ShouldRunOriginalPlayerMove(PortalTransitionPhase::ApproachingPortal),
        "approach must run the original player movement before controlled noclip");
    Expect(
        !PortalTransitionDecision::ShouldRunOriginalPlayerMove(PortalTransitionPhase::IntersectingPortal),
        "controlled noclip must replace original player movement during intersection");
    Expect(
        !PortalTransitionDecision::ShouldRunOriginalPlayerMove(PortalTransitionPhase::CommittingTeleport),
        "controlled noclip must replace original player movement during teleport commit");
    Expect(
        !PortalTransitionDecision::ShouldRunOriginalPlayerMove(PortalTransitionPhase::ExitingPortal),
        "controlled noclip must replace original player movement while exiting");
    Expect(
        PortalTransitionDecision::ShouldRunOriginalPlayerMove(PortalTransitionPhase::Cooldown),
        "cooldown must return to the original player movement");

    Expect(
        !PortalTransitionDecision::ShouldReplaceOriginalPlayerMove(
            PortalTransitionPhase::IntersectingPortal,
            false),
        "intersection must retain original PlayerMove when movement mutation is disabled");
    Expect(
        !PortalTransitionDecision::ShouldReplaceOriginalPlayerMove(
            PortalTransitionPhase::CommittingTeleport,
            false),
        "teleport commit must retain original PlayerMove when movement mutation is disabled");
    Expect(
        PortalTransitionDecision::ShouldReplaceOriginalPlayerMove(
            PortalTransitionPhase::IntersectingPortal,
            true),
        "legacy controlled movement may replace original PlayerMove during intersection");
    Expect(
        !PortalTransitionDecision::ShouldReplaceOriginalPlayerMove(
            PortalTransitionPhase::Idle,
            true),
        "idle movement must never replace original PlayerMove");

    Expect(
        PortalTransitionDecision::ShouldCommitPlaneCrossing(0.50f, -0.25f, true, true, true),
        "front-to-back crossing inside the aperture must commit");
    Expect(
        PortalTransitionDecision::ShouldCommitPlaneCrossing(0.0f, -0.25f, true, true, true),
        "leaving the exact portal plane toward the back must commit");
    Expect(
        !PortalTransitionDecision::ShouldCommitPlaneCrossing(3.0f, 0.25f, true, true, true),
        "remaining in front of the portal must not commit");
    Expect(
        !PortalTransitionDecision::ShouldCommitPlaneCrossing(-0.25f, 0.50f, true, true, true),
        "back-to-front movement must not commit the entry portal");
    Expect(
        !PortalTransitionDecision::ShouldCommitPlaneCrossing(0.50f, -0.25f, true, false, true),
        "crossing outside the aperture must not commit");
    Expect(
        PortalTransitionDecision::ShouldCommitPlaneCrossing(0.50f, -0.25f, false, true, true),
        "a first valid observation behind the portal plane must commit without waiting another command");
    Expect(
        !PortalTransitionDecision::ShouldCommitPlaneCrossing(-0.25f, -0.50f, true, true, true),
        "a player already behind the portal plane must not retrigger the same crossing");
    Expect(
        !PortalTransitionDecision::ShouldCommitPlaneCrossing(0.50f, -0.25f, true, true, false),
        "a crossing while moving away from the portal must not commit");

    float crossingFraction = 0.0f;
    Expect(
        PortalTransitionDecision::TryComputePortalPlaneCrossingFraction(
            8.0f, -12.0f, &crossingFraction),
        "a front-to-back swept segment must report a portal-plane crossing");
    ExpectNear(
        crossingFraction,
        0.4f,
        "the swept portal-plane crossing fraction must locate the zero-depth point");
    Expect(
        !PortalTransitionDecision::TryComputePortalPlaneCrossingFraction(
            -2.0f, -12.0f, &crossingFraction),
        "a segment wholly behind the portal must not report a new crossing");
    Expect(
        !PortalTransitionDecision::TryComputePortalPlaneCrossingFraction(
            8.0f, 2.0f, &crossingFraction),
        "a segment wholly in front of the portal must not report a crossing");

    constexpr float predictedCommitDepth = -0.25f;
    const float predictedLeadTime = PortalTransitionDecision::ComputePredictedPortalCommitLeadTime(
        2.0f,
        -60.0f,
        0.05f,
        predictedCommitDepth);
    ExpectNear(
        predictedLeadTime,
        0.0375f,
        "predicted crossing must advance just behind the entry plane instead of stopping in front");
    ExpectNear(
        2.0f + (-60.0f * predictedLeadTime),
        predictedCommitDepth,
        "the predicted anchor must land in the entry portal's back half-space");
    Expect(
        PortalTransitionDecision::IsPortalCommitHalfSpaceValid(-0.25f, 0.25f),
        "a negative entry depth transformed to a positive exit depth is valid");
    Expect(
        !PortalTransitionDecision::IsPortalCommitHalfSpaceValid(1.5f, -1.5f),
        "the old projected front-to-back depth inversion must be rejected");

    ExpectNear(
        PortalTransitionDecision::ComputeExitClearancePush(4.23f, 16.0f, 2.0f),
        13.77f,
        "an intersecting exit hull must be pushed fully in front of the wall");
    ExpectNear(
        PortalTransitionDecision::ComputeExitClearancePush(20.0f, 16.0f, 2.0f),
        0.0f,
        "an already clear exit hull must not receive an extra push");

    Expect(
        PortalTransitionDecision::ExitClearanceModeName(PortalExitClearanceMode::FullHull) == std::string("FullHull"),
        "the full-hull clearance mode must have a stable diagnostic name");
    Expect(
        PortalTransitionDecision::ExitClearanceModeName(PortalExitClearanceMode::PlaneEpsilon) == std::string("PlaneEpsilon"),
        "the plane-epsilon clearance mode must have a stable diagnostic name");
    Expect(
        PortalTransitionDecision::ExitClearanceModeName(PortalExitClearanceMode::ExactTransform) == std::string("ExactTransform"),
        "the exact-transform clearance mode must have a stable diagnostic name");
    ExpectNear(
        PortalTransitionDecision::ComputeExitClearancePush(
            PortalExitClearanceMode::FullHull, -1.5f, 16.0f, 2.0f),
        19.5f,
        "full-hull mode must clear the complete player hull plus epsilon");
    ExpectNear(
        PortalTransitionDecision::ComputeExitClearancePush(
            PortalExitClearanceMode::PlaneEpsilon, -1.5f, 16.0f, 2.0f),
        3.5f,
        "plane-epsilon mode must only place the transformed center past the portal plane");
    ExpectNear(
        PortalTransitionDecision::ComputeExitClearancePush(
            PortalExitClearanceMode::PlaneEpsilon, 2.0f, 16.0f, 2.0f),
        0.0f,
        "plane-epsilon mode must not push a center already at the requested epsilon");
    ExpectNear(
        PortalTransitionDecision::ComputeExitClearancePush(
            PortalExitClearanceMode::ExactTransform, -1.5f, 16.0f, 2.0f),
        0.0f,
        "exact-transform mode must preserve the transformed position without any exit push");
    Expect(
        PortalTransitionDecision::TryParseExitClearanceMode("full", nullptr) == false,
        "mode parsing must reject a null output pointer");
    PortalExitClearanceMode parsedMode = PortalExitClearanceMode::FullHull;
    Expect(
        PortalTransitionDecision::TryParseExitClearanceMode("plane", &parsedMode)
            && parsedMode == PortalExitClearanceMode::PlaneEpsilon,
        "the short plane command value must select plane-epsilon mode");
    Expect(
        PortalTransitionDecision::TryParseExitClearanceMode("exact", &parsedMode)
            && parsedMode == PortalExitClearanceMode::ExactTransform,
        "the short exact command value must select exact-transform mode");
    Expect(
        !PortalTransitionDecision::TryParseExitClearanceMode("unknown", &parsedMode),
        "unknown clearance modes must be rejected");
    PortalTransitionDecision::SetExitClearanceMode(PortalExitClearanceMode::PlaneEpsilon);
    Expect(
        PortalTransitionDecision::GetExitClearanceMode() == PortalExitClearanceMode::PlaneEpsilon,
        "the runtime clearance selection must retain plane-epsilon mode");
    PortalTransitionDecision::SetExitClearanceMode(PortalExitClearanceMode::FullHull);
    Expect(
        PortalTransitionDecision::GetExitClearanceMode() == PortalExitClearanceMode::FullHull,
        "the runtime clearance selection must return to the full-hull baseline");

    Expect(
        PortalTransitionDecision::IsViewNearPortalPlane(24.0f, 12.0f, -20.0f, 48.0f, 38.0f, 62.0f),
        "a view within the portal-local diagnostic prism must be logged");
    Expect(
        !PortalTransitionDecision::IsViewNearPortalPlane(48.1f, 0.0f, 0.0f, 48.0f, 38.0f, 62.0f),
        "a view beyond the diagnostic depth must not be logged");
    Expect(
        !PortalTransitionDecision::IsViewNearPortalPlane(1.0f, 38.1f, 0.0f, 48.0f, 38.0f, 62.0f),
        "a view beyond the diagnostic lateral boundary must not be logged");

    Expect(
        PortalTransitionDecision::ShouldRestoreWalkOnProbeLoss(PortalTransitionPhase::Idle),
        "probe loss outside controlled traversal may clear back to idle");
    Expect(
        PortalTransitionDecision::ShouldRestoreWalkOnProbeLoss(PortalTransitionPhase::ApproachingPortal),
        "probe loss while approaching may clear back to idle");
    Expect(
        !PortalTransitionDecision::ShouldRestoreWalkOnProbeLoss(PortalTransitionPhase::IntersectingPortal),
        "probe loss during controlled intersection must not immediately restore WALK");
    Expect(
        !PortalTransitionDecision::ShouldRestoreWalkOnProbeLoss(PortalTransitionPhase::CommittingTeleport),
        "probe loss during teleport commit must not immediately restore WALK");
    Expect(
        !PortalTransitionDecision::ShouldRestoreWalkOnProbeLoss(PortalTransitionPhase::ExitingPortal),
        "probe loss during exit must not immediately restore WALK at an unverified position");
    Expect(
        PortalTransitionDecision::ShouldRestoreWalkOnProbeLoss(PortalTransitionPhase::Cooldown),
        "probe loss during cooldown may remain normal walk");

    Expect(
        PortalTransitionDecision::ShouldReleaseExitControlledMove(16.0f, true),
        "GMod-style ipMove releases controlled noclip as soon as frontDist reaches 16");
    Expect(
        PortalTransitionDecision::ShouldReleaseExitControlledMove(15.99f, true) == false,
        "GMod-style ipMove keeps controlled noclip while frontDist is still inside 16");
    Expect(
        PortalTransitionDecision::ShouldReleaseExitControlledMove(8.0f, false),
        "leaving the portal aperture releases controlled noclip even before the plane distance threshold");

    Expect(
        PortalTransitionDecision::ShouldAllowTraversalEntry(PortalTransitionPhase::Cooldown, 1.00f, 1.20f) == false,
        "cooldown must block immediate re-entry after early exit release");
    Expect(
        PortalTransitionDecision::ShouldAllowTraversalEntry(PortalTransitionPhase::Cooldown, 1.20f, 1.20f),
        "cooldown allows entry again once the cooldown time has elapsed");
    Expect(
        PortalTransitionDecision::ShouldAllowTraversalEntry(PortalTransitionPhase::Idle, 1.00f, 1.20f),
        "idle entry is not blocked by a stale cooldown timestamp");
    Expect(
        !PortalTransitionDecision::ShouldAllowTraversalEntry(PortalTransitionPhase::CommittingTeleport, 2.00f, 1.20f),
        "a teleport commit must never admit another traversal entry");
    Expect(
        !PortalTransitionDecision::ShouldAllowTraversalEntry(PortalTransitionPhase::ExitingPortal, 2.00f, 1.20f),
        "the spatial exit-rearm lock must block entry even after the old cooldown timestamp expires");
    ExpectNear(
        PortalTransitionDecision::ComputeExitRearmCooldownUntil(5.00f, 0.20f),
        5.20f,
        "the temporal rearm guard must start when spatial exit clearance releases");
    ExpectNear(
        PortalTransitionDecision::ComputeExitRearmCooldownUntil(5.00f, -0.20f),
        5.00f,
        "a negative cooldown duration must not move the rearm timestamp backward");

    Expect(
        PortalTransitionDecision::ShouldSyncCommittedMoveBeforeControlledMove(PortalTransitionPhase::ExitingPortal, 500.0f * 500.0f),
        "exit controlled prediction must consume committed teleport when predicted origin is still at the entry portal");
    Expect(
        PortalTransitionDecision::ShouldSyncCommittedMoveBeforeControlledMove(PortalTransitionPhase::Cooldown, 500.0f * 500.0f),
        "the command immediately after exit release may still consume the committed teleport");
    Expect(
        !PortalTransitionDecision::ShouldSyncCommittedMoveBeforeControlledMove(PortalTransitionPhase::ExitingPortal, 8.0f * 8.0f),
        "exit controlled prediction must not rewrite already-synced movement");
    Expect(
        !PortalTransitionDecision::ShouldSyncCommittedMoveBeforeControlledMove(PortalTransitionPhase::IntersectingPortal, 500.0f * 500.0f),
        "a new intersection must not consume stale committed teleport movement");
    Expect(
        !PortalTransitionDecision::ShouldSyncCommittedMoveBeforeControlledMove(PortalTransitionPhase::Idle, 500.0f * 500.0f),
        "normal movement must not consume committed teleport sync");
    Expect(
        PortalTransitionDecision::ShouldSyncCommittedViewAnglesWithMove(PortalTransitionPhase::ExitingPortal, true),
        "committed exit movement sync must also update user command view angles");
    Expect(
        PortalTransitionDecision::ShouldSyncCommittedViewAnglesWithMove(PortalTransitionPhase::Cooldown, true),
        "cooldown fallback synchronization must update view angles atomically with movement");
    Expect(
        !PortalTransitionDecision::ShouldSyncCommittedViewAnglesWithMove(PortalTransitionPhase::ExitingPortal, false),
        "view angles must not be rewritten when committed movement was not consumed");
    Expect(
        !PortalTransitionDecision::ShouldSyncCommittedViewAnglesWithMove(PortalTransitionPhase::Idle, true),
        "normal movement must not rewrite user command view angles");
    Expect(
        PortalTransitionDecision::IsWithinGModWallPortalBounds(16.5f, 0.0f, 0.0f),
        "GMod wall portal bounds accepts a centered player close to the portal plane");
    Expect(
        !PortalTransitionDecision::IsWithinGModWallPortalBounds(17.1f, 0.0f, 0.0f),
        "GMod wall portal bounds rejects players too far in front of the portal plane");
    Expect(
        !PortalTransitionDecision::IsWithinGModWallPortalBounds(8.0f, 20.1f, 0.0f),
        "GMod wall portal bounds rejects players outside the portal width");
    Expect(
        !PortalTransitionDecision::IsWithinGModWallPortalBounds(8.0f, 0.0f, 44.1f),
        "GMod wall portal bounds rejects players above the portal height");
    Expect(
        PortalTransitionDecision::IsWithinGModWallPortalBounds(8.0f, 0.0f, -57.0f),
        "L4D2-adapted wall portal bounds accepts the observed local player feet height");
    Expect(
        !PortalTransitionDecision::IsWithinGModWallPortalBounds(8.0f, 0.0f, -61.0f),
        "L4D2-adapted wall portal bounds still rejects players below the practical portal height");

    Expect(
        std::fabs(PortalTransitionDecision::ComputePortalAwareNearClip(true, true, 6.25f, 7.0f, 9.0f, 1.0f) - 1.0f) < 0.001f,
        "portal-aware near clip reduces the main view near plane inside the confirmed clipping interval");
    Expect(
        std::fabs(PortalTransitionDecision::ComputePortalAwareNearClip(false, true, 6.25f, 7.0f, 9.0f, 1.0f) - 7.0f) < 0.001f,
        "disabled portal-aware near clip preserves the original near plane");
    Expect(
        std::fabs(PortalTransitionDecision::ComputePortalAwareNearClip(true, false, 6.25f, 7.0f, 9.0f, 1.0f) - 7.0f) < 0.001f,
        "invalid portal context preserves the original near plane");
    Expect(
        std::fabs(PortalTransitionDecision::ComputePortalAwareNearClip(true, true, 9.01f, 7.0f, 9.0f, 1.0f) - 7.0f) < 0.001f,
        "a view outside the portal near-plane danger interval preserves the original near plane");
    Expect(
        std::fabs(PortalTransitionDecision::ComputePortalAwareNearClip(true, true, -0.01f, 7.0f, 9.0f, 1.0f) - 7.0f) < 0.001f,
        "the pre-Teleport near-clip experiment does not alter a view behind the entry plane");
    Expect(
        std::fabs(PortalTransitionDecision::ComputePortalAwareNearClip(true, true, 0.5f, 0.5f, 9.0f, 1.0f) - 0.5f) < 0.001f,
        "portal-aware near clip never increases an already smaller near plane");

    ExpectNear(
        PortalTransitionDecision::ComputePortalAwareNearClipForPhase(
            PortalTransitionPhase::ExitingPortal, true, true, -1.5f, 7.0f, 18.0f, 1.0f),
        1.0f,
        "exact exit handoff must keep the near plane reduced while the camera is still behind the exit plane");
    ExpectNear(
        PortalTransitionDecision::ComputePortalAwareNearClipForPhase(
            PortalTransitionPhase::Cooldown, true, true, 16.82f, 7.0f, 18.0f, 1.0f),
        1.0f,
        "high-speed exit handoff must survive the immediate transition into cooldown");
    ExpectNear(
        PortalTransitionDecision::ComputePortalAwareNearClipForPhase(
            PortalTransitionPhase::Cooldown, true, true, 18.01f, 7.0f, 18.0f, 1.0f),
        7.0f,
        "exit near clip must stop after the camera has cleared the guarded interval");
    ExpectNear(
        PortalTransitionDecision::ComputePortalAwareNearClipForPhase(
            PortalTransitionPhase::Idle, true, true, 1.0f, 7.0f, 18.0f, 1.0f),
        7.0f,
        "normal gameplay must not inherit the exit handoff near clip");

    Expect(
        PortalTransitionDecision::ShouldUseExactExitVisualGuard(
            true, PortalTransitionPhase::ExitingPortal, PortalExitClearanceMode::ExactTransform,
            true, true, -1.5f, 24.0f),
        "exact exit handoff must append a safe visibility origin while the view is behind the exit plane");
    Expect(
        PortalTransitionDecision::ShouldUseExactExitVisualGuard(
            true, PortalTransitionPhase::Cooldown, PortalExitClearanceMode::ExactTransform,
            true, true, 16.82f, 24.0f),
        "the visibility guard must cover a high-speed handoff that reaches cooldown in one tick");
    Expect(
        !PortalTransitionDecision::ShouldUseExactExitVisualGuard(
            true, PortalTransitionPhase::ExitingPortal, PortalExitClearanceMode::PlaneEpsilon,
            true, true, -1.5f, 24.0f),
        "non-exact clearance modes must remain unchanged for A/B comparison");
    Expect(
        !PortalTransitionDecision::ShouldUseExactExitVisualGuard(
            true, PortalTransitionPhase::ExitingPortal, PortalExitClearanceMode::ExactTransform,
            false, true, -1.5f, 24.0f),
        "recursive portal views must not receive the main-view exit visibility guard");
    Expect(
        !PortalTransitionDecision::ShouldUseExactExitVisualGuard(
            true, PortalTransitionPhase::ExitingPortal, PortalExitClearanceMode::ExactTransform,
            true, true, 24.01f, 24.0f),
        "the visibility guard must stop outside its bounded exit interval");

    Expect(
        PortalTransitionDecision::ShouldApplyEntryCameraHandoff(
            true, PortalTransitionPhase::IntersectingPortal,
            true, true, true, true, -0.001f),
        "the local entry camera must hand off immediately after it crosses the portal plane");
    Expect(
        !PortalTransitionDecision::ShouldApplyEntryCameraHandoff(
            true, PortalTransitionPhase::IntersectingPortal,
            true, true, true, true, 0.0f),
        "the entry camera must remain in the entry world while it is still on the portal plane");
    Expect(
        !PortalTransitionDecision::ShouldApplyEntryCameraHandoff(
            true, PortalTransitionPhase::IntersectingPortal,
            true, true, true, true, 0.001f),
        "the entry camera must remain unchanged in front of the portal plane");
    Expect(
        !PortalTransitionDecision::ShouldApplyEntryCameraHandoff(
            true, PortalTransitionPhase::IntersectingPortal,
            true, true, true, false, -1.0f),
        "a camera outside the portal aperture must never be transformed");
    Expect(
        !PortalTransitionDecision::ShouldApplyEntryCameraHandoff(
            true, PortalTransitionPhase::ExitingPortal,
            true, true, true, true, -1.0f),
        "the exit phase must not transform the already teleported camera a second time");
    Expect(
        !PortalTransitionDecision::ShouldApplyEntryCameraHandoff(
            false, PortalTransitionPhase::IntersectingPortal,
            true, true, true, true, -1.0f),
        "the runtime control must disable only the entry camera handoff experiment");
    Expect(
        !PortalTransitionDecision::ShouldApplyEntryCameraHandoff(
            true, PortalTransitionPhase::IntersectingPortal,
            false, true, true, true, -1.0f),
        "non-local player views must not receive the local camera handoff");
    Expect(
        !PortalTransitionDecision::ShouldApplyEntryCameraHandoff(
            true, PortalTransitionPhase::IntersectingPortal,
            true, false, true, true, -1.0f),
        "the entry camera handoff must not run without active BSP carving");
    Expect(
        !PortalTransitionDecision::ShouldApplyEntryCameraHandoff(
            true, PortalTransitionPhase::IntersectingPortal,
            true, true, false, true, -1.0f),
        "the entry camera handoff must not run until both portals are ready");

    ExpectNear(
        PortalTransitionDecision::ComputeOfficialPortalRemoteClipPlaneDistance(10.0f),
        9.5f,
        "the official remote clip plane must sit half a unit behind a positive portal plane");
    ExpectNear(
        PortalTransitionDecision::ComputeOfficialPortalRemoteClipPlaneDistance(-3.0f),
        -3.5f,
        "the official remote clip plane offset must be orientation-independent");
    ExpectNear(
        PortalTransitionDecision::ComputeOfficialPortalRemoteViewNormalPush(),
        0.0f,
        "the official remote view must preserve the exact transformed camera origin");

    PortalTransitionDecision::SetPortalNearClipFixEnabled(false);
    Expect(
        !PortalTransitionDecision::GetPortalNearClipFixEnabled(),
        "portal-aware near clip runtime control can disable the experiment");
    PortalTransitionDecision::SetPortalNearClipFixEnabled(true);
    Expect(
        PortalTransitionDecision::GetPortalNearClipFixEnabled(),
        "portal-aware near clip runtime control can restore the default enabled state");

    PortalTransitionDecision::SetExactExitVisualGuardEnabled(false);
    Expect(
        !PortalTransitionDecision::GetExactExitVisualGuardEnabled(),
        "the exact exit visual guard can be disabled for a runtime A/B comparison");
    PortalTransitionDecision::SetExactExitVisualGuardEnabled(true);
    Expect(
        PortalTransitionDecision::GetExactExitVisualGuardEnabled(),
        "the exact exit visual guard defaults back to enabled for the next traversal");

    PortalTransitionDecision::SetEntryCameraHandoffEnabled(false);
    Expect(
        !PortalTransitionDecision::GetEntryCameraHandoffEnabled(),
        "the official-style entry camera handoff can be disabled for runtime A/B comparison");
    PortalTransitionDecision::SetEntryCameraHandoffEnabled(true);
    Expect(
        PortalTransitionDecision::GetEntryCameraHandoffEnabled(),
        "the official-style entry camera handoff defaults back to enabled for the next traversal");

    std::cout << "PortalTransitionDecision tests passed\n";
    return 0;
}
