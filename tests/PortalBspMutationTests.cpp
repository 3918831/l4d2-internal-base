#include "../src/Portal/PortalBspBrushAccess.h"
#include "../src/Portal/PortalBspCollisionCarver.h"

#include <cstdlib>
#include <iostream>
#include <map>
#include <vector>

namespace
{
    void Expect(bool condition, const char* message)
    {
        if (condition)
            return;

        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }

    PortalBspQuery::SurfaceBrushResult MakeResolvedResult(int brushIndex, int contents)
    {
        PortalBspQuery::SurfaceBrushResult result{};
        result.status = PortalBspQuery::SurfaceBrushStatus::Success;
        result.pointLeafStatus = PortalBspQuery::PointLeafStatus::Success;
        result.lastBrushStatus = PortalBspQuery::BrushPointStatus::Inside;
        result.leafIndex = brushIndex + 100;
        result.brushIndex = brushIndex;
        result.originalContents = contents;
        result.selectedSampleOffset = 2.0f;
        return result;
    }

    class FakeBrushAccess final : public IPortalBspBrushAccess
    {
    public:
        struct Write
        {
            int brushIndex = -1;
            int expected = 0;
            int replacement = 0;
        };

        std::uint32_t GetMapGeneration() const override
        {
            return generation;
        }

        PortalBrushReadResult ReadBrushContents(
            int brushIndex,
            std::uint32_t expectedGeneration) const override
        {
            if (expectedGeneration != generation)
                return { PortalBrushAccessStatus::GenerationMismatch, 0 };

            const auto it = contents.find(brushIndex);
            if (it == contents.end())
                return { PortalBrushAccessStatus::InvalidBrushIndex, 0 };

            return { PortalBrushAccessStatus::Success, it->second };
        }

        PortalBrushWriteResult CompareAndWriteBrushContents(
            int brushIndex,
            std::uint32_t expectedGeneration,
            int expectedCurrent,
            int replacement) override
        {
            const PortalBrushReadResult read = ReadBrushContents(brushIndex, expectedGeneration);
            if (!read.Succeeded())
                return { read.status, read.contents, read.contents };
            if (read.contents != expectedCurrent)
                return { PortalBrushAccessStatus::UnexpectedContents, read.contents, read.contents };
            if (brushIndex == failBrushIndex && replacement == failReplacement)
                return { PortalBrushAccessStatus::WriteFailed, read.contents, read.contents };

            writes.push_back({ brushIndex, expectedCurrent, replacement });
            contents[brushIndex] = replacement;
            return { PortalBrushAccessStatus::Success, read.contents, replacement };
        }

        std::uint32_t generation = 7;
        int failBrushIndex = -1;
        int failReplacement = 0;
        std::map<int, int> contents;
        std::vector<Write> writes;
    };

    void Bind(
        CPortalBspCollisionCarver& carver,
        PortalBrushOwner owner,
        int brushIndex,
        int contents,
        std::uint32_t generation = 7)
    {
        const PortalBspQuery::Vector3 hit{};
        const PortalBspQuery::Vector3 normal{ 1.0f, 0.0f, 0.0f };
        const auto result = MakeResolvedResult(brushIndex, contents);
        Expect(carver.ApplyResolution(owner, hit, normal, result, generation),
            "test binding resolves");
    }

    void TestDistinctBrushTransaction()
    {
        CPortalBspCollisionCarver carver;
        FakeBrushAccess access;
        access.contents[11] = 0x1;
        access.contents[12] = 0x08000001;
        Bind(carver, PortalBrushOwner::Blue, 11, 0x1);
        Bind(carver, PortalBrushOwner::Orange, 12, 0x08000001);

        Expect(carver.ActivatePairCarving(access),
            "two distinct resolved brushes activate transactionally");
        Expect(access.contents[11] == 0 && access.contents[12] == 0,
            "activation clears both brush contents");
        Expect(carver.IsCarvingActive() && carver.GetModifiedBrushes().size() == 2,
            "activation records two unique modified brushes");
        Expect(carver.GetModifiedBrushes()[0].originalContents != 0
            && carver.GetModifiedBrushes()[1].originalContents != 0,
            "original contents are preserved before writes");

        const std::size_t writesAfterFirstActivation = access.writes.size();
        Expect(carver.ActivatePairCarving(access), "repeated activation is idempotent");
        Expect(access.writes.size() == writesAfterFirstActivation,
            "idempotent activation does not rewrite or replace originals");

        Expect(carver.RestoreAll(access, "test-distinct"),
            "restore succeeds for both distinct brushes");
        Expect(access.contents[11] == 0x1 && access.contents[12] == 0x08000001,
            "restore returns exact original contents");
        const std::size_t writesAfterRestore = access.writes.size();
        Expect(carver.RestoreAll(access, "test-repeated-restore"),
            "repeated restore is a safe no-op");
        Expect(access.writes.size() == writesAfterRestore,
            "repeated restore performs no memory write");
    }

    void TestSharedBrushOwnership()
    {
        CPortalBspCollisionCarver carver;
        FakeBrushAccess access;
        access.contents[20] = 0x1;
        Bind(carver, PortalBrushOwner::Blue, 20, 0x1);
        Bind(carver, PortalBrushOwner::Orange, 20, 0x1);

        Expect(carver.ActivatePairCarving(access), "shared brush pair activates");
        Expect(access.writes.size() == 1 && carver.GetModifiedBrushes().size() == 1,
            "shared brush is written exactly once");
        Expect(carver.GetModifiedBrushes()[0].ownerMask
            == (PortalBrushOwnerMask(PortalBrushOwner::Blue)
                | PortalBrushOwnerMask(PortalBrushOwner::Orange)),
            "shared brush records both portal owners");

        Expect(carver.ReleaseOwner(PortalBrushOwner::Blue, access, "test-blue-release"),
            "first shared owner release succeeds");
        Expect(access.contents[20] == 0 && carver.IsCarvingActive(),
            "shared brush remains empty while orange still owns it");
        Expect(carver.ReleaseOwner(PortalBrushOwner::Orange, access, "test-orange-release"),
            "final shared owner release succeeds");
        Expect(access.contents[20] == 0x1 && !carver.IsCarvingActive(),
            "shared brush restores after final owner release");
    }

    void TestRejectedActivations()
    {
        CPortalBspCollisionCarver oneOwner;
        FakeBrushAccess access;
        access.contents[30] = 0x1;
        Bind(oneOwner, PortalBrushOwner::Blue, 30, 0x1);
        Expect(!oneOwner.ActivatePairCarving(access),
            "one-owner activation is rejected");
        Expect(access.writes.empty(), "one-owner rejection performs no writes");

        CPortalBspCollisionCarver staleContents;
        access.contents[31] = 0x2;
        Bind(staleContents, PortalBrushOwner::Blue, 31, 0x1);
        Bind(staleContents, PortalBrushOwner::Orange, 31, 0x1);
        Expect(!staleContents.ActivatePairCarving(access),
            "stale expected contents are rejected");
        Expect(access.contents[31] == 0x2, "stale rejection preserves external contents");

        CPortalBspCollisionCarver wrongGeneration;
        access.contents[32] = 0x1;
        Bind(wrongGeneration, PortalBrushOwner::Blue, 32, 0x1, 6);
        Bind(wrongGeneration, PortalBrushOwner::Orange, 32, 0x1, 6);
        Expect(!wrongGeneration.ActivatePairCarving(access),
            "map-generation mismatch is rejected");

        CPortalBspCollisionCarver outOfRange;
        Bind(outOfRange, PortalBrushOwner::Blue, 40, 0x1);
        Bind(outOfRange, PortalBrushOwner::Orange, 41, 0x1);
        Expect(!outOfRange.ActivatePairCarving(access),
            "out-of-range brush indices are rejected by access layer");
    }

    void TestFailedSecondWriteRollsBackFirst()
    {
        CPortalBspCollisionCarver carver;
        FakeBrushAccess access;
        access.contents[50] = 0x1;
        access.contents[51] = 0x08000001;
        access.failBrushIndex = 51;
        access.failReplacement = 0;
        Bind(carver, PortalBrushOwner::Blue, 50, 0x1);
        Bind(carver, PortalBrushOwner::Orange, 51, 0x08000001);

        Expect(!carver.ActivatePairCarving(access),
            "failed second write reports transaction failure");
        Expect(access.contents[50] == 0x1 && access.contents[51] == 0x08000001,
            "failed second write rolls back the first brush");
        Expect(!carver.IsCarvingActive() && carver.GetModifiedBrushes().empty(),
            "successful rollback leaves no active mutation state");
    }

    void TestRestoreRejectsWrongGenerationWithoutLosingRecoveryState()
    {
        CPortalBspCollisionCarver carver;
        FakeBrushAccess access;
        access.contents[55] = 0x1;
        access.contents[56] = 0x08000001;
        Bind(carver, PortalBrushOwner::Blue, 55, 0x1);
        Bind(carver, PortalBrushOwner::Orange, 56, 0x08000001);

        Expect(carver.ActivatePairCarving(access),
            "generation restore test activates both brushes");
        access.generation = 8;
        Expect(!carver.RestoreAll(access, "test-stale-generation"),
            "restore rejects a different live map generation");
        Expect(carver.IsCarvingActive() && carver.GetModifiedBrushes().size() == 2,
            "rejected restore retains exact recovery records");
        Expect(carver.GetLastOperation().status == PortalCarveStatus::RestoreRejected
            && carver.GetLastOperation().accessStatus == PortalBrushAccessStatus::GenerationMismatch,
            "generation rejection is visible in operation diagnostics");

        access.generation = 7;
        Expect(carver.RestoreAll(access, "test-original-generation"),
            "restore succeeds when the expected generation is available again");
        Expect(access.contents[55] == 0x1 && access.contents[56] == 0x08000001,
            "retry restores exact original contents");
    }

    void TestPhase1DevelopmentToggle()
    {
        CPortalBspCollisionCarver carver;
        FakeBrushAccess access;
        access.contents[60] = 0x1;
        access.contents[61] = 0x1;
        Bind(carver, PortalBrushOwner::Blue, 60, 0x1);
        Bind(carver, PortalBrushOwner::Orange, 61, 0x1);

        Expect(!carver.IsPhase1Enabled(), "Phase 1 defaults disabled");
        Expect(!carver.TryActivatePhase1(access),
            "disabled Phase 1 never writes a resolved pair");
        Expect(access.contents[60] == 0x1 && access.contents[61] == 0x1,
            "disabled Phase 1 preserves collision");

        Expect(carver.SetPhase1Enabled(true, access, "test-enable"),
            "enabling Phase 1 activates an already resolved pair");
        Expect(carver.IsPhase1Enabled() && carver.IsCarvingActive(),
            "enabled Phase 1 reports active carving");
        Expect(access.contents[60] == 0 && access.contents[61] == 0,
            "enabled Phase 1 clears the pair without proximity input");

        Expect(carver.SetPhase1Enabled(false, access, "test-disable"),
            "disabling Phase 1 restores active brushes");
        Expect(!carver.IsPhase1Enabled() && !carver.IsCarvingActive(),
            "disabled Phase 1 reports inactive after restore");
        Expect(access.contents[60] == 0x1 && access.contents[61] == 0x1,
            "disable restores exact collision values");
    }
}

int main()
{
    TestDistinctBrushTransaction();
    TestSharedBrushOwnership();
    TestRejectedActivations();
    TestFailedSecondWriteRollsBackFirst();
    TestRestoreRejectsWrongGenerationWithoutLosingRecoveryState();
    TestPhase1DevelopmentToggle();
    std::cout << "Portal BSP mutation tests passed\n";
    return 0;
}
