#pragma once
#include "../SDK/L4D2/Interfaces/MaterialSystem.h"

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

    IMaterialSystem* m_pMaterialSystem;
    Custom_IMaterialSystem* m_pCustomMaterialSystem;
    IMaterial* m_pWriteStencilMaterial;

    void PortalInit();
    void CreatePortalMaterial();
    void CreatePortalTexture();
    void PortalShutdown();
};

namespace G { inline L4D2_Portal G_L4D2Portal; }