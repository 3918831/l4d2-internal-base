#pragma once

#include <cstddef>
#include <cstdint>

namespace U::PortalBspOffset
{
    bool RecordCandidate(
        std::uintptr_t address,
        std::uintptr_t engineBase,
        std::size_t engineSize,
        const char* source);

    void ClearCandidate();
    std::uintptr_t GetCandidate();
}
