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
        }

        void AddReadableRange(std::uintptr_t address, std::size_t size)
        {
            ranges.emplace_back(address, size);
        }

        bool Read(std::uintptr_t address, void* destination, std::size_t size) const override
        {
            const auto found = cells.find(address);
            if (found == cells.end() || found->second.size() != size)
                return false;

            std::memcpy(destination, found->second.data(), size);
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

    std::cout << "Portal BSP data tests passed\n";
    return 0;
}
