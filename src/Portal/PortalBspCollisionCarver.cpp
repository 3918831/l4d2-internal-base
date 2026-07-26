#include "PortalBspCollisionCarver.h"

#include "PortalBspData.h"

#include <algorithm>

std::size_t CPortalBspCollisionCarver::OwnerIndex(PortalBrushOwner owner)
{
    return owner == PortalBrushOwner::Blue ? 0u : 1u;
}

bool CPortalBspCollisionCarver::BindPortalBrush(
    PortalBrushOwner owner,
    const PortalBspQuery::Vector3& hitPosition,
    const PortalBspQuery::Vector3& hitNormal,
    std::uint32_t requiredMask,
    const CPortalBspData& bspData,
    bool queryReady)
{
    PortalBspQuery::SurfaceBrushResult result{};
    if (queryReady)
    {
        result = bspData.FindBrushForSurfacePoint(
            hitPosition,
            hitNormal,
            requiredMask);
    }

    return ApplyPlacementResolution(
        owner,
        hitPosition,
        hitNormal,
        result,
        bspData.GetSnapshot().mapGeneration,
        queryReady);
}

bool CPortalBspCollisionCarver::ApplyPlacementResolution(
    PortalBrushOwner owner,
    const PortalBspQuery::Vector3& hitPosition,
    const PortalBspQuery::Vector3& hitNormal,
    const PortalBspQuery::SurfaceBrushResult& result,
    std::uint32_t mapGeneration,
    bool queryReady)
{
    if (queryReady)
        return ApplyResolution(owner, hitPosition, hitNormal, result, mapGeneration);

    PortalBspQuery::SurfaceBrushResult unavailable{};
    unavailable.status = PortalBspQuery::SurfaceBrushStatus::MissingData;
    unavailable.pointLeafStatus = PortalBspQuery::PointLeafStatus::MissingData;
    unavailable.lastBrushStatus = PortalBspQuery::BrushPointStatus::MissingData;
    return ApplyResolution(owner, hitPosition, hitNormal, unavailable, mapGeneration);
}

bool CPortalBspCollisionCarver::ApplyResolution(
    PortalBrushOwner owner,
    const PortalBspQuery::Vector3& hitPosition,
    const PortalBspQuery::Vector3& hitNormal,
    const PortalBspQuery::SurfaceBrushResult& result,
    std::uint32_t mapGeneration)
{
    PortalBrushBinding next{};
    next.status = result.status;
    next.pointLeafStatus = result.pointLeafStatus;
    next.lastBrushStatus = result.lastBrushStatus;
    next.hitPosition = hitPosition;
    next.hitNormal = hitNormal;
    next.samplePosition = result.samplePosition;
    next.leafIndex = result.leafIndex;
    next.brushIndex = result.brushIndex;
    next.originalContents = result.originalContents;
    next.selectedSampleOffset = result.selectedSampleOffset;
    next.samplesTested = result.samplesTested;
    next.candidatesTested = result.candidatesTested;
    next.duplicateCandidatesSkipped = result.duplicateCandidatesSkipped;
    next.invalidCandidatesSkipped = result.invalidCandidatesSkipped;
    next.mapGeneration = mapGeneration;
    next.resolved = result.status == PortalBspQuery::SurfaceBrushStatus::Success
        && result.leafIndex >= 0
        && result.brushIndex >= 0;

    m_Bindings[OwnerIndex(owner)] = next;
    return next.resolved;
}

bool CPortalBspCollisionCarver::ActivatePairCarving(IPortalBspBrushAccess& access)
{
    m_LastOperation = {};
    if (IsCarvingActive())
    {
        m_LastOperation.status = PortalCarveStatus::AlreadyActive;
        m_LastOperation.targetCount = m_ModifiedBrushes.size();
        return true;
    }

    if (!IsPairResolved())
    {
        m_LastOperation.status = PortalCarveStatus::PairUnresolved;
        return false;
    }

    const std::uint32_t generation = m_Bindings[0].mapGeneration;
    if (access.GetMapGeneration() != generation)
    {
        m_LastOperation.status = PortalCarveStatus::GenerationMismatch;
        return false;
    }

    std::vector<ModifiedPortalBrush> targets;
    for (std::size_t ownerIndex = 0; ownerIndex < 2; ++ownerIndex)
    {
        const PortalBrushBinding& binding = m_Bindings[ownerIndex];
        const PortalBrushOwner owner = ownerIndex == 0
            ? PortalBrushOwner::Blue
            : PortalBrushOwner::Orange;
        auto existing = std::find_if(
            targets.begin(), targets.end(),
            [&binding](const ModifiedPortalBrush& target)
            {
                return target.brushIndex == binding.brushIndex;
            });
        if (existing != targets.end())
        {
            if (existing->originalContents != binding.originalContents
                || existing->mapGeneration != binding.mapGeneration)
            {
                m_LastOperation.status = PortalCarveStatus::BindingContentsMismatch;
                m_LastOperation.brushIndex = binding.brushIndex;
                return false;
            }
            existing->ownerMask |= PortalBrushOwnerMask(owner);
            continue;
        }

        ModifiedPortalBrush target{};
        target.brushIndex = binding.brushIndex;
        target.originalContents = binding.originalContents;
        target.ownerMask = PortalBrushOwnerMask(owner);
        target.mapGeneration = binding.mapGeneration;
        targets.push_back(target);
    }

    m_LastOperation.targetCount = targets.size();
    for (const ModifiedPortalBrush& target : targets)
    {
        const PortalBrushReadResult read = access.ReadBrushContents(
            target.brushIndex,
            target.mapGeneration);
        if (!read.Succeeded())
        {
            m_LastOperation.status = PortalCarveStatus::ReadRejected;
            m_LastOperation.accessStatus = read.status;
            m_LastOperation.brushIndex = target.brushIndex;
            return false;
        }
        if (read.contents != target.originalContents)
        {
            m_LastOperation.status = PortalCarveStatus::BindingContentsMismatch;
            m_LastOperation.accessStatus = PortalBrushAccessStatus::UnexpectedContents;
            m_LastOperation.brushIndex = target.brushIndex;
            return false;
        }
    }

    m_ModifiedBrushes.clear();
    for (ModifiedPortalBrush target : targets)
    {
        const PortalBrushWriteResult write = access.CompareAndWriteBrushContents(
            target.brushIndex,
            target.mapGeneration,
            target.originalContents,
            0);
        if (!write.Succeeded())
        {
            m_LastOperation.status = PortalCarveStatus::WriteRejected;
            m_LastOperation.accessStatus = write.status;
            m_LastOperation.brushIndex = target.brushIndex;

            bool rollbackSucceeded = true;
            for (auto it = m_ModifiedBrushes.rbegin(); it != m_ModifiedBrushes.rend(); ++it)
            {
                const PortalBrushWriteResult rollback = access.CompareAndWriteBrushContents(
                    it->brushIndex,
                    it->mapGeneration,
                    0,
                    it->originalContents);
                if (rollback.Succeeded())
                {
                    it->modified = false;
                    ++m_LastOperation.rollbacksCompleted;
                }
                else
                {
                    rollbackSucceeded = false;
                    m_LastOperation.accessStatus = rollback.status;
                    m_LastOperation.brushIndex = it->brushIndex;
                }
            }

            m_ModifiedBrushes.erase(
                std::remove_if(
                    m_ModifiedBrushes.begin(), m_ModifiedBrushes.end(),
                    [](const ModifiedPortalBrush& modified) { return !modified.modified; }),
                m_ModifiedBrushes.end());
            if (!rollbackSucceeded)
                m_LastOperation.status = PortalCarveStatus::RollbackFailed;
            return false;
        }

        target.modified = true;
        m_ModifiedBrushes.push_back(target);
        ++m_LastOperation.writesCompleted;
    }

    m_LastOperation.status = PortalCarveStatus::Success;
    return true;
}

bool CPortalBspCollisionCarver::TryActivatePhase1(IPortalBspBrushAccess& access)
{
    if (!m_Phase1Enabled)
    {
        m_LastOperation = {};
        m_LastOperation.status = PortalCarveStatus::Phase1Disabled;
        return false;
    }
    return ActivatePairCarving(access);
}

bool CPortalBspCollisionCarver::SetPhase1Enabled(
    bool enabled,
    IPortalBspBrushAccess& access,
    const char* reason)
{
    if (!enabled)
    {
        m_Phase1Enabled = false;
        return RestoreAll(access, reason);
    }

    m_Phase1Enabled = true;
    if (!IsPairResolved())
        return true;
    return ActivatePairCarving(access);
}

bool CPortalBspCollisionCarver::RestoreAll(
    IPortalBspBrushAccess& access,
    const char* reason)
{
    (void)reason;
    m_LastOperation = {};
    m_LastOperation.targetCount = m_ModifiedBrushes.size();
    bool restoredAll = true;
    for (auto it = m_ModifiedBrushes.rbegin(); it != m_ModifiedBrushes.rend(); ++it)
    {
        if (!it->modified)
            continue;

        const PortalBrushWriteResult restore = access.CompareAndWriteBrushContents(
            it->brushIndex,
            it->mapGeneration,
            0,
            it->originalContents);
        if (restore.Succeeded())
        {
            it->modified = false;
            ++m_LastOperation.writesCompleted;
        }
        else
        {
            restoredAll = false;
            m_LastOperation.status = PortalCarveStatus::RestoreRejected;
            m_LastOperation.accessStatus = restore.status;
            m_LastOperation.brushIndex = it->brushIndex;
        }
    }

    m_ModifiedBrushes.erase(
        std::remove_if(
            m_ModifiedBrushes.begin(), m_ModifiedBrushes.end(),
            [](const ModifiedPortalBrush& modified) { return !modified.modified; }),
        m_ModifiedBrushes.end());
    if (restoredAll)
        m_LastOperation.status = PortalCarveStatus::Success;
    return restoredAll;
}

bool CPortalBspCollisionCarver::ReleaseOwner(
    PortalBrushOwner owner,
    IPortalBspBrushAccess& access,
    const char* reason)
{
    (void)reason;
    m_LastOperation = {};
    const std::uint8_t ownerMask = PortalBrushOwnerMask(owner);
    bool released = true;
    for (ModifiedPortalBrush& modified : m_ModifiedBrushes)
    {
        if ((modified.ownerMask & ownerMask) == 0)
            continue;

        modified.ownerMask &= static_cast<std::uint8_t>(~ownerMask);
        if (modified.ownerMask != 0 || !modified.modified)
            continue;

        const PortalBrushWriteResult restore = access.CompareAndWriteBrushContents(
            modified.brushIndex,
            modified.mapGeneration,
            0,
            modified.originalContents);
        if (restore.Succeeded())
        {
            modified.modified = false;
            ++m_LastOperation.writesCompleted;
        }
        else
        {
            released = false;
            m_LastOperation.status = PortalCarveStatus::RestoreRejected;
            m_LastOperation.accessStatus = restore.status;
            m_LastOperation.brushIndex = modified.brushIndex;
        }
    }

    UnbindPortalBrush(owner);
    m_ModifiedBrushes.erase(
        std::remove_if(
            m_ModifiedBrushes.begin(), m_ModifiedBrushes.end(),
            [](const ModifiedPortalBrush& modified) { return !modified.modified; }),
        m_ModifiedBrushes.end());
    if (released)
        m_LastOperation.status = PortalCarveStatus::Success;
    return released;
}

void CPortalBspCollisionCarver::UnbindPortalBrush(PortalBrushOwner owner)
{
    m_Bindings[OwnerIndex(owner)] = {};
}

void CPortalBspCollisionCarver::ClearBindings()
{
    m_Bindings[0] = {};
    m_Bindings[1] = {};
}

const PortalBrushBinding& CPortalBspCollisionCarver::GetBinding(PortalBrushOwner owner) const
{
    return m_Bindings[OwnerIndex(owner)];
}

const std::vector<ModifiedPortalBrush>& CPortalBspCollisionCarver::GetModifiedBrushes() const
{
    return m_ModifiedBrushes;
}

const PortalCarveOperation& CPortalBspCollisionCarver::GetLastOperation() const
{
    return m_LastOperation;
}

bool CPortalBspCollisionCarver::IsPairResolved() const
{
    return m_Bindings[0].resolved
        && m_Bindings[1].resolved
        && m_Bindings[0].mapGeneration == m_Bindings[1].mapGeneration;
}

bool CPortalBspCollisionCarver::IsCarvingActive() const
{
    return std::any_of(
        m_ModifiedBrushes.begin(), m_ModifiedBrushes.end(),
        [](const ModifiedPortalBrush& modified) { return modified.modified; });
}

bool CPortalBspCollisionCarver::IsPhase1Enabled() const
{
    return m_Phase1Enabled;
}

void CPortalBspCollisionCarver::DiscardInvalidatedState()
{
    m_Bindings[0] = {};
    m_Bindings[1] = {};
    m_ModifiedBrushes.clear();
    m_LastOperation = {};
}

const char* PortalCarveStatusName(PortalCarveStatus status)
{
    switch (status)
    {
    case PortalCarveStatus::None: return "None";
    case PortalCarveStatus::Success: return "Success";
    case PortalCarveStatus::AlreadyActive: return "AlreadyActive";
    case PortalCarveStatus::Phase1Disabled: return "Phase1Disabled";
    case PortalCarveStatus::PairUnresolved: return "PairUnresolved";
    case PortalCarveStatus::GenerationMismatch: return "GenerationMismatch";
    case PortalCarveStatus::BindingContentsMismatch: return "BindingContentsMismatch";
    case PortalCarveStatus::ReadRejected: return "ReadRejected";
    case PortalCarveStatus::WriteRejected: return "WriteRejected";
    case PortalCarveStatus::RollbackFailed: return "RollbackFailed";
    case PortalCarveStatus::RestoreRejected: return "RestoreRejected";
    default: return "Unknown";
    }
}
