#include "PortalTransitionSimulator.h"

#include <algorithm>
#include <cmath>

#include "../SDK/L4D2/Entities/C_TerrorPlayer.h"
#include "../SDK/L4D2/Includes/usercmd.h"
#include "../SDK/L4D2/Interfaces/ClientEntityList.h"
#include "../SDK/L4D2/Interfaces/EngineClient.h"
#include "../Util/Logger/Logger.h"
#pragma warning(push)
#pragma warning(disable: 4819)
#include "L4D2_Portal.h"
#pragma warning(pop)

namespace
{
    constexpr float kApproachDistance = 56.0f;
    constexpr float kIntersectDistance = 24.0f;
    constexpr float kExitDistance = 72.0f;
    constexpr float kMoveIntoPortalDot = -20.0f;

    PortalTransform::PortalAperture DefaultAperture()
    {
        PortalTransform::PortalAperture aperture;
        aperture.halfWidth = 32.0f;
        aperture.halfHeight = 56.0f;
        aperture.tolerance = 6.0f;
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

    float AbsMin(float a, float b)
    {
        return std::fabs(a) < std::fabs(b) ? a : b;
    }
}

void CPortalTransitionSimulator::Reset()
{
    m_context = {};
    m_nextReadinessLogTime = 0.0f;
    m_nextProbeLogTime = 0.0f;
    m_nextPhaseLogTime = 0.0f;
}

void CPortalTransitionSimulator::Update(CUserCmd* cmd)
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
        if (ShouldLog(currentTime, m_nextReadinessLogTime, 1.0f))
            U::LogDebug("[PortalSim] Update skipped: local player is not available.\n");
        Reset();
        return;
    }

    if (!ArePortalsReady())
    {
        if (ShouldLog(currentTime, m_nextReadinessLogTime, 1.0f))
        {
            U::LogDebug("[PortalSim] readiness blue active=%s closing=%s anim=%d origin=(%.1f %.1f %.1f) normal=(%.2f %.2f %.2f); orange active=%s closing=%s anim=%d origin=(%.1f %.1f %.1f) normal=(%.2f %.2f %.2f).\n",
                BoolText(G::G_L4D2Portal.g_BluePortal.bIsActive),
                BoolText(G::G_L4D2Portal.g_BluePortal.bIsClosing),
                static_cast<int>(G::G_L4D2Portal.g_BluePortal.animState),
                G::G_L4D2Portal.g_BluePortal.origin.x,
                G::G_L4D2Portal.g_BluePortal.origin.y,
                G::G_L4D2Portal.g_BluePortal.origin.z,
                G::G_L4D2Portal.g_BluePortal.normal.x,
                G::G_L4D2Portal.g_BluePortal.normal.y,
                G::G_L4D2Portal.g_BluePortal.normal.z,
                BoolText(G::G_L4D2Portal.g_OrangePortal.bIsActive),
                BoolText(G::G_L4D2Portal.g_OrangePortal.bIsClosing),
                static_cast<int>(G::G_L4D2Portal.g_OrangePortal.animState),
                G::G_L4D2Portal.g_OrangePortal.origin.x,
                G::G_L4D2Portal.g_OrangePortal.origin.y,
                G::G_L4D2Portal.g_OrangePortal.origin.z,
                G::G_L4D2Portal.g_OrangePortal.normal.x,
                G::G_L4D2Portal.g_OrangePortal.normal.y,
                G::G_L4D2Portal.g_OrangePortal.normal.z);
        }
        ClearPhase(currentTime, "portals-not-ready");
        return;
    }

    PortalInfo_t* blueEntry = nullptr;
    PortalInfo_t* blueExit = nullptr;
    PortalInfo_t* orangeEntry = nullptr;
    PortalInfo_t* orangeExit = nullptr;
    if (!TryGetPortalPair(PortalTransitionSide::Blue, blueEntry, blueExit)
        || !TryGetPortalPair(PortalTransitionSide::Orange, orangeEntry, orangeExit)
        || !blueEntry || !blueExit || !orangeEntry || !orangeExit)
    {
        ClearPhase(currentTime, "portal-pair-unavailable");
        return;
    }

    const PortalPlayerAnchor anchor = BuildPlayerAnchor(player);
    const PortalProbe blueProbe = BuildProbe(cmd, anchor, PortalTransitionSide::Blue, *blueEntry, *blueExit);
    const PortalProbe orangeProbe = BuildProbe(cmd, anchor, PortalTransitionSide::Orange, *orangeEntry, *orangeExit);

    if (ShouldLog(currentTime, m_nextProbeLogTime, 0.35f))
    {
        U::LogDebug("[PortalSim] probe phase=%s blue eyeD=%.2f originD=%.2f centerD=%.2f feetD=%.2f inside=%s cmdDot=%.2f velDot=%.2f; orange eyeD=%.2f originD=%.2f centerD=%.2f feetD=%.2f inside=%s cmdDot=%.2f velDot=%.2f.\n",
            PhaseName(m_context.phase),
            blueProbe.eyeDistance,
            blueProbe.originDistance,
            blueProbe.centerDistance,
            blueProbe.feetDistance,
            BoolText(blueProbe.insideAperture),
            blueProbe.cmdDot,
            blueProbe.velDot,
            orangeProbe.eyeDistance,
            orangeProbe.originDistance,
            orangeProbe.centerDistance,
            orangeProbe.feetDistance,
            BoolText(orangeProbe.insideAperture),
            orangeProbe.cmdDot,
            orangeProbe.velDot);
    }

    UpdatePhase(player, SelectBestProbe(blueProbe, orangeProbe), currentTime);
}

bool CPortalTransitionSimulator::IsLocalPlayerTransitioning() const
{
    return m_context.phase != PortalTransitionPhase::Idle
        && m_context.phase != PortalTransitionPhase::Cooldown;
}

bool CPortalTransitionSimulator::IsInCollisionBridgePhase() const
{
    return m_context.phase == PortalTransitionPhase::ApproachingPortal
        || m_context.phase == PortalTransitionPhase::IntersectingPortal
        || m_context.phase == PortalTransitionPhase::ExitingPortal;
}

C_TerrorPlayer* CPortalTransitionSimulator::GetLocalPlayer() const
{
    if (!I::EngineClient || !I::ClientEntityList)
        return nullptr;

    const int localIndex = I::EngineClient->GetLocalPlayer();
    if (localIndex <= 0)
        return nullptr;

    IClientEntity* entity = I::ClientEntityList->GetClientEntity(localIndex);
    return entity ? entity->As<C_TerrorPlayer*>() : nullptr;
}

bool CPortalTransitionSimulator::ArePortalsReady() const
{
    return IsPortalOpenForTransition(G::G_L4D2Portal.g_BluePortal)
        && IsPortalOpenForTransition(G::G_L4D2Portal.g_OrangePortal);
}

bool CPortalTransitionSimulator::TryGetPortalPair(PortalTransitionSide entrySide, PortalInfo_t*& entry, PortalInfo_t*& exit) const
{
    switch (entrySide)
    {
    case PortalTransitionSide::Blue:
        entry = &G::G_L4D2Portal.g_BluePortal;
        exit = &G::G_L4D2Portal.g_OrangePortal;
        return true;
    case PortalTransitionSide::Orange:
        entry = &G::G_L4D2Portal.g_OrangePortal;
        exit = &G::G_L4D2Portal.g_BluePortal;
        return true;
    default:
        entry = nullptr;
        exit = nullptr;
        return false;
    }
}

PortalPlayerAnchor CPortalTransitionSimulator::BuildPlayerAnchor(C_TerrorPlayer* player) const
{
    PortalPlayerAnchor anchor;
    if (!player)
        return anchor;

    anchor.origin = player->m_vecOrigin();
    anchor.eye = player->EyePosition();
    const Vector& eyeAngles = player->EyeAngles();
    anchor.viewAngles.x = eyeAngles.x;
    anchor.viewAngles.y = eyeAngles.y;
    anchor.viewAngles.z = eyeAngles.z;
    anchor.velocity = player->m_vecVelocity();
    anchor.center = player->WorldSpaceCenter();
    anchor.feet = anchor.origin;
    return anchor;
}

CPortalTransitionSimulator::PortalProbe CPortalTransitionSimulator::BuildProbe(CUserCmd* cmd, const PortalPlayerAnchor& anchor, PortalTransitionSide side, PortalInfo_t& entry, PortalInfo_t& exit) const
{
    PortalProbe probe;
    probe.side = side;
    probe.entry = &entry;
    probe.exit = &exit;
    probe.originDistance = SignedDistanceToPortal(entry, anchor.origin);
    probe.eyeDistance = SignedDistanceToPortal(entry, anchor.eye);
    probe.centerDistance = SignedDistanceToPortal(entry, anchor.center);
    probe.feetDistance = SignedDistanceToPortal(entry, anchor.feet);

    const PortalTransform::PortalAperture aperture = DefaultAperture();
    probe.originInside = PortalTransform::IsPointInsideAperture(entry, anchor.origin, aperture);
    probe.eyeInside = PortalTransform::IsPointInsideAperture(entry, anchor.eye, aperture);
    probe.centerInside = PortalTransform::IsPointInsideAperture(entry, anchor.center, aperture);
    probe.feetInside = PortalTransform::IsPointInsideAperture(entry, anchor.feet, aperture);

    const bool apertureProjected = probe.originInside || probe.eyeInside || probe.centerInside || probe.feetInside;
    probe.insideAperture = apertureProjected
        && std::fabs(AbsMin(probe.eyeDistance, probe.centerDistance)) <= kExitDistance;

    const Vector commandMove = cmd ? BuildCommandMoveDirection(*cmd) : Vector(0.0f, 0.0f, 0.0f);
    probe.cmdDot = commandMove.Dot(entry.normal);
    probe.velDot = anchor.velocity.Dot(entry.normal);
    probe.movingIntoPortal = probe.cmdDot <= kMoveIntoPortalDot || probe.velDot <= kMoveIntoPortalDot;
    return probe;
}

const CPortalTransitionSimulator::PortalProbe* CPortalTransitionSimulator::SelectBestProbe(const PortalProbe& blue, const PortalProbe& orange) const
{
    if (m_context.phase != PortalTransitionPhase::Idle
        && m_context.phase != PortalTransitionPhase::Cooldown)
    {
        if (m_context.entrySide == PortalTransitionSide::Blue && ShouldTrackNearPortal(blue))
            return &blue;
        if (m_context.entrySide == PortalTransitionSide::Orange && ShouldTrackNearPortal(orange))
            return &orange;
    }

    const bool blueCanApproach = ShouldEnterApproach(blue) || ShouldEnterIntersecting(blue);
    const bool orangeCanApproach = ShouldEnterApproach(orange) || ShouldEnterIntersecting(orange);
    if (blueCanApproach && !orangeCanApproach)
        return &blue;
    if (orangeCanApproach && !blueCanApproach)
        return &orange;
    if (!blueCanApproach && !orangeCanApproach)
        return nullptr;

    const float blueBestDistance = std::fabs(AbsMin(blue.eyeDistance, blue.centerDistance));
    const float orangeBestDistance = std::fabs(AbsMin(orange.eyeDistance, orange.centerDistance));
    return blueBestDistance <= orangeBestDistance ? &blue : &orange;
}

void CPortalTransitionSimulator::UpdatePhase(C_TerrorPlayer* player, const PortalProbe* probe, float currentTime)
{
    (void)player;

    if (!probe)
    {
        if (!ShouldStayInCurrentPhase(nullptr))
            ClearPhase(currentTime, "no-active-probe");
        return;
    }

    switch (m_context.phase)
    {
    case PortalTransitionPhase::Idle:
    case PortalTransitionPhase::Cooldown:
        if (ShouldEnterIntersecting(*probe))
            SetPhase(PortalTransitionPhase::IntersectingPortal, probe, currentTime, "initial-intersection");
        else if (ShouldEnterApproach(*probe))
            SetPhase(PortalTransitionPhase::ApproachingPortal, probe, currentTime, "approach");
        break;
    case PortalTransitionPhase::ApproachingPortal:
        if (ShouldEnterIntersecting(*probe))
            SetPhase(PortalTransitionPhase::IntersectingPortal, probe, currentTime, "aperture-intersection");
        else if (!ShouldEnterApproach(*probe))
            ClearPhase(currentTime, "approach-lost");
        else
            SetPhase(PortalTransitionPhase::ApproachingPortal, probe, currentTime, "approach-update");
        break;
    case PortalTransitionPhase::IntersectingPortal:
        if (!ShouldStayInCurrentPhase(probe))
            ClearPhase(currentTime, "intersection-lost");
        else
            SetPhase(PortalTransitionPhase::IntersectingPortal, probe, currentTime, "intersection-update");
        break;
    case PortalTransitionPhase::CommittingTeleport:
    case PortalTransitionPhase::ExitingPortal:
        SetPhase(m_context.phase, probe, currentTime, "phase1-observation-only");
        break;
    }
}

void CPortalTransitionSimulator::SetPhase(PortalTransitionPhase phase, const PortalProbe* probe, float currentTime, const char* reason)
{
    const PortalTransitionPhase oldPhase = m_context.phase;
    const PortalTransitionSide oldEntry = m_context.entrySide;

    if (oldPhase != phase || (probe && oldEntry != probe->side))
    {
        U::LogInfo("[PortalSim] phase %s/%s -> %s/%s reason=%s depth=%.2f inside=%s moving=%s cmdDot=%.2f velDot=%.2f.\n",
            PhaseName(oldPhase),
            SideName(oldEntry),
            PhaseName(phase),
            probe ? SideName(probe->side) : SideName(PortalTransitionSide::None),
            reason ? reason : "unknown",
            probe ? AbsMin(probe->eyeDistance, probe->centerDistance) : 0.0f,
            probe ? BoolText(probe->insideAperture) : "false",
            probe ? BoolText(probe->movingIntoPortal) : "false",
            probe ? probe->cmdDot : 0.0f,
            probe ? probe->velDot : 0.0f);
    }
    else if (ShouldLog(currentTime, m_nextPhaseLogTime, 0.75f))
    {
        U::LogDebug("[PortalSim] phase hold %s/%s reason=%s depth=%.2f inside=%s moving=%s.\n",
            PhaseName(phase),
            probe ? SideName(probe->side) : SideName(m_context.entrySide),
            reason ? reason : "unknown",
            probe ? AbsMin(probe->eyeDistance, probe->centerDistance) : m_context.signedDepth,
            BoolText(probe ? probe->insideAperture : m_context.insideAperture),
            BoolText(probe ? probe->movingIntoPortal : m_context.movingIntoPortal));
    }

    if (m_context.phase == PortalTransitionPhase::Idle && phase != PortalTransitionPhase::Idle)
        m_context.enterTime = currentTime;

    m_context.phase = phase;
    m_context.entrySide = probe ? probe->side : PortalTransitionSide::None;
    m_context.exitSide = probe ? (probe->side == PortalTransitionSide::Blue ? PortalTransitionSide::Orange : PortalTransitionSide::Blue) : PortalTransitionSide::None;
    m_context.lastUpdateTime = currentTime;
    m_context.signedDepth = probe ? AbsMin(probe->eyeDistance, probe->centerDistance) : 0.0f;
    m_context.insideAperture = probe ? probe->insideAperture : false;
    m_context.movingIntoPortal = probe ? probe->movingIntoPortal : false;
    m_context.hasValidExitPlacement = false;
}

void CPortalTransitionSimulator::ClearPhase(float currentTime, const char* reason)
{
    if (m_context.phase != PortalTransitionPhase::Idle)
    {
        U::LogInfo("[PortalSim] phase %s/%s -> Idle/None reason=%s lastDepth=%.2f inside=%s moving=%s.\n",
            PhaseName(m_context.phase),
            SideName(m_context.entrySide),
            reason ? reason : "unknown",
            m_context.signedDepth,
            BoolText(m_context.insideAperture),
            BoolText(m_context.movingIntoPortal));
    }

    m_context = {};
    m_context.lastUpdateTime = currentTime;
}

bool CPortalTransitionSimulator::ShouldEnterApproach(const PortalProbe& probe) const
{
    const float distance = std::fabs(AbsMin(probe.eyeDistance, probe.centerDistance));
    return probe.insideAperture
        && probe.movingIntoPortal
        && distance <= kApproachDistance;
}

bool CPortalTransitionSimulator::ShouldEnterIntersecting(const PortalProbe& probe) const
{
    const float distance = std::fabs(AbsMin(probe.eyeDistance, probe.centerDistance));
    return probe.insideAperture
        && probe.movingIntoPortal
        && distance <= kIntersectDistance;
}

bool CPortalTransitionSimulator::ShouldTrackNearPortal(const PortalProbe& probe) const
{
    const float distance = std::fabs(AbsMin(probe.eyeDistance, probe.centerDistance));
    return probe.insideAperture
        && distance <= kExitDistance;
}

bool CPortalTransitionSimulator::ShouldStayInCurrentPhase(const PortalProbe* probe) const
{
    if (!probe)
        return false;

    return ShouldTrackNearPortal(*probe);
}

bool CPortalTransitionSimulator::ShouldLog(float currentTime, float& nextLogTime, float intervalSeconds) const
{
    if (currentTime < nextLogTime)
        return false;

    nextLogTime = currentTime + intervalSeconds;
    return true;
}

const char* CPortalTransitionSimulator::PhaseName(PortalTransitionPhase phase) const
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

const char* CPortalTransitionSimulator::SideName(PortalTransitionSide side) const
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

const char* CPortalTransitionSimulator::BoolText(bool value) const
{
    return value ? "true" : "false";
}
