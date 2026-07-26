#include <array>
#include <cstddef>
#include <iostream>
#include <limits>

#include "../src/Portal/PortalRenderFixMeshWriter.h"

namespace
{
    int g_failures = 0;

    void Expect(bool condition, const char* message)
    {
        if (condition)
            return;

        ++g_failures;
        std::cerr << "FAIL: " << message << '\n';
    }

    void TestWritesSequentialIndicesFromFirstVertex()
    {
        std::array<unsigned short, 8> indices;
        indices.fill(0x7B7B);

        PortalRenderFixMeshWriter::WriteResult result;
        const bool written =
            PortalRenderFixMeshWriter::WriteSequentialStripIndices(
                indices.data(),
                indices.size(),
                4,
                37,
                1,
                result);

        Expect(written, "valid Source index descriptor must be accepted");
        Expect(result.written == 4, "all requested indices must be written");
        Expect(result.firstIndexValue == 37, "first index must include firstVertex");
        Expect(result.lastIndexValue == 40, "last index must include firstVertex");
        Expect(indices[0] == 37, "slot 0 must contain firstVertex");
        Expect(indices[1] == 38, "slot 1 must advance by one element");
        Expect(indices[2] == 39, "slot 2 must advance by one element");
        Expect(indices[3] == 40, "slot 3 must advance by one element");
        Expect(indices[4] == 0x7B7B, "writer must preserve the sentinel after the locked range");
    }

    void TestSupportsLargeFirstVertexWithoutByteOverlap()
    {
        std::array<unsigned short, 7> indices;
        indices.fill(0x6A6A);

        PortalRenderFixMeshWriter::WriteResult result;
        const bool written =
            PortalRenderFixMeshWriter::WriteSequentialStripIndices(
                indices.data(),
                indices.size(),
                5,
                1024,
                1,
                result);

        Expect(written, "large valid firstVertex must be accepted");
        for (std::size_t i = 0; i < 5; ++i)
        {
            Expect(
                indices[i] == static_cast<unsigned short>(1024 + i),
                "indices must be complete 16-bit values at element boundaries");
        }
        Expect(indices[5] == 0x6A6A, "sentinel after written indices must remain intact");
        Expect(indices[6] == 0x6A6A, "distant sentinel must remain intact");
    }

    void TestRejectsInactiveOrUnexpectedIndexIncrement()
    {
        std::array<unsigned short, 4> indices;
        indices.fill(0x5555);
        PortalRenderFixMeshWriter::WriteResult result;

        Expect(
            !PortalRenderFixMeshWriter::WriteSequentialStripIndices(
                indices.data(),
                indices.size(),
                4,
                0,
                0,
                result),
            "inactive index descriptor must fail closed");
        Expect(
            !PortalRenderFixMeshWriter::WriteSequentialStripIndices(
                indices.data(),
                indices.size(),
                4,
                0,
                2,
                result),
            "unexpected index increment must fail closed");
        Expect(indices[0] == 0x5555, "rejected descriptors must not modify memory");
    }

    void TestRejectsInvalidFirstVertexAndOverflow()
    {
        std::array<unsigned short, 4> indices;
        indices.fill(0x4444);
        PortalRenderFixMeshWriter::WriteResult result;

        Expect(
            !PortalRenderFixMeshWriter::WriteSequentialStripIndices(
                indices.data(),
                indices.size(),
                3,
                -1,
                1,
                result),
            "negative firstVertex must be rejected");
        Expect(
            !PortalRenderFixMeshWriter::WriteSequentialStripIndices(
                indices.data(),
                indices.size(),
                3,
                std::numeric_limits<unsigned short>::max() - 1,
                1,
                result),
            "16-bit index overflow must be rejected");
        Expect(indices[0] == 0x4444, "failed validation must occur before writes");
    }

    void TestRejectsInsufficientCapacity()
    {
        std::array<unsigned short, 3> indices;
        indices.fill(0x3333);
        PortalRenderFixMeshWriter::WriteResult result;

        Expect(
            !PortalRenderFixMeshWriter::WriteSequentialStripIndices(
                indices.data(),
                indices.size(),
                4,
                0,
                1,
                result),
            "writer must reject a locked range smaller than the requested index count");
        Expect(indices[0] == 0x3333, "capacity failure must not partially write");
    }
}

int main()
{
    TestWritesSequentialIndicesFromFirstVertex();
    TestSupportsLargeFirstVertexWithoutByteOverlap();
    TestRejectsInactiveOrUnexpectedIndexIncrement();
    TestRejectsInvalidFirstVertexAndOverflow();
    TestRejectsInsufficientCapacity();

    if (g_failures != 0)
    {
        std::cerr << g_failures << " portal render-fix mesh writer test(s) failed.\n";
        return 1;
    }

    std::cout << "All portal render-fix mesh writer tests passed.\n";
    return 0;
}
