#include "PortalTransition.h"
#include "PortalPhysicsMode.h"
#include "PortalBspCollisionCarver.h"
#include "PortalTransitionDecision.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <cmath>
#include <Windows.h>

#include "../SDK/L4D2/Entities/C_TerrorPlayer.h"
#include "../SDK/L4D2/Includes/usercmd.h"
#include "../SDK/L4D2/Includes/edict.h"
#include "../SDK/L4D2/Includes/iserverunknown.h"
#include "../SDK/L4D2/Interfaces/ClientEntityList.h"
#include "../SDK/L4D2/Interfaces/CServerTools.h"
#include "../SDK/L4D2/Interfaces/EngineClient.h"
#include "../SDK/L4D2/Interfaces/EngineTrace.h"
#include "../SDK/L4D2/Interfaces/GameMovement.h"
#include "../SDK/L4D2/Interfaces/IPlayerInfoManager.h"
#include "../SDK/L4D2/Interfaces/MatRenderContext.h"
#include "../SDK/L4D2/Server/ServerDataMap.h"
#include "../Util/Logger/Logger.h"
#include "../Util/Offsets/Offsets.h"
#pragma warning(push)
#pragma warning(disable: 4819)
#include "L4D2_Portal.h"
#include "CustomRender.h"
#pragma warning(pop)

namespace
{
    constexpr float kExitPushDistance = 8.0f;
    constexpr float kExitEyeClearance = 32.0f;
    constexpr float kTeleportCooldown = 0.20f;
    constexpr float kMoveIntoPortalDot = -20.0f;
    constexpr float kPortalEnterDistance = 34.0f;
    constexpr float kPortalExitDistance = 48.0f;
    constexpr float kPortalSessionTimeout = 2.50f;
    constexpr float kPortalHardSessionTimeout = 4.00f;
    constexpr float kPortalClampForward = 28.0f;
    constexpr float kPortalTransitBackDepth = 48.0f;
    constexpr float kPortalEmbedTargetDistance = -2.0f;
    constexpr float kPortalEmbedMaxStep = 4.0f;
    constexpr float kPortalEmbedFallbackFrameTime = 0.015f;
    constexpr float kPortalAssistStartDistance = 17.0f;
    constexpr float kPortalAssistBlockedVelocity = -20.0f;
    constexpr float kPortalRestoreVelocityThresholdSqr = 400.0f;
    constexpr bool kEnableVisualTransition = false;
    constexpr float kVisualExitEyeClearance = 16.0f;
    constexpr float kVisualTransitionDuration = 0.08f;
    constexpr bool kEnableToneTransition = true;
    constexpr float kToneTransitionDuration = 0.35f;
    constexpr float kToneTransitionPendingDuration = 0.12f;
    constexpr float kToneTransitionTriggerScale = 1.40f;
    constexpr float kToneTransitionTargetMultiplier = 0.80f;
    constexpr size_t kServerTeleportVTableIndex = 118;
    constexpr bool kDryRunServerSetAbsTeleport = false;
    constexpr bool kUseServerSetAbsTeleport = false;

    PortalTransform::PortalAperture DefaultAperture()
    {
        PortalTransform::PortalAperture aperture;
        aperture.halfWidth = 32.0f;
        aperture.halfHeight = 56.0f;
        aperture.tolerance = 4.0f;
        return aperture;
    }

    bool IsPortalOpenForTransition(const PortalInfo_t& portal)
    {
        return portal.bIsActive
            && !portal.bIsClosing
            && portal.animState != PORTAL_ANIM_CLOSING
            && portal.animState != PORTAL_ANIM_CLOSED;
    }

    float SignedDistanceToPortal(const PortalInfo_t& portal, const Vector& point)
    {
        return (point - portal.origin).Dot(portal.normal);
    }

    const char* BoolText(bool value)
    {
        return value ? "true" : "false";
    }

    Vector BuildCommandMoveDirection(const CUserCmd& cmd)
    {
        QAngle viewAngles;
        viewAngles.x = cmd.viewangles.x;
        viewAngles.y = cmd.viewangles.y;
        viewAngles.z = 0.0f;

        Vector forward;
        Vector right;
        Vector up;
        U::Math.AngleVectors(viewAngles, &forward, &right, &up);

        Vector move = forward * cmd.forwardmove + right * cmd.sidemove;
        move.z = 0.0f;
        return move;
    }

    bool IsFiniteVector(const Vector& value)
    {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    }

    float VectorLengthSqr(const Vector& value)
    {
        return value.x * value.x + value.y * value.y + value.z * value.z;
    }

    const char* MatchText(bool value)
    {
        return value ? "match" : "different";
    }

    const char* SimulatorPhaseName(PortalTransitionPhase phase)
    {
        switch (phase)
        {
        case PortalTransitionPhase::Idle: return "Idle";
        case PortalTransitionPhase::ApproachingPortal: return "ApproachingPortal";
        case PortalTransitionPhase::IntersectingPortal: return "IntersectingPortal";
        case PortalTransitionPhase::CommittingTeleport: return "CommittingTeleport";
        case PortalTransitionPhase::ExitingPortal: return "ExitingPortal";
        case PortalTransitionPhase::Cooldown: return "Cooldown";
        default: return "Unknown";
        }
    }

    const char* SimulatorSideName(PortalTransitionSide side)
    {
        switch (side)
        {
        case PortalTransitionSide::Blue: return "Blue";
        case PortalTransitionSide::Orange: return "Orange";
        case PortalTransitionSide::None:
        default:
            return "None";
        }
    }

    bool IsReadableAddressRange(const void* address, size_t size)
    {
        if (!address || size == 0)
            return false;

        const uintptr_t start = reinterpret_cast<uintptr_t>(address);
        const uintptr_t end = start + size - 1u;
        MEMORY_BASIC_INFORMATION info = {};
        if (!VirtualQuery(reinterpret_cast<const void*>(start), &info, sizeof(info)))
            return false;

        const bool firstReadable = info.State == MEM_COMMIT
            && !(info.Protect & PAGE_NOACCESS)
            && !(info.Protect & PAGE_GUARD);
        if (!firstReadable)
            return false;

        if (end < reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize)
            return true;

        MEMORY_BASIC_INFORMATION endInfo = {};
        if (!VirtualQuery(reinterpret_cast<const void*>(end), &endInfo, sizeof(endInfo)))
            return false;

        return endInfo.State == MEM_COMMIT
            && !(endInfo.Protect & PAGE_NOACCESS)
            && !(endInfo.Protect & PAGE_GUARD);
    }

    bool TryReadByteOffset(const void* base, uintptr_t offset, unsigned char* value)
    {
        if (!value)
            return false;

        const void* address = reinterpret_cast<const void*>(reinterpret_cast<uintptr_t>(base) + offset);
        if (!IsReadableAddressRange(address, sizeof(unsigned char)))
            return false;

        *value = *reinterpret_cast<const unsigned char*>(address);
        return true;
    }
}

void CPortalTransition::Reset()
{
    RestoreControlledMoveType(GetLocalPlayer(), "portal-transition-reset");
    m_blueState = {};
    m_orangeState = {};
    m_session = {};
    m_visualTransition = {};
    m_toneTransition = {};
    m_lastExitPortal = PortalSide::None;
    m_nextTeleportTime = 0.0f;
    m_nextCrossingLogTime = 0.0f;
    m_nextVisualPlaneLogTime = 0.0f;
    m_nextNearClipLogTime = 0.0f;
    m_nextEntryCameraLogTime = 0.0f;
    m_nextExitVisibilityLogTime = 0.0f;
    m_pendingEnvironmentRenderFrames = 0;
}

bool CPortalTransition::ApplyEntryCameraHandoff(
    C_BasePlayer* player,
    Vector& eyeOrigin,
    Vector& eyeAngles)
{
    const PortalTransitionContext& context =
        G::G_L4D2Portal.m_PortalTransitionSimulator.GetContext();

    PortalSide entrySide = PortalSide::None;
    if (context.entrySide == PortalTransitionSide::Blue)
        entrySide = PortalSide::Blue;
    else if (context.entrySide == PortalTransitionSide::Orange)
        entrySide = PortalSide::Orange;

    PortalInfo_t* entry = nullptr;
    PortalInfo_t* exit = nullptr;
    const bool portalsReady = ArePortalsReady()
        && TryGetPortalPair(entrySide, entry, exit)
        && entry
        && exit;
    const bool insideAperture = portalsReady
        && PortalTransform::IsPointInsideAperture(*entry, eyeOrigin, DefaultAperture());
    const float entrySignedDepth = portalsReady
        ? SignedDistanceToPortal(*entry, eyeOrigin)
        : 0.0f;

    if (!PortalTransitionDecision::ShouldApplyEntryCameraHandoff(
            PortalTransitionDecision::GetEntryCameraHandoffEnabled(),
            context.phase,
            IsLocalPlayer(player),
            G::PortalBspCollisionCarver.IsCarvingActive(),
            portalsReady,
            insideAperture,
            entrySignedDepth))
    {
        return false;
    }

    matrix3x4_t entryToExit;
    if (!PortalTransform::BuildEntryToExitMatrix(*entry, *exit, entryToExit))
        return false;

    const Vector transformedOrigin = PortalTransform::TransformPoint(entryToExit, eyeOrigin);
    QAngle sourceAngles;
    sourceAngles.x = eyeAngles.x;
    sourceAngles.y = eyeAngles.y;
    sourceAngles.z = eyeAngles.z;
    const QAngle transformedAngles = PortalTransform::TransformAngles(entryToExit, sourceAngles);
    const float exitSignedDepth = SignedDistanceToPortal(*exit, transformedOrigin);
    if (!IsFiniteVector(transformedOrigin)
        || !PortalTransitionDecision::IsPortalCommitHalfSpaceValid(
            entrySignedDepth,
            exitSignedDepth))
    {
        U::LogWarning("[PortalEntryViewHandoff] rejected=true phase=%s entry=%s exit=%s entryDepth=%.3f exitDepth=%.3f.\n",
            SimulatorPhaseName(context.phase),
            SideName(entrySide),
            SideName(entrySide == PortalSide::Blue ? PortalSide::Orange : PortalSide::Blue),
            entrySignedDepth,
            exitSignedDepth);
        return false;
    }

    const Vector originalOrigin = eyeOrigin;
    const Vector originalAngles = eyeAngles;
    eyeOrigin = transformedOrigin;
    eyeAngles.x = transformedAngles.x;
    eyeAngles.y = transformedAngles.y;
    eyeAngles.z = transformedAngles.z;

    const float currentTime = I::EngineClient ? I::EngineClient->OBSOLETE_Time() : 0.0f;
    if (!I::EngineClient || ShouldLog(currentTime, m_nextEntryCameraLogTime, 0.02f))
    {
        U::LogDebug("[PortalEntryViewHandoff] applied=true phase=%s entry=%s exit=%s time=%.3f entryDepth=%.3f exitDepth=%.3f insideAperture=true origin=(%.2f %.2f %.2f)->(%.2f %.2f %.2f) angles=(%.2f %.2f %.2f)->(%.2f %.2f %.2f).\n",
            SimulatorPhaseName(context.phase),
            SideName(entrySide),
            SideName(entrySide == PortalSide::Blue ? PortalSide::Orange : PortalSide::Blue),
            currentTime,
            entrySignedDepth,
            exitSignedDepth,
            originalOrigin.x, originalOrigin.y, originalOrigin.z,
            eyeOrigin.x, eyeOrigin.y, eyeOrigin.z,
            originalAngles.x, originalAngles.y, originalAngles.z,
            eyeAngles.x, eyeAngles.y, eyeAngles.z);
    }
    return true;
}

void CPortalTransition::ApplyVisualTransition(CViewSetup& view)
{
    if (kEnableToneTransition && m_toneTransition.active && I::EngineClient && I::MaterialSystem)
    {
        IMatRenderContext* renderContext = I::MaterialSystem->GetRenderContext();
        const float currentTime = I::EngineClient->OBSOLETE_Time();
        if (!renderContext || currentTime >= m_toneTransition.endTime)
        {
            if (m_toneTransition.loggedStart)
                U::LogInfo("[PortalTone] transition expired.\n");
            m_toneTransition = {};
        }
        else
        {
            renderContext->SetToneMappingScaleLinear(m_toneTransition.targetScale);

            if (!m_toneTransition.loggedStart)
            {
                U::LogInfo("[PortalTone] applying tone clamp start=(%.3f %.3f %.3f) target=(%.3f %.3f %.3f) duration=%.3f.\n",
                    m_toneTransition.startScale.x,
                    m_toneTransition.startScale.y,
                    m_toneTransition.startScale.z,
                    m_toneTransition.targetScale.x,
                    m_toneTransition.targetScale.y,
                    m_toneTransition.targetScale.z,
                    std::max(0.001f, m_toneTransition.endTime - m_toneTransition.startTime));
                m_toneTransition.loggedStart = true;
            }
        }
    }

    if (kEnableToneTransition && m_toneTransition.pending && I::EngineClient && I::MaterialSystem)
    {
        IMatRenderContext* renderContext = I::MaterialSystem->GetRenderContext();
        const float currentTime = I::EngineClient->OBSOLETE_Time();
        if (!renderContext || currentTime >= m_toneTransition.pendingUntil)
        {
            if (renderContext)
            {
                const Vector currentScale = renderContext->GetToneMappingScaleLinear();
                U::LogInfo("[PortalTone] skipped reason=%s renderCurrent=(%.3f %.3f %.3f) trigger=%.3f.\n",
                    m_toneTransition.reason ? m_toneTransition.reason : "unknown",
                    currentScale.x,
                    currentScale.y,
                    currentScale.z,
                    kToneTransitionTriggerScale);
            }
            m_toneTransition = {};
        }
        else
        {
            const Vector currentScale = renderContext->GetToneMappingScaleLinear();
            const float maxScale = std::max(currentScale.x, std::max(currentScale.y, currentScale.z));
            if (maxScale >= kToneTransitionTriggerScale)
            {
                m_toneTransition.pending = false;
                m_toneTransition.active = true;
                m_toneTransition.loggedStart = false;
                m_toneTransition.startTime = currentTime;
                m_toneTransition.endTime = currentTime + std::max(0.001f, kToneTransitionDuration);
                m_toneTransition.startScale = currentScale;
                m_toneTransition.targetScale = currentScale * kToneTransitionTargetMultiplier;
                renderContext->SetToneMappingScaleLinear(m_toneTransition.targetScale);

                U::LogInfo("[PortalTone] armed reason=%s renderCurrent=(%.3f %.3f %.3f) target=(%.3f %.3f %.3f) duration=%.3f trigger=%.3f.\n",
                    m_toneTransition.reason ? m_toneTransition.reason : "unknown",
                    m_toneTransition.startScale.x,
                    m_toneTransition.startScale.y,
                    m_toneTransition.startScale.z,
                    m_toneTransition.targetScale.x,
                    m_toneTransition.targetScale.y,
                    m_toneTransition.targetScale.z,
                    kToneTransitionDuration,
                    kToneTransitionTriggerScale);
            }
        }
    }

    if (!kEnableVisualTransition || !m_visualTransition.active || !I::EngineClient)
        return;

    const float currentTime = I::EngineClient->OBSOLETE_Time();
    if (currentTime >= m_visualTransition.endTime)
    {
        U::LogDebug("[PortalTransition][Visual] Compensation expired exit=%s physicalD=%.2f visualD=%.2f.\n",
            SideName(m_visualTransition.exitSide),
            m_visualTransition.physicalExitDistance,
            m_visualTransition.visualExitDistance);
        m_visualTransition = {};
        return;
    }

    const float duration = std::max(0.001f, m_visualTransition.endTime - m_visualTransition.startTime);
    const float elapsed = U::Math.Clamp(currentTime - m_visualTransition.startTime, 0.0f, duration);
    const float fade = elapsed / duration;
    const float offsetDistance = std::max(0.0f, m_visualTransition.physicalExitDistance - m_visualTransition.visualExitDistance);
    const float remainingOffset = offsetDistance * (1.0f - fade);
    if (remainingOffset <= 0.01f)
        return;

    const Vector visualOffset = m_visualTransition.exitNormal * -remainingOffset;
    view.origin += visualOffset;

    if (!m_visualTransition.loggedStart)
    {
        U::LogInfo("[PortalTransition][Visual] Applying view compensation exit=%s physicalD=%.2f visualD=%.2f offset=%.2f duration=%.3f viewOrigin=(%.1f %.1f %.1f).\n",
            SideName(m_visualTransition.exitSide),
            m_visualTransition.physicalExitDistance,
            m_visualTransition.visualExitDistance,
            remainingOffset,
            duration,
            view.origin.x, view.origin.y, view.origin.z);
        m_visualTransition.loggedStart = true;
    }
}

void CPortalTransition::ApplyPortalNearClipFix(CViewSetup& view)
{
    if (!PortalTransitionDecision::GetPortalNearClipFixEnabled()
        || !G::PortalBspCollisionCarver.IsCarvingActive()
        || !I::EngineClient
        || !ArePortalsReady())
    {
        return;
    }

    const PortalTransitionContext& context =
        G::G_L4D2Portal.m_PortalTransitionSimulator.GetContext();
    const bool entryHandoff = context.phase == PortalTransitionPhase::ApproachingPortal
        || context.phase == PortalTransitionPhase::IntersectingPortal;
    const bool exitHandoff = context.phase == PortalTransitionPhase::ExitingPortal
        || context.phase == PortalTransitionPhase::Cooldown;
    if (!entryHandoff && !exitHandoff)
        return;

    if (exitHandoff
        && (!PortalTransitionDecision::GetExactExitVisualGuardEnabled()
            || PortalTransitionDecision::GetExitClearanceMode() != PortalExitClearanceMode::ExactTransform))
    {
        return;
    }

    const PortalTransitionSide selectedSide = exitHandoff ? context.exitSide : context.entrySide;
    const PortalInfo_t* portal = nullptr;
    if (selectedSide == PortalTransitionSide::Blue)
        portal = &G::G_L4D2Portal.g_BluePortal;
    else if (selectedSide == PortalTransitionSide::Orange)
        portal = &G::G_L4D2Portal.g_OrangePortal;
    if (!portal || !IsPortalOpenForTransition(*portal))
        return;

    const PortalTransform::PortalLocalPoint local =
        PortalTransform::WorldToPortalLocal(*portal, view.origin);
    const float signedDepth = SignedDistanceToPortal(*portal, view.origin);
    const bool insideAperture =
        PortalTransform::IsPointInsideAperture(*portal, view.origin, DefaultAperture());
    constexpr float kTargetNearClip = 1.0f;
    const float activationDepth = exitHandoff
        ? std::max(18.0f, view.zNear + 11.0f)
        : std::max(9.0f, view.zNear + 2.0f);
    const float originalNearClip = view.zNear;
    const float appliedNearClip = exitHandoff
        ? PortalTransitionDecision::ComputePortalAwareNearClipForPhase(
            context.phase,
            true,
            insideAperture,
            signedDepth,
            originalNearClip,
            activationDepth,
            kTargetNearClip)
        : PortalTransitionDecision::ComputePortalAwareNearClip(
            true,
            insideAperture,
            signedDepth,
            originalNearClip,
            activationDepth,
            kTargetNearClip);
    if (appliedNearClip >= originalNearClip)
        return;

    // Only the main world view near plane is changed. zNearViewmodel remains
    // untouched so this experiment cannot alter first-person model projection.
    view.zNear = appliedNearClip;

    const float currentTime = I::EngineClient->OBSOLETE_Time();
    if (ShouldLog(currentTime, m_nextNearClipLogTime, 0.05f))
    {
        U::LogDebug("[PortalNearClipFix] phase=%s handoff=%s portal=%s time=%.3f signedDepth=%.3f local=(f=%.3f r=%.3f u=%.3f) insideAperture=%s originalZNear=%.3f appliedZNear=%.3f activationDepth=%.3f clearanceMode=%s.\n",
            SimulatorPhaseName(context.phase),
            exitHandoff ? "exit" : "entry",
            SimulatorSideName(selectedSide),
            currentTime,
            signedDepth,
            local.forward, local.right, local.up,
            BoolText(insideAperture),
            originalNearClip,
            appliedNearClip,
            activationDepth,
            PortalTransitionDecision::ExitClearanceModeName(
                PortalTransitionDecision::GetExitClearanceMode()));
    }
}

bool CPortalTransition::TryGetExactExitVisibilityOrigin(
    const Vector& viewOrigin,
    bool isMainView,
    Vector* safeOrigin,
    float* signedDepthOut,
    int* safeLeafOut)
{
    if (!safeOrigin
        || !G::PortalBspCollisionCarver.IsCarvingActive()
        || !I::EngineClient
        || !I::EngineTrace
        || !ArePortalsReady())
    {
        return false;
    }

    const PortalTransitionContext& context =
        G::G_L4D2Portal.m_PortalTransitionSimulator.GetContext();
    const PortalInfo_t* exit = nullptr;
    if (context.exitSide == PortalTransitionSide::Blue)
        exit = &G::G_L4D2Portal.g_BluePortal;
    else if (context.exitSide == PortalTransitionSide::Orange)
        exit = &G::G_L4D2Portal.g_OrangePortal;
    if (!exit || !IsPortalOpenForTransition(*exit))
        return false;

    constexpr float kVisibilityGuardDepth = 24.0f;
    const float signedDepth = SignedDistanceToPortal(*exit, viewOrigin);
    const bool insideAperture =
        PortalTransform::IsPointInsideAperture(*exit, viewOrigin, DefaultAperture());
    if (!PortalTransitionDecision::ShouldUseExactExitVisualGuard(
            PortalTransitionDecision::GetExactExitVisualGuardEnabled(),
            context.phase,
            PortalTransitionDecision::GetExitClearanceMode(),
            isMainView,
            insideAperture,
            signedDepth,
            kVisibilityGuardDepth))
    {
        return false;
    }

    constexpr float kSafeOffsets[] = { 25.0f, 50.0f, 1.0f };
    int safeLeaf = -1;
    for (float offset : kSafeOffsets)
    {
        const Vector candidate = exit->origin + exit->normal * offset;
        const int leaf = I::EngineTrace->GetLeafContainingPoint(candidate);
        if (leaf <= 0)
            continue;

        *safeOrigin = candidate;
        safeLeaf = leaf;
        break;
    }
    if (safeLeaf <= 0)
        return false;

    if (signedDepthOut)
        *signedDepthOut = signedDepth;
    if (safeLeafOut)
        *safeLeafOut = safeLeaf;

    const float currentTime = I::EngineClient->OBSOLETE_Time();
    if (ShouldLog(currentTime, m_nextExitVisibilityLogTime, 0.05f))
    {
        const int viewLeaf = I::EngineTrace->GetLeafContainingPoint(viewOrigin);
        U::LogDebug("[PortalExitVisibility] phase=%s exit=%s time=%.3f view=(%.2f %.2f %.2f) viewLeaf=%d signedDepth=%.3f safe=(%.2f %.2f %.2f) safeLeaf=%d appended=true clearanceMode=%s.\n",
            SimulatorPhaseName(context.phase),
            SimulatorSideName(context.exitSide),
            currentTime,
            viewOrigin.x, viewOrigin.y, viewOrigin.z,
            viewLeaf,
            signedDepth,
            safeOrigin->x, safeOrigin->y, safeOrigin->z,
            safeLeaf,
            PortalTransitionDecision::ExitClearanceModeName(
                PortalTransitionDecision::GetExitClearanceMode()));
    }
    return true;
}

void CPortalTransition::LogRenderEnvironmentSnapshot(const char* phase, const CViewSetup& view)
{
    if (m_pendingEnvironmentRenderFrames <= 0)
        return;

    C_TerrorPlayer* player = GetLocalPlayer();
    LogEnvironmentSnapshot(phase ? phase : "main-render", player, view.origin, Vector(view.angles.x, view.angles.y, view.angles.z));
    --m_pendingEnvironmentRenderFrames;
}

void CPortalTransition::LogVisualPlaneProbe(const CViewSetup& view)
{
    if (!G::PortalBspCollisionCarver.IsCarvingActive()
        || !I::EngineClient
        || !ArePortalsReady())
    {
        return;
    }

    constexpr float kProbeDepth = 48.0f;
    constexpr float kProbeHalfWidth = 38.0f;
    constexpr float kProbeHalfHeight = 62.0f;
    const PortalInfo_t& blue = G::G_L4D2Portal.g_BluePortal;
    const PortalInfo_t& orange = G::G_L4D2Portal.g_OrangePortal;
    const PortalTransform::PortalLocalPoint blueLocal =
        PortalTransform::WorldToPortalLocal(blue, view.origin);
    const PortalTransform::PortalLocalPoint orangeLocal =
        PortalTransform::WorldToPortalLocal(orange, view.origin);
    const float blueDepth = SignedDistanceToPortal(blue, view.origin);
    const float orangeDepth = SignedDistanceToPortal(orange, view.origin);
    const bool blueNear = PortalTransitionDecision::IsViewNearPortalPlane(
        blueDepth, blueLocal.right, blueLocal.up,
        kProbeDepth, kProbeHalfWidth, kProbeHalfHeight);
    const bool orangeNear = PortalTransitionDecision::IsViewNearPortalPlane(
        orangeDepth, orangeLocal.right, orangeLocal.up,
        kProbeDepth, kProbeHalfWidth, kProbeHalfHeight);
    if (!blueNear && !orangeNear)
        return;

    const float currentTime = I::EngineClient->OBSOLETE_Time();
    if (!ShouldLog(currentTime, m_nextVisualPlaneLogTime, 0.05f))
        return;

    const bool chooseBlue = blueNear
        && (!orangeNear || std::fabs(blueDepth) <= std::fabs(orangeDepth));
    const PortalTransform::PortalLocalPoint& local = chooseBlue ? blueLocal : orangeLocal;
    const float signedDepth = chooseBlue ? blueDepth : orangeDepth;
    const PortalTransitionContext& context =
        G::G_L4D2Portal.m_PortalTransitionSimulator.GetContext();

    U::LogDebug("[PortalVisualPlaneProbe] phase=%s trackedEntry=%s nearest=%s clearanceMode=%s carvingActive=true time=%.3f viewOrigin=(%.2f %.2f %.2f) signedDepth=%.3f local=(f=%.3f r=%.3f u=%.3f) zNear=%.3f fov=%.2f bloomTone=%s.\n",
        SimulatorPhaseName(context.phase),
        SimulatorSideName(context.entrySide),
        chooseBlue ? "Blue" : "Orange",
        PortalTransitionDecision::ExitClearanceModeName(
            PortalTransitionDecision::GetExitClearanceMode()),
        currentTime,
        view.origin.x, view.origin.y, view.origin.z,
        signedDepth,
        local.forward, local.right, local.up,
        view.zNear,
        view.fov,
        BoolText(view.m_bDoBloomAndToneMapping));
}

void CPortalTransition::ArmEnvironmentRenderTrace(int frameCount)
{
    if (frameCount <= 0)
        return;

    m_pendingEnvironmentRenderFrames = std::max(m_pendingEnvironmentRenderFrames, frameCount);
}

void CPortalTransition::ArmToneTransition(const char* reason)
{
    if (!kEnableToneTransition || !I::EngineClient)
        return;

    m_toneTransition = {};
    m_toneTransition.pending = true;
    m_toneTransition.loggedStart = false;
    m_toneTransition.startTime = 0.0f;
    m_toneTransition.pendingUntil = I::EngineClient->OBSOLETE_Time() + std::max(0.001f, kToneTransitionPendingDuration);
    m_toneTransition.reason = reason;

    U::LogInfo("[PortalTone] pending reason=%s pendingDuration=%.3f trigger=%.3f.\n",
        reason ? reason : "unknown",
        kToneTransitionPendingDuration,
        kToneTransitionTriggerScale);
}

bool CPortalTransition::ShouldLogPortalRenderState(const char* phase, int depth) const
{
    if (m_pendingEnvironmentRenderFrames <= 0 || !phase)
        return false;

    if (depth < 1 || depth > 2)
        return false;

    return std::strstr(phase, "before-draw") || std::strstr(phase, "after-pop-rt");
}

void CPortalTransition::LogHookProbe(const char* domain, const char* stage, const char* point, void* gameMovement, CMoveData* move)
{
    const PortalTransitionContext& context = G::G_L4D2Portal.m_PortalTransitionSimulator.GetContext();
    const Vector origin = move ? move->GetAbsOrigin() : Vector();
    const Vector velocity = move ? move->m_vecVelocity : Vector();

    U::LogInfo("[PortalHookProbe] domain=%s stage=%s point=%s phase=%s entry=%s bridge=%s gm=%p mv=%p origin=(%.2f %.2f %.2f) vel=(%.2f %.2f %.2f) buttons=0x%X fmove=%.2f smove=%.2f step=%.2f gameMoved=%s.\n",
        domain ? domain : "unknown",
        stage ? stage : "unknown",
        point ? point : "unknown",
        SimulatorPhaseName(context.phase),
        SimulatorSideName(context.entrySide),
        BoolText(G::G_L4D2Portal.m_PortalTransitionSimulator.IsInCollisionBridgePhase()),
        gameMovement,
        move,
        origin.x, origin.y, origin.z,
        velocity.x, velocity.y, velocity.z,
        move ? move->m_nButtons : 0,
        move ? move->m_flForwardMove : 0.0f,
        move ? move->m_flSideMove : 0.0f,
        move ? move->m_outStepHeight : 0.0f,
        BoolText(move ? move->m_bGameCodeMovedPlayer : false));
}

void CPortalTransition::LogMoveTypeProbe(const char* phase, C_BasePlayer* basePlayer, CMoveData* move)
{
    C_TerrorPlayer* player = basePlayer && IsLocalPlayer(basePlayer)
        ? static_cast<C_TerrorPlayer*>(basePlayer)
        : GetLocalPlayer();

    const int localIndex = I::EngineClient ? I::EngineClient->GetLocalPlayer() : -1;
    IClientEntity* clientLocal = I::ClientEntityList && localIndex > 0 ? I::ClientEntityList->GetClientEntity(localIndex) : nullptr;

    void* toolsBase = nullptr;
    void* edictBase = nullptr;
    bool resolverAgree = false;
    void* serverBase = ResolveServerLocalPlayerForDiagnostics(&toolsBase, &edictBase, &resolverAgree);

    unsigned char clientMoveType = 0;
    const bool hasClientMoveType = player != nullptr;
    if (player)
        clientMoveType = player->m_MoveType();

    unsigned char serverMoveType = 0;
    const bool hasRawServerMoveType = TryReadByteOffset(serverBase, 0x144u, &serverMoveType);
    const auto datamapMoveType = L4D2::ServerDataMap::ProbeMoveType(static_cast<CBaseEntity*>(serverBase));
    MaybeRunServerMoveTypeWriteDryRun(serverBase, phase);

    Vector toolsOrigin;
    QAngle toolsAngles;
    bool toolsPositionOk = false;
    bool toolsNoClip = false;
    if (I::CServerTools)
    {
        toolsPositionOk = I::CServerTools->GetPlayerPosition(toolsOrigin, toolsAngles, clientLocal);
        toolsNoClip = I::CServerTools->IsInNoClipMode(clientLocal);
    }

    const PlayerAnchor anchor = player ? BuildPlayerAnchor(player) : PlayerAnchor();
    const Vector moveOrigin = move ? move->GetAbsOrigin() : Vector();
    const Vector moveVelocity = move ? move->m_vecVelocity : Vector();

    U::LogInfo("[PortalMoveTypeProbe] phase=%s localIndex=%d clientPlayer=%p clientLocal=%p serverChosen=%p toolsBase=%p edictBase=%p resolver=%s clientMoveType=%s:%u serverMoveTypeRaw144=%s:%u serverDataMapMoveType=%s:%d byte=%u reason=%s rootClass=%s fieldClass=%s offset=%d type=%d fieldSize=%u fieldBytes=%d dataMap=%p field=%p toolsNoClip=%s toolsPos=%s toolsOrigin=(%.2f %.2f %.2f) toolsAngles=(%.2f %.2f %.2f) playerOrigin=(%.2f %.2f %.2f) playerEye=(%.2f %.2f %.2f) playerVel=(%.2f %.2f %.2f) mv=%p mvOrigin=(%.2f %.2f %.2f) mvVel=(%.2f %.2f %.2f).\n",
        phase ? phase : "unknown",
        localIndex,
        player,
        clientLocal,
        serverBase,
        toolsBase,
        edictBase,
        MatchText(resolverAgree),
        hasClientMoveType ? "ok" : "missing",
        static_cast<unsigned int>(clientMoveType),
        hasRawServerMoveType ? "ok" : "missing",
        static_cast<unsigned int>(serverMoveType),
        datamapMoveType.ok ? "ok" : "missing",
        datamapMoveType.valueInt,
        static_cast<unsigned int>(datamapMoveType.valueByte),
        datamapMoveType.reason ? datamapMoveType.reason : "unknown",
        datamapMoveType.rootClassName ? datamapMoveType.rootClassName : "unknown",
        datamapMoveType.field.ownerClassName ? datamapMoveType.field.ownerClassName : "unknown",
        datamapMoveType.field.actualOffset,
        static_cast<int>(datamapMoveType.field.fieldType),
        static_cast<unsigned int>(datamapMoveType.field.fieldSize),
        datamapMoveType.field.fieldSizeInBytes,
        datamapMoveType.dataMap,
        datamapMoveType.field.typedesc,
        BoolText(toolsNoClip),
        BoolText(toolsPositionOk),
        toolsOrigin.x, toolsOrigin.y, toolsOrigin.z,
        toolsAngles.x, toolsAngles.y, toolsAngles.z,
        anchor.origin.x, anchor.origin.y, anchor.origin.z,
        anchor.eye.x, anchor.eye.y, anchor.eye.z,
        anchor.velocity.x, anchor.velocity.y, anchor.velocity.z,
        move,
        moveOrigin.x, moveOrigin.y, moveOrigin.z,
        moveVelocity.x, moveVelocity.y, moveVelocity.z);
}

void CPortalTransition::MaybeRunServerMoveTypeWriteDryRun(void* serverBase, const char* phase)
{
    if (!PortalPhysicsMode::ShouldRunDestructiveDiagnostics())
        return;

    if (m_serverMoveTypeWriteDryRunDone || !serverBase)
        return;

    auto before = L4D2::ServerDataMap::ProbeMoveType(static_cast<CBaseEntity*>(serverBase));
    if (!before.ok)
    {
        U::LogInfo("[PortalServerMoveType][DryRun] phase=%s skipped reason=%s entity=%p.\n",
            phase ? phase : "unknown",
            before.reason ? before.reason : "unknown",
            serverBase);
        return;
    }

    m_serverMoveTypeWriteDryRunDone = true;

    const int savedValue = before.valueInt;
    const int targetValue = MOVETYPE_NOCLIP;
    const bool writeTargetOk = L4D2::ServerDataMap::TryWriteFieldInt(static_cast<CBaseEntity*>(serverBase), before.field, targetValue);
    const auto afterTarget = L4D2::ServerDataMap::ProbeMoveType(static_cast<CBaseEntity*>(serverBase));
    const bool restoreOk = L4D2::ServerDataMap::TryWriteFieldInt(static_cast<CBaseEntity*>(serverBase), before.field, savedValue);
    const auto afterRestore = L4D2::ServerDataMap::ProbeMoveType(static_cast<CBaseEntity*>(serverBase));

    bool toolsNoClipAfterRestore = false;
    if (I::CServerTools && I::EngineClient && I::ClientEntityList)
    {
        const int localIndex = I::EngineClient->GetLocalPlayer();
        IClientEntity* clientLocal = localIndex > 0 ? I::ClientEntityList->GetClientEntity(localIndex) : nullptr;
        toolsNoClipAfterRestore = I::CServerTools->IsInNoClipMode(clientLocal);
    }

    U::LogInfo("[PortalServerMoveType][DryRun] phase=%s entity=%p offset=%d type=%d saved=%d target=%d writeTarget=%s afterTarget=%s:%d restore=%s afterRestore=%s:%d toolsNoClipAfterRestore=%s fieldClass=%s dataMap=%p field=%p.\n",
        phase ? phase : "unknown",
        serverBase,
        before.field.actualOffset,
        static_cast<int>(before.field.fieldType),
        savedValue,
        targetValue,
        BoolText(writeTargetOk),
        afterTarget.ok ? "ok" : (afterTarget.reason ? afterTarget.reason : "missing"),
        afterTarget.valueInt,
        BoolText(restoreOk),
        afterRestore.ok ? "ok" : (afterRestore.reason ? afterRestore.reason : "missing"),
        afterRestore.valueInt,
        BoolText(toolsNoClipAfterRestore),
        before.field.ownerClassName ? before.field.ownerClassName : "unknown",
        before.dataMap,
        before.field.typedesc);
}

bool CPortalTransition::EnterControlledNoclip(C_TerrorPlayer* player, const char* reason)
{
    // Keep the destructive movement mutation disabled in BSP traversal mode even
    // if a future caller reaches this legacy entry point directly.
    if (!PortalPhysicsMode::ShouldMutatePlayerMovement())
        return false;

    if (m_controlledMoveType.active)
        return true;

    if (!player)
        player = GetLocalPlayer();
    if (!player)
    {
        U::LogInfo("[PortalEnterState] controlled-noclip rejected reason=%s detail=client-player-unavailable.\n",
            reason ? reason : "unknown");
        return false;
    }

    void* toolsBase = nullptr;
    void* edictBase = nullptr;
    bool resolverAgree = false;
    void* serverBase = ResolveServerLocalPlayerForDiagnostics(&toolsBase, &edictBase, &resolverAgree);
    const auto serverBefore = L4D2::ServerDataMap::ProbeMoveType(static_cast<CBaseEntity*>(serverBase));
    if (!serverBefore.ok)
    {
        U::LogInfo("[PortalEnterState] controlled-noclip rejected reason=%s detail=%s server=%p toolsBase=%p edictBase=%p resolver=%s.\n",
            reason ? reason : "unknown",
            serverBefore.reason ? serverBefore.reason : "server-movetype-unavailable",
            serverBase,
            toolsBase,
            edictBase,
            MatchText(resolverAgree));
        return false;
    }

    const unsigned char savedClientMoveType = player->m_MoveType();
    const int savedServerMoveType = serverBefore.valueInt;
    const bool serverWriteOk = L4D2::ServerDataMap::TryWriteFieldInt(
        static_cast<CBaseEntity*>(serverBase), serverBefore.field, MOVETYPE_NOCLIP);
    const auto serverAfter = L4D2::ServerDataMap::ProbeMoveType(static_cast<CBaseEntity*>(serverBase));
    if (!serverWriteOk || !serverAfter.ok || serverAfter.valueInt != MOVETYPE_NOCLIP)
    {
        const bool restoreOk = L4D2::ServerDataMap::TryWriteFieldInt(
            static_cast<CBaseEntity*>(serverBase), serverBefore.field, savedServerMoveType);
        U::LogInfo("[PortalEnterState] controlled-noclip failed reason=%s client=%u server=%d target=%d write=%s after=%s:%d rollback=%s serverEntity=%p.\n",
            reason ? reason : "unknown",
            static_cast<unsigned int>(savedClientMoveType),
            savedServerMoveType,
            MOVETYPE_NOCLIP,
            BoolText(serverWriteOk),
            serverAfter.ok ? "ok" : (serverAfter.reason ? serverAfter.reason : "missing"),
            serverAfter.valueInt,
            BoolText(restoreOk),
            serverBase);
        return false;
    }

    player->m_MoveType() = MOVETYPE_NOCLIP;
    m_controlledMoveType.active = true;
    m_controlledMoveType.savedClientMoveType = savedClientMoveType;
    m_controlledMoveType.savedServerMoveType = savedServerMoveType;

    U::LogInfo("[PortalEnterState] controlled-noclip entered reason=%s client=%u->%u server=%d->%d serverEntity=%p offset=%d type=%d resolver=%s.\n",
        reason ? reason : "unknown",
        static_cast<unsigned int>(savedClientMoveType),
        static_cast<unsigned int>(player->m_MoveType()),
        savedServerMoveType,
        serverAfter.valueInt,
        serverBase,
        serverBefore.field.actualOffset,
        static_cast<int>(serverBefore.field.fieldType),
        MatchText(resolverAgree));
    return true;
}

void CPortalTransition::RestoreControlledMoveType(C_TerrorPlayer* player, const char* reason)
{
    if (!m_controlledMoveType.active)
        return;

    const unsigned char savedClientMoveType = m_controlledMoveType.savedClientMoveType;
    const int savedServerMoveType = m_controlledMoveType.savedServerMoveType;
    if (!player)
        player = GetLocalPlayer();

    void* toolsBase = nullptr;
    void* edictBase = nullptr;
    bool resolverAgree = false;
    void* serverBase = ResolveServerLocalPlayerForDiagnostics(&toolsBase, &edictBase, &resolverAgree);
    const auto serverBefore = L4D2::ServerDataMap::ProbeMoveType(static_cast<CBaseEntity*>(serverBase));
    const bool serverRestoreOk = serverBefore.ok
        && L4D2::ServerDataMap::TryWriteFieldInt(
            static_cast<CBaseEntity*>(serverBase), serverBefore.field, savedServerMoveType);
    const auto serverAfter = L4D2::ServerDataMap::ProbeMoveType(static_cast<CBaseEntity*>(serverBase));

    const unsigned int clientBefore = player ? static_cast<unsigned int>(player->m_MoveType()) : 0u;
    if (player)
        player->m_MoveType() = savedClientMoveType;
    const unsigned int clientAfter = player ? static_cast<unsigned int>(player->m_MoveType()) : 0u;

    U::LogInfo("[PortalRestoreWalk] controlled-noclip restored reason=%s client=%s:%u->%u serverSaved=%d serverBefore=%s:%d restore=%s serverAfter=%s:%d serverEntity=%p resolver=%s.\n",
        reason ? reason : "unknown",
        player ? "ok" : "missing",
        clientBefore,
        clientAfter,
        savedServerMoveType,
        serverBefore.ok ? "ok" : (serverBefore.reason ? serverBefore.reason : "missing"),
        serverBefore.valueInt,
        BoolText(serverRestoreOk),
        serverAfter.ok ? "ok" : (serverAfter.reason ? serverAfter.reason : "missing"),
        serverAfter.valueInt,
        serverBase,
        MatchText(resolverAgree));

    m_controlledMoveType = {};
}

void CPortalTransition::Update(CUserCmd* cmd)
{
    if (!cmd || !I::EngineClient || !I::EngineClient->IsInGame())
    {
        Reset();
        return;
    }

    const float currentTime = I::EngineClient->OBSOLETE_Time();

    C_TerrorPlayer* player = GetLocalPlayer();
    if (!player)
    {
        if (ShouldLog(currentTime, m_nextStatusLogTime, 1.0f))
            U::LogDebug("[PortalTransition] Update skipped: local player is not available.\n");
        Reset();
        return;
    }

    if (!ArePortalsReady())
    {
        LogPortalReadiness(currentTime);
        Reset();
        return;
    }

    PortalInfo_t* blueEntry = nullptr;
    PortalInfo_t* blueExit = nullptr;
    PortalInfo_t* orangeEntry = nullptr;
    PortalInfo_t* orangeExit = nullptr;

    if (!TryGetPortalPair(PortalSide::Blue, blueEntry, blueExit)
        || !TryGetPortalPair(PortalSide::Orange, orangeEntry, orangeExit))
    {
        Reset();
        return;
    }

    LogDistanceProbe(currentTime, player, *blueEntry, *orangeEntry);
    UpdateTraversalExitState(player);

    if (currentTime < m_nextTeleportTime)
    {
        if (ShouldLog(currentTime, m_nextStatusLogTime, 0.5f))
            U::LogDebug("[PortalTransition] Update skipped: teleport cooldown active now=%.3f next=%.3f.\n",
                currentTime, m_nextTeleportTime);

        RefreshPortalDistance(player, PortalSide::Blue, *blueEntry);
        RefreshPortalDistance(player, PortalSide::Orange, *orangeEntry);
        return;
    }

    if (m_session.mode == TraversalMode::Normal)
    {
        if (TryBeginTraversal(player, cmd, PortalSide::Blue, *blueEntry, *blueExit))
            return;

        if (TryBeginTraversal(player, cmd, PortalSide::Orange, *orangeEntry, *orangeExit))
            return;
    }
    else if (m_session.mode == TraversalMode::InPortal)
    {
        PortalInfo_t* assistEntry = nullptr;
        PortalInfo_t* assistExit = nullptr;
        if (TryGetPortalPair(m_session.entrySide, assistEntry, assistExit) && assistEntry && assistExit)
        {
            if (AssistPortalEmbedding(player, cmd, *assistEntry, *assistExit))
                return;
        }
    }

    if (UpdatePortalCrossing(player, cmd, PortalSide::Blue, *blueEntry, *blueExit))
        return;

    UpdatePortalCrossing(player, cmd, PortalSide::Orange, *orangeEntry, *orangeExit);
}

void CPortalTransition::OnFinishMove(C_BasePlayer* basePlayer, CUserCmd* cmd, CMoveData* move)
{
    (void)cmd;

    if (!move || !IsLocalPlayer(basePlayer) || !ArePortalsReady())
        return;

    C_TerrorPlayer* player = static_cast<C_TerrorPlayer*>(basePlayer);
    if (!player || player->deadflag())
        return;

    static bool loggedMoveDataLayout = false;
    if (!loggedMoveDataLayout)
    {
        const PlayerAnchor anchor = BuildPlayerAnchor(player);
        const Vector originDelta = move->m_vecAbsOrigin - anchor.origin;
        const float originDeltaSqr = VectorLengthSqr(originDelta);
        const bool moveDataLooksValid = IsFiniteVector(move->m_vecAbsOrigin)
            && std::fabs(move->m_vecAbsOrigin.x) < 100000.0f
            && std::fabs(move->m_vecAbsOrigin.y) < 100000.0f
            && std::fabs(move->m_vecAbsOrigin.z) < 100000.0f
            && originDeltaSqr < 4096.0f;

        U::LogInfo("[PortalMoveTypeProbe][MoveDataLayout] sizeof=%u offVelocity=%u offAngles=%u offWishVel=%u offConstraintPastRadius=%u offAbsOrigin=%u samplePlayerOrigin=(%.1f %.1f %.1f) sampleMoveOrigin=(%.1f %.1f %.1f) originDelta=(%.1f %.1f %.1f) sampleMoveVelocity=(%.1f %.1f %.1f) valid=%s.\n",
            static_cast<unsigned int>(sizeof(CMoveData)),
            static_cast<unsigned int>(offsetof(CMoveData, m_vecVelocity)),
            static_cast<unsigned int>(offsetof(CMoveData, m_vecAngles)),
            static_cast<unsigned int>(offsetof(CMoveData, m_outWishVel)),
            static_cast<unsigned int>(offsetof(CMoveData, m_bConstraintPastRadius)),
            static_cast<unsigned int>(offsetof(CMoveData, m_vecAbsOrigin)),
            anchor.origin.x, anchor.origin.y, anchor.origin.z,
            move->m_vecAbsOrigin.x, move->m_vecAbsOrigin.y, move->m_vecAbsOrigin.z,
            originDelta.x, originDelta.y, originDelta.z,
            move->m_vecVelocity.x, move->m_vecVelocity.y, move->m_vecVelocity.z,
            BoolText(moveDataLooksValid));
        loggedMoveDataLayout = true;
    }

    static float nextFinishMoveProbeTime = 0.0f;
    const float finishMoveProbeTime = I::EngineClient ? I::EngineClient->OBSOLETE_Time() : 0.0f;
    if (ShouldLog(finishMoveProbeTime, nextFinishMoveProbeTime, 0.35f))
        LogMoveTypeProbe("finishmove", player, move);

    if (m_session.mode != TraversalMode::InPortal)
        return;

    PortalInfo_t* entry = nullptr;
    PortalInfo_t* exit = nullptr;
    if (!TryGetPortalPair(m_session.entrySide, entry, exit) || !entry || !exit)
        return;

    const PlayerAnchor anchor = BuildPlayerAnchor(player);
    const float eyeDistance = SignedDistanceToPortal(*entry, anchor.eye);
    const bool insideAperture = IsPlayerInsidePortalAperture(player, *entry, anchor);
    const bool moveDataLooksValid = IsFiniteVector(move->m_vecAbsOrigin)
        && std::fabs(move->m_vecAbsOrigin.x) < 100000.0f
        && std::fabs(move->m_vecAbsOrigin.y) < 100000.0f
        && std::fabs(move->m_vecAbsOrigin.z) < 100000.0f;

    const float currentTime = I::EngineClient ? I::EngineClient->OBSOLETE_Time() : 0.0f;
    if (ShouldLog(currentTime, m_session.nextLogTime, 0.20f))
    {
        U::LogDebug("[PortalTransition] FinishMove traversal entry=%s eyeD=%.2f inside=%s playerOrigin=(%.1f %.1f %.1f) moveOrigin=(%.1f %.1f %.1f) moveDataValid=%s vel=(%.1f %.1f %.1f).\n",
            SideName(m_session.entrySide), eyeDistance, BoolText(insideAperture),
            anchor.origin.x, anchor.origin.y, anchor.origin.z,
            move->m_vecAbsOrigin.x, move->m_vecAbsOrigin.y, move->m_vecAbsOrigin.z,
            BoolText(moveDataLooksValid),
            move->m_vecVelocity.x, move->m_vecVelocity.y, move->m_vecVelocity.z);
    }

    ClampMoveToPortalAperture(player, move, *entry);
}

bool CPortalTransition::ShouldBypassPlayerBBoxTrace(
    const Vector& start,
    const Vector& end,
    unsigned int mask,
    int collisionGroup,
    trace_t* trace)
{
    (void)mask;
    (void)collisionGroup;

    // BSP traversal must prove clearance through the live BSP mutation.  Never
    // manufacture a clear trace in this mode.
    if (!PortalPhysicsMode::ShouldUseLegacyCollisionBypass())
        return false;

    if (!trace)
        return false;

    if (trace->fraction >= 1.0f || trace->startsolid || trace->allsolid)
        return false;

    if (!I::EngineClient || !I::EngineClient->IsInGame())
        return false;

    const float currentTime = I::EngineClient->OBSOLETE_Time();

    if (!ArePortalsReady())
    {
        if (ShouldLog(currentTime, m_nextTraceLogTime, 1.0f))
            U::LogDebug("[PortalTransition] TracePlayerBBox blocked but portals are not ready. fraction=%.3f startsolid=%s allsolid=%s.\n",
                trace->fraction, BoolText(trace->startsolid), BoolText(trace->allsolid));
        return false;
    }

    const Vector delta = end - start;
    if (delta.IsZero(0.001f))
        return false;

    PortalInfo_t* entry = nullptr;
    PortalInfo_t* exit = nullptr;

    for (PortalSide side : { PortalSide::Blue, PortalSide::Orange })
    {
        if (!TryGetPortalPair(side, entry, exit) || !entry || !exit)
            continue;

        const float startDistance = SignedDistanceToPortal(*entry, start);
        const float endDistance = SignedDistanceToPortal(*entry, end);
        const bool closeToPortal = std::fabs(startDistance) < 128.0f || std::fabs(endDistance) < 128.0f;
        Vector intersection;
        const bool crossingAperture = IsPointCrossingPortalAperture(*entry, start, end, &intersection);
        const bool entrySessionAperture =
            m_session.mode == TraversalMode::InPortal
            && m_session.entrySide == side
            && PortalTransform::IsPointInsideAperture(*entry, end, DefaultAperture())
            && endDistance <= kPortalEnterDistance
            && endDistance >= -kPortalTransitBackDepth;
        const bool exitSessionAperture =
            m_session.mode == TraversalMode::ExitingPortal
            && m_session.exitSide == side
            && PortalTransform::IsPointInsideAperture(*entry, end, DefaultAperture())
            && endDistance <= kPortalEnterDistance
            && endDistance >= -kPortalTransitBackDepth;
        const bool sessionAperture = entrySessionAperture || exitSessionAperture;

        if (!crossingAperture && !sessionAperture)
        {
            if (closeToPortal && ShouldLog(currentTime, m_nextTraceLogTime, 0.25f))
                U::LogDebug("[PortalTransition] Trace probe rejected side=%s startD=%.2f endD=%.2f crossing=%s session=%s fraction=%.3f.\n",
                    SideName(side), startDistance, endDistance, BoolText(crossingAperture), BoolText(sessionAperture), trace->fraction);
            continue;
        }

        if (ShouldLog(currentTime, m_nextTraceLogTime, 0.25f))
            U::LogDebug("[PortalTransition] Trace portal aperture bypass side=%s crossing=%s session=%s startD=%.2f endD=%.2f hit=(%.1f %.1f %.1f).\n",
                SideName(side), BoolText(crossingAperture), BoolText(sessionAperture), startDistance, endDistance,
                intersection.x, intersection.y, intersection.z);

        trace->fraction = 1.0f;
        trace->allsolid = false;
        trace->startsolid = false;
        trace->endpos = end;
        trace->m_pEnt = nullptr;
        trace->contents = 0;

        U::LogInfo("[PortalTransition] Bypassed player bbox trace through %s portal aperture. start=(%.1f %.1f %.1f) end=(%.1f %.1f %.1f)\n",
            SideName(side), start.x, start.y, start.z, end.x, end.y, end.z);
        return true;
    }

    return false;
}

C_TerrorPlayer* CPortalTransition::GetLocalPlayer() const
{
    if (!I::EngineClient || !I::ClientEntityList)
        return nullptr;

    const int localIndex = I::EngineClient->GetLocalPlayer();
    if (localIndex <= 0)
        return nullptr;

    IClientEntity* entity = I::ClientEntityList->GetClientEntity(localIndex);
    if (!entity)
        return nullptr;

    C_TerrorPlayer* player = entity->As<C_TerrorPlayer*>();
    if (!player || player->deadflag())
        return nullptr;

    return player;
}

bool CPortalTransition::ArePortalsReady() const
{
    return IsPortalOpenForTransition(G::G_L4D2Portal.g_BluePortal)
        && IsPortalOpenForTransition(G::G_L4D2Portal.g_OrangePortal);
}

bool CPortalTransition::TryGetPortalPair(PortalSide entrySide, PortalInfo_t*& entry, PortalInfo_t*& exit) const
{
    switch (entrySide)
    {
    case PortalSide::Blue:
        entry = &G::G_L4D2Portal.g_BluePortal;
        exit = &G::G_L4D2Portal.g_OrangePortal;
        return true;
    case PortalSide::Orange:
        entry = &G::G_L4D2Portal.g_OrangePortal;
        exit = &G::G_L4D2Portal.g_BluePortal;
        return true;
    default:
        entry = nullptr;
        exit = nullptr;
        return false;
    }
}

CPortalTransition::PortalRuntimeState& CPortalTransition::RuntimeStateForSide(PortalSide side)
{
    return side == PortalSide::Blue ? m_blueState : m_orangeState;
}

const CPortalTransition::PortalRuntimeState& CPortalTransition::RuntimeStateForSide(PortalSide side) const
{
    return side == PortalSide::Blue ? m_blueState : m_orangeState;
}

CPortalTransition::PlayerAnchor CPortalTransition::BuildPlayerAnchor(C_TerrorPlayer* player) const
{
    PlayerAnchor anchor;
    if (!player)
        return anchor;

    anchor.origin = player->m_vecOrigin();
    anchor.eye = player->EyePosition();
    anchor.viewOffset = player->m_vecViewOffset();
    anchor.center = player->WorldSpaceCenter();
    anchor.velocity = player->m_vecVelocity();
    return anchor;
}

bool CPortalTransition::IsLocalPlayer(C_BasePlayer* player) const
{
    return player && player == GetLocalPlayer();
}

bool CPortalTransition::IsPlayerInsidePortalAperture(C_TerrorPlayer* player, PortalInfo_t& portal, const PlayerAnchor& anchor) const
{
    if (!player)
        return false;

    const PortalTransform::PortalAperture aperture = DefaultAperture();
    const Vector feet = anchor.origin + Vector(0.0f, 0.0f, 8.0f);
    const bool eyeInside = PortalTransform::IsPointInsideAperture(portal, anchor.eye, aperture);
    const bool centerInside = PortalTransform::IsPointInsideAperture(portal, anchor.center, aperture);
    const bool feetInside = PortalTransform::IsPointInsideAperture(portal, feet, aperture);

    const float eyeDistance = SignedDistanceToPortal(portal, anchor.eye);
    const float centerDistance = SignedDistanceToPortal(portal, anchor.center);
    const bool nearPlane = std::fabs(eyeDistance) <= kPortalEnterDistance || std::fabs(centerDistance) <= kPortalEnterDistance;

    return nearPlane && (eyeInside || (centerInside && feetInside));
}

bool CPortalTransition::IsPointCrossingPortalAperture(const PortalInfo_t& portal, const Vector& start, const Vector& end, Vector* intersection) const
{
    const float startDistance = SignedDistanceToPortal(portal, start);
    const float endDistance = SignedDistanceToPortal(portal, end);
    const float denom = startDistance - endDistance;

    if (startDistance <= 0.0f || endDistance > 0.0f || std::fabs(denom) < 0.001f)
        return false;

    float t = startDistance / denom;
    t = U::Math.Clamp(t, 0.0f, 1.0f);

    const Vector hit = start + (end - start) * t;
    if (intersection)
        *intersection = hit;

    return PortalTransform::IsPointInsideAperture(portal, hit, DefaultAperture());
}

bool CPortalTransition::TryBeginTraversal(C_TerrorPlayer* player, CUserCmd* cmd, PortalSide side, PortalInfo_t& entry, PortalInfo_t& exit)
{
    (void)exit;

    // This is the retired transition path that writes MOVETYPE_NOCLIP directly.
    // The active BSP path uses PortalTransitionSimulator + Teleport instead.
    if (!PortalPhysicsMode::ShouldMutatePlayerMovement())
        return false;

    if (!player)
        return false;

    const float currentTime = I::EngineClient ? I::EngineClient->OBSOLETE_Time() : 0.0f;
    if (currentTime < m_nextTeleportTime && m_lastExitPortal == side)
        return false;

    const PlayerAnchor anchor = BuildPlayerAnchor(player);
    if (!IsPlayerInsidePortalAperture(player, entry, anchor))
        return false;

    const Vector commandMove = cmd ? BuildCommandMoveDirection(*cmd) : Vector();
    const float commandIntoPortal = commandMove.Dot(entry.normal);
    const float velocityIntoPortal = anchor.velocity.Dot(entry.normal);
    const float eyeDistance = SignedDistanceToPortal(entry, anchor.eye);
    const bool intentionallyEntering = commandIntoPortal < kMoveIntoPortalDot || velocityIntoPortal < -15.0f;

    if (!intentionallyEntering)
    {
        if (ShouldLog(currentTime, m_nextStatusLogTime, 0.35f))
        {
            U::LogDebug("[PortalTransition] Near portal but not entering entry=%s eyeD=%.2f cmdDot=%.2f velDot=%.2f.\n",
                SideName(side), eyeDistance, commandIntoPortal, velocityIntoPortal);
        }
        return false;
    }

    m_session.mode = TraversalMode::InPortal;
    m_session.entrySide = side;
    m_session.exitSide = side == PortalSide::Blue ? PortalSide::Orange : PortalSide::Blue;
    m_session.enterTime = currentTime;
    m_session.lastAssistTime = currentTime;
    m_session.nextLogTime = 0.0f;
    m_session.entryVelocity = anchor.velocity;
    m_session.hasEntryVelocity = VectorLengthSqr(anchor.velocity) > kPortalRestoreVelocityThresholdSqr;
    m_session.savedMoveType = player->m_MoveType();
    void* toolsBase = nullptr;
    void* edictBase = nullptr;
    bool resolverAgree = false;
    void* serverBase = ResolveServerLocalPlayerForDiagnostics(&toolsBase, &edictBase, &resolverAgree);
    auto serverBefore = L4D2::ServerDataMap::ProbeMoveType(static_cast<CBaseEntity*>(serverBase));
    if (!serverBefore.ok)
    {
        U::LogInfo("[PortalServerMoveType] phase=enter rejected reason=%s server=%p toolsBase=%p edictBase=%p resolver=%s.\n",
            serverBefore.reason ? serverBefore.reason : "unknown",
            serverBase,
            toolsBase,
            edictBase,
            MatchText(resolverAgree));
        m_session = {};
        return false;
    }

    m_session.savedServerMoveType = serverBefore.valueInt;
    LogMoveTypeProbe("before-enter-noclip", player, nullptr);
    const bool serverWriteOk = L4D2::ServerDataMap::TryWriteFieldInt(static_cast<CBaseEntity*>(serverBase), serverBefore.field, MOVETYPE_NOCLIP);
    const auto serverAfter = L4D2::ServerDataMap::ProbeMoveType(static_cast<CBaseEntity*>(serverBase));
    m_session.usingServerNoclip = serverWriteOk && serverAfter.ok && serverAfter.valueInt == MOVETYPE_NOCLIP;
    if (!m_session.usingServerNoclip)
    {
        const bool restoreOk = L4D2::ServerDataMap::TryWriteFieldInt(static_cast<CBaseEntity*>(serverBase), serverBefore.field, m_session.savedServerMoveType);
        U::LogInfo("[PortalServerMoveType] phase=enter failed saved=%d target=%d write=%s after=%s:%d restore=%s server=%p offset=%d type=%d.\n",
            m_session.savedServerMoveType,
            MOVETYPE_NOCLIP,
            BoolText(serverWriteOk),
            serverAfter.ok ? "ok" : (serverAfter.reason ? serverAfter.reason : "missing"),
            serverAfter.valueInt,
            BoolText(restoreOk),
            serverBase,
            serverBefore.field.actualOffset,
            static_cast<int>(serverBefore.field.fieldType));
        m_session = {};
        return false;
    }

    m_session.usingNoclip = true;
    player->m_MoveType() = MOVETYPE_NOCLIP;
    LogMoveTypeProbe("after-enter-noclip", player, nullptr);

    U::LogInfo("[PortalServerMoveType] phase=enter saved=%d target=%d write=%s after=%s:%d server=%p offset=%d type=%d toolsBase=%p edictBase=%p resolver=%s.\n",
        m_session.savedServerMoveType,
        MOVETYPE_NOCLIP,
        BoolText(serverWriteOk),
        serverAfter.ok ? "ok" : (serverAfter.reason ? serverAfter.reason : "missing"),
        serverAfter.valueInt,
        serverBase,
        serverBefore.field.actualOffset,
        static_cast<int>(serverBefore.field.fieldType),
        toolsBase,
        edictBase,
        MatchText(resolverAgree));

    U::LogInfo("[PortalTransition] Entered portal traversal state entry=%s exit=%s eyeD=%.2f cmdDot=%.2f velDot=%.2f clientMoveType=%u->%u serverMoveType=%d->%d preserveVel=%s origin=(%.1f %.1f %.1f) eye=(%.1f %.1f %.1f).\n",
        SideName(m_session.entrySide), SideName(m_session.exitSide), eyeDistance, commandIntoPortal, velocityIntoPortal,
        static_cast<unsigned int>(m_session.savedMoveType), static_cast<unsigned int>(player->m_MoveType()),
        m_session.savedServerMoveType, serverAfter.valueInt,
        BoolText(m_session.hasEntryVelocity),
        anchor.origin.x, anchor.origin.y, anchor.origin.z, anchor.eye.x, anchor.eye.y, anchor.eye.z);
    return true;
}

void CPortalTransition::UpdateTraversalExitState(C_TerrorPlayer* player)
{
    if (!player || m_session.mode == TraversalMode::Normal || !I::EngineClient)
        return;

    const float currentTime = I::EngineClient->OBSOLETE_Time();
    PortalInfo_t* portal = nullptr;
    PortalInfo_t* other = nullptr;
    const PortalSide trackingSide = m_session.mode == TraversalMode::InPortal ? m_session.entrySide : m_session.exitSide;
    if (!TryGetPortalPair(trackingSide, portal, other) || !portal)
    {
        ClearTraversalSession(player, "tracking portal unavailable");
        return;
    }

    const PlayerAnchor anchor = BuildPlayerAnchor(player);
    const float eyeDistance = SignedDistanceToPortal(*portal, anchor.eye);
    const float centerDistance = SignedDistanceToPortal(*portal, anchor.center);
    const bool insideAperture = IsPlayerInsidePortalAperture(player, *portal, anchor);
    const float sessionAge = currentTime - m_session.enterTime;
    const bool hardTimedOut = sessionAge > kPortalHardSessionTimeout;

    if (m_session.mode == TraversalMode::InPortal)
    {
        const bool backedOutOfEntry = !insideAperture && eyeDistance > kPortalExitDistance && centerDistance > kPortalExitDistance;
        const bool staleInPortal = sessionAge > kPortalSessionTimeout && !insideAperture;
        if (hardTimedOut)
        {
            ClearTraversalSession(player, "hard session timeout");
            return;
        }

        if (backedOutOfEntry)
            ClearTraversalSession(player, "left entry portal");
        else if (staleInPortal)
            ClearTraversalSession(player, "session timeout outside aperture");
        return;
    }

    if (sessionAge > kPortalSessionTimeout)
    {
        ClearTraversalSession(player, "session timeout");
        return;
    }

    const bool clearOfExit = !insideAperture || eyeDistance > kPortalExitDistance;
    if (clearOfExit)
    {
        ClearTraversalSession(player, "left exit portal");
    }
}

void CPortalTransition::ClearTraversalSession(C_TerrorPlayer* player, const char* reason)
{
    if (m_session.usingServerNoclip)
    {
        void* toolsBase = nullptr;
        void* edictBase = nullptr;
        bool resolverAgree = false;
        void* serverBase = ResolveServerLocalPlayerForDiagnostics(&toolsBase, &edictBase, &resolverAgree);
        auto beforeRestore = L4D2::ServerDataMap::ProbeMoveType(static_cast<CBaseEntity*>(serverBase));
        const bool restoreOk = beforeRestore.ok
            && L4D2::ServerDataMap::TryWriteFieldInt(static_cast<CBaseEntity*>(serverBase), beforeRestore.field, m_session.savedServerMoveType);
        const auto afterRestore = L4D2::ServerDataMap::ProbeMoveType(static_cast<CBaseEntity*>(serverBase));

        U::LogInfo("[PortalServerMoveType] phase=restore reason=%s saved=%d before=%s:%d restore=%s after=%s:%d server=%p toolsBase=%p edictBase=%p resolver=%s.\n",
            reason ? reason : "unknown",
            m_session.savedServerMoveType,
            beforeRestore.ok ? "ok" : (beforeRestore.reason ? beforeRestore.reason : "missing"),
            beforeRestore.valueInt,
            BoolText(restoreOk),
            afterRestore.ok ? "ok" : (afterRestore.reason ? afterRestore.reason : "missing"),
            afterRestore.valueInt,
            serverBase,
            toolsBase,
            edictBase,
            MatchText(resolverAgree));
    }

    if (player && m_session.usingNoclip)
    {
        player->m_MoveType() = m_session.savedMoveType;
        LogMoveTypeProbe("after-restore-walk", player, nullptr);
    }

    if (m_session.mode != TraversalMode::Normal)
    {
        U::LogInfo("[PortalTransition] Leaving traversal state mode=%s entry=%s exit=%s reason=%s.\n",
            ModeName(m_session.mode), SideName(m_session.entrySide), SideName(m_session.exitSide), reason ? reason : "unknown");
    }

    m_session = {};
}

bool CPortalTransition::AssistPortalEmbedding(C_TerrorPlayer* player, CUserCmd* cmd, PortalInfo_t& entry, PortalInfo_t& exit)
{
    if (!player || !I::EngineClient)
        return false;

    const float currentTime = I::EngineClient->OBSOLETE_Time();
    const PlayerAnchor anchor = BuildPlayerAnchor(player);
    const float centerDistance = SignedDistanceToPortal(entry, anchor.center);
    const float eyeDistance = SignedDistanceToPortal(entry, anchor.eye);

    if (centerDistance <= kPortalEmbedTargetDistance)
        return false;

    if (!IsPlayerInsidePortalAperture(player, entry, anchor))
        return false;

    const Vector commandMove = cmd ? BuildCommandMoveDirection(*cmd) : Vector();
    const float commandIntoPortal = commandMove.Dot(entry.normal);
    const float velocityIntoPortal = anchor.velocity.Dot(entry.normal);
    if (commandIntoPortal >= kMoveIntoPortalDot && velocityIntoPortal >= -15.0f)
        return false;

    const bool closeToBlockedHull = centerDistance <= kPortalAssistStartDistance;
    const bool blockedByPortalSurface = velocityIntoPortal > kPortalAssistBlockedVelocity;
    if (!closeToBlockedHull || !blockedByPortalSurface)
        return false;

    float frameTime = currentTime - m_session.lastAssistTime;
    if (frameTime <= 0.0f || frameTime > 0.10f)
        frameTime = kPortalEmbedFallbackFrameTime;
    m_session.lastAssistTime = currentTime;

    const float requestedSpeed = std::max(-velocityIntoPortal, -commandIntoPortal * 0.45f);
    const float requestedStep = U::Math.Clamp(requestedSpeed * frameTime, 1.0f, kPortalEmbedMaxStep);
    const float remainingStep = centerDistance - kPortalEmbedTargetDistance;
    const float step = U::Math.Clamp(remainingStep, 0.0f, requestedStep);
    if (step <= 0.0f)
        return false;

    const Vector nudgedOrigin = anchor.origin - entry.normal * step;
    if (!IsFiniteVector(nudgedOrigin))
        return false;

    const float predictedCenterDistance = centerDistance - step;
    if (centerDistance > 0.0f && predictedCenterDistance <= 0.0f)
    {
        PlayerAnchor predictedAnchor = anchor;
        const Vector delta = nudgedOrigin - anchor.origin;
        predictedAnchor.origin = nudgedOrigin;
        predictedAnchor.eye = anchor.eye + delta;
        predictedAnchor.center = anchor.center + delta;
        if (m_session.hasEntryVelocity && VectorLengthSqr(predictedAnchor.velocity) < kPortalRestoreVelocityThresholdSqr)
            predictedAnchor.velocity = m_session.entryVelocity;

        U::LogInfo("[PortalTransition] Assisted embed reaches crossing entry=%s exit=%s centerD=%.2f predictedD=%.2f step=%.2f restoredVel=%s; teleporting without rendering an entry-wall frame.\n",
            SideName(m_session.entrySide), SideName(m_session.exitSide), centerDistance, predictedCenterDistance, step,
            BoolText(m_session.hasEntryVelocity && VectorLengthSqr(anchor.velocity) < kPortalRestoreVelocityThresholdSqr));

        if (!TeleportLocalPlayer(player, entry, exit, m_session.exitSide, &predictedAnchor))
            return false;

        m_session.mode = TraversalMode::ExitingPortal;
        m_session.enterTime = currentTime;
        m_session.nextLogTime = 0.0f;
        m_blueState.hasPreviousDistance = false;
        m_orangeState.hasPreviousDistance = false;
        m_blueState.hasPreviousEyeDistance = false;
        m_orangeState.hasPreviousEyeDistance = false;
        return true;
    }

    if (ShouldLog(currentTime, m_session.nextLogTime, 0.12f))
    {
        U::LogInfo("[PortalTransition] Assisted in-portal embed entry=%s centerD=%.2f eyeD=%.2f step=%.2f cmdDot=%.2f velDot=%.2f oldOrigin=(%.1f %.1f %.1f) newOrigin=(%.1f %.1f %.1f).\n",
            SideName(m_session.entrySide), centerDistance, eyeDistance, step, commandIntoPortal, velocityIntoPortal,
            anchor.origin.x, anchor.origin.y, anchor.origin.z,
            nudgedOrigin.x, nudgedOrigin.y, nudgedOrigin.z);
    }

    Vector preservedVelocity = anchor.velocity;
    if (m_session.hasEntryVelocity && VectorLengthSqr(preservedVelocity) < kPortalRestoreVelocityThresholdSqr)
        preservedVelocity = m_session.entryVelocity;

    EntityTeleport(player, &nudgedOrigin, nullptr, &preservedVelocity, false);
    player->m_vecVelocity() = preservedVelocity;
    return false;
}

void CPortalTransition::ClampMoveToPortalAperture(C_TerrorPlayer* player, CMoveData* move, PortalInfo_t& entry)
{
    if (!player || !move)
        return;

    const PlayerAnchor anchor = BuildPlayerAnchor(player);
    PortalTransform::PortalLocalPoint local = PortalTransform::WorldToPortalLocal(entry, anchor.eye);
    const PortalTransform::PortalAperture aperture = DefaultAperture();

    const float currentTime = I::EngineClient ? I::EngineClient->OBSOLETE_Time() : 0.0f;
    if ((std::fabs(local.forward) > kPortalClampForward
        || std::fabs(local.right) > aperture.halfWidth
        || std::fabs(local.up) > aperture.halfHeight)
        && ShouldLog(currentTime, m_session.nextLogTime, 0.20f))
    {
        U::LogDebug("[PortalTransition] Portal move outside soft aperture entry=%s local=(%.1f %.1f %.1f); leaving engine origin unchanged.\n",
            SideName(m_session.entrySide), local.forward, local.right, local.up);
    }
}

bool CPortalTransition::UpdatePortalCrossing(C_TerrorPlayer* player, CUserCmd* cmd, PortalSide side, PortalInfo_t& entry, PortalInfo_t& exit)
{
    if (m_session.mode == TraversalMode::ExitingPortal)
        return false;

    if (m_session.mode == TraversalMode::InPortal && m_session.entrySide != side)
        return false;

    PortalRuntimeState& state = side == PortalSide::Blue ? m_blueState : m_orangeState;
    const PlayerAnchor anchor = BuildPlayerAnchor(player);
    const Vector center = anchor.center;
    const float distance = SignedDistanceToPortal(entry, center);
    const float eyeDistance = SignedDistanceToPortal(entry, anchor.eye);
    const bool insideAperture = IsPlayerInsidePortalAperture(player, entry, anchor);
    const Vector commandMove = cmd ? BuildCommandMoveDirection(*cmd) : Vector();
    const float commandIntoPortal = commandMove.Dot(entry.normal);
    const float velocityIntoPortal = anchor.velocity.Dot(entry.normal);

    const bool centerCrossedPortalPlane = state.hasPreviousDistance
        && state.previousDistance > 0.0f
        && distance <= 0.0f
        && insideAperture;

    const bool eyeCrossedPortalPlane = state.hasPreviousEyeDistance
        && state.previousEyeDistance > 0.0f
        && eyeDistance <= 0.0f
        && insideAperture;

    const bool crossedPortalPlane = centerCrossedPortalPlane;

    const float currentTime = I::EngineClient ? I::EngineClient->OBSOLETE_Time() : 0.0f;
    if ((std::fabs(eyeDistance) < 96.0f || insideAperture || crossedPortalPlane)
        && ShouldLog(currentTime, m_nextCrossingLogTime, 0.12f))
    {
        U::LogDebug("[PortalTransition] Crossing probe side=%s hasPrev=%s prevD=%.2f curD=%.2f prevEye=%.2f eyeD=%.2f inside=%s centerCrossed=%s eyeCrossed=%s cmdDot=%.2f velDot=%.2f crossed=%s center=(%.1f %.1f %.1f).\n",
            SideName(side), BoolText(state.hasPreviousDistance), state.previousDistance, distance, state.previousEyeDistance, eyeDistance,
            BoolText(insideAperture), BoolText(centerCrossedPortalPlane), BoolText(eyeCrossedPortalPlane),
            commandIntoPortal, velocityIntoPortal, BoolText(crossedPortalPlane), center.x, center.y, center.z);
    }

    state.previousDistance = distance;
    state.previousEyeDistance = eyeDistance;
    state.hasPreviousDistance = true;
    state.hasPreviousEyeDistance = true;

    if (!crossedPortalPlane)
        return false;

    const PortalSide exitSide = side == PortalSide::Blue ? PortalSide::Orange : PortalSide::Blue;
    U::LogInfo("[PortalTransition] Crossing detected: entry=%s exit=%s.\n", SideName(side), SideName(exitSide));

    if (!TeleportLocalPlayer(player, entry, exit, exitSide))
        return false;

    m_session.mode = TraversalMode::ExitingPortal;
    m_session.entrySide = side;
    m_session.exitSide = exitSide;
    m_session.enterTime = currentTime;
    m_session.nextLogTime = 0.0f;
    m_blueState.hasPreviousDistance = false;
    m_orangeState.hasPreviousDistance = false;
    m_blueState.hasPreviousEyeDistance = false;
    m_orangeState.hasPreviousEyeDistance = false;
    return true;
}

bool CPortalTransition::TeleportLocalPlayer(C_TerrorPlayer* player, PortalInfo_t& entry, PortalInfo_t& exit, PortalSide exitSide, const PlayerAnchor* anchor)
{
    if (!player || !I::EngineClient)
        return false;

    matrix3x4_t entryToExit;
    if (!PortalTransform::BuildEntryToExitMatrix(entry, exit, entryToExit))
    {
        U::LogWarning("[PortalTransition] Failed to build entry-to-exit transform.\n");
        return false;
    }

    Vector viewAnglesVector;
    I::EngineClient->GetViewAngles(viewAnglesVector);

    QAngle viewAngles;
    viewAngles.x = viewAnglesVector.x;
    viewAngles.y = viewAnglesVector.y;
    viewAngles.z = viewAnglesVector.z;

    const PlayerAnchor currentAnchor = anchor ? *anchor : BuildPlayerAnchor(player);
    const Vector currentOrigin = currentAnchor.origin;
    const Vector currentEye = currentAnchor.eye;
    Vector currentVelocity = currentAnchor.velocity;
    const bool restoredTraversalVelocity =
        m_session.mode == TraversalMode::InPortal
        && m_session.hasEntryVelocity
        && VectorLengthSqr(currentVelocity) < kPortalRestoreVelocityThresholdSqr;
    if (restoredTraversalVelocity)
        currentVelocity = m_session.entryVelocity;

    const Vector newEye = PortalTransform::TransformPoint(entryToExit, currentEye);
    Vector newOrigin = newEye - currentAnchor.viewOffset;
    newOrigin += exit.normal * kExitPushDistance;

    const Vector pushedEye = newOrigin + currentAnchor.viewOffset;
    const float exitEyeDistance = SignedDistanceToPortal(exit, pushedEye);
    float clearancePush = 0.0f;
    if (exitEyeDistance < kExitEyeClearance)
    {
        clearancePush = kExitEyeClearance - exitEyeDistance;
        newOrigin += exit.normal * clearancePush;
    }

    Vector newVelocity = PortalTransform::TransformVector(entryToExit, currentVelocity);
    QAngle newAngles = PortalTransform::TransformAngles(entryToExit, viewAngles);

    const Vector finalEye = newOrigin + currentAnchor.viewOffset;
    const float finalExitEyeDistance = SignedDistanceToPortal(exit, finalEye);
    const Vector exitReferenceOrigin = exit.origin + exit.normal * 25.0f;

    LogEnvironmentSnapshot("pre-teleport", player, currentEye, Vector(viewAngles.x, viewAngles.y, viewAngles.z), &exitReferenceOrigin);
    LogEnvironmentSnapshot("exit-reference", player, exitReferenceOrigin, Vector(newAngles.x, newAngles.y, newAngles.z), &exitReferenceOrigin);

    U::LogInfo("[PortalTransition] Teleport request entryOrigin=(%.1f %.1f %.1f) exitOrigin=(%.1f %.1f %.1f) oldOrigin=(%.1f %.1f %.1f) oldEye=(%.1f %.1f %.1f) newOrigin=(%.1f %.1f %.1f) newEye=(%.1f %.1f %.1f) oldVel=(%.1f %.1f %.1f) newVel=(%.1f %.1f %.1f) restoredVel=%s oldAng=(%.1f %.1f %.1f) newAng=(%.1f %.1f %.1f) exitEyeD=%.2f clearancePush=%.2f finalExitEyeD=%.2f.\n",
        entry.origin.x, entry.origin.y, entry.origin.z,
        exit.origin.x, exit.origin.y, exit.origin.z,
        currentOrigin.x, currentOrigin.y, currentOrigin.z,
        currentEye.x, currentEye.y, currentEye.z,
        newOrigin.x, newOrigin.y, newOrigin.z,
        finalEye.x, finalEye.y, finalEye.z,
        currentVelocity.x, currentVelocity.y, currentVelocity.z,
        newVelocity.x, newVelocity.y, newVelocity.z,
        BoolText(restoredTraversalVelocity),
        viewAngles.x, viewAngles.y, viewAngles.z,
        newAngles.x, newAngles.y, newAngles.z,
        exitEyeDistance, clearancePush, finalExitEyeDistance);

    U::LogInfo("[PortalTransition][DIAG] About to resolve server-side teleport target clientPlayer=%p exit=%s dryRun=%s useSetAbs=%s.\n",
        player, SideName(exitSide), BoolText(kDryRunServerSetAbsTeleport), BoolText(kUseServerSetAbsTeleport));

    if (!EntityTeleport(player, &newOrigin, &newAngles, &newVelocity))
    {
        U::LogWarning("[PortalTransition] Player Teleport call failed.\n");
        return false;
    }

    if (kEnableVisualTransition && finalExitEyeDistance > kVisualExitEyeClearance)
    {
        const float visualDuration = std::max(0.001f, kVisualTransitionDuration);
        m_visualTransition.active = true;
        m_visualTransition.loggedStart = false;
        m_visualTransition.exitSide = exitSide;
        m_visualTransition.startTime = I::EngineClient->OBSOLETE_Time();
        m_visualTransition.endTime = m_visualTransition.startTime + visualDuration;
        m_visualTransition.physicalExitDistance = finalExitEyeDistance;
        m_visualTransition.visualExitDistance = kVisualExitEyeClearance;
        m_visualTransition.exitNormal = exit.normal;
        m_visualTransition.physicalEye = finalEye;

        U::LogInfo("[PortalTransition][Visual] Armed view compensation exit=%s physicalD=%.2f visualD=%.2f duration=%.3f physicalEye=(%.1f %.1f %.1f).\n",
            SideName(exitSide),
            m_visualTransition.physicalExitDistance,
            m_visualTransition.visualExitDistance,
            visualDuration,
            finalEye.x, finalEye.y, finalEye.z);
    }
    else
    {
        m_visualTransition = {};
    }

    Vector engineAngles(newAngles.x, newAngles.y, newAngles.z);
    I::EngineClient->SetViewAngles(engineAngles);
    player->m_vecVelocity() = newVelocity;
    ArmToneTransition("transition-teleport");
    LogEnvironmentSnapshot("post-teleport", player, finalEye, engineAngles, &exitReferenceOrigin);
    ArmEnvironmentRenderTrace(8);

    m_nextTeleportTime = I::EngineClient->OBSOLETE_Time() + kTeleportCooldown;
    m_lastExitPortal = exitSide;

    U::LogInfo("[PortalTransition] Teleported local player through portal, exit=%s cooldownUntil=%.3f.\n",
        SideName(exitSide), m_nextTeleportTime);
    return true;
}

void* CPortalTransition::ResolveServerLocalPlayerForDiagnostics(void** toolsBase, void** edictBase, bool* resolverAgree) const
{
    if (toolsBase)
        *toolsBase = nullptr;
    if (edictBase)
        *edictBase = nullptr;
    if (resolverAgree)
        *resolverAgree = false;

    if (!I::EngineClient || !I::ClientEntityList)
        return nullptr;

    const int localIndex = I::EngineClient->GetLocalPlayer();
    IClientEntity* clientLocal = localIndex > 0 ? I::ClientEntityList->GetClientEntity(localIndex) : nullptr;

    IServerEntity* toolsServerEntity = nullptr;
    CBaseEntity* toolsBaseEntity = nullptr;
    if (I::CServerTools && clientLocal)
    {
        toolsServerEntity = I::CServerTools->GetIServerEntity(clientLocal);
        toolsBaseEntity = toolsServerEntity ? toolsServerEntity->GetBaseEntity() : nullptr;
    }

    CGlobalVars* globals = I::PlayerInfoManager ? I::PlayerInfoManager->GetGlobalVars() : nullptr;
    edict_t* localEdict = globals && globals->pEdicts && localIndex > 0 ? &globals->pEdicts[localIndex] : nullptr;
    IServerUnknown* edictUnknown = localEdict ? localEdict->GetUnknown() : nullptr;
    CBaseEntity* edictBaseEntity = edictUnknown ? edictUnknown->GetBaseEntity() : nullptr;

    if (toolsBase)
        *toolsBase = toolsBaseEntity;
    if (edictBase)
        *edictBase = edictBaseEntity;
    if (resolverAgree)
        *resolverAgree = toolsBaseEntity && edictBaseEntity && toolsBaseEntity == edictBaseEntity;

    return toolsBaseEntity ? toolsBaseEntity : edictBaseEntity;
}

bool CPortalTransition::EntityTeleport(void* entity, const Vector* origin, const QAngle* angles, const Vector* velocity, bool verbose) const
{
    if (!entity)
    {
        U::LogWarning("[PortalTransition] Server SetAbs teleport rejected: client entity is null.\n");
        return false;
    }

    if (!I::EngineClient || !I::ClientEntityList)
    {
        U::LogWarning("[PortalTransition] Server SetAbs teleport rejected: client interfaces are not ready.\n");
        return false;
    }

    const int localIndex = I::EngineClient->GetLocalPlayer();
    IClientEntity* clientLocal = localIndex > 0 ? I::ClientEntityList->GetClientEntity(localIndex) : nullptr;

    IServerEntity* toolsServerEntity = nullptr;
    CBaseEntity* toolsBaseEntity = nullptr;
    if (I::CServerTools && clientLocal)
    {
        toolsServerEntity = I::CServerTools->GetIServerEntity(clientLocal);
        toolsBaseEntity = toolsServerEntity ? toolsServerEntity->GetBaseEntity() : nullptr;
    }

    CGlobalVars* globals = I::PlayerInfoManager ? I::PlayerInfoManager->GetGlobalVars() : nullptr;
    edict_t* localEdict = globals && globals->pEdicts && localIndex > 0 ? &globals->pEdicts[localIndex] : nullptr;
    IServerUnknown* edictUnknown = localEdict ? localEdict->GetUnknown() : nullptr;
    CBaseEntity* edictBaseEntity = edictUnknown ? edictUnknown->GetBaseEntity() : nullptr;

    CBaseEntity* serverBaseEntity = toolsBaseEntity ? toolsBaseEntity : edictBaseEntity;
    const bool resolverAgree = toolsBaseEntity && edictBaseEntity && toolsBaseEntity == edictBaseEntity;

    if (verbose)
    {
        U::LogInfo("[PortalTransition][DIAG] Server teleport resolver localIndex=%d clientArg=%p clientLocal=%p tools=%p toolsServer=%p toolsBase=%p globals=%p pEdicts=%p edict=%p edictUnknown=%p edictBase=%p resolver=%s chosen=%p.\n",
            localIndex,
            entity,
            clientLocal,
            I::CServerTools,
            toolsServerEntity,
            toolsBaseEntity,
            globals,
            globals ? globals->pEdicts : nullptr,
            localEdict,
            edictUnknown,
            edictBaseEntity,
            MatchText(resolverAgree),
            serverBaseEntity);

        U::LogInfo("[PortalTransition][DIAG] Server SetAbs functions origin=%p angles=%p velocity=%p targetOrigin=(%.1f %.1f %.1f) targetAngles=(%.1f %.1f %.1f) targetVelocity=(%.1f %.1f %.1f).\n",
            reinterpret_cast<void*>(U::Offsets.m_dwSetAbsOrigin),
            reinterpret_cast<void*>(U::Offsets.m_dwSetAbsAngles),
            reinterpret_cast<void*>(U::Offsets.m_dwSetAbsVelocity),
            origin ? origin->x : 0.0f,
            origin ? origin->y : 0.0f,
            origin ? origin->z : 0.0f,
            angles ? angles->x : 0.0f,
            angles ? angles->y : 0.0f,
            angles ? angles->z : 0.0f,
            velocity ? velocity->x : 0.0f,
            velocity ? velocity->y : 0.0f,
            velocity ? velocity->z : 0.0f);
    }

    if (!serverBaseEntity)
    {
        U::LogWarning("[PortalTransition] Server SetAbs teleport rejected: could not resolve server-side local player.\n");
        return false;
    }

    if (kUseServerSetAbsTeleport)
    {
        if (!U::Offsets.m_dwSetAbsOrigin || !U::Offsets.m_dwSetAbsAngles || !U::Offsets.m_dwSetAbsVelocity)
        {
            U::LogError("[PortalTransition] Server SetAbs teleport rejected: one or more SetAbs signatures were not found.\n");
            return false;
        }

        if (kDryRunServerSetAbsTeleport)
        {
            U::LogWarning("[PortalTransition][DIAG] Dry-run only: server-side SetAbs teleport target resolved, but movement calls are disabled for crash-safe validation.\n");
            return false;
        }

        using FnSetAbsOrigin = void(__thiscall*)(void*, const Vector&);
        using FnSetAbsAngles = void(__thiscall*)(void*, const QAngle&);
        using FnSetAbsVelocity = void(__thiscall*)(void*, const Vector&);

        FnSetAbsOrigin setAbsOrigin = reinterpret_cast<FnSetAbsOrigin>(U::Offsets.m_dwSetAbsOrigin);
        FnSetAbsAngles setAbsAngles = reinterpret_cast<FnSetAbsAngles>(U::Offsets.m_dwSetAbsAngles);
        FnSetAbsVelocity setAbsVelocity = reinterpret_cast<FnSetAbsVelocity>(U::Offsets.m_dwSetAbsVelocity);

        const Vector stopVelocity(0.0f, 0.0f, 0.0f);
        setAbsVelocity(serverBaseEntity, stopVelocity);
        setAbsOrigin(serverBaseEntity, *origin);
        setAbsAngles(serverBaseEntity, *angles);
        setAbsVelocity(serverBaseEntity, *velocity);
        return true;
    }

    void** serverVTable = *reinterpret_cast<void***>(serverBaseEntity);
    if (!serverVTable || !serverVTable[kServerTeleportVTableIndex])
    {
        U::LogError("[PortalTransition] Server vtable teleport rejected: vtable/index %u is not available. serverBase=%p vtable=%p.\n",
            static_cast<unsigned int>(kServerTeleportVTableIndex), serverBaseEntity, serverVTable);
        return false;
    }

    if (verbose)
    {
        U::LogInfo("[PortalTransition][DIAG] Calling server-side vtable teleport serverBase=%p vtable=%p index=%u fn=%p.\n",
            serverBaseEntity,
            serverVTable,
            static_cast<unsigned int>(kServerTeleportVTableIndex),
            serverVTable[kServerTeleportVTableIndex]);
    }

    using FnTeleport = void(__thiscall*)(void*, const Vector*, const QAngle*, const Vector*);
    FnTeleport teleport = reinterpret_cast<FnTeleport>(serverVTable[kServerTeleportVTableIndex]);
    teleport(serverBaseEntity, origin, angles, velocity);
    return true;
}

void CPortalTransition::RefreshPortalDistance(C_TerrorPlayer* player, PortalSide side, PortalInfo_t& portal)
{
    if (!player)
        return;

    PortalRuntimeState& state = side == PortalSide::Blue ? m_blueState : m_orangeState;
    state.previousDistance = SignedDistanceToPortal(portal, player->WorldSpaceCenter());
    state.previousEyeDistance = SignedDistanceToPortal(portal, player->EyePosition());
    state.hasPreviousDistance = true;
    state.hasPreviousEyeDistance = true;
}

bool CPortalTransition::ShouldLog(float currentTime, float& nextLogTime, float intervalSeconds)
{
    if (currentTime < nextLogTime)
        return false;

    nextLogTime = currentTime + intervalSeconds;
    return true;
}

const char* CPortalTransition::SideName(PortalSide side) const
{
    switch (side)
    {
    case PortalSide::Blue:
        return "blue";
    case PortalSide::Orange:
        return "orange";
    default:
        return "none";
    }
}

const char* CPortalTransition::ModeName(TraversalMode mode) const
{
    switch (mode)
    {
    case TraversalMode::Normal:
        return "normal";
    case TraversalMode::InPortal:
        return "in-portal";
    case TraversalMode::ExitingPortal:
        return "exiting-portal";
    default:
        return "unknown";
    }
}

void CPortalTransition::LogPortalReadiness(float currentTime)
{
    if (!ShouldLog(currentTime, m_nextStatusLogTime, 1.0f))
        return;

    const PortalInfo_t& blue = G::G_L4D2Portal.g_BluePortal;
    const PortalInfo_t& orange = G::G_L4D2Portal.g_OrangePortal;

    U::LogDebug("[PortalTransition] Update skipped: portals not ready. blue active=%s closing=%s anim=%d scale=%.2f origin=(%.1f %.1f %.1f) normal=(%.2f %.2f %.2f); orange active=%s closing=%s anim=%d scale=%.2f origin=(%.1f %.1f %.1f) normal=(%.2f %.2f %.2f).\n",
        BoolText(blue.bIsActive), BoolText(blue.bIsClosing), static_cast<int>(blue.animState), blue.currentScale,
        blue.origin.x, blue.origin.y, blue.origin.z, blue.normal.x, blue.normal.y, blue.normal.z,
        BoolText(orange.bIsActive), BoolText(orange.bIsClosing), static_cast<int>(orange.animState), orange.currentScale,
        orange.origin.x, orange.origin.y, orange.origin.z, orange.normal.x, orange.normal.y, orange.normal.z);
}

void CPortalTransition::LogDistanceProbe(float currentTime, C_TerrorPlayer* player, PortalInfo_t& blueEntry, PortalInfo_t& orangeEntry)
{
    if (!player || !ShouldLog(currentTime, m_nextDistanceLogTime, 1.0f))
        return;

    const Vector center = player->WorldSpaceCenter();
    const Vector eye = player->EyePosition();
    const float blueDistance = SignedDistanceToPortal(blueEntry, center);
    const float orangeDistance = SignedDistanceToPortal(orangeEntry, center);
    const float blueEyeDistance = SignedDistanceToPortal(blueEntry, eye);
    const float orangeEyeDistance = SignedDistanceToPortal(orangeEntry, eye);
    const bool insideBlue = PortalTransform::IsPointInsideAperture(blueEntry, center, DefaultAperture());
    const bool insideOrange = PortalTransform::IsPointInsideAperture(orangeEntry, center, DefaultAperture());

    U::LogDebug("[PortalTransition] Distance probe mode=%s center=(%.1f %.1f %.1f) eye=(%.1f %.1f %.1f) blueD=%.2f blueEyeD=%.2f insideBlue=%s orangeD=%.2f orangeEyeD=%.2f insideOrange=%s.\n",
        ModeName(m_session.mode),
        center.x, center.y, center.z,
        eye.x, eye.y, eye.z,
        blueDistance, blueEyeDistance, BoolText(insideBlue),
        orangeDistance, orangeEyeDistance, BoolText(insideOrange));
}

void CPortalTransition::LogEnvironmentSnapshot(const char* phase, C_TerrorPlayer* player, const Vector& viewOrigin, const Vector& viewAngles, const Vector* referenceOrigin)
{
    if (!phase)
        phase = "unknown";

    const int sequence = ++m_environmentTraceSequence;
    const float currentTime = I::EngineClient ? I::EngineClient->OBSOLETE_Time() : 0.0f;
    const Vector playerOrigin = player ? player->m_vecOrigin() : Vector();
    const Vector playerEye = player ? player->EyePosition() : viewOrigin;
    const Vector skyboxOrigin = player ? player->m_skybox3d_origin() : Vector();
    const Vector skyboxFogDir = player ? player->m_skybox3d_fog_dirPrimary() : Vector();
    const int viewLeaf = I::EngineTrace ? I::EngineTrace->GetLeafContainingPoint(viewOrigin) : -1;
    const int eyeLeaf = I::EngineTrace ? I::EngineTrace->GetLeafContainingPoint(playerEye) : -1;
    const int refLeaf = (I::EngineTrace && referenceOrigin) ? I::EngineTrace->GetLeafContainingPoint(*referenceOrigin) : -1;
    const int viewContents = I::EngineTrace ? I::EngineTrace->GetPointContents(viewOrigin) : 0;
    const int eyeContents = I::EngineTrace ? I::EngineTrace->GetPointContents(playerEye) : 0;
    const int refContents = (I::EngineTrace && referenceOrigin) ? I::EngineTrace->GetPointContents(*referenceOrigin) : 0;

    Vector ambient(0.0f, 0.0f, 0.0f);
    Vector lightAtView(0.0f, 0.0f, 0.0f);
    if (I::EngineClient)
    {
        I::EngineClient->GetAmbientLightColor(ambient);
        lightAtView = I::EngineClient->GetLightForPoint(viewOrigin, true);
    }

    Vector toneScale(0.0f, 0.0f, 0.0f);
    if (G::G_L4D2Portal.m_pMaterialSystem)
    {
        IMatRenderContext* renderContext = G::G_L4D2Portal.m_pMaterialSystem->GetRenderContext();
        if (renderContext)
            toneScale = renderContext->GetToneMappingScaleLinear();
    }

    VisibleFogVolumeInfo_t fog = {};
    if (I::CustomRender && I::CustomRender->FnGetVisibleFogVolume_func)
        I::CustomRender->GetVisibleFogVolumeInfo(const_cast<Vector&>(viewOrigin), fog);
    VisibleFogVolumeInfo_t refFog = {};
    if (I::CustomRender && I::CustomRender->FnGetVisibleFogVolume_func && referenceOrigin)
        I::CustomRender->GetVisibleFogVolumeInfo(const_cast<Vector&>(*referenceOrigin), refFog);

    const int tonemapHandle = player ? player->m_hTonemapController().ToInt() : -1;
    const int playerFogHandle = player ? player->m_PlayerFog_m_hCtrl() : -1;
    const void* areaBits = player ? player->m_chAreaBits() : nullptr;
    const void* areaPortalBits = player ? player->m_chAreaPortalBits() : nullptr;

    U::LogWarning(
        "[PortalEnvironment] seq=%d phase=%s time=%.3f player=%p viewOrigin=(%.1f %.1f %.1f) viewAngles=(%.1f %.1f %.1f) playerOrigin=(%.1f %.1f %.1f) playerEye=(%.1f %.1f %.1f) leaf(view=%d eye=%d ref=%d) contents(view=0x%X eye=0x%X ref=0x%X) fog(viewVol=%d viewLeaf=%d eyeIn=%s waterDist=%.1f waterH=%.1f mat=%p refVol=%d refLeaf=%d refEyeIn=%s) handles(fog=%d tonemap=%d areaBits=%p areaPortalBits=%p) skybox(scale=%d area=%d origin=(%.1f %.1f %.1f) fogEnable=%d fogBlend=%d fogColor=%d/%d fogStart=%.1f fogEnd=%.1f fogDensity=%.3f fogHDR=%.3f fogDir=(%.2f %.2f %.2f)) light(ambient=(%.3f %.3f %.3f) point=(%.3f %.3f %.3f) tone=(%.3f %.3f %.3f)) refOrigin=(%.1f %.1f %.1f).\n",
        sequence,
        phase,
        currentTime,
        player,
        viewOrigin.x, viewOrigin.y, viewOrigin.z,
        viewAngles.x, viewAngles.y, viewAngles.z,
        playerOrigin.x, playerOrigin.y, playerOrigin.z,
        playerEye.x, playerEye.y, playerEye.z,
        viewLeaf, eyeLeaf, refLeaf,
        viewContents, eyeContents, refContents,
        fog.m_nVisibleFogVolume, fog.m_nVisibleFogVolumeLeaf, BoolText(fog.m_bEyeInFogVolume),
        fog.m_flDistanceToWater, fog.m_flWaterHeight, fog.m_pFogVolumeMaterial,
        refFog.m_nVisibleFogVolume, refFog.m_nVisibleFogVolumeLeaf, BoolText(refFog.m_bEyeInFogVolume),
        playerFogHandle, tonemapHandle, areaBits, areaPortalBits,
        player ? player->m_skybox3d_scale() : 0,
        player ? player->m_skybox3d_area() : 0,
        skyboxOrigin.x, skyboxOrigin.y, skyboxOrigin.z,
        player ? player->m_skybox3d_fog_enable() : 0,
        player ? player->m_skybox3d_fog_blend() : 0,
        player ? player->m_skybox3d_fog_colorPrimary() : 0,
        player ? player->m_skybox3d_fog_colorSecondary() : 0,
        player ? player->m_skybox3d_fog_start() : 0.0f,
        player ? player->m_skybox3d_fog_end() : 0.0f,
        player ? player->m_skybox3d_fog_maxdensity() : 0.0f,
        player ? player->m_skybox3d_fog_HDRColorScale() : 0.0f,
        skyboxFogDir.x, skyboxFogDir.y, skyboxFogDir.z,
        ambient.x, ambient.y, ambient.z,
        lightAtView.x, lightAtView.y, lightAtView.z,
        toneScale.x, toneScale.y, toneScale.z,
        referenceOrigin ? referenceOrigin->x : 0.0f,
        referenceOrigin ? referenceOrigin->y : 0.0f,
        referenceOrigin ? referenceOrigin->z : 0.0f);
}
