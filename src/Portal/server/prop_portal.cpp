#include "prop_portal.h"
#include "../../SDK/L4D2/Interfaces/CServerTools.h"
#include "../L4D2_Portal.h"

CProp_Portal* CProp_Portal::FindPortal(bool bPortal2, bool bCreateIfNothingFound /*= false*/)
{
    // 获取对应颜色的传送门信息
    PortalInfo_t& portalInfo = bPortal2 ? G::G_L4D2Portal.g_OrangePortal : G::G_L4D2Portal.g_BluePortal;

    // 如果传送门实体已存在，直接返回
    if (portalInfo.pPortalEntity != nullptr)
    {
        printf("[CProp_Portal] Reusing existing %s portal entity (ptr: %p)\n", bPortal2 ? "orange" : "blue", portalInfo.pPortalEntity);
        return portalInfo.pPortalEntity;
    }

    // 传送门不存在，需要创建新的
    if (bCreateIfNothingFound)
    {
        CProp_Portal* pPortal = (CProp_Portal*)I::CServerTools->CreateEntityByName("prop_dynamic");
        if (pPortal)
        {
            // 保存新创建的传送门实体指针
            portalInfo.pPortalEntity = pPortal;
            printf("[CProp_Portal] Created new %s portal entity (ptr: %p)\n", bPortal2 ? "orange" : "blue", pPortal);
        }
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
    // Teleport�麯��������118
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
    // SetModel�麯��������27
    using SetModelFn = void(__thiscall*)(void*, const char*);
    SetModelFn func = reinterpret_cast<SetModelFn>(GetVTable()[27]);
    func(this, pModelName);
}
