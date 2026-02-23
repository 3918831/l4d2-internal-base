#include "prop_portal.h"
#include "../../SDK/L4D2/Interfaces/CServerTools.h"
#include "../L4D2_Portal.h"
#include "../../Util/Logger/Logger.h"

CProp_Portal* CProp_Portal::FindPortal(bool bPortal2, bool bCreateIfNothingFound /*= false*/)
{
    // 【安全检查 1】检查 CServerTools 是否可用
    if (!I::CServerTools)
    {
        U::LogError("CServerTools is nullptr! Map may not be fully loaded.\n");
        return nullptr;
    }

    // 【安全检查 2】检查传送门系统是否已初始化
    if (!G::G_L4D2Portal.m_pMaterialSystem)
    {
        U::LogError("Portal system not initialized! MaterialSystem is null.\n");
        U::LogInfo("HINT: Make sure LevelInitPostEntity has been called.\n");
        return nullptr;
    }

    // 获取对应颜色的传送门信息
    PortalInfo_t& portalInfo = bPortal2 ? G::G_L4D2Portal.g_OrangePortal : G::G_L4D2Portal.g_BluePortal;

    // 如果传送门实体已存在，直接返回
    if (portalInfo.pPortalEntity != nullptr)
    {
        U::LogDebug("Reusing existing %s portal entity (ptr: %p)\n", bPortal2 ? "orange" : "blue", portalInfo.pPortalEntity);
        return portalInfo.pPortalEntity;
    }

    // 传送门不存在，需要创建新的
    if (bCreateIfNothingFound)
    {
        U::LogInfo("Creating new %s portal entity...\n", bPortal2 ? "orange" : "blue");
        CProp_Portal* pPortal = (CProp_Portal*)I::CServerTools->CreateEntityByName("prop_dynamic");
        if (pPortal)
        {
            // 保存新创建的传送门实体指针
            portalInfo.pPortalEntity = pPortal;
            U::LogInfo("Created new %s portal entity (ptr: %p)\n", bPortal2 ? "orange" : "blue", pPortal);
        }
        else
        {
            U::LogError("CreateEntityByName returned nullptr!\n");
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
    // 【安全检查】检查 this 指针
    if (!this)
    {
        U::LogError("Teleport: 'this' pointer is null!\n");
        return;
    }

    // Teleport函数偏移量118
    void** vtable = GetVTable();
    if (!vtable)
    {
        U::LogError("Teleport: VTable is null!\n");
        return;
    }

    using FnTeleport = void(__thiscall*)(void*, const Vector*, const QAngle*, const Vector*);
    FnTeleport func = reinterpret_cast<FnTeleport>(vtable[118]);

    if (!func)
    {
        U::LogError("Teleport: VTable[118] is null!\n");
        return;
    }

    func(this, newPosition, newAngles, newVelocity);
}

void CProp_Portal::SetModel(const char* pModelName)
{
    // 【安全检查】检查 this 指针
    if (!this)
    {
        U::LogError("SetModel: 'this' pointer is null!\n");
        return;
    }

    if (!pModelName)
    {
        U::LogError("SetModel: Invalid parameters(pModelName)!\n");
        return;
    }

    // SetModel函数偏移量27
    void** vtable = GetVTable();
    if (!vtable)
    {
        U::LogError("SetModel: VTable is null!\n");
        return;
    }

    using SetModelFn = void(__thiscall*)(void*, const char*);
    SetModelFn func = reinterpret_cast<SetModelFn>(vtable[27]);

    if (!func)
    {
        U::LogError("SetModel: VTable[27] is null!\n");
        return;
    }

    func(this, pModelName);
}
