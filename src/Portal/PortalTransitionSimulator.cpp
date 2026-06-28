#include "PortalTransitionSimulator.h"

#include <algorithm>
#include <cmath>

#include "../SDK/L4D2/Entities/C_TerrorPlayer.h"
#include "../SDK/L4D2/Includes/usercmd.h"
#include "../SDK/L4D2/Interfaces/ClientEntityList.h"
#include "../SDK/L4D2/Interfaces/EngineClient.h"
#include "../Util/Logger/Logger.h"
#include "../Util/Logger/PortalFileLog.h"
#include "PortalPlayerTeleport.h"
#include "PortalTransitionDecision.h"
#pragma warning(push)
#pragma warning(disable: 4819)
#include "L4D2_Portal.h"
#pragma warning(pop)

namespace
{
    constexpr float kApproachDistance = 56.0f;
    constexpr float kIntersectDistance = 24.0f;
    constexpr float kExitDistance = 72.0f;
    constexpr float kExitReleaseDistance = 24.0f;
    constexpr float kExitPlacementEpsilon = 2.0f;
    constexpr float kTeleportCooldown = 0.20f;
    constexpr float kMoveIntoPortalDot = -20.0f;
    constexpr float kPredictedCrossingDepth = 1.5f;

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
    m_lastCommittedMovementCommandNumber = 0;
    m_lastCommittedMovementOrigin = Vector();
    m_lastCommittedMovementVelocity = Vector();
    m_lastCommittedMovementAngles = QAngle();
    m_hasLastCommittedMovement = false;
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
    LogTraversalFrame(cmd, player, anchor, blueProbe, orangeProbe);

    if (m_context.phase == PortalTransitionPhase::ExitingPortal)
    {
        const PortalProbe& exitProbe = m_context.exitSide == PortalTransitionSide::Blue ? blueProbe : orangeProbe;
        UpdateExitPhase(exitProbe, currentTime);
        return;
    }

    if (m_context.phase == PortalTransitionPhase::Cooldown)
    {
        if (currentTime >= m_context.cooldownUntil)
            ClearPhase(currentTime, "teleport-cooldown-complete");
        return;
    }

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

    UpdatePhase(player, cmd, SelectBestProbe(blueProbe, orangeProbe), currentTime);
}

void CPortalTransitionSimulator::LogTraversalFrame(
    CUserCmd* cmd,
    C_TerrorPlayer* player,
    const PortalPlayerAnchor& anchor,
    const PortalProbe& blue,
    const PortalProbe& orange) const
{
    if (!cmd || !player)
        return;

    const PortalProbe* tracked = nullptr;
    if (m_context.phase == PortalTransitionPhase::ExitingPortal)
        tracked = m_context.exitSide == PortalTransitionSide::Blue ? &blue : &orange;
    else if (m_context.entrySide == PortalTransitionSide::Blue)
        tracked = &blue;
    else if (m_context.entrySide == PortalTransitionSide::Orange)
        tracked = &orange;
    else
    {
        const float blueDistance = std::fabs(blue.centerDistance);
        const float orangeDistance = std::fabs(orange.centerDistance);
        const PortalProbe* nearest = blueDistance <= orangeDistance ? &blue : &orange;
        if (std::min(blueDistance, orangeDistance) <= kExitDistance)
            tracked = nearest;
    }

    if (!tracked)
        return;

    const float speed = std::sqrt(anchor.velocity.LenghtSqr());
    U::PortalFileLog::WriteFormat(
        "[PortalTraversalFrame] cmd=%d phase=%s tracked=%s entry=%s exit=%s origin=(%.2f %.2f %.2f) eye=(%.2f %.2f %.2f) center=(%.2f %.2f %.2f) depth(origin=%.2f eye=%.2f center=%.2f feet=%.2f) velocity=(%.2f %.2f %.2f) speed=%.2f velDot=%.2f input=(f=%.1f s=%.1f u=%.1f) cmdDot=%.2f flags=0x%X ground=0x%X inside=%s moving=%s\n",
        cmd->command_number,
        PhaseName(m_context.phase),
        SideName(tracked->side),
        SideName(m_context.entrySide),
        SideName(m_context.exitSide),
        anchor.origin.x, anchor.origin.y, anchor.origin.z,
        anchor.eye.x, anchor.eye.y, anchor.eye.z,
        anchor.center.x, anchor.center.y, anchor.center.z,
        tracked->originDistance,
        tracked->eyeDistance,
        tracked->centerDistance,
        tracked->feetDistance,
        anchor.velocity.x, anchor.velocity.y, anchor.velocity.z,
        speed,
        tracked->velDot,
        cmd->forwardmove,
        cmd->sidemove,
        cmd->upmove,
        tracked->cmdDot,
        player->m_fFlags(),
        player->m_hGroundEntity().ToInt(),
        BoolText(tracked->insideAperture),
        BoolText(tracked->movingIntoPortal));
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

PortalTransitionSide CPortalTransitionSimulator::GetCollisionBridgeSide() const
{
    return m_context.phase == PortalTransitionPhase::ExitingPortal
        ? m_context.exitSide
        : m_context.entrySide;
}

bool CPortalTransitionSimulator::TryCommitMovementCrossing(
    int commandNumber,
    const Vector& movementOrigin,
    const Vector& movementVelocity,
    Vector* committedOrigin,
    Vector* committedVelocity)
{
    if (m_context.phase != PortalTransitionPhase::IntersectingPortal
        || m_context.entrySide == PortalTransitionSide::None
        || m_context.exitSide == PortalTransitionSide::None)
    {
        return false;
    }

    PortalInfo_t* entry = nullptr;
    PortalInfo_t* exit = nullptr;
    if (!TryGetPortalPair(m_context.entrySide, entry, exit) || !entry || !exit)
        return false;

    C_TerrorPlayer* player = GetLocalPlayer();
    if (!player)
        return false;

    PortalPlayerAnchor anchor = BuildPlayerAnchor(player);
    const Vector originFromCenter = anchor.origin - anchor.center;
    const Vector eyeFromOrigin = anchor.eye - anchor.origin;
    anchor.origin = movementOrigin;
    anchor.center = movementOrigin - originFromCenter;
    anchor.feet = movementOrigin;
    anchor.eye = movementOrigin + eyeFromOrigin;
    anchor.velocity = movementVelocity;

    PortalProbe probe;
    probe.side = m_context.entrySide;
    probe.entry = entry;
    probe.exit = exit;
    probe.originDistance = SignedDistanceToPortal(*entry, anchor.origin);
    probe.eyeDistance = SignedDistanceToPortal(*entry, anchor.eye);
    probe.centerDistance = SignedDistanceToPortal(*entry, anchor.center);
    probe.feetDistance = SignedDistanceToPortal(*entry, anchor.feet);
    probe.originInside = PortalTransform::IsPointInsideAperture(*entry, anchor.origin, DefaultAperture());
    probe.eyeInside = PortalTransform::IsPointInsideAperture(*entry, anchor.eye, DefaultAperture());
    probe.centerInside = PortalTransform::IsPointInsideAperture(*entry, anchor.center, DefaultAperture());
    probe.feetInside = PortalTransform::IsPointInsideAperture(*entry, anchor.feet, DefaultAperture());
    probe.insideAperture = probe.originInside || probe.eyeInside || probe.centerInside || probe.feetInside;
    probe.velDot = movementVelocity.Dot(entry->normal);
    probe.movingIntoPortal = m_context.movingIntoPortal || probe.velDot <= kMoveIntoPortalDot;

    if (!m_context.hasSignedDepth
        || !probe.insideAperture
        || !probe.movingIntoPortal
        || m_context.signedDepth < -0.5f
        || probe.centerDistance > 1.5f)
    {
        return false;
    }

    U::LogInfo("[PortalTeleport] predicted movement crossing entry=%s prevDepth=%.2f predictedDepth=%.2f velocity=(%.1f %.1f %.1f).\n",
        SideName(m_context.entrySide),
        m_context.signedDepth,
        probe.centerDistance,
        movementVelocity.x, movementVelocity.y, movementVelocity.z);

    const float currentTime = I::EngineClient ? I::EngineClient->OBSOLETE_Time() : 0.0f;
    return TryCommitTeleport(player, nullptr, probe, currentTime, &anchor, committedOrigin, committedVelocity, commandNumber);
}

bool CPortalTransitionSimulator::TryGetCommittedMovementForCommand(
    int commandNumber,
    Vector* committedOrigin,
    Vector* committedVelocity,
    QAngle* committedAngles) const
{
    if (!m_hasLastCommittedMovement || commandNumber <= 0)
        return false;

    const int commandDelta = commandNumber - m_lastCommittedMovementCommandNumber;
    if (commandDelta < 0 || commandDelta > 1)
        return false;

    if (committedOrigin)
        *committedOrigin = m_lastCommittedMovementOrigin;
    if (committedVelocity)
        *committedVelocity = m_lastCommittedMovementVelocity;
    if (committedAngles)
        *committedAngles = m_lastCommittedMovementAngles;
    return true;
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

void CPortalTransitionSimulator::UpdatePhase(C_TerrorPlayer* player, CUserCmd* cmd, const PortalProbe* probe, float currentTime)
{
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
        if (ShouldPredictPlaneCrossing(*probe))
        {
            TryCommitPredictedPlaneCrossing(player, cmd, *probe, currentTime);
            break;
        }

        if (ShouldEnterIntersecting(*probe))
            SetPhase(PortalTransitionPhase::IntersectingPortal, probe, currentTime, "aperture-intersection");
        else if (!ShouldEnterApproach(*probe))
            ClearPhase(currentTime, "approach-lost");
        else
            SetPhase(PortalTransitionPhase::ApproachingPortal, probe, currentTime, "approach-update");
        break;
    case PortalTransitionPhase::IntersectingPortal:
        if (ShouldPredictPlaneCrossing(*probe))
        {
            TryCommitPredictedPlaneCrossing(player, cmd, *probe, currentTime);
            break;
        }

        if (PortalTransitionDecision::ShouldCommitPlaneCrossing(
            m_context.signedDepth,
            probe->centerDistance,
            m_context.hasSignedDepth,
            probe->insideAperture,
            probe->movingIntoPortal))
        {
            TryCommitTeleport(player, cmd, *probe, currentTime);
            break;
        }

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

void CPortalTransitionSimulator::UpdateExitPhase(const PortalProbe& exitProbe, float currentTime)
{
    m_context.lastUpdateTime = currentTime;
    m_context.signedDepth = exitProbe.centerDistance;
    m_context.insideAperture = exitProbe.insideAperture;
    m_context.movingIntoPortal = false;
    m_context.hasSignedDepth = true;

    const bool clearOfExitPlane = exitProbe.centerDistance >= kExitReleaseDistance;
    const bool leftExitAperture = !exitProbe.insideAperture;
    if (currentTime >= m_context.cooldownUntil && (clearOfExitPlane || leftExitAperture))
    {
        U::LogInfo("[PortalSim] phase ExitingPortal/%s -> Cooldown/%s reason=exit-cleared depth=%.2f inside=%s.\n",
            SideName(m_context.entrySide),
            SideName(m_context.exitSide),
            exitProbe.centerDistance,
            BoolText(exitProbe.insideAperture));
        m_context.phase = PortalTransitionPhase::Cooldown;
    }
}

bool CPortalTransitionSimulator::TryCommitPredictedPlaneCrossing(
    C_TerrorPlayer* player,
    CUserCmd* cmd,
    const PortalProbe& probe,
    float currentTime)
{
    if (!player || !probe.entry || !probe.exit || probe.velDot >= -0.01f)
        return false;

    float interval = 1.0f / 30.0f;
    if (I::GlobalVars && I::GlobalVars->interval_per_tick > 0.0f)
        interval = I::GlobalVars->interval_per_tick;

    const float leadTime = std::min(interval, std::max(0.0f, (probe.centerDistance - kPredictedCrossingDepth) / -probe.velDot));
    PortalPlayerAnchor anchor = BuildPlayerAnchor(player);
    const Vector projectedDelta = anchor.velocity * leadTime;
    anchor.origin = anchor.origin + projectedDelta;
    anchor.eye = anchor.eye + projectedDelta;
    anchor.center = anchor.center + projectedDelta;
    anchor.feet = anchor.feet + projectedDelta;

    PortalProbe projectedProbe = probe;
    projectedProbe.originDistance = SignedDistanceToPortal(*probe.entry, anchor.origin);
    projectedProbe.eyeDistance = SignedDistanceToPortal(*probe.entry, anchor.eye);
    projectedProbe.centerDistance = SignedDistanceToPortal(*probe.entry, anchor.center);
    projectedProbe.feetDistance = SignedDistanceToPortal(*probe.entry, anchor.feet);
    projectedProbe.originInside = PortalTransform::IsPointInsideAperture(*probe.entry, anchor.origin, DefaultAperture());
    projectedProbe.eyeInside = PortalTransform::IsPointInsideAperture(*probe.entry, anchor.eye, DefaultAperture());
    projectedProbe.centerInside = PortalTransform::IsPointInsideAperture(*probe.entry, anchor.center, DefaultAperture());
    projectedProbe.feetInside = PortalTransform::IsPointInsideAperture(*probe.entry, anchor.feet, DefaultAperture());
    projectedProbe.insideAperture = projectedProbe.originInside || projectedProbe.eyeInside || projectedProbe.centerInside || projectedProbe.feetInside;

    U::LogInfo("[PortalTeleport] simulator projected crossing entry=%s leadTime=%.4f depth=%.2f->%.2f velocity=(%.1f %.1f %.1f).\n",
        SideName(probe.side),
        leadTime,
        probe.centerDistance,
        projectedProbe.centerDistance,
        anchor.velocity.x, anchor.velocity.y, anchor.velocity.z);

    return TryCommitTeleport(player, cmd, projectedProbe, currentTime, &anchor);
}

bool CPortalTransitionSimulator::TryCommitTeleport(
    C_TerrorPlayer* player,
    CUserCmd* cmd,
    const PortalProbe& probe,
    float currentTime,
    const PortalPlayerAnchor* overrideAnchor,
    Vector* committedOrigin,
    Vector* committedVelocity,
    int commandNumberOverride)
{
    if (!player || !probe.entry || !probe.exit)
        return false;

    SetPhase(PortalTransitionPhase::CommittingTeleport, &probe, currentTime, "center-crossed-portal-plane");

    matrix3x4_t entryToExit;
    if (!PortalTransform::BuildEntryToExitMatrix(*probe.entry, *probe.exit, entryToExit))
    {
        U::LogWarning("[PortalTeleport] commit failed: entry-to-exit transform is invalid.\n");
        m_context.phase = PortalTransitionPhase::IntersectingPortal;
        m_context.signedDepth = 0.0f;
        m_context.hasSignedDepth = true;
        return false;
    }

    const PortalPlayerAnchor anchor = overrideAnchor ? *overrideAnchor : BuildPlayerAnchor(player);
    const Vector originFromCenter = anchor.origin - anchor.center;
    const Vector transformedCenter = PortalTransform::TransformPoint(entryToExit, anchor.center);
    const Vector mins = player->GetPlayerMins();
    const Vector maxs = player->GetPlayerMaxs();
    const Vector hullHalfExtents(
        (maxs.x - mins.x) * 0.5f,
        (maxs.y - mins.y) * 0.5f,
        (maxs.z - mins.z) * 0.5f);
    const float hullHalfExtentAlongExitNormal =
        std::fabs(probe.exit->normal.x) * hullHalfExtents.x
        + std::fabs(probe.exit->normal.y) * hullHalfExtents.y
        + std::fabs(probe.exit->normal.z) * hullHalfExtents.z;
    const float transformedCenterDepth = SignedDistanceToPortal(*probe.exit, transformedCenter);
    const float exitClearancePush = PortalTransitionDecision::ComputeExitClearancePush(
        transformedCenterDepth,
        hullHalfExtentAlongExitNormal,
        kExitPlacementEpsilon);
    const Vector newOrigin = transformedCenter + originFromCenter + probe.exit->normal * exitClearancePush;
    const Vector newVelocity = PortalTransform::TransformVector(entryToExit, anchor.velocity);

    QAngle sourceAngles = anchor.viewAngles;
    if (cmd)
    {
        sourceAngles.x = cmd->viewangles.x;
        sourceAngles.y = cmd->viewangles.y;
        sourceAngles.z = cmd->viewangles.z;
    }
    else if (I::EngineClient)
    {
        Vector engineAngles;
        I::EngineClient->GetViewAngles(engineAngles);
        sourceAngles.x = engineAngles.x;
        sourceAngles.y = engineAngles.y;
        sourceAngles.z = engineAngles.z;
    }
    const QAngle newAngles = PortalTransform::TransformAngles(entryToExit, sourceAngles);

    if (!PortalPlayerTeleport::Commit(player, newOrigin, newAngles, newVelocity))
    {
        U::LogWarning("[PortalTeleport] commit failed entry=%s exit=%s depth=%.2f.\n",
            SideName(probe.side),
            SideName(probe.side == PortalTransitionSide::Blue ? PortalTransitionSide::Orange : PortalTransitionSide::Blue),
            probe.centerDistance);
        m_context.phase = PortalTransitionPhase::IntersectingPortal;
        m_context.signedDepth = 0.0f;
        m_context.hasSignedDepth = true;
        return false;
    }

    // Defer local view-angle application until the movement sync point writes
    // the matching predicted origin/velocity. Applying the camera here can
    // render a frame with exit-facing angles while the client is still at entry.
    player->m_vecVelocity() = newVelocity;
    if (committedOrigin)
        *committedOrigin = newOrigin;
    if (committedVelocity)
        *committedVelocity = newVelocity;

    const PortalTransitionSide exitSide = probe.side == PortalTransitionSide::Blue
        ? PortalTransitionSide::Orange
        : PortalTransitionSide::Blue;
    const Vector newCenter = newOrigin - originFromCenter;

    m_context.phase = PortalTransitionPhase::ExitingPortal;
    m_context.entrySide = probe.side;
    m_context.exitSide = exitSide;
    m_context.lastUpdateTime = currentTime;
    m_context.signedDepth = SignedDistanceToPortal(*probe.exit, newCenter);
    m_context.cooldownUntil = currentTime + kTeleportCooldown;
    m_context.teleportCommandNumber = cmd ? cmd->command_number : commandNumberOverride;
    m_context.insideAperture = PortalTransform::IsPointInsideAperture(*probe.exit, newCenter, DefaultAperture());
    m_context.movingIntoPortal = false;
    m_context.hasValidExitPlacement = true;
    m_context.hasSignedDepth = true;

    U::LogInfo("[PortalTeleport] committed entry=%s exit=%s cmd=%d rawExitDepth=%.2f hullExtent=%.2f clearancePush=%.2f finalExitDepth=%.2f oldCenter=(%.1f %.1f %.1f) newCenter=(%.1f %.1f %.1f) velocity=(%.1f %.1f %.1f)->(%.1f %.1f %.1f).\n",
        SideName(probe.side),
        SideName(exitSide),
        m_context.teleportCommandNumber,
        transformedCenterDepth,
        hullHalfExtentAlongExitNormal,
        exitClearancePush,
        m_context.signedDepth,
        anchor.center.x, anchor.center.y, anchor.center.z,
        newCenter.x, newCenter.y, newCenter.z,
        anchor.velocity.x, anchor.velocity.y, anchor.velocity.z,
        newVelocity.x, newVelocity.y, newVelocity.z);

    m_lastCommittedMovementCommandNumber = m_context.teleportCommandNumber;
    m_lastCommittedMovementOrigin = newOrigin;
    m_lastCommittedMovementVelocity = newVelocity;
    m_lastCommittedMovementAngles = newAngles;
    m_hasLastCommittedMovement = m_lastCommittedMovementCommandNumber > 0;
    return true;
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
    m_context.signedDepth = probe ? probe->centerDistance : 0.0f;
    m_context.insideAperture = probe ? probe->insideAperture : false;
    m_context.movingIntoPortal = probe ? probe->movingIntoPortal : false;
    m_context.hasValidExitPlacement = false;
    m_context.hasSignedDepth = probe != nullptr;
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

bool CPortalTransitionSimulator::ShouldPredictPlaneCrossing(const PortalProbe& probe) const
{
    if (!m_context.hasSignedDepth
        || !probe.insideAperture
        || !probe.movingIntoPortal
        || probe.centerDistance < 0.0f
        || probe.velDot >= kMoveIntoPortalDot)
    {
        return false;
    }

    float interval = 1.0f / 30.0f;
    if (I::GlobalVars && I::GlobalVars->interval_per_tick > 0.0f)
        interval = I::GlobalVars->interval_per_tick;

    const float predictedDepth = probe.centerDistance + probe.velDot * interval;
    if (predictedDepth > kPredictedCrossingDepth)
        return false;

    U::LogInfo("[PortalTeleport] simulator predicted crossing entry=%s depth=%.2f predictedDepth=%.2f velDot=%.2f interval=%.4f.\n",
        SideName(probe.side),
        probe.centerDistance,
        predictedDepth,
        probe.velDot,
        interval);
    return true;
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
