#pragma once

#include "../SDK/SDK.h"
#include "PortalTransform.h"

class C_TerrorPlayer;
class CUserCmd;
struct PortalInfo_t;

enum class PortalTransitionPhase
{
    Idle,
    ApproachingPortal,
    IntersectingPortal,
    CommittingTeleport,
    ExitingPortal,
    Cooldown,
};

enum class PortalTransitionSide
{
    None,
    Blue,
    Orange,
};

struct PortalPlayerAnchor
{
    Vector origin;
    Vector eye;
    Vector center;
    Vector feet;
    Vector velocity;
    QAngle viewAngles;
};

struct PortalTransitionContext
{
    PortalTransitionPhase phase = PortalTransitionPhase::Idle;
    PortalTransitionSide entrySide = PortalTransitionSide::None;
    PortalTransitionSide exitSide = PortalTransitionSide::None;
    float enterTime = 0.0f;
    float lastUpdateTime = 0.0f;
    float signedDepth = 0.0f;
    bool insideAperture = false;
    bool movingIntoPortal = false;
    bool hasValidExitPlacement = false;
};

class CPortalTransitionSimulator
{
public:
    void Reset();
    void Update(CUserCmd* cmd);

    PortalTransitionPhase GetPhase() const { return m_context.phase; }
    const PortalTransitionContext& GetContext() const { return m_context; }
    bool IsLocalPlayerTransitioning() const;
    bool IsInCollisionBridgePhase() const;

private:
    struct PortalProbe
    {
        PortalTransitionSide side = PortalTransitionSide::None;
        PortalInfo_t* entry = nullptr;
        PortalInfo_t* exit = nullptr;
        float originDistance = 0.0f;
        float eyeDistance = 0.0f;
        float centerDistance = 0.0f;
        float feetDistance = 0.0f;
        float cmdDot = 0.0f;
        float velDot = 0.0f;
        bool originInside = false;
        bool eyeInside = false;
        bool centerInside = false;
        bool feetInside = false;
        bool insideAperture = false;
        bool movingIntoPortal = false;
    };

    C_TerrorPlayer* GetLocalPlayer() const;
    bool ArePortalsReady() const;
    bool TryGetPortalPair(PortalTransitionSide entrySide, PortalInfo_t*& entry, PortalInfo_t*& exit) const;
    PortalPlayerAnchor BuildPlayerAnchor(C_TerrorPlayer* player) const;
    PortalProbe BuildProbe(CUserCmd* cmd, const PortalPlayerAnchor& anchor, PortalTransitionSide side, PortalInfo_t& entry, PortalInfo_t& exit) const;
    const PortalProbe* SelectBestProbe(const PortalProbe& blue, const PortalProbe& orange) const;

    void UpdatePhase(C_TerrorPlayer* player, const PortalProbe* probe, float currentTime);
    void SetPhase(PortalTransitionPhase phase, const PortalProbe* probe, float currentTime, const char* reason);
    void ClearPhase(float currentTime, const char* reason);

    bool ShouldEnterApproach(const PortalProbe& probe) const;
    bool ShouldEnterIntersecting(const PortalProbe& probe) const;
    bool ShouldTrackNearPortal(const PortalProbe& probe) const;
    bool ShouldStayInCurrentPhase(const PortalProbe* probe) const;
    bool ShouldLog(float currentTime, float& nextLogTime, float intervalSeconds) const;

    const char* PhaseName(PortalTransitionPhase phase) const;
    const char* SideName(PortalTransitionSide side) const;
    const char* BoolText(bool value) const;

    PortalTransitionContext m_context;
    float m_nextReadinessLogTime = 0.0f;
    float m_nextProbeLogTime = 0.0f;
    float m_nextPhaseLogTime = 0.0f;
};
