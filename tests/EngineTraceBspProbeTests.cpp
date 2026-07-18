#include <array>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <type_traits>

#include "../src/Hooks/EngineTrace/EngineTraceBspProbe.h"

namespace
{
    void Expect(bool condition, const char* message)
    {
        if (condition)
            return;

        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

int main()
{
    using EngineTraceBspProbe::DecodeGetBrushInfoLayout;
    using EngineTraceBspProbe::FindContiguousCount;
    using EngineTraceBspProbe::GetBrushInfoResult;

    static_assert(std::is_same<GetBrushInfoResult, bool>::value,
        "GetBrushInfo must consume the one-byte AL result as bool");

    const auto ordinary = FindContiguousCount(
        [](std::size_t index) { return index < 137u; },
        1024u);
    Expect(ordinary.complete, "a boundary below the safety cap is complete");
    Expect(ordinary.count == 137u, "the first invalid brush index is the brush count");
    Expect(ordinary.probeCalls < 32u, "the count probe uses logarithmic calls");

    const auto empty = FindContiguousCount(
        [](std::size_t) { return false; },
        1024u);
    Expect(empty.complete, "an invalid index zero proves an empty sequence");
    Expect(empty.count == 0u, "an empty sequence has count zero");

    const auto capped = FindContiguousCount(
        [](std::size_t) { return true; },
        64u);
    Expect(!capped.complete, "a fully valid safety window is not an exact count");
    Expect(capped.count == 64u, "an incomplete result reports the proven lower bound");

    constexpr std::uintptr_t moduleBase = 0x10000000u;
    constexpr std::size_t moduleSize = 0x00100000u;
    constexpr std::uintptr_t bspBase = moduleBase + 0x00083000u;
    constexpr std::uintptr_t numBrushes = bspBase + 0xC0u;
    constexpr std::uintptr_t mapBrushes = bspBase + 0xC4u;
    constexpr std::uintptr_t validatedBrushCount = bspBase + 0xC8u;

    std::array<std::uint8_t, 64> getBrushInfoCode{};
    const auto writeAddress = [&](std::size_t offset, std::uintptr_t address)
    {
        const std::uint32_t address32 = static_cast<std::uint32_t>(address);
        std::memcpy(getBrushInfoCode.data() + offset, &address32, sizeof(address32));
    };

    getBrushInfoCode[4] = 0x3B;
    getBrushInfoCode[5] = 0x35;
    writeAddress(6, numBrushes);
    getBrushInfoCode[16] = 0x3B;
    getBrushInfoCode[17] = 0x35;
    writeAddress(18, validatedBrushCount);
    getBrushInfoCode[28] = 0xA1;
    writeAddress(29, mapBrushes);

    const auto decoded = DecodeGetBrushInfoLayout(
        getBrushInfoCode.data(), getBrushInfoCode.size(), moduleBase, moduleSize);
    Expect(decoded.valid, "the GetBrushInfo operand pattern decodes a layout candidate");
    Expect(decoded.bspBase == bspBase, "the candidate base is map_brushes minus 0xC4");
    Expect(decoded.numBrushes == numBrushes, "the first compare operand is numbrushes");
    Expect(decoded.mapBrushes == mapBrushes, "the mov operand is map_brushes");
    Expect(decoded.validatedBrushCount == validatedBrushCount,
        "the second compare operand is the mirrored brush count");

    getBrushInfoCode[29] += 4;
    const auto inconsistent = DecodeGetBrushInfoLayout(
        getBrushInfoCode.data(), getBrushInfoCode.size(), moduleBase, moduleSize);
    Expect(!inconsistent.valid, "inconsistent +0xC0/+0xC4/+0xC8 operands are rejected");

    std::cout << "EngineTrace BSP probe tests passed\n";
    return 0;
}
