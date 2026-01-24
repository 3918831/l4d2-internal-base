#include "weapon_portalgun.h"
#include "../server/prop_portal.h"
#include "../server/portal_shareddefs.h"
#include "../../Util/Math/Math.h"
#include "../../SDK/L4D2/Interfaces/CServerTools.h"
#include "../L4D2_Portal.h"

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
    // ���������õ�����Ҫ�����µ�trace��UTIL_TraceLine�����ж���Ray_t�����Ȳ�����
    /*Ray_t rayEyeArea;
    rayEyeArea.Init(vTraceStart + vDirection * 24.0f, vTraceStart + vDirection * -24.0f);*/

    // Trace to see where the portal hit
    CTraceFilter pTraceFilter;
    UTIL_TraceLine(vTraceStart, vTraceEnd, MASK_SHOT, &pTraceFilter, &tr);

    if (!tr.DidHit() || tr.startsolid)
    {
        // �������ʲô��û���У���ֱ�ӷ���
        // If it didn't hit anything, fizzle
        if (!bTest)
        {
            // һ�Ѳ�֪����ɶ�ģ���ע��
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
    } else { //Ϊ0�Ļ�����worldspawn

        // ������Դ���ڴ˴�����һ�ζ����飺
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

    // �������ӽ�Ŀǰ�Ƕȶ�Ӧ����������
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

    vDirection = forward; //Դ���뾭����(������һЩ�������)����ʵ���ǰ�forward��ֵ��vDirection

    if (pVector)
    {
        vDirection = *pVector;
    }

    PortalPlacedByType ePlacedBy = PORTAL_PLACED_BY_PLAYER;
    trace_t tr;
    Vector vNormal; // TODO: �Ȱѷ����õ�, ��û���ú�����˵
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
    //    int index = pTrace.m_pEnt->entindex(); //Ϊ0�Ļ�����worldspawn
    //    printf("[Hook] Hit index: %d\n", index);
    //}
    //else {
    //    printf("[Hook] Hit nothing\n");
    //}


    

    // sv_portal_placement_never_fail�Ǵ����ŵ�һ��ָ���������������ڴ����ŷ���ʧ��ʱ����Ȼ���Է��ô�����
    /*if (sv_portal_placement_never_fail.GetBool())
    {
        fPlacementSuccess = 1.0f;
    }*/
    float fPlacementSuccess = 1.0f;

    if (!bTest)
    {
        // TODO: FindPortal����ȫʵ��
        // ���ﲹ�䴫���ŵ�״̬����������������Ѿ����������´�ֱ��TP�������ٴ���FindPortal -> CreateEntityByName����߼�
        // 获取对应颜色的传送门信息，检查是否已存在
        PortalInfo_t& portalInfo = bPortal2 ? G::G_L4D2Portal.g_OrangePortal : G::G_L4D2Portal.g_BluePortal;
        bool bAlreadyExists = (portalInfo.pPortalEntity != nullptr);

        // FindPortal 现在会复用已存在的传送门实体，或创建新的
        CProp_Portal* pPortal = CProp_Portal::FindPortal(bPortal2, true);
        if (pPortal) {
            // 只在第一次创建时设置模型和生成实体
            if (!bAlreadyExists)
            {
                pPortal->SetModel(bPortal2 ? "models/blackops/portal_og.mdl" : "models/blackops/portal.mdl");
                I::CServerTools->DispatchSpawn(pPortal);
                printf("[CWeaponPortalgun]: Created new %s portal entity.\n", bPortal2 ? "orange" : "blue");
            }
            else
            {
                printf("[CWeaponPortalgun]: Moving existing %s portal to new location.\n", bPortal2 ? "orange" : "blue");
            }

            // 总是更新传送门位置（无论新旧）
            pPortal->Teleport(&vFinalPosition, &qFinalAngles, nullptr);

            // 更新传送门信息
            portalInfo.bIsActive = true;
            portalInfo.origin = vFinalPosition;
            portalInfo.angles = qFinalAngles;
        } else {
            printf("[CWeaponPortalgun]: pPortal is nullptr.\n");
        }

        // If it was a failure, put the effect at exactly where the player shot instead of where the portal bumped to
        /*if (fPlacementSuccess < 0.5f)
            vFinalPosition = tr.endpos;*/

        // TODO: PlacePortal未完全实现
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