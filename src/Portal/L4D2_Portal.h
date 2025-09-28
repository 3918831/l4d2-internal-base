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
    IMaterialSystem* m_pMaterialSystem;
    IMaterial* m_pPortalMaterial;
    Custom_IMaterialSystem* m_pCustomMaterialSystem;
    ITexture* m_pStencilTexture; // 新增：用于模板缓冲区的纹理
    bool m_bUseStencilMask; // 新增：控制是否使用模板遮罩
    IClientRenderable* m_pRenderable; // 新增：用于渲染的可渲染对象
    ShaderStencilState_t m_stencilState;
    IMaterial* m_pWriteStencilMaterial;

    void PortalInit();
    void RenderPortalFrame();
    void CreatePortalMaterial();
    void CreatePortalTexture();
    void PortalShutdown();
    void CreateStencilTexture(); // 新增：创建模板缓冲区纹理
    void SetupStencilForModelMask(); // 新增：设置模板缓冲区用于模型遮罩
    void ApplyStencilMaskToPortal(); // 新增：应用模板遮罩到portal渲染
    // 设置模板缓冲区用于渲染特定模型
    void SetStencilForSpecificModel();
    // 查找并设置可渲染对象用于渲染特定模型
    void FindRenderableForModel();
};

namespace G { inline L4D2_Portal G_L4D2Portal; }