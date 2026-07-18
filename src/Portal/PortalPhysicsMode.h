#pragma once

namespace PortalPhysicsMode
{
    enum class Mode
    {
        VisualOnlyBaseline,
        LegacyTraversal,
        BspTraversal
    };

    // Development default: keep portal rendering, but disable all player
    // traversal until BSP collision carving can be tested independently.
    inline constexpr Mode kCurrentMode = Mode::VisualOnlyBaseline;

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

    constexpr bool ShouldMutatePlayerMovement()
    {
        return kCurrentMode == Mode::LegacyTraversal;
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
