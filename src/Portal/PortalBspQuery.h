#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace PortalBspQuery
{
    struct Vector3
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    struct Plane
    {
        Vector3 normal{};
        float distance = 0.0f;
    };

    struct Node
    {
        std::size_t planeIndex = 0;
        std::int32_t frontChild = 0;
        std::int32_t backChild = 0;
    };

    inline constexpr std::uint16_t kBoxBrushSideCount = 0xFFFFu;

    struct BrushSide
    {
        std::size_t planeIndex = 0;
        bool hasPlane = false;
    };

    struct Brush
    {
        std::int32_t contents = 0;
        std::uint16_t sideCount = 0;
        std::uint16_t firstSide = 0;
    };

    struct BoxBrush
    {
        Vector3 mins{};
        Vector3 maxs{};
    };

    struct BrushGeometryView
    {
        const Plane* planes = nullptr;
        std::size_t planeCount = 0;
        const BrushSide* brushSides = nullptr;
        std::size_t brushSideCount = 0;
        const Brush* brushes = nullptr;
        std::size_t brushCount = 0;
        const BoxBrush* boxBrushes = nullptr;
        std::size_t boxBrushCount = 0;
    };

    struct BspTreeView
    {
        const Plane* planes = nullptr;
        std::size_t planeCount = 0;
        const Node* nodes = nullptr;
        std::size_t nodeCount = 0;
        std::size_t leafCount = 0;
        std::int32_t rootNodeIndex = 0;
    };

    struct Leaf
    {
        std::uint16_t firstLeafBrush = 0;
        std::uint16_t leafBrushCount = 0;
    };

    struct BspView
    {
        BspTreeView tree{};
        BrushGeometryView geometry{};
        const Leaf* leaves = nullptr;
        std::size_t leafCount = 0;
        const std::uint16_t* leafBrushes = nullptr;
        std::size_t leafBrushCount = 0;
    };

    inline constexpr std::array<float, 6> kSurfaceSampleOffsets{
        2.0f, 4.0f, 8.0f, 1.0f, 16.0f, 0.5f
    };

    enum class PointLeafStatus
    {
        Success,
        MissingData,
        InvalidRootNode,
        InvalidNodeIndex,
        InvalidPlaneIndex,
        InvalidLeafIndex,
        TraversalLimit
    };

    struct PointLeafResult
    {
        PointLeafStatus status = PointLeafStatus::MissingData;
        std::int32_t leafIndex = -1;
    };

    enum class BrushPointStatus
    {
        Inside,
        Outside,
        MissingData,
        InvalidBrushIndex,
        InvalidSideCount,
        InvalidSideRange,
        NullPlane,
        InvalidPlaneIndex,
        InvalidBoxBrushIndex
    };

    struct BrushPointResult
    {
        BrushPointStatus status = BrushPointStatus::MissingData;

        bool IsInside() const { return status == BrushPointStatus::Inside; }
    };

    enum class SurfaceBrushStatus
    {
        Success,
        NoBrushFound,
        MissingData,
        InvalidRequiredMask,
        InvalidLeafIndex,
        InvalidLeafBrushRange
    };

    struct SurfaceBrushResult
    {
        SurfaceBrushStatus status = SurfaceBrushStatus::MissingData;
        PointLeafStatus pointLeafStatus = PointLeafStatus::MissingData;
        BrushPointStatus lastBrushStatus = BrushPointStatus::MissingData;
        std::int32_t leafIndex = -1;
        std::int32_t brushIndex = -1;
        float selectedSampleOffset = 0.0f;
        std::int32_t originalContents = 0;
        Vector3 samplePosition{};
        std::size_t samplesTested = 0;
        std::size_t candidatesTested = 0;
        std::size_t duplicateCandidatesSkipped = 0;
        std::size_t invalidCandidatesSkipped = 0;
    };

    PointLeafResult PointLeafNum(const Vector3& position, const BspTreeView& tree);
    BrushPointResult ClassifyPointInBrush(
        const Vector3& position,
        std::size_t brushIndex,
        const BrushGeometryView& geometry);
    SurfaceBrushResult FindBrushForSurfacePoint(
        const Vector3& hitPosition,
        const Vector3& hitNormal,
        std::uint32_t requiredMask,
        const BspView& bsp);
}
