#include "PortalBspPhase1.h"

#include "PortalBspData.h"
#include "PortalPhysicsMode.h"
#include "PortalTransitionDecision.h"
#include "../SDK/SDK.h"
#include "../Util/Logger/Logger.h"

#include <cstring>

namespace
{
    const char* BoolText(bool value)
    {
        return value ? "true" : "false";
    }

    const char* OwnerName(PortalBrushOwner owner)
    {
        return owner == PortalBrushOwner::Blue ? "blue" : "orange";
    }

    void InitHullRay(
        Ray_t& ray,
        const Vector& start,
        const Vector& end,
        const Vector& mins,
        const Vector& maxs)
    {
        ray.m_Delta = end - start;
        ray.m_IsSwept = ray.m_Delta.LenghtSqr() != 0.0f;
        ray.m_Extents = (maxs - mins) * 0.5f;
        ray.m_IsRay = ray.m_Extents.LenghtSqr() < 1e-6f;
        const Vector center = (mins + maxs) * 0.5f;
        ray.m_Start = start + center;
        ray.m_StartOffset = center * -1.0f;
        ray.m_pWorldAxisTransform = nullptr;
    }

    C_TerrorPlayer* GetLocalPlayer()
    {
        if (!I::EngineClient || !I::ClientEntityList)
            return nullptr;
        const int index = I::EngineClient->GetLocalPlayer();
        if (index < 0)
            return nullptr;
        IClientEntity* entity = I::ClientEntityList->GetClientEntity(index);
        return entity ? entity->As<C_TerrorPlayer*>() : nullptr;
    }

    void LogHullTrace(
        const char* stage,
        PortalBrushOwner owner,
        const PortalBrushBinding& binding,
        const Vector& mins,
        const Vector& maxs)
    {
        if (!I::EngineTrace || !binding.resolved)
            return;

        const Vector hit(
            binding.hitPosition.x,
            binding.hitPosition.y,
            binding.hitPosition.z);
        const Vector normal(
            binding.hitNormal.x,
            binding.hitNormal.y,
            binding.hitNormal.z);
        const Vector start = hit + normal * 64.0f;
        const Vector end = hit - normal * 64.0f;

        Ray_t ray;
        InitHullRay(ray, start, end, mins, maxs);
        CTraceFilterWorldAndPropsOnly filter;
        trace_t trace;
        std::memset(&trace, 0, sizeof(trace));
        trace.fraction = 1.0f;
        I::EngineTrace->TraceRay(ray, MASK_PLAYERSOLID, &filter, &trace);

        U::LogInfo("[PortalBsp][HullTrace] stage=%s domain=engine-client owner=%s generation=%u brush=%d start=(%.2f,%.2f,%.2f) end=(%.2f,%.2f,%.2f) mins=(%.2f,%.2f,%.2f) maxs=(%.2f,%.2f,%.2f) fraction=%.6f startsolid=%s allsolid=%s endpos=(%.2f,%.2f,%.2f) plane=(%.4f,%.4f,%.4f) destructiveWrites=%s.\n",
            stage ? stage : "unknown",
            OwnerName(owner),
            binding.mapGeneration,
            binding.brushIndex,
            start.x, start.y, start.z,
            end.x, end.y, end.z,
            mins.x, mins.y, mins.z,
            maxs.x, maxs.y, maxs.z,
            trace.fraction,
            BoolText(trace.startsolid),
            BoolText(trace.allsolid),
            trace.endpos.x, trace.endpos.y, trace.endpos.z,
            trace.plane.normal.x, trace.plane.normal.y, trace.plane.normal.z,
            BoolText(G::PortalBspCollisionCarver.IsCarvingActive()));
    }

    void LogHullTraces(const char* stage)
    {
        Vector mins(-16.0f, -16.0f, 0.0f);
        Vector maxs(16.0f, 16.0f, 72.0f);
        if (C_TerrorPlayer* player = GetLocalPlayer())
        {
            mins = player->GetPlayerMins();
            maxs = player->GetPlayerMaxs();
        }

        for (const PortalBrushOwner owner : { PortalBrushOwner::Blue, PortalBrushOwner::Orange })
        {
            LogHullTrace(
                stage,
                owner,
                G::PortalBspCollisionCarver.GetBinding(owner),
                mins,
                maxs);
        }
    }

    void LogOperation(const char* event, const char* reason)
    {
        const PortalCarveOperation& operation = G::PortalBspCollisionCarver.GetLastOperation();
        U::LogInfo("[PortalBsp][Mutation] event=%s reason=%s status=%s access=%s failedBrush=%d targetCount=%zu writesCompleted=%zu rollbacksCompleted=%zu phase1Enabled=%s carvingActive=%s generation=%u.\n",
            event ? event : "unknown",
            reason ? reason : "unknown",
            PortalCarveStatusName(operation.status),
            PortalBrushAccessStatusName(operation.accessStatus),
            operation.brushIndex,
            operation.targetCount,
            operation.writesCompleted,
            operation.rollbacksCompleted,
            BoolText(G::PortalBspCollisionCarver.IsPhase1Enabled()),
            BoolText(G::PortalBspCollisionCarver.IsCarvingActive()),
            G::PortalBspData.GetMapGeneration());
    }

    void LogBrushes(
        const std::vector<ModifiedPortalBrush>& brushes,
        const char* event,
        const char* reason)
    {
        for (const ModifiedPortalBrush& modified : brushes)
        {
            const PortalBrushReadResult current = G::PortalBspData.ReadBrushContents(
                modified.brushIndex,
                modified.mapGeneration);
            U::LogInfo("[PortalBsp][MutationBrush] event=%s reason=%s generation=%u brush=%d ownerMask=0x%02X original=0x%08X replacement=0x00000000 currentValid=%s current=0x%08X modified=%s.\n",
                event ? event : "unknown",
                reason ? reason : "unknown",
                modified.mapGeneration,
                modified.brushIndex,
                static_cast<unsigned int>(modified.ownerMask),
                static_cast<unsigned int>(modified.originalContents),
                BoolText(current.Succeeded()),
                static_cast<unsigned int>(current.contents),
                BoolText(modified.modified));
        }
    }

    void LogModifiedBrushes(const char* event, const char* reason)
    {
        LogBrushes(
            G::PortalBspCollisionCarver.GetModifiedBrushes(),
            event,
            reason);
    }
}

bool PortalBspPhase1::SetEnabled(bool enabled, const char* reason)
{
    if (!enabled && G::PortalBspCollisionCarver.IsCarvingActive())
        LogHullTraces("before-disable-restore");

    const bool result = G::PortalBspCollisionCarver.SetPhase1Enabled(
        enabled,
        G::PortalBspData,
        reason);
    LogOperation(enabled ? "enable" : "disable", reason);
    if (enabled && G::PortalBspCollisionCarver.IsCarvingActive())
    {
        LogModifiedBrushes("write-success", reason);
        LogHullTraces("after-write");
    }
    else if (!enabled && result)
    {
        LogHullTraces("after-disable-restore");
    }
    return result;
}

bool PortalBspPhase1::TryActivate(const char* reason)
{
    if (G::PortalBspCollisionCarver.IsPhase1Enabled()
        && G::PortalBspCollisionCarver.IsPairResolved()
        && !G::PortalBspCollisionCarver.IsCarvingActive())
    {
        LogHullTraces("before-write");
    }

    const bool result = G::PortalBspCollisionCarver.TryActivatePhase1(G::PortalBspData);
    LogOperation("activate", reason);
    if (result && G::PortalBspCollisionCarver.IsCarvingActive())
    {
        LogModifiedBrushes("write-success", reason);
        LogHullTraces("after-write");
    }
    return result;
}

bool PortalBspPhase1::Restore(const char* reason)
{
    if (!G::PortalBspCollisionCarver.IsCarvingActive())
        return true;

    const std::vector<ModifiedPortalBrush> beforeRestore =
        G::PortalBspCollisionCarver.GetModifiedBrushes();
    LogModifiedBrushes("restore-attempt", reason);
    LogHullTraces("before-restore");
    const bool result = G::PortalBspCollisionCarver.RestoreAll(G::PortalBspData, reason);
    LogOperation("restore", reason);
    LogBrushes(
        beforeRestore,
        result ? "restore-success" : "restore-rejected",
        reason);
    LogHullTraces("after-restore");
    return result;
}

bool PortalBspPhase1::PrepareForPlacement(PortalBrushOwner owner, const char* reason)
{
    if (!Restore(reason))
    {
        U::LogError("[PortalBsp][Mutation] event=placement-blocked owner=%s reason=%s restoreFailed=true.\n",
            OwnerName(owner), reason ? reason : "unknown");
        return false;
    }

    // Keep the old binding until the new placement reaches its commit point.
    // An aborted placement can then reactivate the previous pair transactionally.
    U::LogInfo("[PortalBsp][BindingLifecycle] event=PrepareForPlacement owner=%s oldBindingRetained=true carvingActive=false.\n",
        OwnerName(owner));
    return true;
}

bool PortalBspPhase1::RestoreAndClearBindings(const char* reason)
{
    const bool restored = Restore(reason);
    if (restored)
        G::PortalBspCollisionCarver.ClearBindings();
    U::LogInfo("[PortalBsp][BindingLifecycle] event=RestoreAndClear reason=%s restored=%s bindingsCleared=%s.\n",
        reason ? reason : "unknown",
        BoolText(restored),
        BoolText(restored));
    return restored;
}

void PortalBspPhase1::DiscardInvalidatedState(const char* reason)
{
    G::PortalBspCollisionCarver.DiscardInvalidatedState();
    U::LogWarning("[PortalBsp][Mutation] event=discard-invalidated-state reason=%s generation=%u.\n",
        reason ? reason : "unknown",
        G::PortalBspData.GetMapGeneration());
}

void PortalBspPhase1::LogStatus(const char* reason)
{
    const PortalBrushBinding& blue = G::PortalBspCollisionCarver.GetBinding(PortalBrushOwner::Blue);
    const PortalBrushBinding& orange = G::PortalBspCollisionCarver.GetBinding(PortalBrushOwner::Orange);
    U::LogInfo("[PortalBsp][Phase1Status] reason=%s enabled=%s carvingActive=%s pairResolved=%s queryReady=%s generation=%u modified=%zu clearanceMode=%s nearClipFix=%s exactExitVisualGuard=%s entryCameraHandoff=%s remoteView=OfficialExactCameraClipMinus0.5 nearPlaneRenderFix=OfficialStencilProxyOnly depthFogRepair=deferred blue(resolved=%s brush=%d contents=0x%08X generation=%u) orange(resolved=%s brush=%d contents=0x%08X generation=%u) legacyCollisionBypass=false movementMutation=false controlledNoclip=false teleportPredictionSync=%s.\n",
        reason ? reason : "unknown",
        BoolText(G::PortalBspCollisionCarver.IsPhase1Enabled()),
        BoolText(G::PortalBspCollisionCarver.IsCarvingActive()),
        BoolText(G::PortalBspCollisionCarver.IsPairResolved()),
        BoolText(G::PortalBspData.IsQueryReady()),
        G::PortalBspData.GetMapGeneration(),
        G::PortalBspCollisionCarver.GetModifiedBrushes().size(),
        PortalTransitionDecision::ExitClearanceModeName(
            PortalTransitionDecision::GetExitClearanceMode()),
        BoolText(PortalTransitionDecision::GetPortalNearClipFixEnabled()),
        BoolText(PortalTransitionDecision::GetExactExitVisualGuardEnabled()),
        BoolText(PortalTransitionDecision::GetEntryCameraHandoffEnabled()),
        BoolText(blue.resolved), blue.brushIndex, static_cast<unsigned int>(blue.originalContents), blue.mapGeneration,
        BoolText(orange.resolved), orange.brushIndex, static_cast<unsigned int>(orange.originalContents), orange.mapGeneration,
        BoolText(PortalPhysicsMode::ShouldSynchronizeCommittedTeleportPrediction()));
}
