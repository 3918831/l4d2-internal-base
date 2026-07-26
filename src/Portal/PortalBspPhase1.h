#pragma once

#include "PortalBspCollisionCarver.h"

namespace PortalBspPhase1
{
    bool SetEnabled(bool enabled, const char* reason);
    bool TryActivate(const char* reason);
    bool Restore(const char* reason);
    bool PrepareForPlacement(PortalBrushOwner owner, const char* reason);
    bool RestoreAndClearBindings(const char* reason);
    void DiscardInvalidatedState(const char* reason);
    void LogStatus(const char* reason);
}
