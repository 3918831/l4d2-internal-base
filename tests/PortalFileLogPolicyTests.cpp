#include <cstdlib>
#include <iostream>

#include "../src/Util/Logger/PortalFileLog.h"

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
    using U::PortalFileLog::ShouldCapture;
    using U::PortalFileLog::ShouldSuppressConsole;

    Expect(!ShouldSuppressConsole("[PortalPhysicsMode] active=BspPhase1Causal."),
        "physics-mode summaries must remain visible in the console");
    Expect(!ShouldSuppressConsole("[PortalBsp][Command] command=portal_bsp_phase1."),
        "BSP command acknowledgements must remain visible in the console");
    Expect(!ShouldSuppressConsole("[PortalBsp][Phase1Status] enabled=true."),
        "BSP status output must remain visible in the console");
    Expect(!ShouldSuppressConsole("ordinary initialization message"),
        "unrelated non-diagnostic messages must preserve their existing console behavior");

    Expect(ShouldSuppressConsole("[PortalHookProbe] domain=client."),
        "per-move hook probes must be file-only");
    Expect(ShouldSuppressConsole("[PortalMoveTypeProbe] phase=walk."),
        "move-type probes must be file-only");
    Expect(ShouldSuppressConsole("[PortalBounds] phase=Idle."),
        "portal-bound probes must be file-only");
    Expect(ShouldSuppressConsole("[PortalTeleportCommit] committed entry=Blue."),
        "teleport commit details must be file-only");
    Expect(ShouldSuppressConsole("[PortalPredictionSync] committed=true."),
        "prediction synchronization details must be file-only");
    Expect(ShouldSuppressConsole("[PortalBsp][Binding] owner=blue."),
        "detailed BSP binding diagnostics must be file-only");
    Expect(ShouldSuppressConsole("[PortalEnvironment] phase=main-render."),
        "render environment diagnostics must be file-only");
    Expect(ShouldSuppressConsole("[PortalVisualPlaneProbe] side=Blue."),
        "near-plane visual probes must be file-only");
    Expect(ShouldSuppressConsole("[PortalNearClipFix] entry=Blue."),
        "near-clip fix diagnostics must be file-only");
    Expect(ShouldSuppressConsole("[PortalEntryViewHandoff] entry=Blue exit=Orange."),
        "pre-Teleport entry camera handoff diagnostics must be file-only");
    Expect(ShouldSuppressConsole("[PortalOfficialRemoteView] entry=Blue exit=Orange."),
        "official remote-view diagnostics must be file-only");
    Expect(ShouldSuppressConsole("[PortalRenderFix] applied=true portal=Blue."),
        "near-plane proxy diagnostics must be file-only");
    Expect(ShouldSuppressConsole("[PortalExitVisibility] exit=Orange."),
        "exit visibility diagnostics must be file-only");
    Expect(ShouldSuppressConsole("[PortalContinuity] entry=Blue exit=Orange."),
        "teleport continuity diagnostics must be file-only");

    Expect(ShouldCapture("[PortalHookProbe] domain=client."),
        "file-only traversal probes must still be captured");
    Expect(ShouldCapture("[PortalBsp][Mutation] event=write."),
        "file-only BSP diagnostics must still be captured");
    Expect(ShouldCapture("[PortalVisualPlaneProbe] side=Blue."),
        "near-plane visual probes must be captured");
    Expect(ShouldCapture("[PortalNearClipFix] entry=Blue."),
        "near-clip fix diagnostics must be captured");
    Expect(ShouldCapture("[PortalEntryViewHandoff] entry=Blue exit=Orange."),
        "pre-Teleport entry camera handoff diagnostics must be captured");
    Expect(ShouldCapture("[PortalOfficialRemoteView] entry=Blue exit=Orange."),
        "official remote-view diagnostics must be captured");
    Expect(ShouldCapture("[PortalRenderFix] applied=true portal=Blue."),
        "near-plane proxy diagnostics must be captured");
    Expect(ShouldCapture("[PortalExitVisibility] exit=Orange."),
        "exit visibility diagnostics must be captured");
    Expect(ShouldCapture("[PortalContinuity] entry=Blue exit=Orange."),
        "teleport continuity diagnostics must be captured");
    Expect(!ShouldCapture("ordinary initialization message"),
        "unrelated console messages must not be added to the focused file log");

    std::cout << "PortalFileLog policy tests passed\n";
    return 0;
}
