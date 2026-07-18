#include "PortalBspOffset.h"

#include "../Logger/Logger.h"

namespace
{
    std::uintptr_t g_CollisionBspData = 0;
}

bool U::PortalBspOffset::RecordCandidate(
    std::uintptr_t address,
    std::uintptr_t engineBase,
    std::size_t engineSize,
    const char* source)
{
    const bool rangeValid = address != 0u
        && engineBase != 0u
        && engineSize != 0u
        && address >= engineBase
        && address - engineBase < engineSize;
    if (!rangeValid)
    {
        U::LogError("[PortalBsp][Offset] rejected candidate=%p engineBase=%p engineSize=0x%zX source=%s.\n",
            reinterpret_cast<void*>(address),
            reinterpret_cast<void*>(engineBase),
            engineSize,
            source ? source : "unknown");
        g_CollisionBspData = 0u;
        return false;
    }

    if (g_CollisionBspData != address)
    {
        g_CollisionBspData = address;
        U::LogInfo("[PortalBsp][Offset] gBspData=%p rva=0x%08X source=%s readOnly=true.\n",
            reinterpret_cast<void*>(address),
            static_cast<unsigned int>(address - engineBase),
            source ? source : "unknown");
    }

    return true;
}

void U::PortalBspOffset::ClearCandidate()
{
    g_CollisionBspData = 0u;
}

std::uintptr_t U::PortalBspOffset::GetCandidate()
{
    return g_CollisionBspData;
}
