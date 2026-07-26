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

    bool ReadTableBytes(
        const PortalBsp::IMemoryReader& memory,
        const PortalBsp::TableSnapshot& table,
        std::vector<std::uint8_t>& bytes)
    {
        if (table.layout.elementSize == 0u
            || table.canonicalCount > std::numeric_limits<std::size_t>::max() / table.layout.elementSize)
        {
            return false;
        }

        const std::size_t byteCount = static_cast<std::size_t>(table.canonicalCount)
            * table.layout.elementSize;
        if (byteCount == 0u)
            return false;

        bytes.resize(byteCount);
        return memory.Read(table.arrayAddress, bytes.data(), bytes.size());
    }

    template <typename T>
    bool ReadElementValue(
        const std::vector<std::uint8_t>& bytes,
        std::size_t index,
        std::size_t stride,
        std::size_t fieldOffset,
        T& value)
    {
        if (stride == 0u || index > std::numeric_limits<std::size_t>::max() / stride)
            return false;

        const std::size_t elementOffset = index * stride;
        if (fieldOffset > stride || sizeof(T) > stride - fieldOffset)
            return false;
        if (elementOffset > bytes.size()
            || fieldOffset > bytes.size() - elementOffset
            || sizeof(T) > bytes.size() - elementOffset - fieldOffset)
        {
            return false;
        }

        std::memcpy(&value, bytes.data() + elementOffset + fieldOffset, sizeof(T));
        return true;
    }

    bool DecodePlanePointer(
        std::uint32_t pointer,
        const PortalBsp::TableSnapshot& planeTable,
        std::size_t& planeIndex)
    {
        const std::uintptr_t address = pointer;
        if (address == 0u || address < planeTable.arrayAddress || planeTable.layout.elementSize == 0u)
            return false;

        const std::uintptr_t delta = address - planeTable.arrayAddress;
        if (delta % planeTable.layout.elementSize != 0u)
            return false;

        planeIndex = static_cast<std::size_t>(delta / planeTable.layout.elementSize);
        return planeIndex < planeTable.canonicalCount;
    }

    bool HasUsableQueryTable(const PortalBsp::TableSnapshot& table)
    {
        return table.arrayAddress != 0u
            && table.canonicalCount > 0u
            && table.canonicalCount <= table.layout.maximumCount
            && table.layout.elementSize > 0u;
    }

    bool IsWritableRange(std::uintptr_t address, std::size_t size)
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

            const DWORD protection = region.Protect & 0xFFu;
            const bool writable = protection == PAGE_READWRITE
                || protection == PAGE_WRITECOPY
                || protection == PAGE_EXECUTE_READWRITE
                || protection == PAGE_EXECUTE_WRITECOPY;
            if (region.State != MEM_COMMIT
                || (region.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0
                || !writable)
            {
                return false;
            }

            const std::uintptr_t regionBase = reinterpret_cast<std::uintptr_t>(region.BaseAddress);
            if (regionBase > std::numeric_limits<std::uintptr_t>::max() - region.RegionSize)
                return false;
            const std::uintptr_t regionEnd = regionBase + region.RegionSize;
            if (cursor < regionBase || cursor >= regionEnd)
                return false;
            cursor = regionEnd < end ? regionEnd : end;
        }
        return true;
    }

    PortalBrushAccessStatus ResolveLiveBrushContentsAddress(
        const PortalBsp::Snapshot& snapshot,
        int brushIndex,
        std::uint32_t expectedGeneration,
        std::uintptr_t& address)
    {
        address = 0u;
        if (!snapshot.ready)
            return PortalBrushAccessStatus::MissingData;
        if (snapshot.mapGeneration != expectedGeneration)
            return PortalBrushAccessStatus::GenerationMismatch;

        constexpr std::size_t kBrushTableIndex = 6u;
        const PortalBsp::TableSnapshot& brushes = snapshot.tables[kBrushTableIndex];
        if (!brushes.IsValid())
            return PortalBrushAccessStatus::MissingData;
        if (brushIndex < 0
            || static_cast<std::uint32_t>(brushIndex) >= brushes.canonicalCount)
        {
            return PortalBrushAccessStatus::InvalidBrushIndex;
        }

        const std::size_t index = static_cast<std::size_t>(brushIndex);
        if (brushes.layout.elementSize == 0u
            || index > std::numeric_limits<std::size_t>::max() / brushes.layout.elementSize)
        {
            return PortalBrushAccessStatus::AddressOverflow;
        }
        const std::size_t byteOffset = index * brushes.layout.elementSize;
        if (brushes.arrayAddress > std::numeric_limits<std::uintptr_t>::max() - byteOffset)
            return PortalBrushAccessStatus::AddressOverflow;

        address = brushes.arrayAddress + byteOffset;
        return PortalBrushAccessStatus::Success;
    }
}

const char* PortalBrushAccessStatusName(PortalBrushAccessStatus status)
{
    switch (status)
    {
    case PortalBrushAccessStatus::Success: return "Success";
    case PortalBrushAccessStatus::MissingData: return "MissingData";
    case PortalBrushAccessStatus::GenerationMismatch: return "GenerationMismatch";
    case PortalBrushAccessStatus::InvalidBrushIndex: return "InvalidBrushIndex";
    case PortalBrushAccessStatus::AddressOverflow: return "AddressOverflow";
    case PortalBrushAccessStatus::Unreadable: return "Unreadable";
    case PortalBrushAccessStatus::Unwritable: return "Unwritable";
    case PortalBrushAccessStatus::UnexpectedContents: return "UnexpectedContents";
    case PortalBrushAccessStatus::WriteFailed: return "WriteFailed";
    case PortalBrushAccessStatus::VerificationFailed: return "VerificationFailed";
    default: return "Unknown";
    }
}

PortalBrushAccessStatus PortalBsp::ResolveBrushContentsAddress(
    const Snapshot& snapshot,
    int brushIndex,
    std::uint32_t expectedGeneration,
    std::uintptr_t& address)
{
    return ResolveLiveBrushContentsAddress(
        snapshot,
        brushIndex,
        expectedGeneration,
        address);
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

PortalBspQuery::BspView PortalBsp::QueryStorage::View() const
{
    const auto planeData = planes.empty() ? nullptr : planes.data();
    const auto nodeData = nodes.empty() ? nullptr : nodes.data();
    const auto leafData = leaves.empty() ? nullptr : leaves.data();
    const auto leafBrushData = leafBrushes.empty() ? nullptr : leafBrushes.data();
    const auto brushData = brushes.empty() ? nullptr : brushes.data();
    const auto brushSideData = brushSides.empty() ? nullptr : brushSides.data();
    const auto boxBrushData = boxBrushes.empty() ? nullptr : boxBrushes.data();

    return {
        { planeData, planes.size(), nodeData, nodes.size(), leaves.size(), 0 },
        { planeData, planes.size(), brushSideData, brushSides.size(),
            brushData, brushes.size(), boxBrushData, boxBrushes.size() },
        leafData, leaves.size(),
        leafBrushData, leafBrushes.size()
    };
}

PortalBsp::QueryStorage PortalBsp::CaptureQueryStorage(
    const IMemoryReader& memory,
    const Snapshot& snapshot)
{
    QueryStorage result{};
    if (!snapshot.ready)
    {
        result.failureReason = "snapshot-not-ready";
        return result;
    }

    for (const TableSnapshot& table : snapshot.tables)
    {
        if (!HasUsableQueryTable(table))
        {
            result.failureReason = std::string("invalid-table-") + (table.layout.name ? table.layout.name : "unknown");
            return result;
        }
    }

    const TableSnapshot& brushSideTable = snapshot.tables[0];
    const TableSnapshot& boxBrushTable = snapshot.tables[1];
    const TableSnapshot& planeTable = snapshot.tables[2];
    const TableSnapshot& nodeTable = snapshot.tables[3];
    const TableSnapshot& leafTable = snapshot.tables[4];
    const TableSnapshot& leafBrushTable = snapshot.tables[5];
    const TableSnapshot& brushTable = snapshot.tables[6];

    std::vector<std::uint8_t> brushSideBytes;
    std::vector<std::uint8_t> boxBrushBytes;
    std::vector<std::uint8_t> planeBytes;
    std::vector<std::uint8_t> nodeBytes;
    std::vector<std::uint8_t> leafBytes;
    std::vector<std::uint8_t> leafBrushBytes;
    std::vector<std::uint8_t> brushBytes;
    if (!ReadTableBytes(memory, brushSideTable, brushSideBytes)
        || !ReadTableBytes(memory, boxBrushTable, boxBrushBytes)
        || !ReadTableBytes(memory, planeTable, planeBytes)
        || !ReadTableBytes(memory, nodeTable, nodeBytes)
        || !ReadTableBytes(memory, leafTable, leafBytes)
        || !ReadTableBytes(memory, leafBrushTable, leafBrushBytes)
        || !ReadTableBytes(memory, brushTable, brushBytes))
    {
        result.failureReason = "table-bulk-read-failed";
        return result;
    }

    result.planes.resize(planeTable.canonicalCount);
    for (std::size_t i = 0; i < result.planes.size(); ++i)
    {
        PortalBspQuery::Plane& plane = result.planes[i];
        if (!ReadElementValue(planeBytes, i, planeTable.layout.elementSize, 0u, plane.normal.x)
            || !ReadElementValue(planeBytes, i, planeTable.layout.elementSize, 4u, plane.normal.y)
            || !ReadElementValue(planeBytes, i, planeTable.layout.elementSize, 8u, plane.normal.z)
            || !ReadElementValue(planeBytes, i, planeTable.layout.elementSize, 12u, plane.distance))
        {
            result.failureReason = "plane-read-failed";
            return result;
        }
    }

    result.nodes.resize(nodeTable.canonicalCount);
    for (std::size_t i = 0; i < result.nodes.size(); ++i)
    {
        std::uint32_t planePointer = 0;
        PortalBspQuery::Node& node = result.nodes[i];
        if (!ReadElementValue(nodeBytes, i, nodeTable.layout.elementSize, 0u, planePointer)
            || !ReadElementValue(nodeBytes, i, nodeTable.layout.elementSize, 4u, node.frontChild)
            || !ReadElementValue(nodeBytes, i, nodeTable.layout.elementSize, 8u, node.backChild))
        {
            result.failureReason = "node-read-failed";
            return result;
        }

        if (!DecodePlanePointer(planePointer, planeTable, node.planeIndex))
        {
            node.planeIndex = result.planes.size();
            ++result.invalidNodePlanePointers;
        }
    }

    result.leaves.resize(leafTable.canonicalCount);
    for (std::size_t i = 0; i < result.leaves.size(); ++i)
    {
        PortalBspQuery::Leaf& leaf = result.leaves[i];
        if (!ReadElementValue(leafBytes, i, leafTable.layout.elementSize, 8u, leaf.firstLeafBrush)
            || !ReadElementValue(leafBytes, i, leafTable.layout.elementSize, 10u, leaf.leafBrushCount))
        {
            result.failureReason = "leaf-read-failed";
            return result;
        }
    }

    result.leafBrushes.resize(leafBrushTable.canonicalCount);
    for (std::size_t i = 0; i < result.leafBrushes.size(); ++i)
    {
        if (!ReadElementValue(leafBrushBytes, i, leafBrushTable.layout.elementSize, 0u, result.leafBrushes[i]))
        {
            result.failureReason = "leafbrush-read-failed";
            return result;
        }
    }

    result.brushes.resize(brushTable.canonicalCount);
    for (std::size_t i = 0; i < result.brushes.size(); ++i)
    {
        PortalBspQuery::Brush& brush = result.brushes[i];
        if (!ReadElementValue(brushBytes, i, brushTable.layout.elementSize, 0u, brush.contents)
            || !ReadElementValue(brushBytes, i, brushTable.layout.elementSize, 4u, brush.sideCount)
            || !ReadElementValue(brushBytes, i, brushTable.layout.elementSize, 6u, brush.firstSide))
        {
            result.failureReason = "brush-read-failed";
            return result;
        }
    }

    result.brushSides.resize(brushSideTable.canonicalCount);
    for (std::size_t i = 0; i < result.brushSides.size(); ++i)
    {
        std::uint32_t planePointer = 0;
        PortalBspQuery::BrushSide& side = result.brushSides[i];
        if (!ReadElementValue(brushSideBytes, i, brushSideTable.layout.elementSize, 0u, planePointer))
        {
            result.failureReason = "brushside-read-failed";
            return result;
        }

        side.hasPlane = DecodePlanePointer(planePointer, planeTable, side.planeIndex);
        if (!side.hasPlane)
        {
            side.planeIndex = result.planes.size();
            ++result.invalidBrushSidePlanePointers;
        }
    }

    result.boxBrushes.resize(boxBrushTable.canonicalCount);
    for (std::size_t i = 0; i < result.boxBrushes.size(); ++i)
    {
        PortalBspQuery::BoxBrush& box = result.boxBrushes[i];
        if (!ReadElementValue(boxBrushBytes, i, boxBrushTable.layout.elementSize, 0u, box.mins.x)
            || !ReadElementValue(boxBrushBytes, i, boxBrushTable.layout.elementSize, 4u, box.mins.y)
            || !ReadElementValue(boxBrushBytes, i, boxBrushTable.layout.elementSize, 8u, box.mins.z)
            || !ReadElementValue(boxBrushBytes, i, boxBrushTable.layout.elementSize, 16u, box.maxs.x)
            || !ReadElementValue(boxBrushBytes, i, boxBrushTable.layout.elementSize, 20u, box.maxs.y)
            || !ReadElementValue(boxBrushBytes, i, boxBrushTable.layout.elementSize, 24u, box.maxs.z))
        {
            result.failureReason = "boxbrush-read-failed";
            return result;
        }
    }

    result.ready = true;
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

    PortalBsp::QueryStorage nextQuery = PortalBsp::CaptureQueryStorage(memory, next);
    next.mapGeneration = m_MapGeneration;
    m_Snapshot = std::move(next);
    m_QueryStorage = std::move(nextQuery);
    return m_Snapshot.ready;
}

void CPortalBspData::InvalidateForMapChange()
{
    ++m_MapGeneration;
    m_Snapshot = {};
    m_QueryStorage = {};
    m_Snapshot.mapGeneration = m_MapGeneration;
}

PortalBrushReadResult CPortalBspData::ReadBrushContents(
    int brushIndex,
    std::uint32_t expectedGeneration) const
{
    std::uintptr_t address = 0u;
    const PortalBrushAccessStatus resolved = PortalBsp::ResolveBrushContentsAddress(
        m_Snapshot,
        brushIndex,
        expectedGeneration,
        address);
    if (resolved != PortalBrushAccessStatus::Success)
        return { resolved, 0 };

    WindowsMemoryReader memory;
    int contents = 0;
    if (!memory.Read(address, &contents, sizeof(contents)))
        return { PortalBrushAccessStatus::Unreadable, 0 };
    return { PortalBrushAccessStatus::Success, contents };
}

PortalBrushWriteResult CPortalBspData::CompareAndWriteBrushContents(
    int brushIndex,
    std::uint32_t expectedGeneration,
    int expectedCurrent,
    int replacement)
{
    std::uintptr_t address = 0u;
    const PortalBrushAccessStatus resolved = PortalBsp::ResolveBrushContentsAddress(
        m_Snapshot,
        brushIndex,
        expectedGeneration,
        address);
    if (resolved != PortalBrushAccessStatus::Success)
        return { resolved, 0, 0 };

    const PortalBrushReadResult read = ReadBrushContents(brushIndex, expectedGeneration);
    if (!read.Succeeded())
        return { read.status, read.contents, read.contents };
    if (read.contents != expectedCurrent)
    {
        return {
            PortalBrushAccessStatus::UnexpectedContents,
            read.contents,
            read.contents
        };
    }
    if (!IsWritableRange(address, sizeof(replacement)))
        return { PortalBrushAccessStatus::Unwritable, read.contents, read.contents };

    std::memcpy(reinterpret_cast<void*>(address), &replacement, sizeof(replacement));
    const PortalBrushReadResult verify = ReadBrushContents(brushIndex, expectedGeneration);
    if (!verify.Succeeded())
        return { PortalBrushAccessStatus::WriteFailed, read.contents, read.contents };
    if (verify.contents != replacement)
    {
        return {
            PortalBrushAccessStatus::VerificationFailed,
            read.contents,
            verify.contents
        };
    }
    return { PortalBrushAccessStatus::Success, read.contents, verify.contents };
}

PortalBspQuery::SurfaceBrushResult CPortalBspData::FindBrushForSurfacePoint(
    const PortalBspQuery::Vector3& hitPosition,
    const PortalBspQuery::Vector3& hitNormal,
    std::uint32_t requiredMask) const
{
    if (!m_QueryStorage.ready)
        return {};

    return PortalBspQuery::FindBrushForSurfacePoint(
        hitPosition,
        hitNormal,
        requiredMask,
        m_QueryStorage.View());
}
