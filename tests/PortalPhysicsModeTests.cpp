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

    Expect(Current() == Mode::VisualOnlyBaseline,
        "the development baseline defaults to visual-only mode");
    Expect(std::strcmp(CurrentName(), "VisualOnlyBaseline") == 0,
        "the active mode has an unambiguous diagnostic name");
    Expect(ShouldRenderPortals(),
        "visual-only mode preserves portal rendering");
    Expect(!ShouldRunTraversalSimulation(),
        "visual-only mode does not advance the legacy traversal simulator");
    Expect(!ShouldMutatePlayerMovement(),
        "visual-only mode leaves client and server movement untouched");
    Expect(!ShouldUseLegacyCollisionBypass(),
        "visual-only mode cannot clear player collision traces");
    Expect(!ShouldCommitTeleport(),
        "visual-only mode cannot teleport the player");
    Expect(!ShouldRunDestructiveDiagnostics(),
        "visual-only mode cannot temporarily write player physics state for diagnostics");

    std::cout << "PortalPhysicsMode tests passed\n";
    return 0;
}
