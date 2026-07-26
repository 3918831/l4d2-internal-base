#pragma once

#include <cstdio>
#include <cstdarg>
#include <cstring>

namespace U::PortalFileLog
{
    // Relative to the game process working directory. The launcher starts the
    // game from the L4D2 root, so this lands next to L4D2_Portal.dll/exe.
    inline constexpr const char* kPath = "portal_l4d2_traversal.log";

    inline constexpr bool kCaptureGModTraversal = true;
    inline constexpr bool kCaptureLegacyBridge = false;
    inline constexpr bool kCaptureLegacyStageProbe = false;
    inline constexpr bool kCaptureLegacySimulator = false;
    inline constexpr bool kCaptureRenderDiagnostics = true;
    inline constexpr bool kSuppressLegacyConsoleNoise = true;

    inline bool g_Initialized = false;

    inline constexpr const char* kAlwaysNeedles[] = {
        "PortalFileLog",
        "PortalBsp",
        "PortalPhysicsMode",
    };

    inline constexpr const char* kGModTraversalNeedles[] = {
        "PortalGModTraversal",
        "PortalMoveTypeProbe",
        "PortalDataMapProbe",
        "PortalServerEntProp",
        "PortalServerMoveType",
        "PortalHookProbe",
        "PortalEnterState",
        "PortalControlledMove",
        "PortalBounds",
        "PortalNearMiss",
        "PortalGModOffset",
        "PortalPredictionSync",
        "PortalLocalClamp",
        "PortalCrossing",
        "PortalTeleportCommit",
        "PortalContinuity",
        "PortalExitRearm",
        "PortalExitState",
        "PortalRestoreWalk",
    };

    inline constexpr const char* kLegacyBridgeNeedles[] = {
        "PortalBridge",
        "PortalTraversalFrame",
        "PortalWalkMoveFrame",
        "WalkMoveNudge",
        "CommittedMoveSync",
    };

    inline constexpr const char* kLegacyStageProbeNeedles[] = {
        "PortalStage1Probe",
    };

    inline constexpr const char* kLegacySimulatorNeedles[] = {
        "PortalSim",
        "PortalTeleport",
        "PortalTransition",
    };

    inline constexpr const char* kRenderNeedles[] = {
        "PortalEnvironment",
        "PortalRenderState",
        "PortalTone",
        "PortalVisualPlaneProbe",
        "PortalNearClipFix",
        "PortalEntryViewHandoff",
        "PortalOfficialRemoteView",
        "PortalRenderFix",
        "PortalExitVisibility",
    };

    inline bool ContainsAny(const char* text, const char* const* needles, size_t count)
    {
        if (!text)
            return false;

        for (size_t i = 0; i < count; ++i)
        {
            if (needles[i] && std::strstr(text, needles[i]))
                return true;
        }

        return false;
    }

    inline bool ShouldCapture(const char* text)
    {
        if (!text)
            return false;

        if (ContainsAny(text, kAlwaysNeedles, sizeof(kAlwaysNeedles) / sizeof(kAlwaysNeedles[0])))
            return true;

        if (kCaptureGModTraversal && ContainsAny(text, kGModTraversalNeedles, sizeof(kGModTraversalNeedles) / sizeof(kGModTraversalNeedles[0])))
            return true;

        if (kCaptureLegacyBridge && ContainsAny(text, kLegacyBridgeNeedles, sizeof(kLegacyBridgeNeedles) / sizeof(kLegacyBridgeNeedles[0])))
            return true;

        if (kCaptureLegacyStageProbe && ContainsAny(text, kLegacyStageProbeNeedles, sizeof(kLegacyStageProbeNeedles) / sizeof(kLegacyStageProbeNeedles[0])))
            return true;

        if (kCaptureLegacySimulator && ContainsAny(text, kLegacySimulatorNeedles, sizeof(kLegacySimulatorNeedles) / sizeof(kLegacySimulatorNeedles[0])))
            return true;

        if (kCaptureRenderDiagnostics && ContainsAny(text, kRenderNeedles, sizeof(kRenderNeedles) / sizeof(kRenderNeedles[0])))
            return true;

        return false;
    }

    inline bool ShouldSuppressConsole(const char* text)
    {
        if (!kSuppressLegacyConsoleNoise || !text)
            return false;

        static constexpr const char* kConsoleSummaryNeedles[] = {
            "[PortalFileLog]",
            "[PortalPhysicsMode]",
            "[PortalBsp][Command]",
            "[PortalBsp][Phase1Status]",
        };

        if (ContainsAny(text, kConsoleSummaryNeedles, sizeof(kConsoleSummaryNeedles) / sizeof(kConsoleSummaryNeedles[0])))
            return false;

        // Focused traversal/BSP/render diagnostics have already been routed to
        // the dedicated file. LogError bypasses this console policy entirely.
        if (ShouldCapture(text))
            return true;

        static constexpr const char* kLegacyConsoleNoiseNeedles[] = {
            "PortalBridge",
            "PortalStage1Probe",
            "PortalSim",
            "PortalTransition",
            "PortalTeleport",
            "PortalEnvironment",
            "PortalRenderState",
            "PortalTone",
            "PortalTraversalFrame",
            "PortalWalkMoveFrame",
            "WalkMoveNudge",
            "CommittedMoveSync",
        };

        return ContainsAny(text, kLegacyConsoleNoiseNeedles, sizeof(kLegacyConsoleNoiseNeedles) / sizeof(kLegacyConsoleNoiseNeedles[0]));
    }

    inline void EnsureInitialized()
    {
        if (g_Initialized)
            return;

        FILE* file = nullptr;
        if (fopen_s(&file, kPath, "wb") == 0 && file)
        {
            std::fprintf(file, "[PortalFileLog] reset path=%s\n", kPath);
            std::fprintf(file, "[PortalFileLog] capture gmod=%s legacyBridge=%s legacyStageProbe=%s legacySimulator=%s render=%s\n",
                kCaptureGModTraversal ? "on" : "off",
                kCaptureLegacyBridge ? "on" : "off",
                kCaptureLegacyStageProbe ? "on" : "off",
                kCaptureLegacySimulator ? "on" : "off",
                kCaptureRenderDiagnostics ? "on" : "off");
            std::fclose(file);
            g_Initialized = true;
        }
    }

    inline void AppendMarker(const char* marker)
    {
        EnsureInitialized();

        FILE* file = nullptr;
        if (fopen_s(&file, kPath, "ab") != 0 || !file)
            return;

        std::fprintf(file, "[PortalFileLog] %s path=%s\n", marker ? marker : "marker", kPath);
        std::fclose(file);
    }

    inline void Reset()
    {
        if (!g_Initialized)
        {
            EnsureInitialized();
            return;
        }

        AppendMarker("portal init marker");
    }

    inline void Write(const char* text)
    {
        if (!ShouldCapture(text))
            return;

        EnsureInitialized();

        FILE* file = nullptr;
        if (fopen_s(&file, kPath, "ab") != 0 || !file)
            return;

        std::fputs(text, file);
        const size_t len = std::strlen(text);
        if (len == 0 || text[len - 1] != '\n')
            std::fputc('\n', file);

        std::fclose(file);
    }

    inline void WriteFormat(const char* format, ...)
    {
        if (!format)
            return;

        char buffer[2048];
        va_list args;
        va_start(args, format);
        vsprintf_s(buffer, format, args);
        va_end(args);
        Write(buffer);
    }
}
