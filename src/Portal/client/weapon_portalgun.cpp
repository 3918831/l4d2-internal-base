#include "weapon_portalgun.h"
#include "../server/prop_portal.h"
#include "../server/portal_shareddefs.h"
#include "../../Util/Math/Math.h"

// hits solids (not grates) and passes through everything else
#define MASK_SHOT_PORTAL			(CONTENTS_SOLID|CONTENTS_MOVEABLE|CONTENTS_WINDOW|CONTENTS_MONSTER)

inline void UTIL_TraceLine(const Vector& vecAbsStart, const Vector& vecAbsEnd, unsigned int mask,
    ITraceFilter* pFilter, trace_t* ptr)
{
    Ray_t ray;
    ray.Init(vecAbsStart, vecAbsEnd);

    I::EngineTrace->TraceRay(ray, mask, pFilter, ptr);
}


float CWeaponPortalgun::TraceFirePortal(bool bPortal2, const Vector& vTraceStart, const Vector& vDirection, trace_t& tr, Vector& vFinalPosition, QAngle& qFinalAngles, int iPlacedBy, bool bTest /*= false*/)
{
    // 近处检测才用到，主要场景下的trace在UTIL_TraceLine中自行定义Ray_t，故先不定义
    /*Ray_t rayEyeArea;
    rayEyeArea.Init(vTraceStart + vDirection * 24.0f, vTraceStart + vDirection * -24.0f);*/

    // Trace to see where the portal hit
    CTraceFilter pTraceFilter;
    UTIL_TraceLine(vTraceStart, vTraceStart + (vDirection * m_fMaxRange1), MASK_SHOT_PORTAL, &pTraceFilter, &tr);

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

        return PORTAL_ANALOG_SUCCESS_PASSTHROUGH_SURFACE;
    }

}

float CWeaponPortalgun::FirePortal(bool bPortal2, Vector* pVector /*= 0*/, bool bTest /*= false*/)
{
    Vector vEye;
    Vector vDirection;
    Vector vTracerOrigin;

    // 计算主视角目前角度对应的向量坐标
    Vector forward, right, up;
    C_TerrorPlayer* pLocalPlayer = I::ClientEntityList->GetClientEntity(I::EngineClient->GetLocalPlayer())->As<C_TerrorPlayer*>();
    if (pLocalPlayer) {
        Vector eyeAngles = pLocalPlayer->EyeAngles();
        U::Math.AngleVectors(reinterpret_cast<QAngle&>(eyeAngles), &forward, &right, &up);
    }
    vDirection = forward; //源代码经过简化(不考虑一些特殊情况)后，其实就是把forward赋值给vDirection

    vEye = pPlayer->EyePosition();

    // 模拟枪的实际发射点 ：这种偏移模拟了传送门枪在玩家手中的实际发射位置
    //   - 向前30单位：表示枪身位于眼睛前方
    //   - 向右4单位：考虑了枪在右手的握持位置
    //   - 向上 - 5单位：表示枪位于眼睛下方一点的位置
    // 这个变量有点谜，实际进行trace的时候，使用的是vTraceStart而不是vTracerOrigin
    vTracerOrigin = vEye
        + forward * 30.0f
        + right * 4.0f
        + up * (-5.0f);

    if (pVector)
    {
        vDirection = *pVector;
    }
    
    Vector vTraceStart = vEye + (vDirection * m_fMinRange1);

    Vector vFinalPosition;
    QAngle qFinalAngles;

    PortalPlacedByType ePlacedBy = PORTAL_PLACED_BY_PLAYER;
    trace_t tr;

    // TODO: TraceFirePortal待补全实现
    float fPlacementSuccess = TraceFirePortal(bPortal2, vTraceStart, vDirection, tr, vFinalPosition, qFinalAngles, ePlacedBy, bTest);

    // sv_portal_placement_never_fail是传送门的一条指令，允许在允许玩家在传送门放置失败时，仍然可以放置传送门
    /*if (sv_portal_placement_never_fail.GetBool())
    {
        fPlacementSuccess = 1.0f;
    }*/

    if (!bTest)
    {
        // TODO: FindPortal待补全实现
        CProp_Portal* pPortal = CProp_Portal::FindPortal(bPortal2, true);

        // If it was a failure, put the effect at exactly where the player shot instead of where the portal bumped to
        if (fPlacementSuccess < 0.5f)
            vFinalPosition = tr.endpos;

        // TODO: PlacePortal待补全实现
        pPortal->PlacePortal(vFinalPosition, qFinalAngles, fPlacementSuccess, true);
    }

    return fPlacementSuccess;
}

void CWeaponPortalgun::FirePortal1()
{
    FirePortal(false);
}