#pragma once

#include "PortalBspTypes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

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

    Snapshot CaptureSnapshot(
        const IMemoryReader& memory,
        std::uintptr_t bspBase,
        int renderLeafCount);
}

class CPortalBspData
{
public:
    bool CaptureForMap(std::uintptr_t bspBase, int renderLeafCount, const char* mapName);
    void InvalidateForMapChange();

    const PortalBsp::Snapshot& GetSnapshot() const { return m_Snapshot; }
    bool IsReady() const { return m_Snapshot.ready; }

private:
    PortalBsp::Snapshot m_Snapshot{};
    std::uint32_t m_MapGeneration = 0;
};

namespace G
{
    inline CPortalBspData PortalBspData;
}
