#pragma once
#include "../SDK/L4D2/Interfaces/MaterialSystem.h"
//#include "../Portal/public/vector.h"


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
};

namespace G { inline L4D2_Portal G_L4D2Portal; }