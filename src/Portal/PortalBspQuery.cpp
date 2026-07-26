#include "PortalBspQuery.h"

#include <vector>

PortalBspQuery::PointLeafResult PortalBspQuery::PointLeafNum(
    const Vector3& position,
    const BspTreeView& tree)
{
    if (!tree.planes || tree.planeCount == 0 || !tree.nodes || tree.nodeCount == 0 || tree.leafCount == 0)
        return { PointLeafStatus::MissingData, -1 };

    if (tree.rootNodeIndex < 0
        || static_cast<std::size_t>(tree.rootNodeIndex) >= tree.nodeCount)
    {
        return { PointLeafStatus::InvalidRootNode, -1 };
    }

    std::int32_t child = tree.rootNodeIndex;
    std::size_t visitedNodes = 0;
    while (child >= 0)
    {
        if (visitedNodes++ >= tree.nodeCount)
            return { PointLeafStatus::TraversalLimit, -1 };

        const auto nodeIndex = static_cast<std::size_t>(child);
        if (nodeIndex >= tree.nodeCount)
            return { PointLeafStatus::InvalidNodeIndex, -1 };

        const Node& node = tree.nodes[nodeIndex];
        if (node.planeIndex >= tree.planeCount)
            return { PointLeafStatus::InvalidPlaneIndex, -1 };

        const Plane& plane = tree.planes[node.planeIndex];
        const float signedDistance = plane.normal.x * position.x
            + plane.normal.y * position.y
            + plane.normal.z * position.z
            - plane.distance;
        child = signedDistance >= 0.0f ? node.frontChild : node.backChild;
    }

    const std::int64_t decodedLeaf = -1 - static_cast<std::int64_t>(child);
    if (decodedLeaf < 0
        || static_cast<std::uint64_t>(decodedLeaf) >= tree.leafCount)
    {
        return { PointLeafStatus::InvalidLeafIndex, -1 };
    }

    return { PointLeafStatus::Success, static_cast<std::int32_t>(decodedLeaf) };
}

PortalBspQuery::BrushPointResult PortalBspQuery::ClassifyPointInBrush(
    const Vector3& position,
    std::size_t brushIndex,
    const BrushGeometryView& geometry)
{
    if (!geometry.brushes || geometry.brushCount == 0)
        return { BrushPointStatus::MissingData };

    if (brushIndex >= geometry.brushCount)
        return { BrushPointStatus::InvalidBrushIndex };

    const Brush& brush = geometry.brushes[brushIndex];
    if (brush.sideCount == kBoxBrushSideCount)
    {
        const std::size_t boxIndex = brush.firstSide;
        if (!geometry.boxBrushes)
            return { BrushPointStatus::MissingData };
        if (boxIndex >= geometry.boxBrushCount)
            return { BrushPointStatus::InvalidBoxBrushIndex };

        const BoxBrush& box = geometry.boxBrushes[boxIndex];
        const bool inside = position.x >= box.mins.x && position.x <= box.maxs.x
            && position.y >= box.mins.y && position.y <= box.maxs.y
            && position.z >= box.mins.z && position.z <= box.maxs.z;
        return { inside ? BrushPointStatus::Inside : BrushPointStatus::Outside };
    }

    if (brush.sideCount == 0)
        return { BrushPointStatus::InvalidSideCount };

    const std::size_t firstSide = brush.firstSide;
    const std::size_t sideCount = brush.sideCount;
    if (firstSide > geometry.brushSideCount
        || sideCount > geometry.brushSideCount - firstSide)
    {
        return { BrushPointStatus::InvalidSideRange };
    }

    if (!geometry.planes || geometry.planeCount == 0 || !geometry.brushSides)
        return { BrushPointStatus::MissingData };

    for (std::size_t i = 0; i < sideCount; ++i)
    {
        const BrushSide& side = geometry.brushSides[firstSide + i];
        if (!side.hasPlane)
            return { BrushPointStatus::NullPlane };
        if (side.planeIndex >= geometry.planeCount)
            return { BrushPointStatus::InvalidPlaneIndex };

        const Plane& plane = geometry.planes[side.planeIndex];
        const float signedDistance = plane.normal.x * position.x
            + plane.normal.y * position.y
            + plane.normal.z * position.z
            - plane.distance;
        if (signedDistance > 0.0f)
            return { BrushPointStatus::Outside };
    }

    return { BrushPointStatus::Inside };
}

PortalBspQuery::SurfaceBrushResult PortalBspQuery::FindBrushForSurfacePoint(
    const Vector3& hitPosition,
    const Vector3& hitNormal,
    std::uint32_t requiredMask,
    const BspView& bsp)
{
    SurfaceBrushResult result{};
    if (requiredMask == 0u)
    {
        result.status = SurfaceBrushStatus::InvalidRequiredMask;
        return result;
    }

    if (!bsp.leaves || bsp.leafCount == 0
        || !bsp.leafBrushes || bsp.leafBrushCount == 0
        || !bsp.geometry.brushes || bsp.geometry.brushCount == 0)
    {
        result.status = SurfaceBrushStatus::MissingData;
        return result;
    }

    for (const float offset : kSurfaceSampleOffsets)
    {
        ++result.samplesTested;
        const Vector3 sample{
            hitPosition.x - hitNormal.x * offset,
            hitPosition.y - hitNormal.y * offset,
            hitPosition.z - hitNormal.z * offset
        };
        result.samplePosition = sample;

        const PointLeafResult leafResult = PointLeafNum(sample, bsp.tree);
        result.pointLeafStatus = leafResult.status;
        if (leafResult.status != PointLeafStatus::Success)
            continue;

        result.leafIndex = leafResult.leafIndex;
        if (leafResult.leafIndex < 0
            || static_cast<std::size_t>(leafResult.leafIndex) >= bsp.leafCount)
        {
            result.status = SurfaceBrushStatus::InvalidLeafIndex;
            return result;
        }

        const Leaf& leaf = bsp.leaves[static_cast<std::size_t>(leafResult.leafIndex)];
        if (leaf.leafBrushCount == 0)
            continue;

        const std::size_t firstLeafBrush = leaf.firstLeafBrush;
        const std::size_t leafBrushCount = leaf.leafBrushCount;
        if (firstLeafBrush > bsp.leafBrushCount
            || leafBrushCount > bsp.leafBrushCount - firstLeafBrush)
        {
            result.status = SurfaceBrushStatus::InvalidLeafBrushRange;
            return result;
        }

        std::vector<std::uint8_t> seenBrushes(bsp.geometry.brushCount, 0u);
        for (std::size_t i = 0; i < leafBrushCount; ++i)
        {
            const std::size_t brushIndex = bsp.leafBrushes[firstLeafBrush + i];
            if (brushIndex >= bsp.geometry.brushCount)
            {
                ++result.invalidCandidatesSkipped;
                continue;
            }
            if (seenBrushes[brushIndex] != 0u)
            {
                ++result.duplicateCandidatesSkipped;
                continue;
            }
            seenBrushes[brushIndex] = 1u;

            const Brush& brush = bsp.geometry.brushes[brushIndex];
            if ((static_cast<std::uint32_t>(brush.contents) & requiredMask) == 0u)
                continue;

            ++result.candidatesTested;
            const BrushPointResult brushResult = ClassifyPointInBrush(sample, brushIndex, bsp.geometry);
            result.lastBrushStatus = brushResult.status;
            if (!brushResult.IsInside())
                continue;

            result.status = SurfaceBrushStatus::Success;
            result.brushIndex = static_cast<std::int32_t>(brushIndex);
            result.selectedSampleOffset = offset;
            result.originalContents = brush.contents;
            return result;
        }
    }

    result.status = SurfaceBrushStatus::NoBrushFound;
    return result;
}
