#pragma once

#include "../SDK/SDK.h"
#include "PortalCollisionBridge.h"
#include "PortalTransitionSimulator.h"

class CUserCmd;
class C_BasePlayer;
class CMoveData;

struct PortalMoveFrameDiagnostics
{
    bool valid = false;
    int commandNumber = 0;
    bool bridgePhase = false;
    PortalTransitionPhase phase = PortalTransitionPhase::Idle;
    PortalTransitionSide entrySide = PortalTransitionSide::None;

    Vector moveOrigin;
    Vector moveVelocity;
    float stepHeight = 0.0f;
    bool gameCodeMovedPlayer = false;

    Vector playerOrigin;
    Vector playerVelocity;
    int playerFlags = 0;
    int groundEntity = -1;
};

class CPortalStage1Probe
{
public:
    void Reset();
    void Update(CUserCmd* cmd, const CPortalTransitionSimulator& simulator, const CPortalCollisionBridge& bridge);
    void DumpNow(const CPortalTransitionSimulator& simulator, const CPortalCollisionBridge& bridge) const;
    PortalMoveFrameDiagnostics CaptureMoveFrame(C_BasePlayer* player, CUserCmd* cmd, CMoveData* move, const CPortalTransitionSimulator& simulator) const;
    void LogFinishMoveDiagnostics(const PortalMoveFrameDiagnostics& before, const PortalMoveFrameDiagnostics& after) const;
    void LogFrameTraceDiagnostics(const CPortalCollisionBridge& bridge, const PortalMoveFrameDiagnostics& after) const;

private:
    void LogInterfaceSnapshot() const;
    void LogRuntimeSnapshot(CUserCmd* cmd, const CPortalTransitionSimulator& simulator, const CPortalCollisionBridge& bridge) const;

    const char* PhaseName(PortalTransitionPhase phase) const;
    const char* SideName(PortalTransitionSide side) const;
    const char* TraceClassName(PortalTraceClass traceClass) const;
    const char* BoolText(bool value) const;

    float m_nextInterfaceLogTime = 0.0f;
    float m_nextRuntimeLogTime = 0.0f;
};
