#pragma once

#include <vector>
#include "../SDK/L4D2/Interfaces/MaterialSystem.h"
#include "../Portal/client/weapon_portalgun.h"

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

    /*
     * 计算传送门虚拟摄像机的视角
     * @param playerView      - 当前玩家的原始CViewSetup
     * @param pEntrancePortal - 玩家正在“看向”的传送门 (入口)
     * @param pExitPortal     - 应该从哪个传送门“看出去” (出口)
     * @return                - 一个新的、计算好的CViewSetup，用于渲染
     */
    CViewSetup CalculatePortalView(const CViewSetup& playerView, const PortalInfo_t* pEntrancePortal, const PortalInfo_t* pExitPortal);

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

#ifdef RECURSIVE_RENDERING
    int screenWidth, screenHeight;
    int m_nClearFlags;
    PortalInfo_t* m_pCurrentExitPortal;

    // 递归渲染传送门
    // 这个核心函数现在要承担起管理栈状态的责任，在它进入递归前，将新计算出的视角入栈；在它完成工作后，必须将自己的视角出栈。
    bool RenderPortalViewRecursive(const CViewSetup& previousView, PortalInfo_t* entryPortal, PortalInfo_t* exitPortal);

    // 视图栈
    std::vector<CViewSetup> m_vViewStack;
    // 纹理池
    std::vector<ITexture*> m_vPortalTextures;
    // 对应的材质也需要动态修改
    IMaterial* m_pDynamicPortalMaterial = nullptr;

    // 新增：用于递归终止的材质
    IMaterial* m_pBlackoutMaterial = nullptr;

    // 全局或类成员变量，用于跟踪当前递归深度
    int m_nPortalRenderDepth = 0;
    const int MAX_PORTAL_RECURSION_DEPTH = 5; // 设置一个合理的递归上限

    std::unique_ptr<CWeaponPortalgun> m_pWeaponPortalgun;
#endif
};

namespace G { inline L4D2_Portal G_L4D2Portal; }