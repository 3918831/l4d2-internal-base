#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace EngineTraceBspProbe
{
    // GetBrushInfo returns through AL (mov al, 1 / xor al, al) on the inspected
    // 32-bit Windows engine build. Treating it as int preserves stale upper bits.
    using GetBrushInfoResult = bool;

    struct GetBrushInfoLayoutCandidate
    {
        std::uintptr_t bspBase = 0;
        std::uintptr_t numBrushes = 0;
        std::uintptr_t mapBrushes = 0;
        std::uintptr_t validatedBrushCount = 0;
        bool valid = false;
    };

    inline bool IsAddressInsideModule(
        std::uintptr_t address,
        std::uintptr_t moduleBase,
        std::size_t moduleSize)
    {
        if (moduleBase == 0 || moduleSize == 0 || address < moduleBase)
            return false;

        return address - moduleBase < moduleSize;
    }

    inline std::uint32_t ReadImmediate32(const std::uint8_t* address)
    {
        std::uint32_t value = 0;
        std::memcpy(&value, address, sizeof(value));
        return value;
    }

    // Decodes the three absolute operands at the beginning of GetBrushInfo:
    // cmp esi,[numbrushes], cmp esi,[validated count], mov eax,[map_brushes].
    // Their +0xC0/+0xC4/+0xC8 relationship is required before accepting a base.
    inline GetBrushInfoLayoutCandidate DecodeGetBrushInfoLayout(
        const std::uint8_t* code,
        std::size_t codeSize,
        std::uintptr_t moduleBase,
        std::size_t moduleSize)
    {
        GetBrushInfoLayoutCandidate candidate{};
        if (!code || codeSize < 6)
            return candidate;

        std::uintptr_t compareOperands[2]{};
        std::size_t compareCount = 0;
        std::uintptr_t brushArrayOperand = 0;

        for (std::size_t i = 0; i + 6 <= codeSize; ++i)
        {
            if (compareCount < 2 && code[i] == 0x3B && code[i + 1] == 0x35)
            {
                compareOperands[compareCount++] = ReadImmediate32(code + i + 2);
                i += 5;
                continue;
            }

            if (compareCount == 2 && code[i] == 0xA1 && i + 5 <= codeSize)
            {
                brushArrayOperand = ReadImmediate32(code + i + 1);
                break;
            }
        }

        if (compareCount != 2 || brushArrayOperand < 0xC4u)
            return candidate;

        const std::uintptr_t base = brushArrayOperand - 0xC4u;
        const bool relationshipsMatch = compareOperands[0] == base + 0xC0u
            && brushArrayOperand == base + 0xC4u
            && compareOperands[1] == base + 0xC8u;
        if (!relationshipsMatch
            || !IsAddressInsideModule(base, moduleBase, moduleSize)
            || !IsAddressInsideModule(compareOperands[0], moduleBase, moduleSize)
            || !IsAddressInsideModule(brushArrayOperand, moduleBase, moduleSize)
            || !IsAddressInsideModule(compareOperands[1], moduleBase, moduleSize))
        {
            return candidate;
        }

        candidate.bspBase = base;
        candidate.numBrushes = compareOperands[0];
        candidate.mapBrushes = brushArrayOperand;
        candidate.validatedBrushCount = compareOperands[1];
        candidate.valid = true;
        return candidate;
    }

    struct ContiguousCountResult
    {
        std::size_t count = 0;
        std::size_t probeCalls = 0;
        bool complete = false;
    };

    // Finds the first invalid index in a contiguous [0, count) engine table.
    // Exponential search keeps the live GetBrushInfo diagnostic logarithmic.
    template <typename IsValidIndex>
    ContiguousCountResult FindContiguousCount(IsValidIndex&& isValidIndex, std::size_t maxExclusive)
    {
        ContiguousCountResult result{};
        if (maxExclusive == 0)
            return result;

        const auto probe = [&](std::size_t index)
        {
            ++result.probeCalls;
            return isValidIndex(index);
        };

        if (!probe(0))
        {
            result.complete = true;
            return result;
        }

        if (maxExclusive == 1)
        {
            result.count = 1;
            return result;
        }

        std::size_t knownValid = 0;
        std::size_t candidate = 1;
        while (candidate < maxExclusive && probe(candidate))
        {
            knownValid = candidate;
            if (candidate > maxExclusive / 2)
            {
                candidate = maxExclusive;
                break;
            }
            candidate *= 2;
        }

        if (candidate >= maxExclusive)
        {
            const std::size_t lastIndex = maxExclusive - 1;
            if (knownValid != lastIndex && !probe(lastIndex))
            {
                candidate = lastIndex;
            }
            else
            {
                result.count = maxExclusive;
                return result;
            }
        }

        std::size_t firstInvalid = candidate;
        while (knownValid + 1 < firstInvalid)
        {
            const std::size_t middle = knownValid + (firstInvalid - knownValid) / 2;
            if (probe(middle))
                knownValid = middle;
            else
                firstInvalid = middle;
        }

        result.count = firstInvalid;
        result.complete = true;
        return result;
    }
}
