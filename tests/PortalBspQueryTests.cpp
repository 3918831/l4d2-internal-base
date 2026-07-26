#include "../src/Portal/PortalBspQuery.h"

#include <cstddef>
#include <cstdlib>
#include <iostream>

namespace
{
    void Expect(bool condition, const char* message)
    {
        if (condition)
            return;

        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }

    PortalBspQuery::Plane MakePlane(float x, float y, float z, float distance)
    {
        return { { x, y, z }, distance };
    }

    PortalBspQuery::BspTreeView MakeTree(
        const PortalBspQuery::Plane* planes,
        std::size_t planeCount,
        const PortalBspQuery::Node* nodes,
        std::size_t nodeCount,
        std::size_t leafCount)
    {
        return { planes, planeCount, nodes, nodeCount, leafCount, 0 };
    }

    void TestFrontChildTraversal()
    {
        const PortalBspQuery::Plane planes[] = {
            MakePlane(1.0f, 0.0f, 0.0f, 0.0f),
            MakePlane(0.0f, 1.0f, 0.0f, 0.0f)
        };
        const PortalBspQuery::Node nodes[] = {
            { 0, 1, -1 },
            { 1, -2, -3 }
        };

        const PortalBspQuery::PointLeafResult result = PortalBspQuery::PointLeafNum(
            { 1.0f, 1.0f, 0.0f },
            MakeTree(planes, 2, nodes, 2, 3));

        Expect(result.status == PortalBspQuery::PointLeafStatus::Success,
            "front child traversal succeeds");
        Expect(result.leafIndex == 1,
            "front child traversal reaches leaf 1");
    }

    void TestBackChildTraversal()
    {
        const PortalBspQuery::Plane planes[] = {
            MakePlane(1.0f, 0.0f, 0.0f, 0.0f),
            MakePlane(0.0f, 1.0f, 0.0f, 0.0f)
        };
        const PortalBspQuery::Node nodes[] = {
            { 0, -1, 1 },
            { 1, -2, -3 }
        };

        const PortalBspQuery::PointLeafResult result = PortalBspQuery::PointLeafNum(
            { -1.0f, -1.0f, 0.0f },
            MakeTree(planes, 2, nodes, 2, 3));

        Expect(result.status == PortalBspQuery::PointLeafStatus::Success,
            "back child traversal succeeds");
        Expect(result.leafIndex == 2,
            "back child traversal reaches leaf 2");
    }

    void TestNegativeChildDecodesLeaf()
    {
        const PortalBspQuery::Plane planes[] = {
            MakePlane(1.0f, 0.0f, 0.0f, 0.0f)
        };
        const PortalBspQuery::Node nodes[] = {
            { 0, -3, -1 }
        };

        const PortalBspQuery::PointLeafResult result = PortalBspQuery::PointLeafNum(
            { 1.0f, 0.0f, 0.0f },
            MakeTree(planes, 1, nodes, 1, 3));

        Expect(result.status == PortalBspQuery::PointLeafStatus::Success,
            "negative child decodes to a valid leaf");
        Expect(result.leafIndex == 2,
            "child -3 decodes to leaf 2 using -1 - child");
    }

    void TestInvalidDecodedLeafRejected()
    {
        const PortalBspQuery::Plane planes[] = {
            MakePlane(1.0f, 0.0f, 0.0f, 0.0f)
        };
        const PortalBspQuery::Node nodes[] = {
            { 0, -4, -1 }
        };

        const PortalBspQuery::PointLeafResult result = PortalBspQuery::PointLeafNum(
            { 1.0f, 0.0f, 0.0f },
            MakeTree(planes, 1, nodes, 1, 3));

        Expect(result.status == PortalBspQuery::PointLeafStatus::InvalidLeafIndex,
            "decoded leaf outside leafCount is rejected");
        Expect(result.leafIndex == -1,
            "invalid leaf result does not expose an index");
    }

    void TestConvexBrushContainsInteriorPoint()
    {
        const PortalBspQuery::Plane planes[] = {
            MakePlane(1.0f, 0.0f, 0.0f, 1.0f),
            MakePlane(-1.0f, 0.0f, 0.0f, 1.0f),
            MakePlane(0.0f, 1.0f, 0.0f, 1.0f),
            MakePlane(0.0f, -1.0f, 0.0f, 1.0f),
            MakePlane(0.0f, 0.0f, 1.0f, 1.0f),
            MakePlane(0.0f, 0.0f, -1.0f, 1.0f)
        };
        const PortalBspQuery::BrushSide sides[] = {
            { 0, true }, { 1, true }, { 2, true },
            { 3, true }, { 4, true }, { 5, true }
        };
        const PortalBspQuery::Brush brushes[] = {
            { 0x1, 6, 0 }
        };
        const PortalBspQuery::BrushGeometryView geometry{
            planes, 6, sides, 6, brushes, 1, nullptr, 0
        };

        const auto result = PortalBspQuery::ClassifyPointInBrush({ 0.0f, 0.0f, 0.0f }, 0, geometry);
        Expect(result.status == PortalBspQuery::BrushPointStatus::Inside,
            "convex brush contains an interior point");
    }

    void TestConvexBrushRejectsPointPastOnePlane()
    {
        const PortalBspQuery::Plane planes[] = {
            MakePlane(1.0f, 0.0f, 0.0f, 1.0f)
        };
        const PortalBspQuery::BrushSide sides[] = {
            { 0, true }
        };
        const PortalBspQuery::Brush brushes[] = {
            { 0x1, 1, 0 }
        };
        const PortalBspQuery::BrushGeometryView geometry{
            planes, 1, sides, 1, brushes, 1, nullptr, 0
        };

        const auto result = PortalBspQuery::ClassifyPointInBrush({ 1.25f, 0.0f, 0.0f }, 0, geometry);
        Expect(result.status == PortalBspQuery::BrushPointStatus::Outside,
            "convex brush rejects a point beyond a side plane");
    }

    void TestConvexBrushRejectsNullPlane()
    {
        const PortalBspQuery::Plane planes[] = {
            MakePlane(1.0f, 0.0f, 0.0f, 1.0f)
        };
        const PortalBspQuery::BrushSide sides[] = {
            { 0, false }
        };
        const PortalBspQuery::Brush brushes[] = {
            { 0x1, 1, 0 }
        };
        const PortalBspQuery::BrushGeometryView geometry{
            planes, 1, sides, 1, brushes, 1, nullptr, 0
        };

        const auto result = PortalBspQuery::ClassifyPointInBrush({ 0.0f, 0.0f, 0.0f }, 0, geometry);
        Expect(result.status == PortalBspQuery::BrushPointStatus::NullPlane,
            "convex brush rejects a side with a null plane");
    }

    void TestConvexBrushRejectsZeroSides()
    {
        const PortalBspQuery::Brush brushes[] = {
            { 0x1, 0, 0 }
        };
        const PortalBspQuery::BrushGeometryView geometry{
            nullptr, 0, nullptr, 0, brushes, 1, nullptr, 0
        };

        const auto result = PortalBspQuery::ClassifyPointInBrush({ 0.0f, 0.0f, 0.0f }, 0, geometry);
        Expect(result.status == PortalBspQuery::BrushPointStatus::InvalidSideCount,
            "convex brush rejects zero sides");
    }

    void TestConvexBrushRejectsSideRangeOverflow()
    {
        const PortalBspQuery::Plane planes[] = {
            MakePlane(1.0f, 0.0f, 0.0f, 1.0f)
        };
        const PortalBspQuery::BrushSide sides[] = {
            { 0, true }
        };
        const PortalBspQuery::Brush brushes[] = {
            { 0x1, 2, 0 }
        };
        const PortalBspQuery::BrushGeometryView geometry{
            planes, 1, sides, 1, brushes, 1, nullptr, 0
        };

        const auto result = PortalBspQuery::ClassifyPointInBrush({ 0.0f, 0.0f, 0.0f }, 0, geometry);
        Expect(result.status == PortalBspQuery::BrushPointStatus::InvalidSideRange,
            "convex brush rejects a side range beyond the table");
    }

    void TestBoxBrushContainsCenterAndBoundaryFace()
    {
        const PortalBspQuery::Brush brushes[] = {
            { 0x1, PortalBspQuery::kBoxBrushSideCount, 0 }
        };
        const PortalBspQuery::BoxBrush boxes[] = {
            { { -1.0f, -2.0f, -3.0f }, { 1.0f, 2.0f, 3.0f } }
        };
        const PortalBspQuery::BrushGeometryView geometry{
            nullptr, 0, nullptr, 0, brushes, 1, boxes, 1
        };

        const auto center = PortalBspQuery::ClassifyPointInBrush({ 0.0f, 0.0f, 0.0f }, 0, geometry);
        const auto face = PortalBspQuery::ClassifyPointInBrush({ 1.0f, 2.0f, 3.0f }, 0, geometry);
        Expect(center.status == PortalBspQuery::BrushPointStatus::Inside,
            "box brush contains its center");
        Expect(face.status == PortalBspQuery::BrushPointStatus::Inside,
            "box brush includes its boundary faces");
    }

    void TestBoxBrushRejectsOutsidePoint()
    {
        const PortalBspQuery::Brush brushes[] = {
            { 0x1, PortalBspQuery::kBoxBrushSideCount, 0 }
        };
        const PortalBspQuery::BoxBrush boxes[] = {
            { { -1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f, 1.0f } }
        };
        const PortalBspQuery::BrushGeometryView geometry{
            nullptr, 0, nullptr, 0, brushes, 1, boxes, 1
        };

        const auto result = PortalBspQuery::ClassifyPointInBrush({ 1.01f, 0.0f, 0.0f }, 0, geometry);
        Expect(result.status == PortalBspQuery::BrushPointStatus::Outside,
            "box brush rejects a point outside one bound");
    }

    void TestBoxBrushRejectsInvalidIndex()
    {
        const PortalBspQuery::Brush brushes[] = {
            { 0x1, PortalBspQuery::kBoxBrushSideCount, 1 }
        };
        const PortalBspQuery::BoxBrush boxes[] = {
            { { -1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f, 1.0f } }
        };
        const PortalBspQuery::BrushGeometryView geometry{
            nullptr, 0, nullptr, 0, brushes, 1, boxes, 1
        };

        const auto result = PortalBspQuery::ClassifyPointInBrush({ 0.0f, 0.0f, 0.0f }, 0, geometry);
        Expect(result.status == PortalBspQuery::BrushPointStatus::InvalidBoxBrushIndex,
            "box brush rejects an index outside the box table");
    }

    void TestSurfaceQueryEnumeratesLeafBrushesAndFiltersContents()
    {
        const PortalBspQuery::Plane planes[] = {
            MakePlane(1.0f, 0.0f, 0.0f, 0.0f)
        };
        const PortalBspQuery::Node nodes[] = {
            { 0, -1, -1 }
        };
        const PortalBspQuery::Leaf leaves[] = {
            { 0, 2 }
        };
        const std::uint16_t leafBrushes[] = { 0, 1 };
        const PortalBspQuery::Brush brushes[] = {
            { 0x2, PortalBspQuery::kBoxBrushSideCount, 0 },
            { 0x1, PortalBspQuery::kBoxBrushSideCount, 1 }
        };
        const PortalBspQuery::BoxBrush boxes[] = {
            { { -20.0f, -1.0f, -1.0f }, { 0.0f, 1.0f, 1.0f } },
            { { -20.0f, -1.0f, -1.0f }, { 0.0f, 1.0f, 1.0f } }
        };
        const PortalBspQuery::BspView bsp{
            { planes, 1, nodes, 1, 1, 0 },
            { planes, 1, nullptr, 0, brushes, 2, boxes, 2 },
            leaves, 1,
            leafBrushes, 2
        };

        const auto result = PortalBspQuery::FindBrushForSurfacePoint(
            { 0.0f, 0.0f, 0.0f },
            { 1.0f, 0.0f, 0.0f },
            0x1,
            bsp);

        Expect(result.status == PortalBspQuery::SurfaceBrushStatus::Success,
            "surface query finds a player-solid brush");
        Expect(result.leafIndex == 0, "surface query reports the selected leaf");
        Expect(result.brushIndex == 1, "surface query skips a brush rejected by the required mask");
        Expect(result.originalContents == 0x1, "surface query reports original brush contents");
    }

    void TestSurfaceQuerySkipsDuplicateCandidatesPerSample()
    {
        const PortalBspQuery::Plane planes[] = {
            MakePlane(1.0f, 0.0f, 0.0f, 0.0f)
        };
        const PortalBspQuery::Node nodes[] = {
            { 0, -1, -1 }
        };
        const PortalBspQuery::Leaf leaves[] = {
            { 0, 2 }
        };
        const std::uint16_t leafBrushes[] = { 0, 0 };
        const PortalBspQuery::Brush brushes[] = {
            { 0x1, PortalBspQuery::kBoxBrushSideCount, 0 }
        };
        const PortalBspQuery::BoxBrush boxes[] = {
            { { -100.0f, -1.0f, -1.0f }, { -90.0f, 1.0f, 1.0f } }
        };
        const PortalBspQuery::BspView bsp{
            { planes, 1, nodes, 1, 1, 0 },
            { planes, 1, nullptr, 0, brushes, 1, boxes, 1 },
            leaves, 1,
            leafBrushes, 2
        };

        const auto result = PortalBspQuery::FindBrushForSurfacePoint(
            { 0.0f, 0.0f, 0.0f },
            { 1.0f, 0.0f, 0.0f },
            0x1,
            bsp);

        Expect(result.status == PortalBspQuery::SurfaceBrushStatus::NoBrushFound,
            "surface query reports no containing brush");
        Expect(result.samplesTested == PortalBspQuery::kSurfaceSampleOffsets.size(),
            "surface query evaluates every configured sample when none match");
        Expect(result.candidatesTested == PortalBspQuery::kSurfaceSampleOffsets.size(),
            "duplicate leafbrush entries are tested once per sample");
        Expect(result.duplicateCandidatesSkipped == PortalBspQuery::kSurfaceSampleOffsets.size(),
            "duplicate leafbrush entries are diagnosed and skipped");
    }

    void TestSurfaceQueryUsesRequiredSampleOrder()
    {
        constexpr float expectedOffsets[] = { 2.0f, 4.0f, 8.0f, 1.0f, 16.0f, 0.5f };
        Expect(PortalBspQuery::kSurfaceSampleOffsets.size() == 6,
            "surface query exposes all six required sample offsets");
        for (std::size_t i = 0; i < PortalBspQuery::kSurfaceSampleOffsets.size(); ++i)
        {
            Expect(PortalBspQuery::kSurfaceSampleOffsets[i] == expectedOffsets[i],
                "surface query preserves SourcePawn sample ordering");
        }

        const PortalBspQuery::Plane planes[] = {
            MakePlane(1.0f, 0.0f, 0.0f, 0.0f)
        };
        const PortalBspQuery::Node nodes[] = {
            { 0, -1, -1 }
        };
        const PortalBspQuery::Leaf leaves[] = {
            { 0, 1 }
        };
        const std::uint16_t leafBrushes[] = { 0 };
        const PortalBspQuery::Brush brushes[] = {
            { 0x1, PortalBspQuery::kBoxBrushSideCount, 0 }
        };
        const PortalBspQuery::BoxBrush boxes[] = {
            { { -4.25f, -1.0f, -1.0f }, { -3.75f, 1.0f, 1.0f } }
        };
        const PortalBspQuery::BspView bsp{
            { planes, 1, nodes, 1, 1, 0 },
            { planes, 1, nullptr, 0, brushes, 1, boxes, 1 },
            leaves, 1,
            leafBrushes, 1
        };

        const auto result = PortalBspQuery::FindBrushForSurfacePoint(
            { 0.0f, 0.0f, 0.0f },
            { 1.0f, 0.0f, 0.0f },
            0x1,
            bsp);

        Expect(result.status == PortalBspQuery::SurfaceBrushStatus::Success,
            "surface query finds the brush at the second configured sample");
        Expect(result.selectedSampleOffset == 4.0f,
            "surface query reports the selected sample offset");
        Expect(result.samplesTested == 2,
            "surface query tests offset 2 before offset 4");
    }
}

int main()
{
    TestFrontChildTraversal();
    TestBackChildTraversal();
    TestNegativeChildDecodesLeaf();
    TestInvalidDecodedLeafRejected();
    TestConvexBrushContainsInteriorPoint();
    TestConvexBrushRejectsPointPastOnePlane();
    TestConvexBrushRejectsNullPlane();
    TestConvexBrushRejectsZeroSides();
    TestConvexBrushRejectsSideRangeOverflow();
    TestBoxBrushContainsCenterAndBoundaryFace();
    TestBoxBrushRejectsOutsidePoint();
    TestBoxBrushRejectsInvalidIndex();
    TestSurfaceQueryEnumeratesLeafBrushesAndFiltersContents();
    TestSurfaceQuerySkipsDuplicateCandidatesPerSample();
    TestSurfaceQueryUsesRequiredSampleOrder();

    std::cout << "PortalBspQuery tests passed\n";
    return 0;
}
