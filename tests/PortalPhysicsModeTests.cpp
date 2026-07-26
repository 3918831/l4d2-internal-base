#include <cstdlib>
#include <cstring>
#include <iostream>

#include "../src/Portal/PortalPhysicsMode.h"

namespace
{
    void Expect(bool condition, const char* message)
    {
        if (condition)
            return;

        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

int main()
{
    using namespace PortalPhysicsMode;

    Expect(Current() == Mode::BspTraversal,
        "the controlled Phase 1 artifact selects BSP traversal mode");
    Expect(std::strcmp(CurrentName(), "BspTraversal") == 0,
        "the causal BSP mode has an unambiguous diagnostic name");
    Expect(ShouldRenderPortals(),
        "BSP traversal preserves portal rendering");
    Expect(ShouldRunTraversalSimulation(),
        "BSP traversal reuses the existing transition simulator");
    Expect(!ShouldRunTraversalForCollisionState(Mode::BspTraversal, false),
        "BSP traversal must stay idle until BSP carving is actually active");
    Expect(ShouldRunTraversalForCollisionState(Mode::BspTraversal, true),
        "BSP traversal may run after BSP carving becomes active");
    Expect(ShouldRunTraversalForCollisionState(Mode::LegacyTraversal, false),
        "legacy traversal does not depend on BSP carving state");
    Expect(!ShouldRunTraversalForCollisionState(Mode::VisualOnlyBaseline, true),
        "visual-only mode never runs traversal even if a stale carving flag is present");
    Expect(!ShouldMutatePlayerMovement(),
        "BSP traversal never changes client or server move type");
    Expect(!ShouldUseLegacyCollisionBypass(),
        "BSP traversal cannot clear collision trace results");
    Expect(ShouldCommitTeleport(),
        "BSP traversal reuses the existing teleport commit");
    Expect(ShouldSynchronizeCommittedTeleportPrediction(Mode::BspTraversal),
        "BSP traversal must synchronize a committed teleport into client prediction");
    Expect(ShouldSynchronizeCommittedTeleportPrediction(Mode::LegacyTraversal),
        "legacy traversal retains committed teleport prediction synchronization");
    Expect(!ShouldSynchronizeCommittedTeleportPrediction(Mode::VisualOnlyBaseline),
        "visual-only mode cannot synchronize a teleport that it never commits");
    Expect(!ShouldRunDestructiveDiagnostics(),
        "BSP traversal cannot temporarily write player physics state for diagnostics");

    std::cout << "PortalPhysicsMode tests passed\n";
    return 0;
}
