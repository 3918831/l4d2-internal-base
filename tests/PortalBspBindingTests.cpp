#include "../src/Portal/PortalBspCollisionCarver.h"

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

    PortalBspQuery::SurfaceBrushResult MakeResolvedResult(
        int leafIndex,
        int brushIndex,
        int contents,
        float offset)
    {
        PortalBspQuery::SurfaceBrushResult result{};
        result.status = PortalBspQuery::SurfaceBrushStatus::Success;
        result.pointLeafStatus = PortalBspQuery::PointLeafStatus::Success;
        result.lastBrushStatus = PortalBspQuery::BrushPointStatus::Inside;
        result.leafIndex = leafIndex;
        result.brushIndex = brushIndex;
        result.originalContents = contents;
        result.selectedSampleOffset = offset;
        result.samplePosition = { -offset, 0.0f, 0.0f };
        result.samplesTested = 2;
        result.candidatesTested = 3;
        result.duplicateCandidatesSkipped = 1;
        return result;
    }
}

int main()
{
    CPortalBspCollisionCarver carver;
    const PortalBspQuery::Vector3 hit{ 0.0f, 0.0f, 0.0f };
    const PortalBspQuery::Vector3 normal{ 1.0f, 0.0f, 0.0f };

    const auto blueResult = MakeResolvedResult(7, 11, 0x200400B, 2.0f);
    Expect(carver.ApplyResolution(PortalBrushOwner::Blue, hit, normal, blueResult, 3),
        "successful blue resolution creates a binding");
    const PortalBrushBinding& blue = carver.GetBinding(PortalBrushOwner::Blue);
    Expect(blue.resolved, "blue binding is marked resolved");
    Expect(blue.leafIndex == 7 && blue.brushIndex == 11,
        "blue binding keeps leaf and brush identity");
    Expect(blue.originalContents == 0x200400B && blue.selectedSampleOffset == 2.0f,
        "blue binding keeps contents and selected sample");
    Expect(blue.samplesTested == 2 && blue.candidatesTested == 3
        && blue.duplicateCandidatesSkipped == 1,
        "blue binding keeps query diagnostic counters");
    Expect(blue.mapGeneration == 3, "blue binding keeps map generation");

    const auto orangeResult = MakeResolvedResult(8, 12, 0x1, 4.0f);
    Expect(carver.ApplyResolution(PortalBrushOwner::Orange, hit, normal, orangeResult, 3),
        "successful orange resolution creates an independent binding");
    Expect(carver.IsPairResolved(), "both successful owner bindings form a resolved pair");

    CPortalBspCollisionCarver staleCacheCarver;
    Expect(staleCacheCarver.ApplyResolution(PortalBrushOwner::Blue, hit, normal, blueResult, 2),
        "test setup creates an old resolved binding");
    const auto staleSuccess = MakeResolvedResult(99, 123, 0x1, 2.0f);
    Expect(!staleCacheCarver.ApplyPlacementResolution(
        PortalBrushOwner::Blue,
        hit,
        normal,
        staleSuccess,
        3,
        false),
        "placement rejects a stale success result when query initialization failed");
    Expect(!staleCacheCarver.GetBinding(PortalBrushOwner::Blue).resolved
        && staleCacheCarver.GetBinding(PortalBrushOwner::Blue).status
            == PortalBspQuery::SurfaceBrushStatus::MissingData,
        "query initialization failure replaces stale ownership with an unresolved binding");

    PortalBspQuery::SurfaceBrushResult failed{};
    failed.status = PortalBspQuery::SurfaceBrushStatus::NoBrushFound;
    Expect(!carver.ApplyResolution(PortalBrushOwner::Blue, hit, normal, failed, 3),
        "failed replacement is reported");
    Expect(!carver.GetBinding(PortalBrushOwner::Blue).resolved,
        "failed replacement clears the moved portal's stale binding");
    Expect(carver.GetBinding(PortalBrushOwner::Orange).resolved,
        "failed blue replacement does not clear orange ownership");
    Expect(!carver.IsPairResolved(), "an unresolved owner breaks pair readiness");

    carver.ClearBindings();
    Expect(!carver.GetBinding(PortalBrushOwner::Blue).resolved
        && !carver.GetBinding(PortalBrushOwner::Orange).resolved,
        "lifecycle clear removes both read-only bindings");

    std::cout << "Portal BSP binding tests passed\n";
    return 0;
}
