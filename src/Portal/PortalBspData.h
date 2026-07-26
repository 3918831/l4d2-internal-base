#pragma once

#include "PortalBspBrushAccess.h"
#include "PortalBspQuery.h"
#include "PortalBspTypes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace PortalBsp
{
    inline constexpr std::size_t kCollisionModelRequiredBytes = 40u;

    class IMemoryReader
    {
    public:
        virtual ~IMemoryReader() = default;
        virtual bool Read(std::uintptr_t address, void* destination, std::size_t size) const = 0;
        virtual bool IsReadableRange(std::uintptr_t address, std::size_t size) const = 0;
    };

    struct TableSnapshot
    {
        ArrayLayout layout{};
        std::uint32_t canonicalCount = 0;
        std::uintptr_t arrayAddress = 0;
        std::uint32_t validatedCount = 0;
        ArrayMetadataValidation metadata{};
        bool fieldsReadable = false;
        bool spanReadable = false;

        bool IsValid() const
        {
            return fieldsReadable && metadata.valid && spanReadable;
        }
    };

    struct Snapshot
    {
        std::uintptr_t base = 0;
        std::uintptr_t rootNode = 0;
        std::uint32_t emptyLeaf = 0;
        std::uint32_t solidLeaf = 0;
        std::uint32_t collisionModelCount = 0;
        std::uintptr_t collisionModels = 0;
        std::uint32_t validatedCollisionModelCount = 0;
        std::array<TableSnapshot, kRequiredArrays.size()> tables{};
        LeafInvariantValidation leafInvariant{};
        RootNodeInvariantValidation rootInvariant{};
        int renderLeafCount = -1;
        std::uint32_t mapGeneration = 0;
        std::string mapName;
        bool scalarFieldsReadable = false;
        bool collisionModelsValid = false;
        bool ready = false;
    };

    PortalBrushAccessStatus ResolveBrushContentsAddress(
        const Snapshot& snapshot,
        int brushIndex,
        std::uint32_t expectedGeneration,
        std::uintptr_t& address);

    Snapshot CaptureSnapshot(
        const IMemoryReader& memory,
        std::uintptr_t bspBase,
        int renderLeafCount);

    struct QueryStorage
    {
        std::vector<PortalBspQuery::Plane> planes;
        std::vector<PortalBspQuery::Node> nodes;
        std::vector<PortalBspQuery::Leaf> leaves;
        std::vector<std::uint16_t> leafBrushes;
        std::vector<PortalBspQuery::Brush> brushes;
        std::vector<PortalBspQuery::BrushSide> brushSides;
        std::vector<PortalBspQuery::BoxBrush> boxBrushes;
        std::size_t invalidNodePlanePointers = 0;
        std::size_t invalidBrushSidePlanePointers = 0;
        std::string failureReason;
        bool ready = false;

        PortalBspQuery::BspView View() const;
    };

    QueryStorage CaptureQueryStorage(
        const IMemoryReader& memory,
        const Snapshot& snapshot);
}

class CPortalBspData final : public IPortalBspBrushAccess
{
public:
    bool CaptureForMap(std::uintptr_t bspBase, int renderLeafCount, const char* mapName);
    void InvalidateForMapChange();

    const PortalBsp::Snapshot& GetSnapshot() const { return m_Snapshot; }
    const PortalBsp::QueryStorage& GetQueryStorage() const { return m_QueryStorage; }
    bool IsReady() const { return m_Snapshot.ready; }
    bool IsQueryReady() const { return m_QueryStorage.ready; }
    std::uint32_t GetMapGeneration() const override { return m_Snapshot.mapGeneration; }
    PortalBrushReadResult ReadBrushContents(
        int brushIndex,
        std::uint32_t expectedGeneration) const override;
    PortalBrushWriteResult CompareAndWriteBrushContents(
        int brushIndex,
        std::uint32_t expectedGeneration,
        int expectedCurrent,
        int replacement) override;
    PortalBspQuery::SurfaceBrushResult FindBrushForSurfacePoint(
        const PortalBspQuery::Vector3& hitPosition,
        const PortalBspQuery::Vector3& hitNormal,
        std::uint32_t requiredMask) const;

private:
    PortalBsp::Snapshot m_Snapshot{};
    PortalBsp::QueryStorage m_QueryStorage{};
    std::uint32_t m_MapGeneration = 0;
};

namespace G
{
    inline CPortalBspData PortalBspData;
}
