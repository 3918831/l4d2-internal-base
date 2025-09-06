#pragma once
#include "../SDK/L4D2/Interfaces/MaterialSystem.h"

class Custom_IMaterialSystem {
public:
    ITexture* CreateNamedRenderTargetEx(const char* pRtName, int w, int h, int sizeMode, int img_format, int depth, int textureFlags, int rtFlags)
	{
		using FN = ITexture * (__thiscall*)(Custom_IMaterialSystem*, const char*, int, int, int, int, int, int, int);
		DWORD abc = (int)(*(FN**)this)[85];
		return (*(FN**)this)[85](this, pRtName, w, h, sizeMode,img_format,depth,textureFlags,rtFlags);
	}
};
class L4D2_Portal {
public:
    ITexture* m_pPortalTexture;
    IMaterialSystem* m_pMaterialSystem;
    IMaterial* m_pPortalMaterial;
    Custom_IMaterialSystem* m_pCustomMaterialSystem;

    void PortalInit();
    void RenderPortalFrame();
    void CreatePortalMaterial();
    void CreatePortalTexture();
    void PortalShutdown();
};
