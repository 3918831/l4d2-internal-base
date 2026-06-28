#include "PortalPlayerTeleport.h"

#include "../SDK/L4D2/Entities/C_TerrorPlayer.h"
#include "../SDK/L4D2/Includes/edict.h"
#include "../SDK/L4D2/Includes/iserverunknown.h"
#include "../SDK/L4D2/Interfaces/ClientEntityList.h"
#include "../SDK/L4D2/Interfaces/CServerTools.h"
#include "../SDK/L4D2/Interfaces/EngineClient.h"
#include "../SDK/L4D2/Interfaces/IPlayerInfoManager.h"
#include "../Util/Logger/Logger.h"

namespace
{
    constexpr size_t kServerTeleportVTableIndex = 118;

    CBaseEntity* ResolveServerLocalPlayer(C_TerrorPlayer* clientPlayer)
    {
        if (!clientPlayer || !I::EngineClient || !I::ClientEntityList)
            return nullptr;

        const int localIndex = I::EngineClient->GetLocalPlayer();
        if (localIndex <= 0)
            return nullptr;

        IClientEntity* clientLocal = I::ClientEntityList->GetClientEntity(localIndex);
        if (!clientLocal || clientLocal != clientPlayer)
            return nullptr;

        if (I::CServerTools)
        {
            IServerEntity* serverEntity = I::CServerTools->GetIServerEntity(clientLocal);
            CBaseEntity* baseEntity = serverEntity ? serverEntity->GetBaseEntity() : nullptr;
            if (baseEntity)
                return baseEntity;
        }

        CGlobalVars* globals = I::PlayerInfoManager ? I::PlayerInfoManager->GetGlobalVars() : nullptr;
        if (!globals || !globals->pEdicts)
            return nullptr;

        edict_t* localEdict = &globals->pEdicts[localIndex];
        IServerUnknown* unknown = localEdict ? localEdict->GetUnknown() : nullptr;
        return unknown ? unknown->GetBaseEntity() : nullptr;
    }
}

bool PortalPlayerTeleport::Commit(
    C_TerrorPlayer* clientPlayer,
    const Vector& origin,
    const QAngle& angles,
    const Vector& velocity)
{
    CBaseEntity* serverPlayer = ResolveServerLocalPlayer(clientPlayer);
    if (!serverPlayer)
    {
        U::LogWarning("[PortalTeleport] rejected: server local player could not be resolved.\n");
        return false;
    }

    void** vtable = *reinterpret_cast<void***>(serverPlayer);
    if (!vtable || !vtable[kServerTeleportVTableIndex])
    {
        U::LogError("[PortalTeleport] rejected: Teleport[%u] is unavailable serverPlayer=%p vtable=%p.\n",
            static_cast<unsigned int>(kServerTeleportVTableIndex), serverPlayer, vtable);
        return false;
    }

    using FnTeleport = void(__thiscall*)(void*, const Vector*, const QAngle*, const Vector*);
    const FnTeleport teleport = reinterpret_cast<FnTeleport>(vtable[kServerTeleportVTableIndex]);
    teleport(serverPlayer, &origin, &angles, &velocity);
    return true;
}
