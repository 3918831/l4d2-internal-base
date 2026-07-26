#pragma once

#include <cstddef>

namespace PortalRenderFixMeshWriter
{
    struct WriteResult
    {
        int written = 0;
        unsigned short firstIndexValue = 0;
        unsigned short lastIndexValue = 0;
    };

    bool WriteSequentialStripIndices(
        unsigned short* indices,
        std::size_t indexCapacity,
        int indexCount,
        int firstVertex,
        unsigned char indexSize,
        WriteResult& result);
}
