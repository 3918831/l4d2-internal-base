#pragma once

#include <cstddef>
#include <cstdint>

namespace EngineTraceDiagnostics
{
    struct ModuleRange
    {
        std::uintptr_t base = 0;
        std::size_t size = 0;
    };

    struct AddressDescription
    {
        bool insideModule = false;
        std::uintptr_t rva = 0;
    };

    inline AddressDescription DescribeAddress(const ModuleRange& module, std::uintptr_t address)
    {
        if (module.base == 0 || module.size == 0)
            return {};

        const std::uintptr_t end = module.base + module.size;
        if (end < module.base || address < module.base || address >= end)
            return {};

        return { true, address - module.base };
    }
}
