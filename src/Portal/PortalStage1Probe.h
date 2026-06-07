#pragma once

#include "../SDK/SDK.h"
#include "PortalCollisionBridge.h"
#include "PortalTransitionSimulator.h"

class CUserCmd;

class CPortalStage1Probe
{
public:
    void Reset();
    void Update(CUserCmd* cmd, const CPortalTransitionSimulator& simulator, const CPortalCollisionBridge& bridge);
    void DumpNow(const CPortalTransitionSimulator& simulator, const CPortalCollisionBridge& bridge) const;

private:
    void LogInterfaceSnapshot() const;
    void LogRuntimeSnapshot(CUserCmd* cmd, const CPortalTransitionSimulator& simulator, const CPortalCollisionBridge& bridge) const;

    const char* PhaseName(PortalTransitionPhase phase) const;
    const char* SideName(PortalTransitionSide side) const;
    const char* BoolText(bool value) const;

    float m_nextInterfaceLogTime = 0.0f;
    float m_nextRuntimeLogTime = 0.0f;
};
