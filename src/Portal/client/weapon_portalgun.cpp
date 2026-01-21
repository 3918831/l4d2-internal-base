#include "weapon_portalgun.h"
#include "../server/prop_portal.h"
#include "../server/portal_shareddefs.h"
#include "../../Util/Math/Math.h"
#include "../../SDK/L4D2/Interfaces/CServerTools.h"

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
        printf("[CWeaponPortalgun] Hit nothing.\n");
        return;
    }
    
    int hitEntityIndex = tr.m_pEnt->entindex();
    if (hitEntityIndex != 0) {
        printf("[CWeaponPortalgun] Hit index: %d.\n", hitEntityIndex);
    } else { //为0的话就是worldspawn

        // 传送门源码在此处做了一次额外检查：
        //Vector vUp(0.0f, 0.0f, 1.0f);
        //if ((tr.plane.normal.x > -0.001f && tr.plane.normal.x < 0.001f) && (tr.plane.normal.y > -0.001f && tr.plane.normal.y < 0.001f))
        //{
        //    //plane is a level floor/ceiling
        //    vUp = vDirection;
        //}

        printf("[CWeaponPortalgun] hit worldspawn success.\n");
        U::Math.VectorAngles(tr.plane.normal, qFinalAngles);
        vFinalPosition = tr.endpos;
        vNormal = tr.plane.normal;
    }

}

void CWeaponPortalgun::FirePortal(bool bPortal2, Vector* pVector /*= 0*/, bool bTest /*= false*/)
{
    C_TerrorPlayer* pLocalPlayer = I::ClientEntityList->GetClientEntity(I::EngineClient->GetLocalPlayer())->As<C_TerrorPlayer*>();
    if (!pLocalPlayer || pLocalPlayer->deadflag()) {
        printf("[CWeaponPortalgun] pLocalPlayer is nullptr or deadflag is true.\n");
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
    //    printf("[Hook] Hit index: %d\n", index);
    //}
    //else {
    //    printf("[Hook] Hit nothing\n");
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
        CProp_Portal* pPortal = CProp_Portal::FindPortal(bPortal2, true);
        if (pPortal) {
            pPortal->SetModel(bPortal2 ? "models/blackops/portal_og.mdl" : "models/blackops/portal.mdl");
            pPortal->Teleport(&vFinalPosition, &qFinalAngles, nullptr);
            I::CServerTools->DispatchSpawn(pPortal);
            printf("[CWeaponPortalgun]: Created portal spawned.\n");
        } else {
            printf("[CWeaponPortalgun]: pPortal is nullptr.\n");
        }

        // If it was a failure, put the effect at exactly where the player shot instead of where the portal bumped to
        /*if (fPlacementSuccess < 0.5f)
            vFinalPosition = tr.endpos;*/

        // TODO: PlacePortal待补全实现
        pPortal->PlacePortal(vFinalPosition, qFinalAngles, fPlacementSuccess, false);
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