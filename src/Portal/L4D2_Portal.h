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

class CProp_Portal; // 前置声明

struct PortalInfo_t
{
    bool bIsActive = false; // 传送门是否已激活
    Vector origin;          // 传送门在世界中的位置
    QAngle angles;          // 传送门在世界中的朝向
    Vector normal;          // 传送门法线向量
    CProp_Portal* pPortalEntity = nullptr; // 传送门实体指针

    // 平滑缩放动画相关
    Vector lastOrigin = Vector(0, 0, 0); // 上一次的位置，用于检测位置变化
    float currentScale = 1.0f;           // 当前缩放比例 (0.0f ~ 1.0f)
    bool isAnimating = false;            // 是否正在播放缩放动画
    float lastTime = 0.0f;               // 上一次的时间戳，用于计算实际帧时间
    const float SCALE_SPEED = 2.0f;      // 缩放速度（每秒增加的比例）
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

    ITexture* m_pPortalTexture_Blue;
    ITexture* m_pPortalTexture_Orange;

    IMaterial* m_pPortalMaterial_Blue;
    IMaterial* m_pPortalMaterial_Orange;

    IMaterialSystem* m_pMaterialSystem;
    Custom_IMaterialSystem* m_pCustomMaterialSystem;
    IMaterial* m_pWriteStencilMaterial;

    void PortalInit();
    void CreatePortalMaterial();
    void CreatePortalTexture();
    void PortalShutdown();

    PortalInfo_t g_BluePortal;
    PortalInfo_t g_OrangePortal;

    // 模式1和2共用的变量和函数
#if PORTAL_RENDER_MODE == 1 || PORTAL_RENDER_MODE == 2
    int screenWidth, screenHeight;
    int m_nClearFlags;
    PortalInfo_t* m_pCurrentExitPortal;
    IMaterial* m_pDynamicPortalMaterial = nullptr;

    void RenderViewToTexture(void *ecx, void *edx, const CViewSetup &mainView, PortalInfo_t *entryPortal, PortalInfo_t *exitPortal, ITexture *pTargetTex);

    // 调试函数
    void DumpTextureToDisk(ITexture *pTexture, const char *pFilename);
    void WriteBMP(const char *filename, int width, int height, unsigned char *data);

    const int MAX_PORTAL_RECURSION_DEPTH = 5; // 设置一个合理的递归上限
#endif

    // 模式1专用的变量和函数
#if PORTAL_RENDER_MODE == 1
    // 递归渲染传送门
    bool RenderPortalViewRecursive(const CViewSetup& previousView, PortalInfo_t* entryPortal, PortalInfo_t* exitPortal);

    // 视图栈
    std::vector<CViewSetup> m_vViewStack;
    // 纹理池
    std::vector<ITexture*> m_vPortalTextures;
    // 用于递归终止的材质
    IMaterial* m_pBlackoutMaterial = nullptr;
    // 全局或类成员变量，用于跟踪当前递归深度
    int m_nPortalRenderDepth = 0;
#endif

    // 模式3专用的变量和函数
#if PORTAL_RENDER_MODE == 3
    int m_nClearFlags;
    IMaterial* m_pDynamicPortalMaterial = nullptr;
    IMaterial* m_pBlackoutMaterial = nullptr;
    void BuildRenderStack(const CViewSetup &currentView, PortalInfo_t *entry, PortalInfo_t *exit, int depth);
    void RenderTextureInternal(const CViewSetup &view, PortalInfo_t *entry, PortalInfo_t *exit, ITexture *targetTex);
    void DumpTextureToDisk(ITexture *pTexture, const char *pFilename);
    void WriteBMP(const char *filename, int width, int height, unsigned char *data);

    struct PortalRenderRequest {
        CViewSetup view;          // 计算好的视角
        PortalInfo_t* entry;      // 入口
        PortalInfo_t* exit;       // 出口
        ITexture* targetTex;      // 要写入的纹理
        int depth;                // 当前深度
    };

    const int MAX_PORTAL_RECURSION_DEPTH = 5; // 设置一个合理的递归上限

    // 分离两组纹理池
    // m_vTexForBlue[i]:  存储第 i 层递归中，应贴在蓝门上的纹理（即从橙门看出的视角）
    // m_vTexForOrange[i]: 存储第 i 层递归中，应贴在橙门上的纹理（即从蓝门看出的视角）
    std::vector<ITexture*> m_vTexForBlue;
    std::vector<ITexture*> m_vTexForOrange;
    // 用于存储预计算的渲染队列
    std::vector<PortalRenderRequest> m_renderQueue;
    // 当前正在渲染哪一层（用于 DME 判断贴什么图）
    int m_nProcessingDepth = 0;
#endif

    std::unique_ptr<CWeaponPortalgun> m_pWeaponPortalgun;
};

namespace G { inline L4D2_Portal G_L4D2Portal; }