#pragma once

#include "../SDK/SDK.h"
#include "PortalTransform.h"

class C_TerrorPlayer;
class C_BasePlayer;
class CUserCmd;
class CMoveData;
struct PortalInfo_t;

class CPortalTransition
{
public:
    void Reset();
    void Update(CUserCmd* cmd);
    void OnFinishMove(C_BasePlayer* player, CUserCmd* cmd, CMoveData* move);
    void ApplyVisualTransition(CViewSetup& view);
    void LogRenderEnvironmentSnapshot(const char* phase, const CViewSetup& view);
    void LogEnvironmentSnapshot(const char* phase, C_TerrorPlayer* player, const Vector& viewOrigin, const Vector& viewAngles, const Vector* referenceOrigin = nullptr);
    void LogHookProbe(const char* domain, const char* stage, const char* point, void* gameMovement, CMoveData* move);
    void LogMoveTypeProbe(const char* phase, C_BasePlayer* player = nullptr, CMoveData* move = nullptr);
    void ArmEnvironmentRenderTrace(int frameCount);
    void ArmToneTransition(const char* reason);
    bool ShouldLogPortalRenderState(const char* phase, int depth) const;

    bool ShouldBypassPlayerBBoxTrace(
        const Vector& start,
        const Vector& end,
        unsigned int mask,
        int collisionGroup,
        trace_t* trace);

private:
    enum class PortalSide
    {
        None,
        Blue,
        Orange,
    };

    enum class TraversalMode
    {
        Normal,
        InPortal,
        ExitingPortal,
    };

    struct PortalRuntimeState
    {
        float previousDistance = 0.0f;
        float previousEyeDistance = 0.0f;
        bool hasPreviousDistance = false;
        bool hasPreviousEyeDistance = false;
    };

    struct PlayerAnchor
    {
        Vector origin;
        Vector eye;
        Vector viewOffset;
        Vector center;
        Vector velocity;
    };

    struct TraversalSession
    {
        TraversalMode mode = TraversalMode::Normal;
        PortalSide entrySide = PortalSide::None;
        PortalSide exitSide = PortalSide::None;
        float enterTime = 0.0f;
        float lastAssistTime = 0.0f;
        float nextLogTime = 0.0f;
        Vector entryVelocity;
        bool usingNoclip = false;
        bool hasEntryVelocity = false;
        unsigned char savedMoveType = 0;
    };

    struct VisualTransitionState
    {
        bool active = false;
        bool loggedStart = false;
        PortalSide exitSide = PortalSide::None;
        float startTime = 0.0f;
        float endTime = 0.0f;
        float physicalExitDistance = 0.0f;
        float visualExitDistance = 0.0f;
        Vector exitNormal;
        Vector physicalEye;
    };

    struct ToneTransitionState
    {
        bool pending = false;
        bool active = false;
        bool loggedStart = false;
        bool loggedPending = false;
        float startTime = 0.0f;
        float endTime = 0.0f;
        float pendingUntil = 0.0f;
        Vector startScale;
        Vector targetScale;
        const char* reason = nullptr;
    };

    PortalRuntimeState m_blueState;
    PortalRuntimeState m_orangeState;
    TraversalSession m_session;
    VisualTransitionState m_visualTransition;
    ToneTransitionState m_toneTransition;
    PortalSide m_lastExitPortal = PortalSide::None;
    float m_nextTeleportTime = 0.0f;
    float m_nextStatusLogTime = 0.0f;
    float m_nextDistanceLogTime = 0.0f;
    float m_nextCrossingLogTime = 0.0f;
    float m_nextTraceLogTime = 0.0f;
    int m_environmentTraceSequence = 0;
    int m_pendingEnvironmentRenderFrames = 0;

    C_TerrorPlayer* GetLocalPlayer() const;
    bool ArePortalsReady() const;
    bool TryGetPortalPair(PortalSide entrySide, PortalInfo_t*& entry, PortalInfo_t*& exit) const;
    PortalRuntimeState& RuntimeStateForSide(PortalSide side);
    const PortalRuntimeState& RuntimeStateForSide(PortalSide side) const;
    PlayerAnchor BuildPlayerAnchor(C_TerrorPlayer* player) const;
    bool IsLocalPlayer(C_BasePlayer* player) const;
    bool IsPlayerInsidePortalAperture(C_TerrorPlayer* player, PortalInfo_t& portal, const PlayerAnchor& anchor) const;
    bool IsPointCrossingPortalAperture(const PortalInfo_t& portal, const Vector& start, const Vector& end, Vector* intersection = nullptr) const;
    bool TryBeginTraversal(C_TerrorPlayer* player, CUserCmd* cmd, PortalSide side, PortalInfo_t& entry, PortalInfo_t& exit);
    void UpdateTraversalExitState(C_TerrorPlayer* player);
    void ClearTraversalSession(C_TerrorPlayer* player, const char* reason);
    bool AssistPortalEmbedding(C_TerrorPlayer* player, CUserCmd* cmd, PortalInfo_t& entry, PortalInfo_t& exit);
    void ClampMoveToPortalAperture(C_TerrorPlayer* player, CMoveData* move, PortalInfo_t& entry);
    bool UpdatePortalCrossing(C_TerrorPlayer* player, CUserCmd* cmd, PortalSide side, PortalInfo_t& entry, PortalInfo_t& exit);
    bool TeleportLocalPlayer(C_TerrorPlayer* player, PortalInfo_t& entry, PortalInfo_t& exit, PortalSide exitSide, const PlayerAnchor* anchor = nullptr);
    bool EntityTeleport(void* entity, const Vector* origin, const QAngle* angles, const Vector* velocity, bool verbose = true) const;
    void* ResolveServerLocalPlayerForDiagnostics(void** toolsBase, void** edictBase, bool* resolverAgree) const;
    void RefreshPortalDistance(C_TerrorPlayer* player, PortalSide side, PortalInfo_t& portal);
    bool ShouldLog(float currentTime, float& nextLogTime, float intervalSeconds);
    const char* SideName(PortalSide side) const;
    const char* ModeName(TraversalMode mode) const;
    void LogPortalReadiness(float currentTime);
    void LogDistanceProbe(float currentTime, C_TerrorPlayer* player, PortalInfo_t& blueEntry, PortalInfo_t& orangeEntry);
};
