#include "PortalRenderFixMeshWriter.h"

#include <limits>

namespace PortalRenderFixMeshWriter
{
    bool WriteSequentialStripIndices(
        unsigned short* indices,
        std::size_t indexCapacity,
        int indexCount,
        int firstVertex,
        unsigned char indexSize,
        WriteResult& result)
    {
        result = WriteResult();
        if (!indices
            || indexCount <= 0
            || firstVertex < 0
            || indexSize != 1)
        {
            return false;
        }

        const std::size_t count = static_cast<std::size_t>(indexCount);
        if (indexCapacity < count)
            return false;

        const int maximumIndex =
            static_cast<int>(std::numeric_limits<unsigned short>::max());
        if (firstVertex > maximumIndex - (indexCount - 1))
            return false;

        for (int index = 0; index < indexCount; ++index)
        {
            indices[static_cast<std::size_t>(index) * indexSize] =
                static_cast<unsigned short>(firstVertex + index);
        }

        result.written = indexCount;
        result.firstIndexValue =
            static_cast<unsigned short>(firstVertex);
        result.lastIndexValue =
            static_cast<unsigned short>(firstVertex + indexCount - 1);
        return true;
    }
}
