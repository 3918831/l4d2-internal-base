#include "PortalCollisionBridge.h"
#include "PortalPhysicsMode.h"

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
    constexpr bool kVerboseBridgeTraceLog = false;

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

void CPortalCollisionBridge::BeginFrame(int commandNumber)
{
    m_diagnostics.frameCommandNumber = commandNumber;
    m_diagnostics.frameTotal = 0;
    m_diagnostics.frameHorizontal = 0;
    m_diagnostics.frameZeroLength = 0;
    m_diagnostics.frameVerticalGround = 0;
    m_diagnostics.frameStepUpDown = 0;
    m_diagnostics.frameOther = 0;
    m_diagnostics.frameAccepted = 0;
    m_diagnostics.frameRejectedByAperture = 0;
    m_diagnostics.frameRejectedByPhase = 0;
    m_diagnostics.frameRejectedByPair = 0;
    m_diagnostics.frameStepAccepted = 0;
    m_diagnostics.frameStepRejected = 0;
    m_diagnostics.frameHasStepTrace = false;
    m_diagnostics.frameLastStepAccepted = false;
    m_diagnostics.frameLastStepStart = Vector();
    m_diagnostics.frameLastStepEnd = Vector();
    m_diagnostics.frameLastStepEndPos = Vector();
    m_diagnostics.frameLastStepPlaneNormal = Vector();
    m_diagnostics.frameLastStepFraction = 0.0f;
    m_diagnostics.frameLastStepStartSolid = false;
    m_diagnostics.frameLastStepAllSolid = false;
    m_diagnostics.frameLastStepStartDistance = 0.0f;
    m_diagnostics.frameLastStepEndDistance = 0.0f;
    m_diagnostics.frameLastStepPhase = PortalTransitionPhase::Idle;
    m_diagnostics.frameLastStepEntrySide = PortalTransitionSide::None;
    m_diagnostics.frameLastStepPlayerOrigin = Vector();
    m_diagnostics.frameLastStepPlayerVelocity = Vector();
    m_diagnostics.frameLastStepPlayerFlags = 0;
    m_diagnostics.frameLastStepGroundEntity = -1;
    m_diagnostics.frameLastHorizontalTrace = {};
    m_diagnostics.frameLastOtherTrace = {};
}

bool CPortalCollisionBridge::TryBypassPlayerBBoxTrace(const PortalTraceRequest& request, const CPortalTransitionSimulator& simulator)
{
    if (!PortalPhysicsMode::ShouldUseLegacyCollisionBypass())
        return false;

    const PortalTraceClass traceClass = ClassifyTrace(request);
    CountFrameTrace(traceClass);
    ++m_diagnostics.totalRequests;
    m_diagnostics.lastStart = request.start;
    m_diagnostics.lastEnd = request.end;
    m_diagnostics.lastOriginalFraction = request.trace ? request.trace->fraction : 0.0f;
    m_diagnostics.lastOriginalStartSolid = request.trace ? request.trace->startsolid : false;
    m_diagnostics.lastOriginalAllSolid = request.trace ? request.trace->allsolid : false;
    m_diagnostics.lastPhase = simulator.GetContext().phase;
    m_diagnostics.lastEntrySide = simulator.GetCollisionBridgeSide();
    m_diagnostics.lastClass = traceClass;
    m_diagnostics.lastAccepted = false;

    if (!IsTraceEligible(request))
    {
        RecordFrameTraceSnapshot(request, simulator, traceClass, false, false, nullptr);
        return false;
    }
    ++m_diagnostics.eligibleRequests;

    const PortalTransitionContext& context = simulator.GetContext();
    const PortalTransitionSide collisionSide = simulator.GetCollisionBridgeSide();
    if (!simulator.IsInCollisionBridgePhase() || collisionSide == PortalTransitionSide::None)
    {
        ++m_diagnostics.rejectedByPhase;
        ++m_diagnostics.frameRejectedByPhase;
        RecordFrameTraceSnapshot(request, simulator, traceClass, true, false, nullptr);
        RecordFrameDecision(traceClass, false);
        return false;
    }

    if (!I::EngineClient || !I::EngineClient->IsInGame())
    {
        RecordFrameTraceSnapshot(request, simulator, traceClass, true, false, nullptr);
        RecordFrameDecision(traceClass, false);
        return false;
    }

    PortalInfo_t* entry = nullptr;
    PortalInfo_t* exit = nullptr;
    if (!TryGetPortalPair(collisionSide, entry, exit) || !entry || !exit)
    {
        ++m_diagnostics.rejectedByPortalPair;
        ++m_diagnostics.frameRejectedByPair;
        RecordFrameTraceSnapshot(request, simulator, traceClass, true, false, nullptr);
        RecordFrameDecision(traceClass, false);
        return false;
    }

    const float currentTime = I::EngineClient->OBSOLETE_Time();
    Vector intersection;
    if (!IsTraceThroughActiveAperture(request, *entry, context, &intersection))
    {
        const bool bypassStepProbe = IsStepProbeInsideActiveAperture(request, *entry, context, &intersection);
        if (bypassStepProbe)
        {
            trace_t* trace = request.trace;
            const float originalFraction = trace->fraction;
            const bool originalStartSolid = trace->startsolid;
            const bool originalAllSolid = trace->allsolid;
            RecordStepTrace(request, simulator, traceClass, true, entry);
            RecordFrameTraceSnapshot(request, simulator, traceClass, true, true, entry);
            RecordFrameDecision(traceClass, true);
            ClearTraceHit(request);
            ++m_diagnostics.acceptedBypasses;
            ++m_diagnostics.frameAccepted;

            m_diagnostics.lastAccepted = true;
            m_diagnostics.lastAcceptedClass = traceClass;
            m_diagnostics.lastAcceptedStart = request.start;
            m_diagnostics.lastAcceptedEnd = request.end;
            m_diagnostics.lastAcceptedHit = intersection;
            m_diagnostics.lastAcceptedStartDistance = SignedDistanceToPortal(*entry, request.start);
            m_diagnostics.lastAcceptedEndDistance = SignedDistanceToPortal(*entry, request.end);
            m_diagnostics.lastAcceptedOriginalFraction = originalFraction;
            m_diagnostics.lastAcceptedOriginalStartSolid = originalStartSolid;
            m_diagnostics.lastAcceptedOriginalAllSolid = originalAllSolid;

            if (kVerboseBridgeTraceLog && ShouldLog(currentTime, m_nextTraceLogTime, 0.20f))
            {
                U::LogInfo("[PortalBridge] step probe bypass accepted class=%s side=%s phase=%s start=(%.1f %.1f %.1f) end=(%.1f %.1f %.1f) ctxDepth=%.2f fraction=%.3f->%.3f startsolid=%s->false allsolid=%s->false.\n",
                    TraceClassName(traceClass),
                    SideName(collisionSide),
                    PhaseName(context.phase),
                    request.start.x, request.start.y, request.start.z,
                    request.end.x, request.end.y, request.end.z,
                    context.signedDepth,
                    originalFraction,
                    trace->fraction,
                    BoolText(originalStartSolid),
                    BoolText(originalAllSolid));
            }
            return true;
        }

        ++m_diagnostics.rejectedByAperture;
        ++m_diagnostics.frameRejectedByAperture;
        m_diagnostics.lastRejectedClass = traceClass;
        if (traceClass == PortalTraceClass::VerticalGroundProbe)
            ++m_diagnostics.verticalRejected;
        if (traceClass == PortalTraceClass::VerticalGroundProbe || traceClass == PortalTraceClass::StepUpDownProbe)
            ++m_diagnostics.groundLikeRejected;

        const float startDistance = SignedDistanceToPortal(*entry, request.start);
        const float endDistance = SignedDistanceToPortal(*entry, request.end);
        m_diagnostics.lastRejectedApertureStartDistance = startDistance;
        m_diagnostics.lastRejectedApertureEndDistance = endDistance;
        const bool closeToPortal = std::fabs(startDistance) <= kNearPortalDistance
            || std::fabs(endDistance) <= kNearPortalDistance
            || std::fabs(context.signedDepth) <= kNearPortalDistance;

        if (kVerboseBridgeTraceLog && closeToPortal && ShouldLog(currentTime, m_nextTraceLogTime, 0.25f))
        {
            U::LogDebug("[PortalBridge] rejected class=%s side=%s phase=%s startD=%.2f endD=%.2f ctxDepth=%.2f inside=%s moving=%s fraction=%.3f startsolid=%s allsolid=%s.\n",
                TraceClassName(traceClass),
                SideName(collisionSide),
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
        RecordStepTrace(request, simulator, traceClass, false, entry);
        RecordFrameTraceSnapshot(request, simulator, traceClass, true, false, entry);
        RecordFrameDecision(traceClass, false);
        return false;
    }

    trace_t* trace = request.trace;
    const float originalFraction = trace->fraction;
    const bool originalStartSolid = trace->startsolid;
    const bool originalAllSolid = trace->allsolid;
    RecordStepTrace(request, simulator, traceClass, true, entry);
    RecordFrameTraceSnapshot(request, simulator, traceClass, true, true, entry);
    RecordFrameDecision(traceClass, true);
    ClearTraceHit(request);
    ++m_diagnostics.acceptedBypasses;
    ++m_diagnostics.frameAccepted;
    if (traceClass == PortalTraceClass::HorizontalMove)
        ++m_diagnostics.horizontalAccepted;
    if (traceClass == PortalTraceClass::ZeroLengthPositionTest)
        ++m_diagnostics.zeroLengthAccepted;
    if (originalStartSolid || originalAllSolid)
        ++m_diagnostics.startSolidAccepted;

    m_diagnostics.lastAccepted = true;
    m_diagnostics.lastAcceptedClass = traceClass;
    m_diagnostics.lastAcceptedStart = request.start;
    m_diagnostics.lastAcceptedEnd = request.end;
    m_diagnostics.lastAcceptedHit = intersection;
    m_diagnostics.lastAcceptedStartDistance = SignedDistanceToPortal(*entry, request.start);
    m_diagnostics.lastAcceptedEndDistance = SignedDistanceToPortal(*entry, request.end);
    m_diagnostics.lastAcceptedOriginalFraction = originalFraction;
    m_diagnostics.lastAcceptedOriginalStartSolid = originalStartSolid;
    m_diagnostics.lastAcceptedOriginalAllSolid = originalAllSolid;

    if (kVerboseBridgeTraceLog && ShouldLog(currentTime, m_nextTraceLogTime, 0.20f))
    {
        U::LogInfo("[PortalBridge] bypass accepted class=%s side=%s phase=%s start=(%.1f %.1f %.1f) end=(%.1f %.1f %.1f) hit=(%.1f %.1f %.1f) ctxDepth=%.2f fraction=%.3f->%.3f startsolid=%s->false allsolid=%s->false.\n",
            TraceClassName(traceClass),
            SideName(collisionSide),
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

void CPortalCollisionBridge::CountFrameTrace(PortalTraceClass traceClass)
{
    ++m_diagnostics.frameTotal;
    switch (traceClass)
    {
    case PortalTraceClass::HorizontalMove:
        ++m_diagnostics.frameHorizontal;
        break;
    case PortalTraceClass::ZeroLengthPositionTest:
        ++m_diagnostics.frameZeroLength;
        break;
    case PortalTraceClass::VerticalGroundProbe:
        ++m_diagnostics.frameVerticalGround;
        break;
    case PortalTraceClass::StepUpDownProbe:
        ++m_diagnostics.frameStepUpDown;
        break;
    case PortalTraceClass::Other:
    default:
        ++m_diagnostics.frameOther;
        break;
    }
}

void CPortalCollisionBridge::RecordFrameDecision(PortalTraceClass traceClass, bool accepted)
{
    if (traceClass != PortalTraceClass::StepUpDownProbe)
        return;

    if (accepted)
        ++m_diagnostics.frameStepAccepted;
    else
        ++m_diagnostics.frameStepRejected;
}

void CPortalCollisionBridge::RecordStepTrace(const PortalTraceRequest& request, const CPortalTransitionSimulator& simulator, PortalTraceClass traceClass, bool accepted, const PortalInfo_t* entry)
{
    if (traceClass != PortalTraceClass::StepUpDownProbe)
        return;

    trace_t* trace = request.trace;
    const PortalTransitionContext& context = simulator.GetContext();
    C_TerrorPlayer* player = GetLocalPlayer();

    m_diagnostics.frameHasStepTrace = true;
    m_diagnostics.frameLastStepAccepted = accepted;
    m_diagnostics.frameLastStepStart = request.start;
    m_diagnostics.frameLastStepEnd = request.end;
    m_diagnostics.frameLastStepEndPos = trace ? trace->endpos : Vector();
    m_diagnostics.frameLastStepPlaneNormal = trace ? trace->plane.normal : Vector();
    m_diagnostics.frameLastStepFraction = trace ? trace->fraction : 0.0f;
    m_diagnostics.frameLastStepStartSolid = trace ? trace->startsolid : false;
    m_diagnostics.frameLastStepAllSolid = trace ? trace->allsolid : false;
    m_diagnostics.frameLastStepStartDistance = entry ? SignedDistanceToPortal(*entry, request.start) : 0.0f;
    m_diagnostics.frameLastStepEndDistance = entry ? SignedDistanceToPortal(*entry, request.end) : 0.0f;
    m_diagnostics.frameLastStepPhase = context.phase;
    m_diagnostics.frameLastStepEntrySide = simulator.GetCollisionBridgeSide();
    m_diagnostics.frameLastStepPlayerOrigin = player ? player->m_vecOrigin() : Vector();
    m_diagnostics.frameLastStepPlayerVelocity = player ? player->m_vecVelocity() : Vector();
    m_diagnostics.frameLastStepPlayerFlags = player ? player->m_fFlags() : 0;
    m_diagnostics.frameLastStepGroundEntity = player ? player->m_hGroundEntity().ToInt() : -1;
}

void CPortalCollisionBridge::RecordFrameTraceSnapshot(const PortalTraceRequest& request, const CPortalTransitionSimulator& simulator, PortalTraceClass traceClass, bool eligible, bool accepted, const PortalInfo_t* entry)
{
    if (traceClass != PortalTraceClass::HorizontalMove && traceClass != PortalTraceClass::Other)
        return;

    trace_t* trace = request.trace;
    const PortalTransitionContext& context = simulator.GetContext();
    C_TerrorPlayer* player = GetLocalPlayer();

    PortalFrameTraceSnapshot snapshot;
    snapshot.hasTrace = true;
    snapshot.eligible = eligible;
    snapshot.accepted = accepted;
    snapshot.start = request.start;
    snapshot.end = request.end;
    snapshot.endPos = trace ? trace->endpos : Vector();
    snapshot.planeNormal = trace ? trace->plane.normal : Vector();
    snapshot.fraction = trace ? trace->fraction : 0.0f;
    snapshot.startSolid = trace ? trace->startsolid : false;
    snapshot.allSolid = trace ? trace->allsolid : false;
    snapshot.startDistance = entry ? SignedDistanceToPortal(*entry, request.start) : 0.0f;
    snapshot.endDistance = entry ? SignedDistanceToPortal(*entry, request.end) : 0.0f;
    snapshot.phase = context.phase;
    snapshot.entrySide = simulator.GetCollisionBridgeSide();
    snapshot.playerOrigin = player ? player->m_vecOrigin() : Vector();
    snapshot.playerVelocity = player ? player->m_vecVelocity() : Vector();
    snapshot.playerFlags = player ? player->m_fFlags() : 0;
    snapshot.groundEntity = player ? player->m_hGroundEntity().ToInt() : -1;

    if (traceClass == PortalTraceClass::HorizontalMove)
        m_diagnostics.frameLastHorizontalTrace = snapshot;
    else
        m_diagnostics.frameLastOtherTrace = snapshot;
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

PortalTraceClass CPortalCollisionBridge::ClassifyTrace(const PortalTraceRequest& request) const
{
    const Vector delta = request.end - request.start;
    const float horizontalSqr = delta.x * delta.x + delta.y * delta.y;
    const float verticalAbs = std::fabs(delta.z);

    if (horizontalSqr < 0.0001f && verticalAbs < 0.0001f)
        return PortalTraceClass::ZeroLengthPositionTest;

    if (horizontalSqr < 0.25f && verticalAbs > 0.5f && delta.z < 0.0f && verticalAbs <= 4.0f)
        return PortalTraceClass::VerticalGroundProbe;

    if (horizontalSqr < 16.0f && verticalAbs > kMaxVerticalTraceZ)
        return PortalTraceClass::StepUpDownProbe;

    if (horizontalSqr >= 0.25f && verticalAbs <= kMaxVerticalTraceZ)
        return PortalTraceClass::HorizontalMove;

    return PortalTraceClass::Other;
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

bool CPortalCollisionBridge::IsStepProbeInsideActiveAperture(const PortalTraceRequest& request, const PortalInfo_t& entry, const PortalTransitionContext& context, Vector* intersection) const
{
    if (ClassifyTrace(request) != PortalTraceClass::StepUpDownProbe)
        return false;

    if (context.phase != PortalTransitionPhase::IntersectingPortal || !context.insideAperture || !context.movingIntoPortal)
        return false;

    const float startDistance = SignedDistanceToPortal(entry, request.start);
    const float endDistance = SignedDistanceToPortal(entry, request.end);
    if (std::fabs(startDistance) > kNearPortalDistance && std::fabs(endDistance) > kNearPortalDistance)
        return false;

    const Vector midpoint = (request.start + request.end) * 0.5f;
    const PortalTransform::PortalAperture aperture = BridgeAperture();
    if (!PortalTransform::IsPointInsideAperture(entry, midpoint, aperture))
        return false;

    if (intersection)
        *intersection = midpoint;

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

const char* CPortalCollisionBridge::TraceClassName(PortalTraceClass traceClass) const
{
    switch (traceClass)
    {
    case PortalTraceClass::HorizontalMove: return "HorizontalMove";
    case PortalTraceClass::ZeroLengthPositionTest: return "ZeroLengthPositionTest";
    case PortalTraceClass::VerticalGroundProbe: return "VerticalGroundProbe";
    case PortalTraceClass::StepUpDownProbe: return "StepUpDownProbe";
    case PortalTraceClass::Other:
    default:
        return "Other";
    }
}

const char* CPortalCollisionBridge::BoolText(bool value) const
{
    return value ? "true" : "false";
}
