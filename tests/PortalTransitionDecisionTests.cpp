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
        !PortalTransitionDecision::ShouldCommitPlaneCrossing(0.50f, -0.25f, false, true, true),
        "a crossing without previous-frame history must not commit");
    Expect(
        !PortalTransitionDecision::ShouldCommitPlaneCrossing(0.50f, -0.25f, true, true, false),
        "a crossing while moving away from the portal must not commit");

    ExpectNear(
        PortalTransitionDecision::ComputeExitClearancePush(4.23f, 16.0f, 2.0f),
        13.77f,
        "an intersecting exit hull must be pushed fully in front of the wall");
    ExpectNear(
        PortalTransitionDecision::ComputeExitClearancePush(20.0f, 16.0f, 2.0f),
        0.0f,
        "an already clear exit hull must not receive an extra push");

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
        PortalTransitionDecision::ShouldSyncCommittedMoveBeforeControlledMove(PortalTransitionPhase::ExitingPortal, 500.0f * 500.0f),
        "exit controlled prediction must consume committed teleport when predicted origin is still at the entry portal");
    Expect(
        !PortalTransitionDecision::ShouldSyncCommittedMoveBeforeControlledMove(PortalTransitionPhase::ExitingPortal, 8.0f * 8.0f),
        "exit controlled prediction must not rewrite already-synced movement");
    Expect(
        !PortalTransitionDecision::ShouldSyncCommittedMoveBeforeControlledMove(PortalTransitionPhase::Idle, 500.0f * 500.0f),
        "normal movement must not consume committed teleport sync");
    Expect(
        PortalTransitionDecision::ShouldSyncCommittedViewAnglesWithMove(PortalTransitionPhase::ExitingPortal, true),
        "committed exit movement sync must also update user command view angles");
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

    std::cout << "PortalTransitionDecision tests passed\n";
    return 0;
}
