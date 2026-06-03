#include "PortalTransition.h"

#include <algorithm>
#include <cstddef>
#include <cmath>

#include "../SDK/L4D2/Entities/C_TerrorPlayer.h"
#include "../SDK/L4D2/Includes/usercmd.h"
#include "../SDK/L4D2/Includes/edict.h"
#include "../SDK/L4D2/Includes/iserverunknown.h"
#include "../SDK/L4D2/Interfaces/ClientEntityList.h"
#include "../SDK/L4D2/Interfaces/CServerTools.h"
#include "../SDK/L4D2/Interfaces/EngineClient.h"
#include "../SDK/L4D2/Interfaces/GameMovement.h"
#include "../SDK/L4D2/Interfaces/IPlayerInfoManager.h"
#include "../Util/Logger/Logger.h"
#include "../Util/Offsets/Offsets.h"
#pragma warning(push)
#pragma warning(disable: 4819)
#include "L4D2_Portal.h"
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
}

void CPortalTransition::Reset()
{
    m_blueState = {};
    m_orangeState = {};
    m_session = {};
    m_visualTransition = {};
    m_lastExitPortal = PortalSide::None;
    m_nextTeleportTime = 0.0f;
    m_nextCrossingLogTime = 0.0f;
}

void CPortalTransition::ApplyVisualTransition(CViewSetup& view)
{
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

        U::LogInfo("[PortalTransition][MoveData] layout sizeof=%u offVelocity=%u offAngles=%u offWishVel=%u offConstraintPastRadius=%u offAbsOrigin=%u samplePlayerOrigin=(%.1f %.1f %.1f) sampleMoveOrigin=(%.1f %.1f %.1f) originDelta=(%.1f %.1f %.1f) sampleMoveVelocity=(%.1f %.1f %.1f) valid=%s.\n",
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
    m_session.usingNoclip = true;
    player->m_MoveType() = MOVETYPE_NOCLIP;

    U::LogInfo("[PortalTransition] Entered portal traversal state entry=%s exit=%s eyeD=%.2f cmdDot=%.2f velDot=%.2f moveType=%u->%u preserveVel=%s origin=(%.1f %.1f %.1f) eye=(%.1f %.1f %.1f).\n",
        SideName(m_session.entrySide), SideName(m_session.exitSide), eyeDistance, commandIntoPortal, velocityIntoPortal,
        static_cast<unsigned int>(m_session.savedMoveType), static_cast<unsigned int>(player->m_MoveType()),
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
    if (player && m_session.usingNoclip)
    {
        player->m_MoveType() = m_session.savedMoveType;
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

    m_nextTeleportTime = I::EngineClient->OBSOLETE_Time() + kTeleportCooldown;
    m_lastExitPortal = exitSide;

    U::LogInfo("[PortalTransition] Teleported local player through portal, exit=%s cooldownUntil=%.3f.\n",
        SideName(exitSide), m_nextTeleportTime);
    return true;
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
