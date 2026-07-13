#pragma once

#include "../SDK/SDK.h"
#include "PortalTransitionSimulator.h"

class CMoveData;

namespace PortalControlledNoclipMovement
{
    bool TryApply(
        const char* domain,
        unsigned int heartbeat,
        CMoveData* move,
        const PortalTransitionContext& context);
}
