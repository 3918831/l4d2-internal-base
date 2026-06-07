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

enum class PortalTraceClass
{
    Other,
    HorizontalMove,
    ZeroLengthPositionTest,
    VerticalGroundProbe,
    StepUpDownProbe,
};

struct PortalFrameTraceSnapshot
{
    bool hasTrace = false;
    bool eligible = false;
    bool accepted = false;
    Vector start;
    Vector end;
    Vector endPos;
    Vector planeNormal;
    float fraction = 0.0f;
    bool startSolid = false;
    bool allSolid = false;
    float startDistance = 0.0f;
    float endDistance = 0.0f;
    PortalTransitionPhase phase = PortalTransitionPhase::Idle;
    PortalTransitionSide entrySide = PortalTransitionSide::None;
    Vector playerOrigin;
    Vector playerVelocity;
    int playerFlags = 0;
    int groundEntity = -1;
};

struct PortalCollisionBridgeDiagnostics
{
    unsigned int totalRequests = 0;
    unsigned int eligibleRequests = 0;
    unsigned int rejectedByPhase = 0;
    unsigned int rejectedByPortalPair = 0;
    unsigned int rejectedByAperture = 0;
    unsigned int acceptedBypasses = 0;
    unsigned int horizontalAccepted = 0;
    unsigned int zeroLengthAccepted = 0;
    unsigned int startSolidAccepted = 0;
    unsigned int verticalRejected = 0;
    unsigned int groundLikeRejected = 0;

    Vector lastStart;
    Vector lastEnd;
    float lastOriginalFraction = 0.0f;
    bool lastOriginalStartSolid = false;
    bool lastOriginalAllSolid = false;
    PortalTransitionPhase lastPhase = PortalTransitionPhase::Idle;
    PortalTransitionSide lastEntrySide = PortalTransitionSide::None;
    PortalTraceClass lastClass = PortalTraceClass::Other;
    PortalTraceClass lastAcceptedClass = PortalTraceClass::Other;
    PortalTraceClass lastRejectedClass = PortalTraceClass::Other;
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

    int frameCommandNumber = 0;
    unsigned int frameTotal = 0;
    unsigned int frameHorizontal = 0;
    unsigned int frameZeroLength = 0;
    unsigned int frameVerticalGround = 0;
    unsigned int frameStepUpDown = 0;
    unsigned int frameOther = 0;
    unsigned int frameAccepted = 0;
    unsigned int frameRejectedByAperture = 0;
    unsigned int frameRejectedByPhase = 0;
    unsigned int frameRejectedByPair = 0;
    unsigned int frameStepAccepted = 0;
    unsigned int frameStepRejected = 0;
    bool frameHasStepTrace = false;
    bool frameLastStepAccepted = false;
    Vector frameLastStepStart;
    Vector frameLastStepEnd;
    Vector frameLastStepEndPos;
    Vector frameLastStepPlaneNormal;
    float frameLastStepFraction = 0.0f;
    bool frameLastStepStartSolid = false;
    bool frameLastStepAllSolid = false;
    float frameLastStepStartDistance = 0.0f;
    float frameLastStepEndDistance = 0.0f;
    PortalTransitionPhase frameLastStepPhase = PortalTransitionPhase::Idle;
    PortalTransitionSide frameLastStepEntrySide = PortalTransitionSide::None;
    Vector frameLastStepPlayerOrigin;
    Vector frameLastStepPlayerVelocity;
    int frameLastStepPlayerFlags = 0;
    int frameLastStepGroundEntity = -1;
    PortalFrameTraceSnapshot frameLastHorizontalTrace;
    PortalFrameTraceSnapshot frameLastOtherTrace;
};

class CPortalCollisionBridge
{
public:
    void Reset();
    void BeginFrame(int commandNumber);
    bool TryBypassPlayerBBoxTrace(const PortalTraceRequest& request, const CPortalTransitionSimulator& simulator);
    const PortalCollisionBridgeDiagnostics& GetDiagnostics() const { return m_diagnostics; }

private:
    void CountFrameTrace(PortalTraceClass traceClass);
    void RecordFrameDecision(PortalTraceClass traceClass, bool accepted);
    void RecordStepTrace(const PortalTraceRequest& request, const CPortalTransitionSimulator& simulator, PortalTraceClass traceClass, bool accepted, const PortalInfo_t* entry);
    void RecordFrameTraceSnapshot(const PortalTraceRequest& request, const CPortalTransitionSimulator& simulator, PortalTraceClass traceClass, bool eligible, bool accepted, const PortalInfo_t* entry);
    bool TryGetPortalPair(PortalTransitionSide entrySide, PortalInfo_t*& entry, PortalInfo_t*& exit) const;
    bool IsPortalOpenForCollisionBridge(const PortalInfo_t& portal) const;
    bool IsTraceEligible(const PortalTraceRequest& request) const;
    PortalTraceClass ClassifyTrace(const PortalTraceRequest& request) const;
    bool IsTraceThroughActiveAperture(const PortalTraceRequest& request, const PortalInfo_t& entry, const PortalTransitionContext& context, Vector* intersection) const;
    bool IsMovementTraceTowardPortal(const PortalTraceRequest& request, const PortalInfo_t& entry) const;
    bool IsHullFrontInsideAperture(const PortalTraceRequest& request, const PortalInfo_t& entry, Vector* frontPoint) const;
    C_TerrorPlayer* GetLocalPlayer() const;
    void ClearTraceHit(const PortalTraceRequest& request) const;
    bool ShouldLog(float currentTime, float& nextLogTime, float intervalSeconds) const;

    const char* SideName(PortalTransitionSide side) const;
    const char* PhaseName(PortalTransitionPhase phase) const;
    const char* TraceClassName(PortalTraceClass traceClass) const;
    const char* BoolText(bool value) const;

    float m_nextTraceLogTime = 0.0f;
    PortalCollisionBridgeDiagnostics m_diagnostics;
};
