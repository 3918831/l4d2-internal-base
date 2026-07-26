#pragma once

namespace PortalPhysicsMode
{
    enum class Mode
    {
        VisualOnlyBaseline,
        LegacyTraversal,
        BspTraversal
    };

    // Controlled Phase 1 artifact: BSP carving supplies wall clearance while
    // the existing simulator/transform/teleport path remains authoritative.
    // Legacy trace clearing and move-type mutation stay disabled in this mode.
    inline constexpr Mode kCurrentMode = Mode::BspTraversal;

    constexpr Mode Current()
    {
        return kCurrentMode;
    }

    constexpr const char* CurrentName()
    {
        switch (kCurrentMode)
        {
        case Mode::LegacyTraversal: return "LegacyTraversal";
        case Mode::BspTraversal: return "BspTraversal";
        case Mode::VisualOnlyBaseline:
        default: return "VisualOnlyBaseline";
        }
    }

    constexpr bool ShouldRenderPortals()
    {
        return true;
    }

    constexpr bool ShouldRunTraversalSimulation()
    {
        return kCurrentMode == Mode::LegacyTraversal
            || kCurrentMode == Mode::BspTraversal;
    }

    constexpr bool ShouldRunTraversalForCollisionState(Mode mode, bool bspCarvingActive)
    {
        switch (mode)
        {
        case Mode::LegacyTraversal:
            return true;
        case Mode::BspTraversal:
            return bspCarvingActive;
        case Mode::VisualOnlyBaseline:
        default:
            return false;
        }
    }

    constexpr bool ShouldRunTraversalForCollisionState(bool bspCarvingActive)
    {
        return ShouldRunTraversalForCollisionState(kCurrentMode, bspCarvingActive);
    }

    constexpr bool ShouldMutatePlayerMovement()
    {
        return kCurrentMode == Mode::LegacyTraversal;
    }

    constexpr bool ShouldSynchronizeCommittedTeleportPrediction(Mode mode)
    {
        return mode == Mode::LegacyTraversal
            || mode == Mode::BspTraversal;
    }

    constexpr bool ShouldSynchronizeCommittedTeleportPrediction()
    {
        return ShouldSynchronizeCommittedTeleportPrediction(kCurrentMode);
    }

    constexpr bool ShouldUseLegacyCollisionBypass()
    {
        return kCurrentMode == Mode::LegacyTraversal;
    }

    constexpr bool ShouldCommitTeleport()
    {
        return kCurrentMode == Mode::LegacyTraversal
            || kCurrentMode == Mode::BspTraversal;
    }

    constexpr bool ShouldRunDestructiveDiagnostics()
    {
        return kCurrentMode == Mode::LegacyTraversal;
    }
}
