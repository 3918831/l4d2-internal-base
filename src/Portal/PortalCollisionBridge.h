#pragma once

#include "../SDK/SDK.h"
#include "../SDK/L4D2/Interfaces/EngineTrace.h"
#include "PortalTransitionSimulator.h"

struct PortalInfo_t;
class C_TerrorPlayer;

struct PortalTraceRequest
{
    Vector start;
    Vector end;
    unsigned int mask = 0;
    int collisionGroup = 0;
    trace_t* trace = nullptr;
};

struct PortalCollisionBridgeDiagnostics
{
    unsigned int totalRequests = 0;
    unsigned int eligibleRequests = 0;
    unsigned int rejectedByPhase = 0;
    unsigned int rejectedByPortalPair = 0;
    unsigned int rejectedByAperture = 0;
    unsigned int acceptedBypasses = 0;

    Vector lastStart;
    Vector lastEnd;
    float lastOriginalFraction = 0.0f;
    bool lastOriginalStartSolid = false;
    bool lastOriginalAllSolid = false;
    PortalTransitionPhase lastPhase = PortalTransitionPhase::Idle;
    PortalTransitionSide lastEntrySide = PortalTransitionSide::None;
    bool lastAccepted = false;

    Vector lastAcceptedStart;
    Vector lastAcceptedEnd;
    Vector lastAcceptedHit;
    float lastAcceptedStartDistance = 0.0f;
    float lastAcceptedEndDistance = 0.0f;
    float lastAcceptedOriginalFraction = 0.0f;
    bool lastAcceptedOriginalStartSolid = false;
    bool lastAcceptedOriginalAllSolid = false;

    float lastRejectedApertureStartDistance = 0.0f;
    float lastRejectedApertureEndDistance = 0.0f;
};

class CPortalCollisionBridge
{
public:
    void Reset();
    bool TryBypassPlayerBBoxTrace(const PortalTraceRequest& request, const CPortalTransitionSimulator& simulator);
    const PortalCollisionBridgeDiagnostics& GetDiagnostics() const { return m_diagnostics; }

private:
    bool TryGetPortalPair(PortalTransitionSide entrySide, PortalInfo_t*& entry, PortalInfo_t*& exit) const;
    bool IsPortalOpenForCollisionBridge(const PortalInfo_t& portal) const;
    bool IsTraceEligible(const PortalTraceRequest& request) const;
    bool IsTraceThroughActiveAperture(const PortalTraceRequest& request, const PortalInfo_t& entry, const PortalTransitionContext& context, Vector* intersection) const;
    bool IsMovementTraceTowardPortal(const PortalTraceRequest& request, const PortalInfo_t& entry) const;
    bool IsHullFrontInsideAperture(const PortalTraceRequest& request, const PortalInfo_t& entry, Vector* frontPoint) const;
    C_TerrorPlayer* GetLocalPlayer() const;
    void ClearTraceHit(const PortalTraceRequest& request) const;
    bool ShouldLog(float currentTime, float& nextLogTime, float intervalSeconds) const;

    const char* SideName(PortalTransitionSide side) const;
    const char* PhaseName(PortalTransitionPhase phase) const;
    const char* BoolText(bool value) const;

    float m_nextTraceLogTime = 0.0f;
    PortalCollisionBridgeDiagnostics m_diagnostics;
};
