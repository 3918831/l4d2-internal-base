#include "PortalStage1Probe.h"

#include <cmath>
#include <cstring>

#include "../SDK/L4D2/Entities/C_TerrorPlayer.h"
#include "../SDK/L4D2/Interfaces/ClientEntityList.h"
#include "../SDK/L4D2/Interfaces/CServerTools.h"
#include "../SDK/L4D2/Interfaces/EngineClient.h"
#include "../SDK/L4D2/Interfaces/EngineTrace.h"
#include "../SDK/L4D2/Interfaces/GameMovement.h"
#include "../SDK/L4D2/Interfaces/ICvar.h"
#include "../SDK/L4D2/Interfaces/MoveHelper.h"
#include "../SDK/L4D2/Interfaces/Prediction.h"
#include "../SDK/L4D2/Interfaces/Vphysics.h"
#include "../SDK/L4D2/Includes/const.h"
#include "../SDK/L4D2/Includes/globalvars_base.h"
#include "../SDK/L4D2/Includes/usercmd.h"
#include "../Util/Logger/Logger.h"
#include "../Util/Math/Math.h"
#include "../Util/Offsets/Offsets.h"
#include "L4D2_Portal.h"
#include "PortalTransform.h"

namespace
{
    constexpr float kInterfaceLogInterval = 5.0f;
    constexpr float kRuntimeLogInterval = 1.0f;

    PortalTransform::PortalAperture Stage1Aperture()
    {
        PortalTransform::PortalAperture aperture;
        aperture.halfWidth = 32.0f;
        aperture.halfHeight = 56.0f;
        aperture.tolerance = 6.0f;
        return aperture;
    }

    bool IsPortalReadyForStage1(const PortalInfo_t& portal)
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

    C_TerrorPlayer* GetLocalPlayer()
    {
        if (!I::EngineClient || !I::ClientEntityList)
            return nullptr;

        const int localIndex = I::EngineClient->GetLocalPlayer();
        if (localIndex <= 0)
            return nullptr;

        IClientEntity* entity = I::ClientEntityList->GetClientEntity(localIndex);
        return entity ? entity->As<C_TerrorPlayer*>() : nullptr;
    }
}

void CPortalStage1Probe::Reset()
{
    m_nextInterfaceLogTime = 0.0f;
    m_nextRuntimeLogTime = 0.0f;
}

void CPortalStage1Probe::Update(CUserCmd* cmd, const CPortalTransitionSimulator& simulator, const CPortalCollisionBridge& bridge)
{
    if (!cmd || !cmd->command_number || !I::EngineClient || !I::EngineClient->IsInGame())
        return;

    const float currentTime = I::EngineClient->OBSOLETE_Time();
    if (currentTime >= m_nextInterfaceLogTime)
    {
        m_nextInterfaceLogTime = currentTime + kInterfaceLogInterval;
        LogInterfaceSnapshot();
    }

    if (currentTime >= m_nextRuntimeLogTime)
    {
        m_nextRuntimeLogTime = currentTime + kRuntimeLogInterval;
        LogRuntimeSnapshot(cmd, simulator, bridge);
    }
}

void CPortalStage1Probe::DumpNow(const CPortalTransitionSimulator& simulator, const CPortalCollisionBridge& bridge) const
{
    U::LogWarning("[PortalStage1Probe] manual dump requested.\n");
    LogInterfaceSnapshot();
    LogRuntimeSnapshot(nullptr, simulator, bridge);
}

void CPortalStage1Probe::LogInterfaceSnapshot() const
{
    const bool stage1Ready = I::EngineClient
        && I::ClientEntityList
        && I::EngineTrace
        && I::GameMovement
        && I::Cvar
        && I::GlobalVars
        && U::Offsets.m_dwTracePlayerBBox != 0;

    U::LogWarning("[PortalStage1Probe][Interfaces] stage1Ready=%s EngineClient=%p ClientEntityList=%p EngineTrace=%p GameMovement=%p Cvar=%p GlobalVars=%p TracePlayerBBox=%p.\n",
        BoolText(stage1Ready),
        I::EngineClient,
        I::ClientEntityList,
        I::EngineTrace,
        I::GameMovement,
        I::Cvar,
        I::GlobalVars,
        reinterpret_cast<void*>(U::Offsets.m_dwTracePlayerBBox));

    U::LogWarning("[PortalStage1Probe][Interfaces] optional/future Prediction=%p MoveHelper=%p PhysicsCollision=%p CServerTools=%p SetAbsOrigin=%p SetAbsAngles=%p SetAbsVelocity=%p.\n",
        I::Prediction,
        I::MoveHelper,
        I::PhysicsCollision,
        I::CServerTools,
        reinterpret_cast<void*>(U::Offsets.m_dwSetAbsOrigin),
        reinterpret_cast<void*>(U::Offsets.m_dwSetAbsAngles),
        reinterpret_cast<void*>(U::Offsets.m_dwSetAbsVelocity));
}

void CPortalStage1Probe::LogRuntimeSnapshot(CUserCmd* cmd, const CPortalTransitionSimulator& simulator, const CPortalCollisionBridge& bridge) const
{
    C_TerrorPlayer* player = GetLocalPlayer();
    if (!player || player->deadflag())
    {
        U::LogDebug("[PortalStage1Probe][Runtime] local player unavailable player=%p.\n", player);
        return;
    }

    const PortalInfo_t& blue = G::G_L4D2Portal.g_BluePortal;
    const PortalInfo_t& orange = G::G_L4D2Portal.g_OrangePortal;
    const bool blueReady = IsPortalReadyForStage1(blue);
    const bool orangeReady = IsPortalReadyForStage1(orange);
    const bool pairReady = blueReady && orangeReady;

    const Vector origin = player->m_vecOrigin();
    const Vector eye = player->EyePosition();
    const Vector center = player->WorldSpaceCenter();
    const Vector feet(origin.x, origin.y, origin.z);
    const Vector velocity = player->m_vecVelocity();
    const Vector viewAngles = player->EyeAngles();
    const Vector mins = player->GetPlayerMins();
    const Vector maxs = player->GetPlayerMaxs();

    Vector forward;
    U::Math.AngleVectors(viewAngles, &forward);

    IHandleEntity* contentsEntity = nullptr;
    const int centerContents = I::EngineTrace
        ? I::EngineTrace->GetPointContents(center, MASK_PLAYERSOLID, &contentsEntity)
        : 0;
    const int eyeLeaf = I::EngineTrace ? I::EngineTrace->GetLeafContainingPoint(eye) : -1;
    const bool eyeOutsideWorld = I::EngineTrace ? I::EngineTrace->PointOutsideWorld(eye) : true;

    trace_t rayTrace;
    std::memset(&rayTrace, 0, sizeof(rayTrace));
    rayTrace.fraction = 1.0f;
    if (I::EngineTrace)
    {
        Ray_t ray;
        ray.Init(eye, eye + forward * 96.0f);
        CTraceFilterWorldAndPropsOnly filter;
        I::EngineTrace->TraceRay(ray, MASK_PLAYERSOLID, &filter, &rayTrace);
    }

    const PortalTransform::PortalAperture aperture = Stage1Aperture();
    const float blueCenterD = blueReady ? SignedDistanceToPortal(blue, center) : 0.0f;
    const float orangeCenterD = orangeReady ? SignedDistanceToPortal(orange, center) : 0.0f;
    const bool blueInside = blueReady ? PortalTransform::IsPointInsideAperture(blue, center, aperture) : false;
    const bool orangeInside = orangeReady ? PortalTransform::IsPointInsideAperture(orange, center, aperture) : false;
    const float blueMoveDot = blueReady ? velocity.Dot(blue.normal) : 0.0f;
    const float orangeMoveDot = orangeReady ? velocity.Dot(orange.normal) : 0.0f;

    const PortalTransitionContext& context = simulator.GetContext();
    const PortalCollisionBridgeDiagnostics& diag = bridge.GetDiagnostics();

    U::LogWarning("[PortalStage1Probe][Runtime] pairReady=%s blue(active=%s ready=%s anim=%d ent=%p pos=(%.1f %.1f %.1f) n=(%.2f %.2f %.2f)) orange(active=%s ready=%s anim=%d ent=%p pos=(%.1f %.1f %.1f) n=(%.2f %.2f %.2f)).\n",
        BoolText(pairReady),
        BoolText(blue.bIsActive),
        BoolText(blueReady),
        blue.animState,
        blue.pPortalEntity,
        blue.origin.x, blue.origin.y, blue.origin.z,
        blue.normal.x, blue.normal.y, blue.normal.z,
        BoolText(orange.bIsActive),
        BoolText(orangeReady),
        orange.animState,
        orange.pPortalEntity,
        orange.origin.x, orange.origin.y, orange.origin.z,
        orange.normal.x, orange.normal.y, orange.normal.z);

    U::LogWarning("[PortalStage1Probe][Player] ptr=%p cmd=%d buttons=0x%X fmove=%.1f smove=%.1f upmove=%.1f origin=(%.1f %.1f %.1f) eye=(%.1f %.1f %.1f) center=(%.1f %.1f %.1f) vel=(%.1f %.1f %.1f) mins=(%.1f %.1f %.1f) maxs=(%.1f %.1f %.1f) contents=0x%X contentsEnt=%p eyeLeaf=%d outside=%s rayFrac=%.3f rayStartSolid=%s rayAllSolid=%s.\n",
        player,
        cmd ? cmd->command_number : 0,
        cmd ? cmd->buttons : 0,
        cmd ? cmd->forwardmove : 0.0f,
        cmd ? cmd->sidemove : 0.0f,
        cmd ? cmd->upmove : 0.0f,
        origin.x, origin.y, origin.z,
        eye.x, eye.y, eye.z,
        center.x, center.y, center.z,
        velocity.x, velocity.y, velocity.z,
        mins.x, mins.y, mins.z,
        maxs.x, maxs.y, maxs.z,
        centerContents,
        contentsEntity,
        eyeLeaf,
        BoolText(eyeOutsideWorld),
        rayTrace.fraction,
        BoolText(rayTrace.startsolid),
        BoolText(rayTrace.allsolid));

    U::LogWarning("[PortalStage1Probe][Geometry] blue centerD=%.2f inside=%s velDot=%.2f; orange centerD=%.2f inside=%s velDot=%.2f; sim phase=%s entry=%s exit=%s depth=%.2f inside=%s moving=%s bridgePhase=%s.\n",
        blueCenterD,
        BoolText(blueInside),
        blueMoveDot,
        orangeCenterD,
        BoolText(orangeInside),
        orangeMoveDot,
        PhaseName(context.phase),
        SideName(context.entrySide),
        SideName(context.exitSide),
        context.signedDepth,
        BoolText(context.insideAperture),
        BoolText(context.movingIntoPortal),
        BoolText(simulator.IsInCollisionBridgePhase()));

    U::LogWarning("[PortalStage1Probe][Bridge] total=%u eligible=%u accepted=%u rejectPhase=%u rejectPair=%u rejectAperture=%u lastAccepted=%s lastPhase=%s lastEntry=%s lastFrac=%.3f lastStartSolid=%s lastAllSolid=%s lastStart=(%.1f %.1f %.1f) lastEnd=(%.1f %.1f %.1f) lastRejectD=(%.2f->%.2f) lastAcceptD=(%.2f->%.2f) lastAcceptFrac=%.3f lastAcceptStart=(%.1f %.1f %.1f) lastAcceptEnd=(%.1f %.1f %.1f) lastAcceptHit=(%.1f %.1f %.1f).\n",
        diag.totalRequests,
        diag.eligibleRequests,
        diag.acceptedBypasses,
        diag.rejectedByPhase,
        diag.rejectedByPortalPair,
        diag.rejectedByAperture,
        BoolText(diag.lastAccepted),
        PhaseName(diag.lastPhase),
        SideName(diag.lastEntrySide),
        diag.lastOriginalFraction,
        BoolText(diag.lastOriginalStartSolid),
        BoolText(diag.lastOriginalAllSolid),
        diag.lastStart.x, diag.lastStart.y, diag.lastStart.z,
        diag.lastEnd.x, diag.lastEnd.y, diag.lastEnd.z,
        diag.lastRejectedApertureStartDistance,
        diag.lastRejectedApertureEndDistance,
        diag.lastAcceptedStartDistance,
        diag.lastAcceptedEndDistance,
        diag.lastAcceptedOriginalFraction,
        diag.lastAcceptedStart.x, diag.lastAcceptedStart.y, diag.lastAcceptedStart.z,
        diag.lastAcceptedEnd.x, diag.lastAcceptedEnd.y, diag.lastAcceptedEnd.z,
        diag.lastAcceptedHit.x, diag.lastAcceptedHit.y, diag.lastAcceptedHit.z);
}

const char* CPortalStage1Probe::PhaseName(PortalTransitionPhase phase) const
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

const char* CPortalStage1Probe::SideName(PortalTransitionSide side) const
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

const char* CPortalStage1Probe::BoolText(bool value) const
{
    return value ? "true" : "false";
}
