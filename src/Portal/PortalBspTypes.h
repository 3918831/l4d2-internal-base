#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace PortalBsp
{
    inline constexpr std::uint32_t kMapRootNodeOffset = 0x00u;
    inline constexpr std::uint32_t kEmptyLeafOffset = 0xA0u;
    inline constexpr std::uint32_t kSolidLeafOffset = 0xA4u;
    inline constexpr std::uint32_t kNumCollisionModelsOffset = 0xB4u;
    inline constexpr std::uint32_t kMapCollisionModelsOffset = 0xB8u;
    inline constexpr std::uint32_t kValidatedCollisionModelCountOffset = 0xBCu;

    // Large enough for the target maps while still rejecting corrupt metadata.
    inline constexpr std::uint32_t kMaximumTableCount = 1u << 20;

    struct ArrayLayout
    {
        const char* name = nullptr;
        std::uint32_t canonicalCountOffset = 0;
        std::uint32_t pointerOffset = 0;
        std::uint32_t validatedCountOffset = 0;
        std::size_t elementSize = 0;
        std::uint32_t maximumCount = 0;
        std::uint32_t validatedCountDelta = 0;
        const char* evidence = nullptr;
    };

    inline constexpr ArrayLayout kBrushSides{
        "brushsides", 0x64u, 0x68u, 0x6Cu, 8u, kMaximumTableCount, 0u, "ida+runtime" };
    inline constexpr ArrayLayout kBoxBrushes{
        "boxbrushes", 0x70u, 0x74u, 0x78u, 48u, kMaximumTableCount, 0u, "ida+runtime" };
    inline constexpr ArrayLayout kPlanes{
        "planes", 0x7Cu, 0x80u, 0x84u, 20u, kMaximumTableCount, 0u, "ida+runtime" };
    inline constexpr ArrayLayout kNodes{
        "nodes", 0x88u, 0x8Cu, 0x90u, 12u, kMaximumTableCount, 6u, "ida+runtime+boxhull6" };
    inline constexpr ArrayLayout kLeafs{
        "leafs", 0x94u, 0x98u, 0x9Cu, 16u, kMaximumTableCount, 0u, "ida+runtime" };
    inline constexpr ArrayLayout kLeafBrushes{
        "leafbrushes", 0xA8u, 0xACu, 0xB0u, 2u, kMaximumTableCount, 0u, "ida+runtime" };
    inline constexpr ArrayLayout kBrushes{
        "brushes", 0xC0u, 0xC4u, 0xC8u, 8u, kMaximumTableCount, 0u, "ida+runtime" };

    inline constexpr std::array<ArrayLayout, 7> kRequiredArrays{
        kBrushSides,
        kBoxBrushes,
        kPlanes,
        kNodes,
        kLeafs,
        kLeafBrushes,
        kBrushes,
    };

    struct ArrayMetadataValidation
    {
        std::size_t byteSize = 0;
        bool countPositive = false;
        bool countReasonable = false;
        bool countRelationMatches = false;
        bool pointerPresent = false;
        bool rangeDoesNotOverflow = false;
        bool valid = false;
    };

    inline ArrayMetadataValidation ValidateArrayMetadata(
        std::uint32_t canonicalCount,
        std::uintptr_t pointer,
        std::uint32_t validatedCount,
        std::size_t elementSize,
        std::uint32_t maximumCount,
        std::uint32_t validatedCountDelta)
    {
        ArrayMetadataValidation result{};
        result.countPositive = canonicalCount > 0u;
        result.countReasonable = canonicalCount <= maximumCount
            && validatedCount <= maximumCount;
        const bool relationDoesNotOverflow = canonicalCount
            <= std::numeric_limits<std::uint32_t>::max() - validatedCountDelta;
        result.countRelationMatches = relationDoesNotOverflow
            && validatedCount == canonicalCount + validatedCountDelta;
        result.pointerPresent = pointer != 0u;

        const bool multiplicationSafe = elementSize > 0u
            && validatedCount <= std::numeric_limits<std::size_t>::max() / elementSize;
        if (multiplicationSafe)
        {
            result.byteSize = static_cast<std::size_t>(validatedCount) * elementSize;
            result.rangeDoesNotOverflow = pointer <= std::numeric_limits<std::uintptr_t>::max() - result.byteSize;
        }

        result.valid = result.countPositive
            && result.countReasonable
            && result.countRelationMatches
            && result.pointerPresent
            && result.rangeDoesNotOverflow;
        return result;
    }

    struct LeafInvariantValidation
    {
        bool collisionCountMatches = false;
        bool emptyLeafMatches = false;
        bool solidLeafMatches = false;
        bool valid = false;
    };

    inline LeafInvariantValidation ValidateLeafInvariants(
        std::uint32_t collisionLeafCount,
        int renderLeafCount,
        std::uint32_t emptyLeaf,
        std::uint32_t solidLeaf)
    {
        LeafInvariantValidation result{};
        if (renderLeafCount < 0
            || static_cast<std::uint64_t>(renderLeafCount) + 1u > std::numeric_limits<std::uint32_t>::max())
        {
            return result;
        }

        const auto renderCount = static_cast<std::uint32_t>(renderLeafCount);
        result.collisionCountMatches = collisionLeafCount == renderCount + 1u;
        result.emptyLeafMatches = emptyLeaf == renderCount;
        result.solidLeafMatches = solidLeaf == 0u;
        result.valid = result.collisionCountMatches
            && result.emptyLeafMatches
            && result.solidLeafMatches;
        return result;
    }

    struct RootNodeInvariantValidation
    {
        bool rootPresent = false;
        bool nodesPresent = false;
        bool rootMatchesNodes = false;
        bool valid = false;
    };

    inline RootNodeInvariantValidation ValidateRootNodeInvariant(
        std::uintptr_t rootNode,
        std::uintptr_t mapNodes)
    {
        RootNodeInvariantValidation result{};
        result.rootPresent = rootNode != 0u;
        result.nodesPresent = mapNodes != 0u;
        result.rootMatchesNodes = rootNode == mapNodes;
        result.valid = result.rootPresent && result.nodesPresent && result.rootMatchesNodes;
        return result;
    }
}
