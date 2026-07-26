#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../src/Portal/PortalBspData.h"

namespace
{
    void Expect(bool condition, const char* message)
    {
        if (condition)
            return;

        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }

    class FakeMemoryReader final : public PortalBsp::IMemoryReader
    {
    public:
        template <typename T>
        void Set(std::uintptr_t address, const T& value)
        {
            std::vector<std::uint8_t> bytes(sizeof(T));
            std::memcpy(bytes.data(), &value, sizeof(T));
            cells[address] = std::move(bytes);
            const auto* source = reinterpret_cast<const std::uint8_t*>(&value);
            for (std::size_t i = 0; i < sizeof(T); ++i)
                byteCells[address + i] = source[i];
        }

        void AddReadableRange(std::uintptr_t address, std::size_t size)
        {
            ranges.emplace_back(address, size);
        }

        bool Read(std::uintptr_t address, void* destination, std::size_t size) const override
        {
            const auto found = cells.find(address);
            if (found != cells.end() && found->second.size() == size)
            {
                std::memcpy(destination, found->second.data(), size);
                return true;
            }

            if (!IsReadableRange(address, size))
                return false;

            auto* output = static_cast<std::uint8_t*>(destination);
            for (std::size_t i = 0; i < size; ++i)
            {
                const auto byte = byteCells.find(address + i);
                output[i] = byte == byteCells.end() ? 0u : byte->second;
            }
            return true;
        }

        bool IsReadableRange(std::uintptr_t address, std::size_t size) const override
        {
            return std::any_of(ranges.begin(), ranges.end(), [&](const auto& range)
            {
                return address >= range.first
                    && size <= range.second
                    && address - range.first <= range.second - size;
            });
        }

    private:
        std::unordered_map<std::uintptr_t, std::vector<std::uint8_t>> cells;
        std::unordered_map<std::uintptr_t, std::uint8_t> byteCells;
        std::vector<std::pair<std::uintptr_t, std::size_t>> ranges;
    };

    void PopulateValidSnapshot(FakeMemoryReader& memory, std::uintptr_t base)
    {
        std::uintptr_t nextArray = 0x30000000u;
        for (const auto& layout : PortalBsp::kRequiredArrays)
        {
            const std::uint32_t count = layout.name == PortalBsp::kLeafs.name ? 561u : 64u;
            const std::uint32_t allocatedCount = count + layout.validatedCountDelta;
            const std::uint32_t pointer = static_cast<std::uint32_t>(nextArray);
            memory.Set(base + layout.canonicalCountOffset, count);
            memory.Set(base + layout.pointerOffset, pointer);
            memory.Set(base + layout.validatedCountOffset, allocatedCount);
            memory.AddReadableRange(nextArray, static_cast<std::size_t>(allocatedCount) * layout.elementSize);
            nextArray += 0x00100000u;
        }

        const std::uint32_t nodes = 0x30300000u;
        memory.Set(base + PortalBsp::kMapRootNodeOffset, nodes);
        memory.Set(base + PortalBsp::kEmptyLeafOffset, std::uint32_t{ 560u });
        memory.Set(base + PortalBsp::kSolidLeafOffset, std::uint32_t{ 0u });
        memory.Set(base + PortalBsp::kNumCollisionModelsOffset, std::uint32_t{ 1u });
        memory.Set(base + PortalBsp::kMapCollisionModelsOffset, std::uint32_t{ 0x38000000u });
        memory.Set(base + PortalBsp::kValidatedCollisionModelCountOffset, std::uint32_t{ 1u });
        memory.AddReadableRange(0x38000000u, PortalBsp::kCollisionModelRequiredBytes);
    }

    PortalBsp::Snapshot MakeMinimalQuerySnapshot()
    {
        PortalBsp::Snapshot snapshot{};
        snapshot.ready = true;

        constexpr std::uintptr_t addresses[] = {
            0x40000000u,
            0x40100000u,
            0x40200000u,
            0x40300000u,
            0x40400000u,
            0x40500000u,
            0x40600000u
        };
        for (std::size_t i = 0; i < snapshot.tables.size(); ++i)
        {
            snapshot.tables[i].layout = PortalBsp::kRequiredArrays[i];
            snapshot.tables[i].canonicalCount = 1;
            snapshot.tables[i].validatedCount = 1 + snapshot.tables[i].layout.validatedCountDelta;
            snapshot.tables[i].arrayAddress = addresses[i];
        }
        return snapshot;
    }

    void PopulateMinimalQueryMemory(FakeMemoryReader& memory)
    {
        constexpr std::uint32_t brushSides = 0x40000000u;
        constexpr std::uint32_t boxBrushes = 0x40100000u;
        constexpr std::uint32_t planes = 0x40200000u;
        constexpr std::uint32_t nodes = 0x40300000u;
        constexpr std::uint32_t leafs = 0x40400000u;
        constexpr std::uint32_t leafBrushes = 0x40500000u;
        constexpr std::uint32_t brushes = 0x40600000u;

        memory.Set(planes + 0u, 1.0f);
        memory.Set(planes + 4u, 0.0f);
        memory.Set(planes + 8u, 0.0f);
        memory.Set(planes + 12u, 0.0f);

        memory.Set(nodes + 0u, planes);
        memory.Set(nodes + 4u, std::int32_t{ -1 });
        memory.Set(nodes + 8u, std::int32_t{ -1 });

        memory.Set(leafs + 8u, std::uint16_t{ 0 });
        memory.Set(leafs + 10u, std::uint16_t{ 1 });
        memory.Set(leafBrushes, std::uint16_t{ 0 });

        memory.Set(brushes + 0u, std::int32_t{ 0x1 });
        memory.Set(brushes + 4u, PortalBspQuery::kBoxBrushSideCount);
        memory.Set(brushes + 6u, std::uint16_t{ 0 });

        memory.Set(brushSides + 0u, planes);

        memory.Set(boxBrushes + 0u, -20.0f);
        memory.Set(boxBrushes + 4u, -1.0f);
        memory.Set(boxBrushes + 8u, -1.0f);
        memory.Set(boxBrushes + 16u, 0.0f);
        memory.Set(boxBrushes + 20u, 1.0f);
        memory.Set(boxBrushes + 24u, 1.0f);

        memory.AddReadableRange(brushSides, PortalBsp::kBrushSides.elementSize);
        memory.AddReadableRange(boxBrushes, PortalBsp::kBoxBrushes.elementSize);
        memory.AddReadableRange(planes, PortalBsp::kPlanes.elementSize);
        memory.AddReadableRange(nodes, PortalBsp::kNodes.elementSize);
        memory.AddReadableRange(leafs, PortalBsp::kLeafs.elementSize);
        memory.AddReadableRange(leafBrushes, PortalBsp::kLeafBrushes.elementSize);
        memory.AddReadableRange(brushes, PortalBsp::kBrushes.elementSize);
    }
}

int main()
{
    constexpr std::uintptr_t base = 0x20000000u;

    FakeMemoryReader validMemory;
    PopulateValidSnapshot(validMemory, base);
    const auto valid = PortalBsp::CaptureSnapshot(validMemory, base, 560);
    Expect(valid.ready, "a complete two-count BSP layout is ready");
    Expect(valid.rootInvariant.valid, "root node matches the node array");
    Expect(valid.leafInvariant.valid, "collision/render leaf relationship is valid");
    Expect(valid.collisionModelsValid, "world collision model metadata is readable");

    FakeMemoryReader countMismatch;
    PopulateValidSnapshot(countMismatch, base);
    countMismatch.Set(base + PortalBsp::kNodes.validatedCountOffset, std::uint32_t{ 69u });
    const auto mismatched = PortalBsp::CaptureSnapshot(countMismatch, base, 560);
    Expect(!mismatched.ready, "a node count mismatch blocks readiness");
    Expect(!mismatched.tables[3].metadata.countRelationMatches, "the node table explains the mismatch");

    FakeMemoryReader truncatedNodeAllocation;
    PopulateValidSnapshot(truncatedNodeAllocation, base);
    truncatedNodeAllocation.Set(base + PortalBsp::kNodes.pointerOffset, std::uint32_t{ 0x3E000000u });
    truncatedNodeAllocation.AddReadableRange(0x3E000000u, 64u * PortalBsp::kNodes.elementSize);
    const auto truncatedNodes = PortalBsp::CaptureSnapshot(truncatedNodeAllocation, base, 560);
    Expect(!truncatedNodes.ready, "a node range covering only logical nodes is rejected");
    Expect(truncatedNodes.tables[3].metadata.byteSize == 70u * PortalBsp::kNodes.elementSize,
        "node span includes all six allocated box-hull nodes");
    Expect(!truncatedNodes.tables[3].spanReadable, "the truncated allocated node span is diagnosed");

    FakeMemoryReader missingPlaneRange;
    PopulateValidSnapshot(missingPlaneRange, base);
    missingPlaneRange.Set(base + PortalBsp::kPlanes.pointerOffset, std::uint32_t{ 0x3F000000u });
    const auto unreadablePlane = PortalBsp::CaptureSnapshot(missingPlaneRange, base, 560);
    Expect(!unreadablePlane.ready, "an unreadable plane span blocks readiness");
    Expect(!unreadablePlane.tables[2].spanReadable, "the plane table diagnoses the missing span");

    FakeMemoryReader oldLeafAssumption;
    PopulateValidSnapshot(oldLeafAssumption, base);
    oldLeafAssumption.Set(base + PortalBsp::kLeafs.canonicalCountOffset, std::uint32_t{ 560u });
    oldLeafAssumption.Set(base + PortalBsp::kLeafs.validatedCountOffset, std::uint32_t{ 560u });
    const auto wrongLeafs = PortalBsp::CaptureSnapshot(oldLeafAssumption, base, 560);
    Expect(!wrongLeafs.ready, "collision leaf count equality with render leaves is rejected");
    Expect(!wrongLeafs.leafInvariant.collisionCountMatches, "leaf mismatch is diagnosed");

    FakeMemoryReader queryMemory;
    PopulateMinimalQueryMemory(queryMemory);
    const PortalBsp::Snapshot querySnapshot = MakeMinimalQuerySnapshot();
    const PortalBsp::QueryStorage queryStorage = PortalBsp::CaptureQueryStorage(queryMemory, querySnapshot);
    Expect(queryStorage.ready, "checked live-memory fields normalize into a pure query view");
    Expect(queryStorage.invalidNodePlanePointers == 0, "valid node plane pointers decode to indices");
    Expect(queryStorage.invalidBrushSidePlanePointers == 0, "valid brush-side plane pointers decode to indices");

    const auto queryResult = PortalBspQuery::FindBrushForSurfacePoint(
        { 0.0f, 0.0f, 0.0f },
        { 1.0f, 0.0f, 0.0f },
        0x1,
        queryStorage.View());
    Expect(queryResult.status == PortalBspQuery::SurfaceBrushStatus::Success,
        "normalized live-memory view supports the pure surface query");
    Expect(queryResult.brushIndex == 0, "normalized query returns the live brush index");

    PortalBsp::Snapshot addressSnapshot = querySnapshot;
    addressSnapshot.mapGeneration = 7;
    PortalBsp::TableSnapshot& liveBrushes = addressSnapshot.tables[6];
    liveBrushes.fieldsReadable = true;
    liveBrushes.spanReadable = true;
    liveBrushes.metadata.valid = true;
    std::uintptr_t brushContentsAddress = 0;
    Expect(PortalBsp::ResolveBrushContentsAddress(
        addressSnapshot, 0, 7, brushContentsAddress) == PortalBrushAccessStatus::Success,
        "live brush zero resolves for the current map generation");
    Expect(brushContentsAddress == liveBrushes.arrayAddress,
        "brush contents address points at the first field of the live brush");
    Expect(PortalBsp::ResolveBrushContentsAddress(
        addressSnapshot, 1, 7, brushContentsAddress) == PortalBrushAccessStatus::InvalidBrushIndex,
        "brush index equal to canonical count is rejected");
    Expect(PortalBsp::ResolveBrushContentsAddress(
        addressSnapshot, 0, 6, brushContentsAddress) == PortalBrushAccessStatus::GenerationMismatch,
        "stale map generation is rejected before address use");

    std::cout << "Portal BSP data tests passed\n";
    return 0;
}
