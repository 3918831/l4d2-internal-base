#include "prop_portal.h"
#include "../../SDK/L4D2/Interfaces/CServerTools.h"

CProp_Portal* CProp_Portal::FindPortal(bool bPortal2, bool bCreateIfNothingFound /*= false*/)
{
    if (bCreateIfNothingFound)
    {
        CProp_Portal* pPortal = (CProp_Portal*)I::CServerTools->CreateEntityByName("prop_dynamic");
        return pPortal;
    }
    return nullptr;
}

void CProp_Portal::PlacePortal(const Vector& vOrigin, const QAngle& qAngles, float fPlacementSuccess, bool bDelay /*= false*/)
{

    return;
}

void CProp_Portal::Teleport(Vector const* newPosition, QAngle const* newAngles, Vector const* newVelocity)
{
    //if (!newPosition || !newAngles)
    //{
    //    printf("Portal::Teleport: Invalid parameters(newPosition/newAngles)!\n");
    //    return;
    //}
    // Teleport虚函数索引：118
    using FnTeleport = void(__thiscall*)(void*, const Vector*, const QAngle*, const Vector*);
    FnTeleport func = reinterpret_cast<FnTeleport>(GetVTable()[118]);
    func(this, newPosition, newAngles, newVelocity);
}

void CProp_Portal::SetModel(const char* pModelName)
{
    if (!pModelName)
    {
        printf("Portal::SetModel: Invalid parameters(pModelName)!\n");
        return;
    }
    // SetModel虚函数索引：27
    using SetModelFn = void(__thiscall*)(void*, const char*);
    SetModelFn func = reinterpret_cast<SetModelFn>(GetVTable()[27]);
    func(this, pModelName);
}
