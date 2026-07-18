#include "PortalBspData.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>

namespace
{
    class WindowsMemoryReader final : public PortalBsp::IMemoryReader
    {
    public:
        bool Read(std::uintptr_t address, void* destination, std::size_t size) const override
        {
            if (!destination || !IsReadableRange(address, size))
                return false;

            std::memcpy(destination, reinterpret_cast<const void*>(address), size);
            return true;
        }

        bool IsReadableRange(std::uintptr_t address, std::size_t size) const override
        {
            if (address == 0u || size == 0u
                || address > std::numeric_limits<std::uintptr_t>::max() - size)
            {
                return false;
            }

            const std::uintptr_t end = address + size;
            std::uintptr_t cursor = address;
            while (cursor < end)
            {
                MEMORY_BASIC_INFORMATION region{};
                if (VirtualQuery(reinterpret_cast<const void*>(cursor), &region, sizeof(region)) != sizeof(region))
                    return false;

                if (region.State != MEM_COMMIT
                    || (region.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
                {
                    return false;
                }

                const std::uintptr_t regionBase = reinterpret_cast<std::uintptr_t>(region.BaseAddress);
                if (regionBase > std::numeric_limits<std::uintptr_t>::max() - region.RegionSize)
                    return false;

                const std::uintptr_t regionEnd = regionBase + region.RegionSize;
                if (cursor < regionBase || cursor >= regionEnd)
                    return false;

                cursor = (regionEnd < end) ? regionEnd : end;
            }

            return true;
        }
    };

    template <typename T>
    bool ReadValue(const PortalBsp::IMemoryReader& memory, std::uintptr_t address, T& value)
    {
        return memory.Read(address, &value, sizeof(value));
    }

    bool TablePointersChanged(
        const PortalBsp::Snapshot& lhs,
        const PortalBsp::Snapshot& rhs)
    {
        for (std::size_t i = 0; i < lhs.tables.size(); ++i)
        {
            if (lhs.tables[i].arrayAddress != rhs.tables[i].arrayAddress)
                return true;
        }

        return lhs.collisionModels != rhs.collisionModels || lhs.rootNode != rhs.rootNode;
    }
}

PortalBsp::Snapshot PortalBsp::CaptureSnapshot(
    const IMemoryReader& memory,
    std::uintptr_t bspBase,
    int renderLeafCount)
{
    Snapshot result{};
    result.base = bspBase;
    result.renderLeafCount = renderLeafCount;
    if (bspBase == 0u)
        return result;

    for (std::size_t i = 0; i < kRequiredArrays.size(); ++i)
    {
        TableSnapshot& table = result.tables[i];
        table.layout = kRequiredArrays[i];

        std::uint32_t pointer32 = 0;
        table.fieldsReadable = ReadValue(memory, bspBase + table.layout.canonicalCountOffset, table.canonicalCount)
            && ReadValue(memory, bspBase + table.layout.pointerOffset, pointer32)
            && ReadValue(memory, bspBase + table.layout.validatedCountOffset, table.validatedCount);
        table.arrayAddress = pointer32;
        if (!table.fieldsReadable)
            continue;

        table.metadata = ValidateArrayMetadata(
            table.canonicalCount,
            table.arrayAddress,
            table.validatedCount,
            table.layout.elementSize,
            table.layout.maximumCount,
            table.layout.validatedCountDelta);
        table.spanReadable = table.metadata.valid
            && memory.IsReadableRange(table.arrayAddress, table.metadata.byteSize);
    }

    std::uint32_t rootNode32 = 0;
    std::uint32_t collisionModels32 = 0;
    result.scalarFieldsReadable = ReadValue(memory, bspBase + kMapRootNodeOffset, rootNode32)
        && ReadValue(memory, bspBase + kEmptyLeafOffset, result.emptyLeaf)
        && ReadValue(memory, bspBase + kSolidLeafOffset, result.solidLeaf)
        && ReadValue(memory, bspBase + kNumCollisionModelsOffset, result.collisionModelCount)
        && ReadValue(memory, bspBase + kMapCollisionModelsOffset, collisionModels32)
        && ReadValue(memory, bspBase + kValidatedCollisionModelCountOffset, result.validatedCollisionModelCount);
    result.rootNode = rootNode32;
    result.collisionModels = collisionModels32;

    const TableSnapshot& nodes = result.tables[3];
    const TableSnapshot& leafs = result.tables[4];
    result.rootInvariant = ValidateRootNodeInvariant(result.rootNode, nodes.arrayAddress);
    result.leafInvariant = ValidateLeafInvariants(
        leafs.canonicalCount,
        renderLeafCount,
        result.emptyLeaf,
        result.solidLeaf);

    result.collisionModelsValid = result.scalarFieldsReadable
        && result.collisionModelCount > 0u
        && result.collisionModelCount <= kMaximumTableCount
        && result.collisionModelCount == result.validatedCollisionModelCount
        && result.collisionModels != 0u
        && memory.IsReadableRange(result.collisionModels, kCollisionModelRequiredBytes);

    const bool allTablesValid = std::all_of(
        result.tables.begin(), result.tables.end(),
        [](const TableSnapshot& table) { return table.IsValid(); });
    result.ready = result.scalarFieldsReadable
        && allTablesValid
        && result.rootInvariant.valid
        && result.leafInvariant.valid
        && result.collisionModelsValid;
    return result;
}

bool CPortalBspData::CaptureForMap(
    std::uintptr_t bspBase,
    int renderLeafCount,
    const char* mapName)
{
    WindowsMemoryReader memory;
    PortalBsp::Snapshot next = PortalBsp::CaptureSnapshot(memory, bspBase, renderLeafCount);
    next.mapName = mapName ? mapName : "unknown";

    const bool generationChanged = m_Snapshot.base == 0u
        || m_Snapshot.mapName != next.mapName
        || m_Snapshot.base != next.base
        || TablePointersChanged(m_Snapshot, next);
    if (generationChanged)
        ++m_MapGeneration;

    next.mapGeneration = m_MapGeneration;
    m_Snapshot = std::move(next);
    return m_Snapshot.ready;
}

void CPortalBspData::InvalidateForMapChange()
{
    ++m_MapGeneration;
    m_Snapshot = {};
    m_Snapshot.mapGeneration = m_MapGeneration;
}
