#pragma once
#include "../SDK/L4D2/Interfaces/MaterialSystem.h"
//#include "../Portal/public/vector.h"

enum
{
    FRUSTUM_RIGHT = 0,
    FRUSTUM_LEFT = 1,
    FRUSTUM_TOP = 2,
    FRUSTUM_BOTTOM = 3,
    FRUSTUM_NEARZ = 4,
    FRUSTUM_FARZ = 5,
    FRUSTUM_NUMPLANES = 6
};

//inline int SignbitsForPlane(cplane_t* out)
//{
//    int	bits, j;
//
//    // for fast box on planeside test
//
//    bits = 0;
//    for (j = 0; j < 3; j++)
//    {
//        if (out->normal[j] < 0)
//            bits |= 1 << j;
//    }
//    return bits;
//}
//
//class Frustum_t
//{
//public:
//    void SetPlane(int i, int nType, const Vector& vecNormal, float dist)
//    {
//        m_Plane[i].normal = vecNormal;
//        m_Plane[i].dist = dist;
//        m_Plane[i].type = nType;
//        m_Plane[i].signbits = SignbitsForPlane(&m_Plane[i]);
//        m_AbsNormal[i].Init(fabs(vecNormal.x), fabs(vecNormal.y), fabs(vecNormal.z));
//    }
//
//    inline const cplane_t* GetPlane(int i) const { return &m_Plane[i]; }
//    inline const Vector& GetAbsNormal(int i) const { return m_AbsNormal[i]; }
//
//private:
//    cplane_t	m_Plane[FRUSTUM_NUMPLANES];
//    Vector		m_AbsNormal[FRUSTUM_NUMPLANES];
//};

// 1. 定义 cplane_t 结构体
//struct cplane_t
//{
//    Vector	normal;
//    float	dist;
//    byte	type;
//    byte	signbits;
//    byte	pad[2];
//};

// 2. 定义 Frustum_t 为一个 cplane_t 数组的类型别名
typedef cplane_t Frustum_t[FRUSTUM_NUMPLANES];


struct PortalInfo_t
{
    bool bIsActive = false; // 传送门是否已激活
    Vector origin;          // 传送门在世界中的位置
    QAngle angles;          // 传送门在世界中的朝向
};

class Custom_IMaterialSystem {
public:
    ITexture* CreateNamedRenderTargetEx(const char* pRtName, int w, int h, int sizeMode, int img_format, int depth, int textureFlags, int rtFlags)
	{
		using FN = ITexture * (__thiscall*)(Custom_IMaterialSystem*, const char*, int, int, int, int, int, int, int);
		DWORD abc = (int)(*(FN**)this)[84];
		return (*(FN**)this)[84](this, pRtName, w, h, sizeMode,img_format,depth,textureFlags,rtFlags);
	}

    void UnLockRTAllocation()
	{
		((DWORD*)this)[2734] = false;//m_bDisableRenderTargetAllocationForever
	}

    void LockRTAllocation()
    {
        ((DWORD*)this)[2734] = true;//m_bDisableRenderTargetAllocationForever
    }
};
class L4D2_Portal {
public:
    ITexture* m_pPortalTexture;
    ITexture* m_pPortalTexture_2;

    IMaterial* m_pPortalMaterial;
    IMaterial* m_pPortalMaterial_2;

    IMaterialSystem* m_pMaterialSystem;
    Custom_IMaterialSystem* m_pCustomMaterialSystem;
    IMaterial* m_pWriteStencilMaterial;

    void PortalInit();
    void CreatePortalMaterial();
    void CreatePortalTexture();
    void PortalShutdown();

    PortalInfo_t g_BluePortal;
    PortalInfo_t g_OrangePortal;

    Frustum_t m_Frustum;
};

namespace G { inline L4D2_Portal G_L4D2Portal; }