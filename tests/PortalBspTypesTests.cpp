#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <limits>

#include "../src/Portal/PortalBspTypes.h"

namespace
{
    void Expect(bool condition, const char* message)
    {
        if (condition)
            return;

        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

int main()
{
    using namespace PortalBsp;

    Expect(kBrushSides.canonicalCountOffset == 0x64u, "brushside canonical count offset");
    Expect(kBrushSides.pointerOffset == 0x68u, "brushside pointer offset");
    Expect(kBrushSides.validatedCountOffset == 0x6Cu, "brushside validated count offset");
    Expect(kPlanes.canonicalCountOffset == 0x7Cu, "plane canonical count offset");
    Expect(kNodes.pointerOffset == 0x8Cu, "node pointer offset");
    Expect(kNodes.validatedCountDelta == 6u, "node allocation includes six box-hull nodes");
    Expect(kPlanes.validatedCountDelta == 0u, "ordinary arrays require equal logical and allocated counts");
    Expect(kLeafs.canonicalCountOffset == 0x94u, "leaf canonical count offset");
    Expect(kLeafBrushes.pointerOffset == 0xACu, "leafbrush pointer offset");
    Expect(kBrushes.validatedCountOffset == 0xC8u, "brush validated count offset");

    const auto validArray = ValidateArrayMetadata(
        148u, 0x20000000u, 148u, kBrushes.elementSize, kBrushes.maximumCount, 0u);
    Expect(validArray.valid, "matching counts and a safe non-null range are valid");
    Expect(validArray.byteSize == 148u * 8u, "array byte size uses the verified element stride");

    const auto mismatchedCount = ValidateArrayMetadata(
        148u, 0x20000000u, 147u, kBrushes.elementSize, kBrushes.maximumCount, 0u);
    Expect(!mismatchedCount.valid, "canonical and validated counts must match");
    Expect(!mismatchedCount.countRelationMatches, "count mismatch is diagnosed");

    const auto validNodes = ValidateArrayMetadata(
        549u, 0x30000000u, 555u, kNodes.elementSize, kNodes.maximumCount, kNodes.validatedCountDelta);
    Expect(validNodes.valid, "node allocation count is logical node count plus six box-hull nodes");
    Expect(validNodes.countRelationMatches, "the node plus-six relationship is diagnosed");
    Expect(validNodes.byteSize == 555u * 12u, "node readable span covers the complete allocated array");

    const auto missingBoxHullNode = ValidateArrayMetadata(
        549u, 0x30000000u, 554u, kNodes.elementSize, kNodes.maximumCount, kNodes.validatedCountDelta);
    Expect(!missingBoxHullNode.valid, "node allocation count plus five is rejected");

    const auto extraBoxHullNode = ValidateArrayMetadata(
        549u, 0x30000000u, 556u, kNodes.elementSize, kNodes.maximumCount, kNodes.validatedCountDelta);
    Expect(!extraBoxHullNode.valid, "node allocation count plus seven is rejected");

    const auto nullArray = ValidateArrayMetadata(
        148u, 0u, 148u, kBrushes.elementSize, kBrushes.maximumCount, 0u);
    Expect(!nullArray.valid, "a populated table cannot have a null pointer");

    const auto unreasonableCount = ValidateArrayMetadata(
        kBrushes.maximumCount + 1u,
        0x20000000u,
        kBrushes.maximumCount + 1u,
        kBrushes.elementSize,
        kBrushes.maximumCount,
        0u);
    Expect(!unreasonableCount.valid, "counts above the safety ceiling are rejected");

    const std::uintptr_t nearEnd = std::numeric_limits<std::uintptr_t>::max() - 3u;
    const auto overflowingRange = ValidateArrayMetadata(
        2u, nearEnd, 2u, 8u, 1024u, 0u);
    Expect(!overflowingRange.valid, "pointer plus byte span overflow is rejected");
    Expect(!overflowingRange.rangeDoesNotOverflow, "overflow is diagnosed separately");

    const auto validLeafs = ValidateLeafInvariants(561u, 560, 560u, 0u);
    Expect(validLeafs.valid, "collision BSP appends one empty leaf after render leaves");
    Expect(validLeafs.collisionCountMatches, "collision leaf count is render count plus one");
    Expect(validLeafs.emptyLeafMatches, "empty leaf index equals render leaf count");
    Expect(validLeafs.solidLeafMatches, "solid leaf index is zero");

    const auto oldEqualityAssumption = ValidateLeafInvariants(560u, 560, 560u, 0u);
    Expect(!oldEqualityAssumption.valid, "collision and render leaf counts must not be treated as equal");

    const auto validRoot = ValidateRootNodeInvariant(0x30001000u, 0x30001000u);
    Expect(validRoot.valid, "map_rootnode must match the map_nodes base for the world BSP");

    const auto wrongRoot = ValidateRootNodeInvariant(0x30002000u, 0x30001000u);
    Expect(!wrongRoot.valid, "a root pointer outside the node array base is rejected");

    std::cout << "Portal BSP type tests passed\n";
    return 0;
}
