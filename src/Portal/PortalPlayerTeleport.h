#pragma once

#include "../SDK/SDK.h"

class C_TerrorPlayer;

namespace PortalPlayerTeleport
{
    bool Commit(
        C_TerrorPlayer* clientPlayer,
        const Vector& origin,
        const QAngle& angles,
        const Vector& velocity);
}
