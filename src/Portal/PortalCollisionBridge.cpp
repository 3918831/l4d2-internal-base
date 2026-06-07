#include "PortalCollisionBridge.h"

#include <algorithm>
#include <cmath>

#include "../SDK/L4D2/Entities/C_TerrorPlayer.h"
#include "../SDK/L4D2/Interfaces/ClientEntityList.h"
#include "../SDK/L4D2/Interfaces/EngineClient.h"
#include "../Util/Logger/Logger.h"
#include "../Util/Math/Math.h"
#pragma warning(push)
#pragma warning(disable: 4819)
#include "L4D2_Portal.h"
#pragma warning(pop)

namespace
{
    constexpr float kNearPortalDistance = 96.0f;
    constexpr float kSessionApertureDistance = 80.0f;
    constexpr float kHullFrontBridgeDistance = 8.0f;
    constexpr float kMaxVerticalTraceZ = 8.0f;

    PortalTransform::PortalAperture BridgeAperture()
    {
        PortalTransform::PortalAperture aperture;
        aperture.halfWidth = 32.0f;
        aperture.halfHeight = 56.0f;
        aperture.tolerance = 6.0f;
        return aperture;
    }

    float SignedDistanceToPortal(const PortalInfo_t& portal, const Vector& point)
    {
        return (point - portal.origin).Dot(portal.normal);
    }

    float MaxAbs(float a, float b)
    {
        return std::max(std::fabs(a), std::fabs(b));
    }
}

void CPortalCollisionBridge::Reset()
{
    m_nextTraceLogTime = 0.0f;
    m_diagnostics = {};
}

bool CPortalCollisionBridge::TryBypassPlayerBBoxTrace(const PortalTraceRequest& request, const CPortalTransitionSimulator& simulator)
{
    ++m_diagnostics.totalRequests;
    m_diagnostics.lastStart = request.start;
    m_diagnostics.lastEnd = request.end;
    m_diagnostics.lastOriginalFraction = request.trace ? request.trace->fraction : 0.0f;
    m_diagnostics.lastOriginalStartSolid = request.trace ? request.trace->startsolid : false;
    m_diagnostics.lastOriginalAllSolid = request.trace ? request.trace->allsolid : false;
    m_diagnostics.lastPhase = simulator.GetContext().phase;
    m_diagnostics.lastEntrySide = simulator.GetContext().entrySide;
    m_diagnostics.lastAccepted = false;

    if (!IsTraceEligible(request))
        return false;
    ++m_diagnostics.eligibleRequests;

    const PortalTransitionContext& context = simulator.GetContext();
    if (!simulator.IsInCollisionBridgePhase() || context.entrySide == PortalTransitionSide::None)
    {
        ++m_diagnostics.rejectedByPhase;
        return false;
    }

    if (!I::EngineClient || !I::EngineClient->IsInGame())
        return false;

    PortalInfo_t* entry = nullptr;
    PortalInfo_t* exit = nullptr;
    if (!TryGetPortalPair(context.entrySide, entry, exit) || !entry || !exit)
    {
        ++m_diagnostics.rejectedByPortalPair;
        return false;
    }

    const float currentTime = I::EngineClient->OBSOLETE_Time();
    Vector intersection;
    if (!IsTraceThroughActiveAperture(request, *entry, context, &intersection))
    {
        ++m_diagnostics.rejectedByAperture;
        const float startDistance = SignedDistanceToPortal(*entry, request.start);
        const float endDistance = SignedDistanceToPortal(*entry, request.end);
        m_diagnostics.lastRejectedApertureStartDistance = startDistance;
        m_diagnostics.lastRejectedApertureEndDistance = endDistance;
        const bool closeToPortal = std::fabs(startDistance) <= kNearPortalDistance
            || std::fabs(endDistance) <= kNearPortalDistance
            || std::fabs(context.signedDepth) <= kNearPortalDistance;

        if (closeToPortal && ShouldLog(currentTime, m_nextTraceLogTime, 0.25f))
        {
            U::LogDebug("[PortalBridge] rejected side=%s phase=%s startD=%.2f endD=%.2f ctxDepth=%.2f inside=%s moving=%s fraction=%.3f startsolid=%s allsolid=%s.\n",
                SideName(context.entrySide),
                PhaseName(context.phase),
                startDistance,
                endDistance,
                context.signedDepth,
                BoolText(context.insideAperture),
                BoolText(context.movingIntoPortal),
                request.trace ? request.trace->fraction : 0.0f,
                BoolText(request.trace ? request.trace->startsolid : false),
                BoolText(request.trace ? request.trace->allsolid : false));
        }
        return false;
    }

    trace_t* trace = request.trace;
    const float originalFraction = trace->fraction;
    const bool originalStartSolid = trace->startsolid;
    const bool originalAllSolid = trace->allsolid;
    ClearTraceHit(request);
    ++m_diagnostics.acceptedBypasses;
    m_diagnostics.lastAccepted = true;
    m_diagnostics.lastAcceptedStart = request.start;
    m_diagnostics.lastAcceptedEnd = request.end;
    m_diagnostics.lastAcceptedHit = intersection;
    m_diagnostics.lastAcceptedStartDistance = SignedDistanceToPortal(*entry, request.start);
    m_diagnostics.lastAcceptedEndDistance = SignedDistanceToPortal(*entry, request.end);
    m_diagnostics.lastAcceptedOriginalFraction = originalFraction;
    m_diagnostics.lastAcceptedOriginalStartSolid = originalStartSolid;
    m_diagnostics.lastAcceptedOriginalAllSolid = originalAllSolid;

    if (ShouldLog(currentTime, m_nextTraceLogTime, 0.20f))
    {
        U::LogInfo("[PortalBridge] bypass accepted side=%s phase=%s start=(%.1f %.1f %.1f) end=(%.1f %.1f %.1f) hit=(%.1f %.1f %.1f) ctxDepth=%.2f fraction=%.3f->%.3f startsolid=%s->false allsolid=%s->false.\n",
            SideName(context.entrySide),
            PhaseName(context.phase),
            request.start.x, request.start.y, request.start.z,
            request.end.x, request.end.y, request.end.z,
            intersection.x, intersection.y, intersection.z,
            context.signedDepth,
            originalFraction,
            trace->fraction,
            BoolText(originalStartSolid),
            BoolText(originalAllSolid));
    }
    return true;
}

bool CPortalCollisionBridge::TryGetPortalPair(PortalTransitionSide entrySide, PortalInfo_t*& entry, PortalInfo_t*& exit) const
{
    switch (entrySide)
    {
    case PortalTransitionSide::Blue:
        entry = &G::G_L4D2Portal.g_BluePortal;
        exit = &G::G_L4D2Portal.g_OrangePortal;
        return IsPortalOpenForCollisionBridge(*entry) && IsPortalOpenForCollisionBridge(*exit);
    case PortalTransitionSide::Orange:
        entry = &G::G_L4D2Portal.g_OrangePortal;
        exit = &G::G_L4D2Portal.g_BluePortal;
        return IsPortalOpenForCollisionBridge(*entry) && IsPortalOpenForCollisionBridge(*exit);
    default:
        entry = nullptr;
        exit = nullptr;
        return false;
    }
}

bool CPortalCollisionBridge::IsPortalOpenForCollisionBridge(const PortalInfo_t& portal) const
{
    return portal.bIsActive
        && !portal.bIsClosing
        && portal.animState != PORTAL_ANIM_CLOSING
        && portal.animState != PORTAL_ANIM_CLOSED;
}

bool CPortalCollisionBridge::IsTraceEligible(const PortalTraceRequest& request) const
{
    if (!request.trace)
        return false;

    if (request.trace->fraction >= 1.0f && !request.trace->startsolid && !request.trace->allsolid)
        return false;

    // CMoveData::SetAbsOrigin validates candidate positions with a zero-length
    // TracePlayerBBox(pos, pos). Portal aperture positions must be allowed there
    // too, otherwise movement traces succeed but the final origin write is rejected.
    return true;
}

bool CPortalCollisionBridge::IsTraceThroughActiveAperture(const PortalTraceRequest& request, const PortalInfo_t& entry, const PortalTransitionContext& context, Vector* intersection) const
{
    const float startDistance = SignedDistanceToPortal(entry, request.start);
    const float endDistance = SignedDistanceToPortal(entry, request.end);
    const bool nearPortal = std::fabs(startDistance) <= kNearPortalDistance
        || std::fabs(endDistance) <= kNearPortalDistance
        || std::fabs(context.signedDepth) <= kNearPortalDistance;
    if (!nearPortal)
        return false;

    if (!IsMovementTraceTowardPortal(request, entry))
        return false;

    const PortalTransform::PortalAperture aperture = BridgeAperture();
    const bool endInsideAperture = PortalTransform::IsPointInsideAperture(entry, request.end, aperture)
        && std::fabs(endDistance) <= kSessionApertureDistance;

    bool crossesPlane = false;
    Vector hit = request.end;
    const float distanceDelta = endDistance - startDistance;
    if (std::fabs(distanceDelta) > 0.001f && startDistance * endDistance <= 0.0f)
    {
        const float t = startDistance / (startDistance - endDistance);
        hit = request.start + (request.end - request.start) * t;
        crossesPlane = PortalTransform::IsPointInsideAperture(entry, hit, aperture);
    }

    if (!crossesPlane && !endInsideAperture)
    {
        Vector hullFront;
        const bool hullFrontInside =
            context.insideAperture
            && context.movingIntoPortal
            && IsHullFrontInsideAperture(request, entry, &hullFront);
        if (!hullFrontInside)
            return false;

        if (intersection)
            *intersection = hullFront;
        return true;
    }

    if (intersection)
        *intersection = crossesPlane ? hit : request.end;

    return true;
}

bool CPortalCollisionBridge::IsMovementTraceTowardPortal(const PortalTraceRequest& request, const PortalInfo_t& entry) const
{
    const Vector delta = request.end - request.start;
    const float horizontalSqr = delta.x * delta.x + delta.y * delta.y;
    const float verticalAbs = std::fabs(delta.z);

    if (horizontalSqr < 0.25f && verticalAbs > 0.5f)
        return false;

    if (verticalAbs > kMaxVerticalTraceZ && verticalAbs * verticalAbs > horizontalSqr * 0.50f)
        return false;

    const float startDistance = SignedDistanceToPortal(entry, request.start);
    const float endDistance = SignedDistanceToPortal(entry, request.end);
    const float moveTowardPortal = delta.Dot(entry.normal);
    if (moveTowardPortal > 0.1f && endDistance > startDistance)
        return false;

    return true;
}

bool CPortalCollisionBridge::IsHullFrontInsideAperture(const PortalTraceRequest& request, const PortalInfo_t& entry, Vector* frontPoint) const
{
    C_TerrorPlayer* player = GetLocalPlayer();
    if (!player)
        return false;

    const Vector mins = player->GetPlayerMins();
    const Vector maxs = player->GetPlayerMaxs();
    const float minForward = -(
        std::fabs(entry.normal.x) * MaxAbs(mins.x, maxs.x)
        + std::fabs(entry.normal.y) * MaxAbs(mins.y, maxs.y)
        + std::fabs(entry.normal.z) * MaxAbs(mins.z, maxs.z));

    const PortalTransform::PortalAperture aperture = BridgeAperture();
    bool foundFrontCorner = false;
    bool allFrontCornersInside = true;
    Vector frontSum;
    int frontCount = 0;

    const float xs[2] = { mins.x, maxs.x };
    const float ys[2] = { mins.y, maxs.y };
    const float zs[2] = { mins.z, maxs.z };

    for (float x : xs)
    {
        for (float y : ys)
        {
            for (float z : zs)
            {
                const Vector offset(x, y, z);
                const float projectedForward = offset.Dot(entry.normal);
                if (projectedForward > minForward + 0.1f)
                    continue;

                const Vector corner = request.end + offset;
                const float cornerDistance = SignedDistanceToPortal(entry, corner);
                if (cornerDistance > kHullFrontBridgeDistance)
                    return false;

                foundFrontCorner = true;
                ++frontCount;
                frontSum += corner;

                if (!PortalTransform::IsPointInsideAperture(entry, corner, aperture))
                    allFrontCornersInside = false;
            }
        }
    }

    if (!foundFrontCorner || !allFrontCornersInside)
        return false;

    if (frontPoint)
        *frontPoint = frontSum / static_cast<float>(frontCount);

    return true;
}

C_TerrorPlayer* CPortalCollisionBridge::GetLocalPlayer() const
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

void CPortalCollisionBridge::ClearTraceHit(const PortalTraceRequest& request) const
{
    trace_t* trace = request.trace;
    if (!trace)
        return;

    trace->startpos = request.start;
    trace->endpos = request.end;
    trace->plane = {};
    trace->fraction = 1.0f;
    trace->contents = 0;
    trace->dispFlags = 0;
    trace->allsolid = false;
    trace->startsolid = false;
    trace->fractionleftsolid = 0.0f;
    trace->surface = {};
    trace->hitgroup = 0;
    trace->physicsbone = 0;
    trace->m_pEnt = nullptr;
    trace->hitbox = 0;
}

bool CPortalCollisionBridge::ShouldLog(float currentTime, float& nextLogTime, float intervalSeconds) const
{
    if (currentTime < nextLogTime)
        return false;

    nextLogTime = currentTime + intervalSeconds;
    return true;
}

const char* CPortalCollisionBridge::SideName(PortalTransitionSide side) const
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

const char* CPortalCollisionBridge::PhaseName(PortalTransitionPhase phase) const
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

const char* CPortalCollisionBridge::BoolText(bool value) const
{
    return value ? "true" : "false";
}
