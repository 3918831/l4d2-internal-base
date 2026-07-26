#pragma once

#include "PortalBspBrushAccess.h"
#include "PortalBspQuery.h"

#include <cstdint>
#include <vector>

class CPortalBspData;

enum class PortalBrushOwner
{
    Blue,
    Orange
};

constexpr std::uint8_t PortalBrushOwnerMask(PortalBrushOwner owner)
{
    return owner == PortalBrushOwner::Blue ? 0x1u : 0x2u;
}

struct PortalBrushBinding
{
    PortalBspQuery::SurfaceBrushStatus status = PortalBspQuery::SurfaceBrushStatus::MissingData;
    PortalBspQuery::PointLeafStatus pointLeafStatus = PortalBspQuery::PointLeafStatus::MissingData;
    PortalBspQuery::BrushPointStatus lastBrushStatus = PortalBspQuery::BrushPointStatus::MissingData;
    PortalBspQuery::Vector3 hitPosition{};
    PortalBspQuery::Vector3 hitNormal{};
    PortalBspQuery::Vector3 samplePosition{};
    std::int32_t leafIndex = -1;
    std::int32_t brushIndex = -1;
    std::int32_t originalContents = 0;
    float selectedSampleOffset = 0.0f;
    std::size_t samplesTested = 0;
    std::size_t candidatesTested = 0;
    std::size_t duplicateCandidatesSkipped = 0;
    std::size_t invalidCandidatesSkipped = 0;
    std::uint32_t mapGeneration = 0;
    bool resolved = false;
};

struct ModifiedPortalBrush
{
    std::int32_t brushIndex = -1;
    std::int32_t originalContents = 0;
    std::uint8_t ownerMask = 0;
    std::uint32_t mapGeneration = 0;
    bool modified = false;
};

enum class PortalCarveStatus
{
    None,
    Success,
    AlreadyActive,
    Phase1Disabled,
    PairUnresolved,
    GenerationMismatch,
    BindingContentsMismatch,
    ReadRejected,
    WriteRejected,
    RollbackFailed,
    RestoreRejected
};

struct PortalCarveOperation
{
    PortalCarveStatus status = PortalCarveStatus::None;
    PortalBrushAccessStatus accessStatus = PortalBrushAccessStatus::Success;
    std::int32_t brushIndex = -1;
    std::size_t targetCount = 0;
    std::size_t writesCompleted = 0;
    std::size_t rollbacksCompleted = 0;

    bool Succeeded() const
    {
        return status == PortalCarveStatus::Success
            || status == PortalCarveStatus::AlreadyActive;
    }
};

class CPortalBspCollisionCarver
{
public:
    bool BindPortalBrush(
        PortalBrushOwner owner,
        const PortalBspQuery::Vector3& hitPosition,
        const PortalBspQuery::Vector3& hitNormal,
        std::uint32_t requiredMask,
        const CPortalBspData& bspData,
        bool queryReady);

    bool ApplyPlacementResolution(
        PortalBrushOwner owner,
        const PortalBspQuery::Vector3& hitPosition,
        const PortalBspQuery::Vector3& hitNormal,
        const PortalBspQuery::SurfaceBrushResult& result,
        std::uint32_t mapGeneration,
        bool queryReady);

    bool ApplyResolution(
        PortalBrushOwner owner,
        const PortalBspQuery::Vector3& hitPosition,
        const PortalBspQuery::Vector3& hitNormal,
        const PortalBspQuery::SurfaceBrushResult& result,
        std::uint32_t mapGeneration);

    bool ActivatePairCarving(IPortalBspBrushAccess& access);
    bool TryActivatePhase1(IPortalBspBrushAccess& access);
    bool SetPhase1Enabled(
        bool enabled,
        IPortalBspBrushAccess& access,
        const char* reason);
    bool RestoreAll(IPortalBspBrushAccess& access, const char* reason);
    bool ReleaseOwner(
        PortalBrushOwner owner,
        IPortalBspBrushAccess& access,
        const char* reason);

    void UnbindPortalBrush(PortalBrushOwner owner);
    void ClearBindings();

    const PortalBrushBinding& GetBinding(PortalBrushOwner owner) const;
    const std::vector<ModifiedPortalBrush>& GetModifiedBrushes() const;
    const PortalCarveOperation& GetLastOperation() const;
    bool IsPairResolved() const;
    bool IsCarvingActive() const;
    bool IsPhase1Enabled() const;
    void DiscardInvalidatedState();

private:
    static std::size_t OwnerIndex(PortalBrushOwner owner);

    PortalBrushBinding m_Bindings[2]{};
    std::vector<ModifiedPortalBrush> m_ModifiedBrushes;
    PortalCarveOperation m_LastOperation{};
    bool m_Phase1Enabled = false;
};

const char* PortalCarveStatusName(PortalCarveStatus status);

namespace G
{
    inline CPortalBspCollisionCarver PortalBspCollisionCarver;
}
