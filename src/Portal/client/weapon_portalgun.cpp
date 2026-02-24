#include "weapon_portalgun.h"
#include "../server/prop_portal.h"
#include "../server/portal_shareddefs.h"
#include "../../Util/Math/Math.h"
#include "../../SDK/L4D2/Interfaces/CServerTools.h"
#include "../L4D2_Portal.h"
#include "../../Util/Logger/Logger.h"

// hits solids (not grates) and passes through everything else
#define MASK_SHOT_PORTAL            (CONTENTS_SOLID|CONTENTS_MOVEABLE|CONTENTS_WINDOW|CONTENTS_MONSTER)

inline void UTIL_TraceLine(const Vector& vecAbsStart, const Vector& vecAbsEnd, unsigned int mask,
    ITraceFilter* pFilter, trace_t* ptr)
{
    Ray_t ray;
    ray.Init(vecAbsStart, vecAbsEnd);

    I::EngineTrace->TraceRay(ray, mask, pFilter, ptr);
}


void CWeaponPortalgun::TraceFirePortal(bool bPortal2, const Vector& vTraceStart, const Vector& vTraceEnd, trace_t& tr, Vector& vFinalPosition, Vector& vNormal, QAngle& qFinalAngles, int iPlacedBy, bool bTest /*= false*/)
{
    // 近处检测才用到，主要场景下的trace在UTIL_TraceLine中自行定义Ray_t，故先不定义
    /*Ray_t rayEyeArea;
    rayEyeArea.Init(vTraceStart + vDirection * 24.0f, vTraceStart + vDirection * -24.0f);*/

    // Trace to see where the portal hit
    CTraceFilter pTraceFilter;
    UTIL_TraceLine(vTraceStart, vTraceEnd, MASK_SHOT, &pTraceFilter, &tr);

    if (!tr.DidHit() || tr.startsolid)
    {
        // 射线如果什么都没打中，则直接返回
        // If it didn't hit anything, fizzle
        if (!bTest)
        {
            // 一堆不知道干啥的，先注释
            /*CProp_Portal* pPortal = CProp_Portal::FindPortal(bPortal2, true);

            pPortal->m_iDelayedFailure = PORTAL_FIZZLE_NONE;
            VectorAngles(-vDirection, pPortal->m_qDelayedAngles);
            pPortal->m_vDelayedPosition = tr.endpos;

            vFinalPosition = pPortal->m_vDelayedPosition;
            qFinalAngles = pPortal->m_qDelayedAngles;*/
        }
        U::LogDebug("Hit nothing.\n");
        return;
    }

    int hitEntityIndex = tr.m_pEnt->entindex();
    if (hitEntityIndex != 0) {
        U::LogDebug("Hit index: %d.\n", hitEntityIndex);
    } else { //为0的话就是worldspawn

        // 传送门源码在此处做了一次额外检查：
        //Vector vUp(0.0f, 0.0f, 1.0f);
        //if ((tr.plane.normal.x > -0.001f && tr.plane.normal.x < 0.001f) && (tr.plane.normal.y > -0.001f && tr.plane.normal.y < 0.001f))
        //{
        //    //plane is a level floor/ceiling
        //    vUp = vDirection;
        //}

        U::LogInfo("hit worldspawn success.\n");
        U::Math.VectorAngles(tr.plane.normal, qFinalAngles);

        // 最终决定传送门创建的位置vFinalPosition加上0.5个法向量偏移，可以保证纹理在墙面的前面，避免Z-fighting问题
        vFinalPosition = tr.endpos + tr.plane.normal * 0.5f;
        vNormal = tr.plane.normal;
    }

}

void CWeaponPortalgun::FirePortal(bool bPortal2, Vector* pVector /*= 0*/, bool bTest /*= false*/)
{
    // 【安全检查 1】检查 EngineClient 是否可用
    if (!I::EngineClient)
    {
        U::LogError("I::EngineClient is nullptr!\n");
        return;
    }

    // 【安全检查 2】检查 ClientEntityList 是否可用
    if (!I::ClientEntityList)
    {
        U::LogError("I::ClientEntityList is nullptr!\n");
        return;
    }

    // 【安全检查 3】检查传送门系统是否已初始化
    if (!G::G_L4D2Portal.m_pMaterialSystem)
    {
        U::LogError("Portal system not initialized! MaterialSystem is null.\n");
        U::LogInfo("HINT: Make sure LevelInitPostEntity has been called.\n");
        return;
    }

    // 【安全检查 4】检查 CServerTools 是否可用
    if (!I::CServerTools)
    {
        U::LogError("I::CServerTools is nullptr! Map may not be fully loaded.\n");
        return;
    }

    // 【检查】如果传送门正在关闭，忽略创建请求
    PortalInfo_t& portalInfo = bPortal2 ? G::G_L4D2Portal.g_OrangePortal : G::G_L4D2Portal.g_BluePortal;
    if (portalInfo.isAnimating && portalInfo.bIsClosing)
    {
        U::LogDebug("Portal creation blocked: close animation in progress.\n");
        return;
    }

    int localPlayerIndex = I::EngineClient->GetLocalPlayer();
    if (localPlayerIndex == -1)
    {
        U::LogError("GetLocalPlayer returned -1!\n");
        return;
    }

    C_TerrorPlayer* pLocalPlayer = I::ClientEntityList->GetClientEntity(localPlayerIndex)->As<C_TerrorPlayer*>();
    if (!pLocalPlayer || pLocalPlayer->deadflag()) {
        U::LogDebug("pLocalPlayer is nullptr or deadflag is true.\n");
        return;
    }

    Vector vDirection;
    Vector vTracerOrigin;

    // 计算主视角目前角度对应的向量坐标
    Vector forward, right, up;

    Vector eyePosition = pLocalPlayer->EyePosition();

    Vector eyeAngles = pLocalPlayer->EyeAngles();
    U::Math.AngleVectors(reinterpret_cast<QAngle&>(eyeAngles), &forward, &right, &up);

    vTracerOrigin = eyePosition
        + forward * 30.0f
        + right * 4.0f
        + up * (-5.0f);
    Vector vTracerEnd;
    vTracerEnd.x = vTracerOrigin.x + forward.x * m_fMaxRange1;
    vTracerEnd.y = vTracerOrigin.y + forward.y * m_fMaxRange1;
    vTracerEnd.z = vTracerOrigin.z + forward.z * m_fMaxRange1;

    vDirection = forward; //源代码经过简化(不考虑一些特殊情况)后，其实就是把forward赋值给vDirection

    if (pVector)
    {
        vDirection = *pVector;
    }

    PortalPlacedByType ePlacedBy = PORTAL_PLACED_BY_PLAYER;
    trace_t tr;
    Vector vNormal; // TODO: 先把法线拿到, 有没有用后面再说
    QAngle qFinalAngles;
    Vector vFinalPosition;
    TraceFirePortal(bPortal2, vTracerOrigin, vTracerEnd, tr, vFinalPosition, vNormal, qFinalAngles, ePlacedBy, bTest);
    //Ray_t ray;
    //ray.Init(vTracerOrigin, vTracerEnd);
    //unsigned int fMask = MASK_SHOT | CONTENTS_GRATE;
    //CTraceFilter pTraceFilter;
    //trace_t pTrace;
    //I::EngineTrace->TraceRay(ray, fMask, &pTraceFilter, &pTrace);
    //if (pTrace.DidHit()) {
    //    int index = pTrace.m_pEnt->entindex(); //为0的话就是worldspawn
    //    U::LogDebug("[Hook] Hit index: %d\n", index);
    //}
    //else {
    //    U::LogDebug("[Hook] Hit nothing\n");
    //}


    

    // sv_portal_placement_never_fail是传送门的一条指令，允许在允许玩家在传送门放置失败时，仍然可以放置传送门
    /*if (sv_portal_placement_never_fail.GetBool())
    {
        fPlacementSuccess = 1.0f;
    }*/
    float fPlacementSuccess = 1.0f;

    if (!bTest)
    {
        // TODO: FindPortal待补全实现
        // 这里补充传送门的状态管理，如果传送门已经被创建，下次直接TP而不是再次走FindPortal -> CreateEntityByName这个逻辑
        // 获取对应颜色的传送门信息，检查是否已存在
        PortalInfo_t& portalInfo = bPortal2 ? G::G_L4D2Portal.g_OrangePortal : G::G_L4D2Portal.g_BluePortal;
        bool bAlreadyExists = (portalInfo.pPortalEntity != nullptr);

        U::LogDebug("Calling FindPortal for %s portal...\n", bPortal2 ? "orange" : "blue");

        // FindPortal 现在会复用已存在的传送门实体，或创建新的
        CProp_Portal* pPortal = CProp_Portal::FindPortal(bPortal2, true);

        // 【安全检查 5】检查 FindPortal 是否成功返回
        if (!pPortal)
        {
            U::LogError("FindPortal returned nullptr! Aborting portal creation.\n");
            return;
        }

        U::LogDebug("FindPortal succeeded, pPortal = %p\n", pPortal);

        // 只在第一次创建时设置模型和生成实体
        if (!bAlreadyExists)
        {
            U::LogDebug("First time creating portal, setting model...\n");
            const char* modelName = bPortal2 ? "models/blackops/portal_og.mdl" : "models/blackops/portal.mdl";
            pPortal->SetModel(modelName);

            // 【安全检查 6】再次检查 CServerTools（防止在 SetModel 过程中失效）
            if (!I::CServerTools)
            {
                U::LogError("I::CServerTools became nullptr after SetModel!\n");
                return;
            }

            I::CServerTools->DispatchSpawn(pPortal);
            U::LogInfo("Created new %s portal entity.\n", bPortal2 ? "orange" : "blue");
        }
        else
        {
            U::LogInfo("Moving existing %s portal to new location.\n", bPortal2 ? "orange" : "blue");
        }

        // 总是更新传送门位置（无论新旧）
        U::LogDebug("Calling Teleport to position (%.2f, %.2f, %.2f)\n",
               vFinalPosition.x, vFinalPosition.y, vFinalPosition.z);
        pPortal->Teleport(&vFinalPosition, &qFinalAngles, nullptr);

        // 更新传送门信息
        portalInfo.bIsActive = true;
        portalInfo.origin = vFinalPosition;
        portalInfo.angles = qFinalAngles;
        U::Math.AngleVectors(qFinalAngles, &portalInfo.normal, nullptr, nullptr);
        U::LogInfo("Portal %s updated successfully.\n", bPortal2 ? "orange" : "blue");

        // If it was a failure, put the effect at exactly where the player shot instead of where the portal bumped to
        /*if (fPlacementSuccess < 0.5f)
            vFinalPosition = tr.endpos;*/

        // TODO: PlacePortal未完全实现
        U::LogDebug("Calling PlacePortal...\n");
        pPortal->PlacePortal(vFinalPosition, qFinalAngles, fPlacementSuccess, false);
        U::LogDebug("FirePortal completed.\n");
    }

    /*return fPlacementSuccess;*/
}

void CWeaponPortalgun::FirePortal1()
{
    FirePortal(false);
}

void CWeaponPortalgun::FirePortal2()
{
    FirePortal(true);
}