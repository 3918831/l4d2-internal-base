#include <cstdlib>
#include <iostream>

#include "../src/Hooks/EngineTrace/EngineTraceDiagnostics.h"
#include "../src/Util/Logger/PortalFileLog.h"

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
    Expect(
        U::PortalFileLog::ShouldCapture("[PortalBsp][EngineTrace] slot=16"),
        "PortalBsp diagnostics are persisted to the focused traversal log");

    const EngineTraceDiagnostics::ModuleRange module{ 0x10000000u, 0x00100000u };

    const auto inside = EngineTraceDiagnostics::DescribeAddress(module, 0x10001234u);
    Expect(inside.insideModule, "address inside the module is accepted");
    Expect(inside.rva == 0x1234u, "RVA is relative to the module base");

    const auto atEnd = EngineTraceDiagnostics::DescribeAddress(module, 0x10100000u);
    Expect(!atEnd.insideModule, "module end is an exclusive boundary");

    const auto before = EngineTraceDiagnostics::DescribeAddress(module, 0x0FFFFFFFu);
    Expect(!before.insideModule, "address before module base is rejected");
    Expect(before.rva == 0u, "rejected address has no misleading RVA");

    const EngineTraceDiagnostics::ModuleRange overflowed{ 0xFFFFFF00u, 0x1000u };
    const auto wrapped = EngineTraceDiagnostics::DescribeAddress(overflowed, 0x00000020u);
    Expect(!wrapped.insideModule, "overflowed module range is rejected");

    std::cout << "EngineTraceDiagnostics tests passed\n";
    return 0;
}
