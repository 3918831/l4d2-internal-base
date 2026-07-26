
#include <iostream>
#include "../SDK/L4D2/Includes/const.h"
#include "../SDK/L4D2/Interfaces/MaterialSystem.h"
#include "../SDK/L4D2/Interfaces/RenderView.h"
#include "../SDK/L4D2/Interfaces/MatRenderContext.h"
#include "../SDK/L4D2/Interfaces/ModelInfo.h"
#include "../SDK/L4D2/Interfaces/ModelRender.h"
#include "../SDK/L4D2/Interfaces/IInput.h"
#include "../SDK/L4D2/Interfaces/IVEngineServer.h"
//#include "../SDK/L4D2/KeyValues/KeyValues.h"
#include "../SDK/L4D2/Entities/C_BasePlayer.h"
#include "../Hooks/BaseClient/BaseClient.h"
#include "../Hooks/Hooks.h"
#include "../Util/Math/Math.h"
#include "../Util/Math/Vector/Vector4D.h"
#include "../Util/Logger/Logger.h"
#include "L4D2_Portal.h"
#include "CustomRender.h"
#include "PortalBspCollisionCarver.h"
#include "PortalBspPhase1.h"
#include "PortalTransitionDecision.h"

#include <cmath>
#include <fstream>
#include <vector>

// ITexture* g_pPortalTexture = nullptr;
//IMaterialSystem* g_pPortalMaterialSystem = nullptr;
IMaterial* g_pPortalMaterial = nullptr;
IMaterial* g_pPortalMaterial_2 = nullptr;
IMaterial* g_pPortalMaterial_3 = nullptr;

namespace
{
    const char* PortalTextureName(ITexture* texture)
    {
        return texture ? texture->GetName() : "<null>";
    }

    void LogPortalRenderState(
        const char* phase,
        int depth,
        const CViewSetup& view,
        PortalInfo_t* entryPortal,
        PortalInfo_t* exitPortal,
        ITexture* targetTexture,
        IMatRenderContext* renderContext,
        const ViewCustomVisibility_t* customVis,
        const VisibleFogVolumeInfo_t* fogInfo)
    {
        if (!G::G_L4D2Portal.m_PortalTransition.ShouldLogPortalRenderState(phase, depth))
            return;

        if (!renderContext)
            return;

        Vector toneScale = renderContext->GetToneMappingScaleLinear();

        int viewportX = 0;
        int viewportY = 0;
        int viewportW = 0;
        int viewportH = 0;
        renderContext->GetViewport(viewportX, viewportY, viewportW, viewportH);

        float fogStart = 0.0f;
        float fogEnd = 0.0f;
        float fogZ = 0.0f;
        renderContext->GetFogDistances(&fogStart, &fogEnd, &fogZ);

        unsigned char fogColor[3] = { 0, 0, 0 };
        renderContext->GetFogColor(fogColor);

        const int targetW = targetTexture ? targetTexture->GetActualWidth() : 0;
        const int targetH = targetTexture ? targetTexture->GetActualHeight() : 0;
        const int visCount = customVis ? customVis->m_nNumVisOrigins : -1;
        const int forcedLeaf = customVis ? customVis->m_iForceViewLeaf : -1;

        const Vector firstVisOrigin =
            (customVis && customVis->m_nNumVisOrigins > 0)
                ? customVis->m_rgVisOrigins[0]
                : Vector(0.0f, 0.0f, 0.0f);

        const int viewLeaf = I::EngineTrace ? I::EngineTrace->GetLeafContainingPoint(view.origin) : -1;
        const int exitLeaf = (I::EngineTrace && exitPortal) ? I::EngineTrace->GetLeafContainingPoint(exitPortal->origin) : -1;

        U::LogWarning(
            "[PortalRenderState] phase=%s depth=%d bloomTone=%d viewLeaf=%d exitLeaf=%d "
            "viewOrigin=(%.1f %.1f %.1f) viewAngles=(%.1f %.1f %.1f) "
            "entry=(%.1f %.1f %.1f) exit=(%.1f %.1f %.1f) "
            "target=%s targetSize=%dx%d currentRT=%p viewport=%d,%d,%d,%d "
            "tone=(%.3f %.3f %.3f) fogMode=%d fogDist=(%.1f %.1f %.1f) fogColor=(%u %u %u) "
            "visCount=%d forcedLeaf=%d firstVis=(%.1f %.1f %.1f) "
            "fogInfo=(vol=%d leaf=%d eye=%d dist=%.1f water=%.1f mat=%p)\n",
            phase ? phase : "<null>",
            depth,
            view.m_bDoBloomAndToneMapping ? 1 : 0,
            viewLeaf,
            exitLeaf,
            view.origin.x, view.origin.y, view.origin.z,
            view.angles.x, view.angles.y, view.angles.z,
            entryPortal ? entryPortal->origin.x : 0.0f,
            entryPortal ? entryPortal->origin.y : 0.0f,
            entryPortal ? entryPortal->origin.z : 0.0f,
            exitPortal ? exitPortal->origin.x : 0.0f,
            exitPortal ? exitPortal->origin.y : 0.0f,
            exitPortal ? exitPortal->origin.z : 0.0f,
            PortalTextureName(targetTexture),
            targetW,
            targetH,
            renderContext->GetRenderTarget(),
            viewportX, viewportY, viewportW, viewportH,
            toneScale.x, toneScale.y, toneScale.z,
            renderContext->GetFogMode(),
            fogStart, fogEnd, fogZ,
            static_cast<unsigned>(fogColor[0]),
            static_cast<unsigned>(fogColor[1]),
            static_cast<unsigned>(fogColor[2]),
            visCount,
            forcedLeaf,
            firstVisOrigin.x, firstVisOrigin.y, firstVisOrigin.z,
            fogInfo ? fogInfo->m_nVisibleFogVolume : -999,
            fogInfo ? fogInfo->m_nVisibleFogVolumeLeaf : -999,
            fogInfo ? (fogInfo->m_bEyeInFogVolume ? 1 : 0) : -1,
            fogInfo ? fogInfo->m_flDistanceToWater : 0.0f,
            fogInfo ? fogInfo->m_flWaterHeight : 0.0f,
            fogInfo ? fogInfo->m_pFogVolumeMaterial : nullptr);
    }

    void LogOfficialRemoteView(
        int renderDepth,
        const CViewSetup& sourceView,
        const CViewSetup& remoteView,
        const PortalInfo_t* entryPortal,
        const PortalInfo_t* exitPortal,
        const Vector& exitNormal,
        float clipPlaneDistance)
    {
        if (renderDepth != 1
            || !entryPortal
            || !exitPortal
            || !I::EngineClient)
        {
            return;
        }

        Vector entryNormal;
        U::Math.AngleVectors(entryPortal->angles, &entryNormal, nullptr, nullptr);
        const float entryEyeDepth =
            entryNormal.Dot(sourceView.origin - entryPortal->origin);
        if (std::fabs(entryEyeDepth) > 64.0f)
            return;

        static float nextLogTime = 0.0f;
        const float currentTime = I::EngineClient->OBSOLETE_Time();
        if (currentTime < nextLogTime)
            return;

        const float exitEyeDepth =
            exitNormal.Dot(remoteView.origin - exitPortal->origin);
        const float exitEyeClipDistance =
            exitNormal.Dot(remoteView.origin) - clipPlaneDistance;
        const char* entryName =
            entryPortal == &G::G_L4D2Portal.g_BluePortal ? "Blue" : "Orange";
        const char* exitName =
            exitPortal == &G::G_L4D2Portal.g_BluePortal ? "Blue" : "Orange";

        U::LogDebug(
            "[PortalOfficialRemoteView] entry=%s exit=%s time=%.3f "
            "entryEyeDepth=%.4f exitEyeDepth=%.4f exitEyeClipDistance=%.4f "
            "cameraNormalPush=%.3f clipOffset=-0.500 clipD=%.4f "
            "sourceOrigin=(%.2f %.2f %.2f) remoteOrigin=(%.2f %.2f %.2f) "
            "exitOrigin=(%.2f %.2f %.2f) exitNormal=(%.4f %.4f %.4f).\n",
            entryName,
            exitName,
            currentTime,
            entryEyeDepth,
            exitEyeDepth,
            exitEyeClipDistance,
            PortalTransitionDecision::ComputeOfficialPortalRemoteViewNormalPush(),
            clipPlaneDistance,
            sourceView.origin.x, sourceView.origin.y, sourceView.origin.z,
            remoteView.origin.x, remoteView.origin.y, remoteView.origin.z,
            exitPortal->origin.x, exitPortal->origin.y, exitPortal->origin.z,
            exitNormal.x, exitNormal.y, exitNormal.z);
        nextLogTime = currentTime + 0.25f;
    }
}

// g_bIsRenderingPortalTexture 已在 Hooks.h 中声明
void L4D2_Portal::CreatePortalTexture()
{
    if (!m_pCustomMaterialSystem)
    {
        U::LogError("Material system not initialized!\n");
        return;
    }

    m_pCustomMaterialSystem->UnLockRTAllocation();
    m_pMaterialSystem->BeginRenderTargetAllocation();
    m_pPortalTexture_Blue = m_pMaterialSystem->CreateNamedRenderTargetTextureEx("_rt_Portal1Texture",
        1, 1,
        RT_SIZE_FULL_FRAME_BUFFER,
        I::MaterialSystem->GetBackBufferFormat(), // or IMAGE_FORMAT_RGBA8888, 
        MATERIAL_RT_DEPTH_SHARED,
        0,//TEXTUREFLAGS_NOMIP,
       CREATERENDERTARGETFLAGS_HDR);

    m_pPortalTexture_Orange = m_pMaterialSystem->CreateNamedRenderTargetTextureEx("_rt_Portal1Texture_2",
        1, 1,
        RT_SIZE_FULL_FRAME_BUFFER,
        I::MaterialSystem->GetBackBufferFormat(), // or IMAGE_FORMAT_RGBA8888, 
        MATERIAL_RT_DEPTH_SHARED,
        0,//TEXTUREFLAGS_NOMIP,
        CREATERENDERTARGETFLAGS_HDR);

#if PORTAL_RENDER_MODE == 1
    // 模式1: 预分配纹理池
    m_vPortalTextures.reserve(MAX_PORTAL_RECURSION_DEPTH);

    for (int i = 0; i < MAX_PORTAL_RECURSION_DEPTH; ++i)
    {
        // 创建一个唯一的纹理名称
        char textureName[64];
        sprintf_s(textureName, "_portal_texture_%d", i);

        // 使用您熟悉的方式创建纹理
        // 注意：这里的参数可能需要根据您的具体需求微调
        ITexture* newTexture = I::MaterialSystem->CreateNamedRenderTargetTextureEx(
            textureName,
            1, 1,
            RT_SIZE_FULL_FRAME_BUFFER,
            I::MaterialSystem->GetBackBufferFormat(), // or IMAGE_FORMAT_RGBA8888, 
            MATERIAL_RT_DEPTH_SEPARATE,
            0,//TEXTUREFLAGS_NOMIP,
            CREATERENDERTARGETFLAGS_HDR);

        if (newTexture) {
            m_vPortalTextures.push_back(newTexture);
        } else {
            // 处理错误，例如打印日志
            U::LogError("Failed to create portal texture %d\n", i);
        }
    }
#endif

#if PORTAL_RENDER_MODE == 3
    // 模式3: 初始化蓝门纹理
    for (int i = 0; i < MAX_PORTAL_RECURSION_DEPTH; ++i)
    {
        // 创建一个唯一的纹理名称
        char textureName[64];
        sprintf_s(textureName, "_blue_portal_texture_%d", i);

        // 使用您熟悉的方式创建纹理
        // 注意：这里的参数可能需要根据您的具体需求微调
        ITexture* newTexture = I::MaterialSystem->CreateNamedRenderTargetTextureEx(
            textureName,
            1, 1,
            RT_SIZE_FULL_FRAME_BUFFER,
            I::MaterialSystem->GetBackBufferFormat(), // or IMAGE_FORMAT_RGBA8888, 
            MATERIAL_RT_DEPTH_SEPARATE,
            0,//TEXTUREFLAGS_NOMIP,
            CREATERENDERTARGETFLAGS_HDR);

        if (newTexture) {
            m_vTexForBlue.push_back(newTexture);
        } else {
            // 处理错误，例如打印日志
            U::LogError("Failed to create portal texture %d\n", i);
        }
    }

    // 初始化橙门纹理
    for (int i = 0; i < MAX_PORTAL_RECURSION_DEPTH; ++i)
    {
        // 创建一个唯一的纹理名称
        char textureName[64];
        sprintf_s(textureName, "_orange_portal_texture_%d", i);

        // 使用您熟悉的方式创建纹理
        // 注意：这里的参数可能需要根据您的具体需求微调
        ITexture* newTexture = I::MaterialSystem->CreateNamedRenderTargetTextureEx(
            textureName,
            1, 1,
            RT_SIZE_FULL_FRAME_BUFFER,
            I::MaterialSystem->GetBackBufferFormat(), // or IMAGE_FORMAT_RGBA8888, 
            MATERIAL_RT_DEPTH_SEPARATE,
            0,//TEXTUREFLAGS_NOMIP,
            CREATERENDERTARGETFLAGS_HDR);

        if (newTexture) {
            m_vTexForOrange.push_back(newTexture);
        } else {
            // 处理错误，例如打印日志
            U::LogError("Failed to create portal texture %d\n", i);
        }
    }
#endif

    m_pMaterialSystem->EndRenderTargetAllocation();

    if (!m_pPortalTexture_Blue || !m_pPortalTexture_Orange)
    {
        U::LogError("Failed to create portal texture!\n");
        return;
    } else {
        U::LogInfo("m_pPortalTexture_Blue Name: %s\n", m_pPortalTexture_Blue->GetName());
    }

    U::LogInfo("Created portal texture successfully\n");
}

void L4D2_Portal::CreatePortalMaterial()
{
    if (!m_pMaterialSystem)
    {
        U::LogError("Material system not initialized!\n");
        return;
    }

    // 使用FindMaterial查找游戏内置材质
    g_pPortalMaterial = m_pMaterialSystem->FindMaterial("models/zimu/zimu1_hd/zimu1_hd", TEXTURE_GROUP_MODEL, true, nullptr);
    g_pPortalMaterial_2 = m_pMaterialSystem->FindMaterial("models/zimu/zimu2_hd/zimu2_hd", TEXTURE_GROUP_MODEL, true, nullptr);
    g_pPortalMaterial_3 = m_pMaterialSystem->FindMaterial("dev/write_stencil", TEXTURE_GROUP_OTHER);
#if PORTAL_RENDER_MODE == 1 || PORTAL_RENDER_MODE == 2 || PORTAL_RENDER_MODE == 3
    m_pDynamicPortalMaterial = m_pMaterialSystem->FindMaterial("dev/portal_content", TEXTURE_GROUP_OTHER);

    if (!m_pDynamicPortalMaterial) {
        U::LogError("Failed to find dynamic portal material!\n");
        return;
    }
#endif
#if PORTAL_RENDER_MODE == 1 || PORTAL_RENDER_MODE == 3
    m_pBlackoutMaterial = m_pMaterialSystem->FindMaterial("dev/portal_blackout", TEXTURE_GROUP_OTHER);

    if (!m_pBlackoutMaterial) {
        U::LogError("Failed to find blackout material!\n");
        return;
    }
#endif
#if PORTAL_RENDER_MODE == 1
    m_pWriteStencilMaterial = m_pMaterialSystem->FindMaterial("dev/write_stencil", TEXTURE_GROUP_OTHER);

    if (!m_pWriteStencilMaterial) {
        U::LogError("Failed to find write_stencil material!\n");
        return;
    }
#endif

    if (!g_pPortalMaterial || !g_pPortalMaterial_2 || !g_pPortalMaterial_3)
    {
        U::LogError("Failed to find portal material!\n");
        return;
    }

    U::LogInfo("Found portal material successfully\n");

    // 设置材质为可绘制状态
    g_pPortalMaterial->IncrementReferenceCount();
    g_pPortalMaterial_2->IncrementReferenceCount();
    g_pPortalMaterial_3->IncrementReferenceCount();
#if PORTAL_RENDER_MODE == 1 || PORTAL_RENDER_MODE == 2 || PORTAL_RENDER_MODE == 3
    m_pDynamicPortalMaterial->IncrementReferenceCount();
#endif
#if PORTAL_RENDER_MODE == 1 || PORTAL_RENDER_MODE == 3
    m_pBlackoutMaterial->IncrementReferenceCount();
#endif
#if PORTAL_RENDER_MODE == 1
    m_pWriteStencilMaterial->IncrementReferenceCount();
#endif
    // 查找并设置基础纹理参数
    IMaterialVar* pBaseTextureVar = g_pPortalMaterial->FindVar("$basetexture", NULL, false);
    IMaterialVar* pBaseTextureVar_2 = g_pPortalMaterial_2->FindVar("$basetexture", NULL, false);
    if (pBaseTextureVar && pBaseTextureVar_2)
    {
        if (m_pPortalTexture_Blue) {
            pBaseTextureVar->SetTextureValue(m_pPortalTexture_Blue);
            pBaseTextureVar_2->SetTextureValue(m_pPortalTexture_Orange);
            U::LogInfo("Set base texture to portal render target\n");

            //IMaterialVar* pBaseTextureVarofDynamic = m_pDynamicPortalMaterial->FindVar("$basetexture", NULL, false);
            //if (pBaseTextureVarofDynamic) {
            //    pBaseTextureVarofDynamic->SetTextureValue(pBaseTextureVar->GetTextureValue());
            //}
        } else {
            U::LogError("Failed to set base texture because portal texture is null\n");
        }
    }
    else
    {
        // 尝试查找diffusemap作为替代
        pBaseTextureVar = g_pPortalMaterial->FindVar("$diffusemap", NULL, false);
        pBaseTextureVar_2 = g_pPortalMaterial_2->FindVar("$diffusemap", NULL, false);
        if (pBaseTextureVar && pBaseTextureVar_2)
        {
            pBaseTextureVar->SetTextureValue(m_pPortalTexture_Blue);
            pBaseTextureVar_2->SetTextureValue(m_pPortalTexture_Orange);
            U::LogInfo("Set diffuse map to portal render target\n");
        }
        else
        {
            U::LogError("Failed to find texture variables\n");
        }
    }

    // 设置材质为半透明
    // IMaterialVar* pTranslucentVar = g_pPortalMaterial->FindVar("$translucent", NULL, false);
    // if (pTranslucentVar)
    // {
    //     pTranslucentVar->SetIntValue(1);
    // }

    // 禁用Z缓冲区测试，允许透过其他物体看到传送门内容
    // IMaterialVar* pIgnoreZVar = g_pPortalMaterial->FindVar("$ignorez", NULL, false);
    // if (pIgnoreZVar)
    // {
    //     pIgnoreZVar->SetIntValue(1);
    // }
}

void L4D2_Portal::PortalInit()
{
    // 获取材质系统接口
    m_pMaterialSystem = I::MaterialSystem;
    m_pCustomMaterialSystem = reinterpret_cast<Custom_IMaterialSystem*>(m_pMaterialSystem);

    if (!m_pMaterialSystem)
    {
        U::LogError("Failed to get MaterialSystem interface!\n");
        return;
    }

    U::LogDebug("I::MaterialSystem: 0x%08X\n", (INT32)(I::MaterialSystem));
    U::LogDebug("m_pMaterialSystem: 0x%08X\n", (INT32)m_pMaterialSystem);
    U::LogDebug("m_pCustomMaterialSystem: 0x%08X\n", (INT32)m_pCustomMaterialSystem);

    U::LogInfo("Got MaterialSystem interface success\n");

    // 创建或查找材质
    CreatePortalTexture();

    // 创建测试用材质,本质上是查找zimu的材质,实际业务暂时用不到
    CreatePortalMaterial();

    // 初始化完成后，可以调用RenderPortalFrame进行渲染
    U::LogInfo("Initialization completed\n\n");

    m_pPortalMaterial_Blue = g_pPortalMaterial;
    m_pPortalMaterial_Orange = g_pPortalMaterial_2;

    U::LogDebug("g_pPortalMaterial: %p\n", g_pPortalMaterial);
    U::LogDebug("g_pPortalMaterial_2: %p\n", g_pPortalMaterial_2);
    U::LogDebug("m_pPortalTexture_Blue: %p\n", m_pPortalTexture_Blue);
    U::LogDebug("m_pMaterialSystem: %p\n", m_pMaterialSystem);
    U::LogDebug("m_pPortalMaterial_Blue: %p\n", m_pPortalMaterial_Blue);
    U::LogDebug("m_pCustomMaterialSystem: %p\n", m_pCustomMaterialSystem);

#if PORTAL_RENDER_MODE == 1 || PORTAL_RENDER_MODE == 2
    m_nClearFlags = 0;
    I::EngineClient->GetScreenSize(screenWidth, screenHeight);
#elif PORTAL_RENDER_MODE == 3
    m_nClearFlags = 0;
#endif
    m_pWeaponPortalgun = std::make_unique<CWeaponPortalgun>();
    m_PortalTransition.Reset();
    m_PortalTransitionSimulator.Reset();
    m_PortalCollisionBridge.Reset();
    m_PortalStage1Probe.Reset();
    U::PortalFileLog::Reset();
    U::LogWarning("[PortalFileLog] writing focused traversal diagnostics to game-root portal_l4d2_traversal.log.\n");
    U::LogWarning("[PortalGModTraversal] focused log capture is ready for local-player traversal work.\n");
    U::LogWarning("[PortalStage1Probe] installed. Use console command portal_stage1_probe for an immediate stage-1 interface dump.\n");
}

// 清理函数，在不需要传送门时调用
void L4D2_Portal::PortalShutdown()
{
    const bool bspRestored = PortalBspPhase1::RestoreAndClearBindings("PortalShutdown");
    U::LogInfo("[PortalBsp][BindingLifecycle] event=PortalShutdown restored=%s bindingsCleared=%s destructiveWrites=%s.\n",
        bspRestored ? "true" : "false",
        bspRestored ? "true" : "false",
        G::PortalBspCollisionCarver.IsCarvingActive() ? "true" : "false");
    m_PortalTransition.Reset();
    m_PortalTransitionSimulator.Reset();
    m_PortalCollisionBridge.Reset();
    m_PortalStage1Probe.Reset();

    // 1. 释放全局材质引用
    if (g_pPortalMaterial)
    {
        g_pPortalMaterial->DecrementReferenceCount();
        g_pPortalMaterial = nullptr;
    }

    if (g_pPortalMaterial_2)
    {
        g_pPortalMaterial_2->DecrementReferenceCount();
        g_pPortalMaterial_2 = nullptr;
    }

    if (g_pPortalMaterial_3)
    {
        g_pPortalMaterial_3->DecrementReferenceCount();
        g_pPortalMaterial_3 = nullptr;
    }

    // 2. 释放类成员材质引用
    if (m_pPortalMaterial_Blue)
    {
        m_pPortalMaterial_Blue->DecrementReferenceCount();
        m_pPortalMaterial_Blue = nullptr;
    }

    if (m_pPortalMaterial_Orange)
    {
        m_pPortalMaterial_Orange->DecrementReferenceCount();
        m_pPortalMaterial_Orange = nullptr;
    }

#if PORTAL_RENDER_MODE == 1 || PORTAL_RENDER_MODE == 2 || PORTAL_RENDER_MODE == 3
    // 释放动态传送门材质
    if (m_pDynamicPortalMaterial) {
        m_pDynamicPortalMaterial->DecrementReferenceCount();
        m_pDynamicPortalMaterial = nullptr;
    }
#endif

#if PORTAL_RENDER_MODE == 1 || PORTAL_RENDER_MODE == 3
    // 释放 blackout 材质
    if (m_pBlackoutMaterial) {
        m_pBlackoutMaterial->DecrementReferenceCount();
        m_pBlackoutMaterial = nullptr;
    }
#endif

#if PORTAL_RENDER_MODE == 1
    // 释放模式1专用材质
    if (m_pWriteStencilMaterial) {
        m_pWriteStencilMaterial->DecrementReferenceCount();
        m_pWriteStencilMaterial = nullptr;
    }

    // 清理纹理池
    if (!m_vPortalTextures.empty())
    {
        for (ITexture* pTexture : m_vPortalTextures)
        {
            // 纹理由材质系统管理，置空即可
            if (pTexture)
            {
                pTexture = nullptr;
            }
        }
        m_vPortalTextures.clear();
    }
    // 清理视图栈
    if (!m_vViewStack.empty())
    {
        m_vViewStack.clear();
    }
#endif

#if PORTAL_RENDER_MODE == 3
    if (!m_vTexForBlue.empty())
    {
        for (ITexture* pTexture : m_vTexForBlue)
        {
            // 纹理由材质系统管理，置空即可
            if (pTexture)
            {
                pTexture = nullptr;
            }
        }
        m_vTexForBlue.clear();
    }

    if (!m_vTexForOrange.empty())
    {
        for (ITexture* pTexture : m_vTexForOrange)
        {
            // 纹理由材质系统管理，置空即可
            if (pTexture)
            {
                pTexture = nullptr;
            }
        }
        m_vTexForOrange.clear();
    }
#endif

    // 释放传送门纹理
    if (m_pPortalTexture_Blue)
    {
        // 纹理由材质系统管理，置空即可
        m_pPortalTexture_Blue = nullptr;
    }

    if (m_pPortalTexture_Orange)
    {
        m_pPortalTexture_Orange = nullptr;
    }

    // 7. 清理传送门实体指针
    // 注意：实体由游戏引擎管理，我们只需要清空指针，不需要手动删除
    if (g_BluePortal.pPortalEntity)
    {
        g_BluePortal.pPortalEntity = nullptr;
        g_BluePortal.bIsActive = false;
    }

    if (g_OrangePortal.pPortalEntity)
    {
        g_OrangePortal.pPortalEntity = nullptr;
        g_OrangePortal.bIsActive = false;
    }

    // 8. 清理材质系统接口（不需要释放，只是清空指针）
    m_pMaterialSystem = nullptr;
    m_pCustomMaterialSystem = nullptr;

    U::LogInfo("Shutdown completed\n");
}

/*
 * 计算传送门虚拟摄像机的视角
 * @param playerView      - 当前玩家的原始CViewSetup
 * @param pEntrancePortal - 玩家正在“看向”的传送门 (入口)
 * @param pExitPortal     - 应该从哪个传送门“看出去” (出口)
 * @return                - 一个新的、计算好的CViewSetup，用于渲染
 */
CViewSetup L4D2_Portal::CalculatePortalView(const CViewSetup& playerView, const PortalInfo_t* pEntrancePortal, const PortalInfo_t* pExitPortal)
{
    // 1. 预计算“入口到出口”的变换矩阵
    matrix3x4_t entranceMatrix, exitMatrix, entranceMatrixInverse, thisToLinkedMatrix;
    matrix3x4_t mat_180_Z_Rot;
    U::Math.SetIdentityMatrix(mat_180_Z_Rot);
    mat_180_Z_Rot[0][0] = -1.0f;
    mat_180_Z_Rot[1][1] = -1.0f;

    U::Math.AngleMatrix(pEntrancePortal->angles, pEntrancePortal->origin, entranceMatrix);
    U::Math.AngleMatrix(pExitPortal->angles, pExitPortal->origin, exitMatrix);

    U::Math.MatrixInverse(entranceMatrix, entranceMatrixInverse);

    matrix3x4_t tempMatrix;
    U::Math.ConcatTransforms(mat_180_Z_Rot, entranceMatrixInverse, tempMatrix);
    U::Math.ConcatTransforms(exitMatrix, tempMatrix, thisToLinkedMatrix);

    // 2. 直接复制 playerView，以此为基础进行修改
    CViewSetup portalView = playerView;

    // 3. 变换 origin
    U::Math.VectorTransform(playerView.origin, thisToLinkedMatrix, portalView.origin);

    // 4. 变换 angles
    matrix3x4_t playerAnglesMatrix, newAnglesMatrix;
    U::Math.AngleMatrix(playerView.angles, playerAnglesMatrix);
    U::Math.ConcatTransforms(thisToLinkedMatrix, playerAnglesMatrix, newAnglesMatrix);
    U::Math.MatrixAngles(newAnglesMatrix, portalView.angles);

    // 5. 确保 zNear 不小于引擎允许的最小值
    if (portalView.zNear < 1.0f) {
        portalView.zNear = 1.0f;
    }

    // Match Portal SDK: keep the remote camera at the exact transformed eye.
    // Visibility recovery is supplied separately through safe PVS origins.
    Vector exitPortalNormal;
    U::Math.AngleVectors(pExitPortal->angles, &exitPortalNormal, nullptr, nullptr);
    portalView.origin += exitPortalNormal
        * PortalTransitionDecision::ComputeOfficialPortalRemoteViewNormalPush();
    return portalView;
}

#if PORTAL_RENDER_MODE == 1
bool L4D2_Portal::RenderPortalViewRecursive(const CViewSetup& previousView, PortalInfo_t* entryPortal, PortalInfo_t* exitPortal)
{
    // 1. 递归深度检查, 检查递归深度是否超限
    if (m_nPortalRenderDepth >= MAX_PORTAL_RECURSION_DEPTH) {
        // 已经到了最深处，我们不再继续渲染世界，
        // 而是将这一层的纹理清空为一个指定的颜色。
        // 由于我们已经没有更深的纹理可用，这里我们“假装”已经渲染了最深层。
        // 但我们实际上什么都不做，因为没有纹理可以写入。
        // 调用者 DrawModelExecute 将会使用一个未被渲染的纹理，
        // 这个问题我们将在 DrawModelExecute 中处理。
        return false;
    }

    IMatRenderContext* pRenderContext = G::G_L4D2Portal.m_pMaterialSystem->GetRenderContext();
    if (!pRenderContext) return false;

    // 2. 状态更新
    // --- 核心逻辑：入栈 ---
    m_nPortalRenderDepth++;
    m_pCurrentExitPortal = exitPortal;

    // 3. 计算透视正确的渲染视角 (Render View)
    // 位于墙后，保持透视正确
    CViewSetup newPortalView = CalculatePortalView(previousView, entryPortal, exitPortal);
    //newPortalView.m_bDoBloomAndToneMapping = false;
    m_vViewStack.push_back(newPortalView);

    // 4. 【核心解决丢模型】构造 ViewCustomVisibility_t
    // 计算位于墙前的安全可见性坐标 (Vis Origin)
    ViewCustomVisibility_t customVis;
    Vector exitNormal;
    U::Math.AngleVectors(exitPortal->angles, &exitNormal, nullptr, nullptr);
    
    // 策略优化：多点采样 + 有效性检查
    // 我们生成 3 个探测点：中心，中心前移 20，中心前移 50
    // 只要其中有一个在有效 Leaf 里，渲染就稳了
    Vector testPoints[] = {
        exitPortal->origin + (exitNormal * 1.0f),  // 紧贴门面
        exitPortal->origin + (exitNormal * 25.0f), // 稍微靠前
        exitPortal->origin + (exitNormal * 50.0f)  // 明显靠前
    };

    int validLeafIndex = -1;

    for (const auto& pt : testPoints) {
        // 模仿官方：先检查点是否有效
        int leafID = I::EngineTrace->GetLeafContainingPoint(pt);
        if (leafID != -1 && leafID != 0) { // 0 通常是 Solid        
            customVis.AddVisOrigin(pt);            
            // 记录第一个有效的 Leaf ID 用于强制指定
            if (validLeafIndex == -1) validLeafIndex = leafID;
            // I::DebugOverlay->AddBoxOverlay(pt, Vector(-2,-2,-2), Vector(2,2,2), Vector(0,0,0), 0, 255, 0, 100, 0.1f);
        } else {
            // [调试] 无效点 (在墙里)，画个红框
            // I::DebugOverlay->AddBoxOverlay(pt, Vector(-2,-2,-2), Vector(2,2,2), Vector(0,0,0), 255, 0, 0, 100, 0.1f);
        }
    }

    // 模仿官方：强制指定 View Leaf (如果找到了有效 Leaf)
    if (validLeafIndex != -1) {
        customVis.ForceViewLeaf(validLeafIndex);
    }



    // // 生成探测点：中心 + 前移 50 单位 (确保在室外空气中)
    // Vector safeOrigin = exitPortal->origin + (exitNormal * 50.0f);
    // // 添加探测点
    // customVis.AddVisOrigin(safeOrigin);
    // // 双重保险：添加传送门表面点
    // customVis.AddVisOrigin(exitPortal->origin + (exitNormal * 1.0f));
    // 5. 准备渲染目标
    // 从池中获取当前深度的渲染目标纹理
    // 此时 m_nPortalRenderDepth 最小为 1, 所以索引是安全的 [0]
    // 从池中获取当前深度的渲染目标纹理
    ITexture* pRenderTarget = m_vPortalTextures[m_nPortalRenderDepth - 1];

    // 6. 临时切换第三人称 (为了在传送门里看到自己)
    bool* pCameraInThirdPerson = I::IInput->m_fCameraInThirdPerson();
    bool bOriginalThirdPerson = *pCameraInThirdPerson;
    *pCameraInThirdPerson = true;

    // 7. 设置渲染上下文
    LogPortalRenderState("recursive-before-push-rt", m_nPortalRenderDepth, newPortalView, entryPortal, exitPortal, pRenderTarget, pRenderContext, &customVis, nullptr);
    pRenderContext->PushRenderTargetAndViewport();
    pRenderContext->SetRenderTarget(pRenderTarget);
    pRenderContext->Viewport(0, 0, pRenderTarget->GetActualWidth(), pRenderTarget->GetActualHeight());
    pRenderContext->ClearColor4ub(0, 0, 0, 255);
    pRenderContext->ClearBuffers(true, true, true);
    LogPortalRenderState("recursive-after-push-rt", m_nPortalRenderDepth, newPortalView, entryPortal, exitPortal, pRenderTarget, pRenderContext, &customVis, nullptr);

    // 8. 设置剪裁平面 (Clip Plane) - 解决物理遮挡
    float clipPlane[4];
    clipPlane[0] = exitNormal.x;
    clipPlane[1] = exitNormal.y;
    clipPlane[2] = exitNormal.z;

    // Portal SDK moves the remote clip plane half a unit behind the exit
    // portal so the carrying wall is clipped without losing half-in objects.
    clipPlane[3] =
        PortalTransitionDecision::ComputeOfficialPortalRemoteClipPlaneDistance(
            exitNormal.Dot(exitPortal->origin));
    LogOfficialRemoteView(
        m_nPortalRenderDepth,
        previousView,
        newPortalView,
        entryPortal,
        exitPortal,
        exitNormal,
        clipPlane[3]);

    pRenderContext->PushCustomClipPlane(clipPlane);
    pRenderContext->EnableClipping(true);

    // 9. 执行渲染
    // Push 真实视角 (墙后)
    I::CustomRender->Push3DView(newPortalView, 0, pRenderTarget, I::CustomView->GetFrustum(), nullptr);
    
    // 计算环境光 (使用最稳的那个有效点，如果没有就用原点)
    Vector fogOrigin = (customVis.m_nNumVisOrigins > 0) ? customVis.m_rgVisOrigins[0] : exitPortal->origin;
    VisibleFogVolumeInfo_t fog_1;
    I::CustomRender->GetVisibleFogVolumeInfo(fogOrigin, fog_1);
    WaterRenderInfo_t water_1;
    I::CustomView->DetermineWaterRenderInfo(&fog_1, &water_1);
    LogPortalRenderState("recursive-before-draw", m_nPortalRenderDepth, newPortalView, entryPortal, exitPortal, pRenderTarget, pRenderContext, &customVis, &fog_1);

    // 【关键调用】传入 &customVis 解决丢模型
    // 这一步会递归触发 DrawModelExecute，从而渲染更深层的传送门
    I::CustomView->DrawWorldAndEntities(true, newPortalView, m_nClearFlags, &fog_1, &water_1, &customVis);
    LogPortalRenderState("recursive-after-draw", m_nPortalRenderDepth, newPortalView, entryPortal, exitPortal, pRenderTarget, pRenderContext, &customVis, &fog_1);
    
    // 10. 恢复与清理
    I::CustomRender->PopView(I::CustomView->GetFrustum());
    pRenderContext->EnableClipping(false);
    pRenderContext->PopCustomClipPlane();
    LogPortalRenderState("recursive-before-pop-rt", m_nPortalRenderDepth, newPortalView, entryPortal, exitPortal, pRenderTarget, pRenderContext, &customVis, &fog_1);
    pRenderContext->PopRenderTargetAndViewport();
    LogPortalRenderState("recursive-after-pop-rt", m_nPortalRenderDepth, newPortalView, entryPortal, exitPortal, pRenderTarget, pRenderContext, &customVis, &fog_1);
    *pCameraInThirdPerson = bOriginalThirdPerson;        
    m_vViewStack.pop_back();
    m_nPortalRenderDepth--;
    return true;
}
#endif

#if PORTAL_RENDER_MODE == 1 || PORTAL_RENDER_MODE == 2
void L4D2_Portal::RenderViewToTexture(void* ecx, void* edx, const CViewSetup& mainView, PortalInfo_t* entryPortal, PortalInfo_t* exitPortal, ITexture* pTargetTex)
{
    if (!pTargetTex) return;
    IMatRenderContext* pRenderContext = m_pMaterialSystem->GetRenderContext();
    if (!pRenderContext) return;

    // 1. 计算透视正确的视角 (Render View) - 保持在墙后
    CViewSetup portalView = CalculatePortalView(mainView, entryPortal, exitPortal);

    // 2. 构造官方风格的可见性数据 (Custom Visibility)
    ViewCustomVisibility_t customVis;
    
    // 获取出口法线
    Vector exitNormal;
    U::Math.AngleVectors(exitPortal->angles, &exitNormal, nullptr, nullptr);

    // 策略优化：多点采样 + 有效性检查
    // 我们生成 3 个探测点：中心，中心前移 20，中心前移 50
    // 只要其中有一个在有效 Leaf 里，渲染就稳了
    Vector testPoints[] = {
        exitPortal->origin + (exitNormal * 1.0f),  // 紧贴门面
        exitPortal->origin + (exitNormal * 25.0f), // 稍微靠前
        exitPortal->origin + (exitNormal * 50.0f)  // 明显靠前
    };

    int validLeafIndex = -1;

    for (const auto& pt : testPoints) {
        // 模仿官方：先检查点是否有效
        int leafID = I::EngineTrace->GetLeafContainingPoint(pt);
        if (leafID != -1 && leafID != 0) { // 0 通常是 Solid        
            customVis.AddVisOrigin(pt);            
            // 记录第一个有效的 Leaf ID 用于强制指定
            if (validLeafIndex == -1) validLeafIndex = leafID;
            // I::DebugOverlay->AddBoxOverlay(pt, Vector(-2,-2,-2), Vector(2,2,2), Vector(0,0,0), 0, 255, 0, 100, 0.1f);
        } else {
            // [调试] 无效点 (在墙里)，画个红框
            // I::DebugOverlay->AddBoxOverlay(pt, Vector(-2,-2,-2), Vector(2,2,2), Vector(0,0,0), 255, 0, 0, 100, 0.1f);
        }
    }

    // 模仿官方：强制指定 View Leaf (如果找到了有效 Leaf)
    if (validLeafIndex != -1) {
        customVis.ForceViewLeaf(validLeafIndex);
    }

    // 3. 准备渲染环境
    LogPortalRenderState("to-texture-before-push-rt", m_nPortalRenderDepth, portalView, entryPortal, exitPortal, pTargetTex, pRenderContext, &customVis, nullptr);
    pRenderContext->PushRenderTargetAndViewport();
    pRenderContext->SetRenderTarget(pTargetTex);
    pRenderContext->Viewport(0, 0, pTargetTex->GetActualWidth(), pTargetTex->GetActualHeight());    
    pRenderContext->ClearColor4ub(0, 0, 0, 255);
    pRenderContext->ClearBuffers(true, true, true);
    LogPortalRenderState("to-texture-after-push-rt", m_nPortalRenderDepth, portalView, entryPortal, exitPortal, pTargetTex, pRenderContext, &customVis, nullptr);

    // 4. 设置剪裁平面 (Clip Plane) - 模仿官方处理遮挡
    // 官方代码：m_vForward.Dot( origin - forward * 0.5f )
    float clipPlane[4];
    clipPlane[0] = exitNormal.x; 
    clipPlane[1] = exitNormal.y; 
    clipPlane[2] = exitNormal.z;
    clipPlane[3] =
        PortalTransitionDecision::ComputeOfficialPortalRemoteClipPlaneDistance(
            exitNormal.Dot(exitPortal->origin));

    pRenderContext->PushCustomClipPlane(clipPlane);
    pRenderContext->EnableClipping(true);

    // 5. 渲染
    // Push 真实视角 (墙后) 以保持透视
    I::CustomRender->Push3DView(portalView, 0, pTargetTex, I::CustomView->GetFrustum(), nullptr);
    
    // 计算环境光 (使用最稳的那个有效点，如果没有就用原点)
    Vector fogOrigin = (customVis.m_nNumVisOrigins > 0) ? customVis.m_rgVisOrigins[0] : exitPortal->origin;
    VisibleFogVolumeInfo_t fog_1;
    I::CustomRender->GetVisibleFogVolumeInfo(fogOrigin, fog_1); 
    WaterRenderInfo_t water_1;
    I::CustomView->DetermineWaterRenderInfo(&fog_1, &water_1);
    LogPortalRenderState("to-texture-before-draw", m_nPortalRenderDepth, portalView, entryPortal, exitPortal, pTargetTex, pRenderContext, &customVis, &fog_1);

    // if (pTargetTex == m_pPortalTexture_Blue) {
    //     I::ModelRender->ForcedMaterialOverride(m_pPortalMaterial_Blue);
    // } else {
    //     I::ModelRender->ForcedMaterialOverride(m_pPortalMaterial_Orange);
    // }
    
    // 【核心】传入 customVis
    I::CustomView->DrawWorldAndEntities(true, portalView, m_nClearFlags, &fog_1, &water_1, &customVis);
    LogPortalRenderState("to-texture-after-draw", m_nPortalRenderDepth, portalView, entryPortal, exitPortal, pTargetTex, pRenderContext, &customVis, &fog_1);
    
    // I::ModelRender->ForcedMaterialOverride(nullptr);

    // 6. 恢复
    I::CustomRender->PopView(I::CustomView->GetFrustum());
    pRenderContext->EnableClipping(false);
    pRenderContext->PopCustomClipPlane();
    LogPortalRenderState("to-texture-before-pop-rt", m_nPortalRenderDepth, portalView, entryPortal, exitPortal, pTargetTex, pRenderContext, &customVis, &fog_1);
    pRenderContext->PopRenderTargetAndViewport();
    LogPortalRenderState("to-texture-after-pop-rt", m_nPortalRenderDepth, portalView, entryPortal, exitPortal, pTargetTex, pRenderContext, &customVis, &fog_1);
}
#endif

#if PORTAL_RENDER_MODE == 3
void L4D2_Portal::BuildRenderStack(const CViewSetup& currentView, PortalInfo_t* entry, PortalInfo_t* exit, int depth)
{
    if (depth >= MAX_PORTAL_RECURSION_DEPTH) return;

    // 1. 计算下一层的视角
    CViewSetup nextView = CalculatePortalView(currentView, entry, exit);

    // 2. 确定目标纹理
    ITexture* pTargetTexture = nullptr;

    // 逻辑推导：
    // 如果 entry 是蓝门 (我们正往蓝门里看)，那么下一层的视角是从橙门(exit)发出的。
    // 这个视角产生的画面，最终应该贴在【蓝门】上。
    if (entry == &G::G_L4D2Portal.g_BluePortal) {
        pTargetTexture = m_vTexForBlue[depth];
    } else if (entry == &G::G_L4D2Portal.g_OrangePortal) {
        pTargetTexture = m_vTexForOrange[depth];
    } else {
        U::LogWarning("BuildRenderStack: entry is not BluePortal or OrangePortal\n");
    }
    
    // 2. 简单的视锥体剔除检查 (可选优化)
    // 检查 exit 门是否在 currentView 的视野内，如果看不见就不需要继续递归了
    // 这里先省略，假设都能看见

    // 3. 记录请求 (注意：纹理索引对应 depth)
    PortalRenderRequest req;
    req.view = nextView;
    req.entry = exit; // 下一层的入口变成了当前的出口
    req.exit = entry; // 下一层的出口变成了当前的入口
    req.targetTex = pTargetTexture;
    req.depth = depth + 1; // 深度 1 开始

    m_renderQueue.push_back(req);

    // 4. 继续递归
    // 注意：这里需要分叉递归，但在简单的“无限长廊”场景中（门对门），
    // 我们只需要继续沿着当前的 exit -> entry 路径递归即可。
    BuildRenderStack(nextView, exit, entry, depth + 1);
}
#endif

// RenderPortalViewRecursive 里的渲染代码剥离出来，去掉递归调用，只负责画一帧。
#if PORTAL_RENDER_MODE == 3
void L4D2_Portal::RenderTextureInternal(const CViewSetup& mainView, PortalInfo_t* entryPortal, PortalInfo_t* exitPortal, ITexture* pTargetTex)
{
    // ... 获取 RenderContext, PushRenderTarget 等标准操作 ...
    // ... PushCustomClipPlane (使用 +0.5f ~ +1.0f 修正遮挡) ...
    if (!pTargetTex) return;
    IMatRenderContext* pRenderContext = m_pMaterialSystem->GetRenderContext();
    if (!pRenderContext) return;

    // 1. 计算透视正确的视角 (Render View) - 保持在墙后
    CViewSetup portalView = CalculatePortalView(mainView, entryPortal, exitPortal);
    
    // 2. 构造官方风格的可见性数据 (Custom Visibility)
    // 【重要】ViewCustomVisibility_t 依然是必须的，解决丢模型
    // 获取出口法线
    ViewCustomVisibility_t customVis;
    Vector exitNormal;
    U::Math.AngleVectors(exitPortal->angles, &exitNormal, nullptr, nullptr);


    // 策略优化：多点采样 + 有效性检查
    // 我们生成 3 个探测点：中心，中心前移 20，中心前移 50
    // 只要其中有一个在有效 Leaf 里，渲染就稳了
    Vector testPoints[] = {
        exitPortal->origin + (exitNormal * 1.0f),  // 紧贴门面
        exitPortal->origin + (exitNormal * 25.0f), // 稍微靠前
        exitPortal->origin + (exitNormal * 50.0f)  // 明显靠前
    };

    int validLeafIndex = -1;

    for (const auto& pt : testPoints) {
        // 模仿官方：先检查点是否有效
        int leafID = I::EngineTrace->GetLeafContainingPoint(pt);
        if (leafID != -1 && leafID != 0) { // 0 通常是 Solid        
            customVis.AddVisOrigin(pt);            
            // 记录第一个有效的 Leaf ID 用于强制指定
            if (validLeafIndex == -1) validLeafIndex = leafID;
            // I::DebugOverlay->AddBoxOverlay(pt, Vector(-2,-2,-2), Vector(2,2,2), Vector(0,0,0), 0, 255, 0, 100, 0.1f);
        } else {
            // [调试] 无效点 (在墙里)，画个红框
            // I::DebugOverlay->AddBoxOverlay(pt, Vector(-2,-2,-2), Vector(2,2,2), Vector(0,0,0), 255, 0, 0, 100, 0.1f);
        }
    }

    // 模仿官方：强制指定 View Leaf (如果找到了有效 Leaf)
    if (validLeafIndex != -1) {
        customVis.ForceViewLeaf(validLeafIndex);
    }

    // 3. 准备渲染环境
    LogPortalRenderState("mode3-before-push-rt", m_nProcessingDepth, portalView, entryPortal, exitPortal, pTargetTex, pRenderContext, &customVis, nullptr);
    pRenderContext->PushRenderTargetAndViewport();
    pRenderContext->SetRenderTarget(pTargetTex);
    pRenderContext->Viewport(0, 0, pTargetTex->GetActualWidth(), pTargetTex->GetActualHeight());    
    pRenderContext->ClearColor4ub(255, 0, 0, 255);
    pRenderContext->ClearBuffers(true, true, true);
    LogPortalRenderState("mode3-after-push-rt", m_nProcessingDepth, portalView, entryPortal, exitPortal, pTargetTex, pRenderContext, &customVis, nullptr);

    // 4. 设置剪裁平面 (Clip Plane) - 模仿官方处理遮挡
    // 官方代码：m_vForward.Dot( origin - forward * 0.5f )
    float clipPlane[4];
    clipPlane[0] = exitNormal.x; 
    clipPlane[1] = exitNormal.y; 
    clipPlane[2] = exitNormal.z;
    clipPlane[3] =
        PortalTransitionDecision::ComputeOfficialPortalRemoteClipPlaneDistance(
            exitNormal.Dot(exitPortal->origin));

    pRenderContext->PushCustomClipPlane(clipPlane);
    pRenderContext->EnableClipping(true);

    // 5. 渲染
    // Push 真实视角 (墙后) 以保持透视
    I::CustomRender->Push3DView(portalView, 0, pTargetTex, nullptr, nullptr);
    
    // 计算环境光 (使用最稳的那个有效点，如果没有就用原点)
    Vector fogOrigin = (customVis.m_nNumVisOrigins > 0) ? customVis.m_rgVisOrigins[0] : exitPortal->origin;
    VisibleFogVolumeInfo_t fog_1;
    I::CustomRender->GetVisibleFogVolumeInfo(fogOrigin, fog_1); 
    WaterRenderInfo_t water_1;
    I::CustomView->DetermineWaterRenderInfo(&fog_1, &water_1);
    LogPortalRenderState("mode3-before-draw", m_nProcessingDepth, portalView, entryPortal, exitPortal, pTargetTex, pRenderContext, &customVis, &fog_1);
    
    // 【核心】传入 customVis
    I::CustomView->DrawWorldAndEntities(true, portalView, m_nClearFlags, &fog_1, &water_1, &customVis);
    LogPortalRenderState("mode3-after-draw", m_nProcessingDepth, portalView, entryPortal, exitPortal, pTargetTex, pRenderContext, &customVis, &fog_1);

    // 6. 恢复
    I::CustomRender->PopView(nullptr);
    pRenderContext->EnableClipping(false);
    pRenderContext->PopCustomClipPlane();
    LogPortalRenderState("mode3-before-pop-rt", m_nProcessingDepth, portalView, entryPortal, exitPortal, pTargetTex, pRenderContext, &customVis, &fog_1);
    pRenderContext->PopRenderTargetAndViewport();
    LogPortalRenderState("mode3-after-pop-rt", m_nProcessingDepth, portalView, entryPortal, exitPortal, pTargetTex, pRenderContext, &customVis, &fog_1);
}
#endif

// 调试函数 - 所有模式共用
#if PORTAL_RENDER_MODE != 0
// 简单的 BMP 写入器 (无需依赖)
void L4D2_Portal::WriteBMP(const char* filename, int width, int height, unsigned char* data)
{
    // BMP 文件头 (14 bytes)
    unsigned char fileHeader[14] = {
        'B','M', // Magic
        0,0,0,0, // File size (filled later)
        0,0,     // Reserved
        0,0,     // Reserved
        54,0,0,0 // Offset to pixel data
    };

    // BMP 信息头 (40 bytes)
    unsigned char infoHeader[40] = {
        40,0,0,0, // Info header size
        0,0,0,0,  // Width (filled later)
        0,0,0,0,  // Height (filled later)
        1,0,      // Planes
        32,0,     // Bits per pixel (32 for BGRA)
        0,0,0,0,  // Compression (0 = none)
        0,0,0,0,  // Image size (can be 0 for uncompressed)
        0,0,0,0,  // X pixels per meter
        0,0,0,0,  // Y pixels per meter
        0,0,0,0,  // Colors used
        0,0,0,0   // Colors important
    };

    int fileSize = 54 + (width * height * 4); // 4 bytes per pixel

    // 填充文件头数据
    fileHeader[2] = (unsigned char)(fileSize);
    fileHeader[3] = (unsigned char)(fileSize >> 8);
    fileHeader[4] = (unsigned char)(fileSize >> 16);
    fileHeader[5] = (unsigned char)(fileSize >> 24);

    // 填充信息头数据
    infoHeader[4] = (unsigned char)(width);
    infoHeader[5] = (unsigned char)(width >> 8);
    infoHeader[6] = (unsigned char)(width >> 16);
    infoHeader[7] = (unsigned char)(width >> 24);

    infoHeader[8] = (unsigned char)(height);
    infoHeader[9] = (unsigned char)(height >> 8);
    infoHeader[10] = (unsigned char)(height >> 16);
    infoHeader[11] = (unsigned char)(height >> 24);

    std::ofstream f(filename, std::ios::out | std::ios::binary);
    if (!f) return;

    f.write((char*)fileHeader, 14);
    f.write((char*)infoHeader, 40);
    f.write((char*)data, width * height * 4);
    f.close();

    U::LogDebug("Texture saved to: %s\n", filename);
}

void L4D2_Portal::DumpTextureToDisk(ITexture* pTexture, const char* pFilename)
{
    if (!pTexture || !m_pMaterialSystem) return;

    IMatRenderContext* pRenderContext = m_pMaterialSystem->GetRenderContext();
    if (!pRenderContext) return;

    int w = pTexture->GetActualWidth();
    int h = pTexture->GetActualHeight();

    // 1. 临时绑定该纹理为 RenderTarget
    // 这样做是为了能使用 ReadPixels 从中读取数据
    pRenderContext->PushRenderTargetAndViewport();
    pRenderContext->SetRenderTarget(pTexture);
    pRenderContext->Viewport(0, 0, w, h);

    // 2. 分配内存缓冲区 (RGBA = 4字节)
    int bufferSize = w * h * 4;
    unsigned char* pPixelData = new unsigned char[bufferSize];

    // 3. 从 GPU 读取像素
    // 注意：这里使用 IMAGE_FORMAT_BGRA8888，因为 BMP 格式原生是 BGR 顺序
    // 如果读出来颜色是反的（红蓝互换），改成 IMAGE_FORMAT_RGBA8888
    pRenderContext->ReadPixels(0, 0, w, h, pPixelData, IMAGE_FORMAT_BGRA8888);

    // 4. 恢复之前的 RT
    pRenderContext->PopRenderTargetAndViewport();

    // 5. 写入磁盘
    // 注意：Source引擎保存的文件通常在游戏根目录下，或者 bin 目录下
    // 为了方便找，建议写绝对路径，或者文件名写死
    WriteBMP(pFilename, w, h, pPixelData);

    // 6. 清理
    delete[] pPixelData;
}
#endif

// ============================================================================
// 缩放曲线函数实现
// ============================================================================

namespace ScaleCurves
{
    // 线性：f(t) = t
    float Linear(float t)
    {
        return t;
    }

    // 缓入（二次方）：慢速开始，快速结束
    // f(t) = t^2
    float EaseInQuad(float t)
    {
        return t * t;
    }

    // 缓出（二次方）：快速开始，慢速结束
    // f(t) = t * (2 - t) = 2t - t^2
    float EaseOutQuad(float t)
    {
        return t * (2.0f - t);
    }

    // 缓入缓出：结合两者
    // f(t) = 2t^2 (t < 0.5), f(t) = -1 + (4 - 2t)t (t >= 0.5)
    float EaseInOut(float t)
    {
        return t < 0.5f ? 2.0f * t * t : -1.0f + (4.0f - 2.0f * t) * t;
    }

    // 弹性效果：带有回弹的动画
    // 模拟弹簧振荡效果
    float Elastic(float t)
    {
        const float c4 = (2.0f * 3.14159265f) / 3.0f;

        if (t == 0.0f) return 0.0f;
        if (t == 1.0f) return 1.0f;

        return t == 0.0f ? 0.0f :
               t == 1.0f ? 1.0f :
               powf(2.0f, -10.0f * t) * sinf((t * 10.0f - 0.75f) * c4) + 1.0f;
    }
}

// ============================================================================
// 传送门缩放动画更新函数
// ============================================================================

#include "../SDK/L4D2/Entities/C_BaseAnimating.h"

// ============================================================================
// 统一的动画管理函数
// ============================================================================

bool L4D2_Portal::CanStartAnimation(const PortalInfo_t* pPortal) const
{
    if (!pPortal) return false;

    // 只有在空闲或已关闭状态才能开始新动画
    return (pPortal->animState == PORTAL_ANIM_IDLE ||
            pPortal->animState == PORTAL_ANIM_CLOSED);
}

void L4D2_Portal::StartPortalOpenAnimation(PortalInfo_t* pPortal, const Vector& newPosition)
{
    if (!pPortal || !I::EngineClient) return;

    // 检查是否可以开始新动画（如果正在关闭，需要等待完成）
    if (pPortal->animState == PORTAL_ANIM_CLOSING) {
        return; // 正在关闭，不允许打断
    }

    // 设置新位置
    pPortal->origin = newPosition;
    pPortal->lastOrigin = newPosition;

    // 开始打开动画
    pPortal->animState = PORTAL_ANIM_OPENING;
    pPortal->isAnimating = true;
    pPortal->bIsActive = true;
    pPortal->bIsClosing = false;
    pPortal->currentScale = 0.0f;
    pPortal->animStartTime = I::EngineClient->OBSOLETE_Time();
}

void L4D2_Portal::StartPortalCloseAnimation(PortalInfo_t* pPortal)
{
    m_PortalTransition.Reset();
    m_PortalTransitionSimulator.Reset();
    m_PortalCollisionBridge.Reset();
    m_PortalStage1Probe.Reset();

    // 跳过空指针、未激活的、已经缩放到0的、或正在关闭中的传送门
    if (!pPortal || !pPortal->bIsActive || pPortal->currentScale <= 0.0f || pPortal->animState == PORTAL_ANIM_CLOSING) {
        return;
    }

    const PortalBrushOwner owner = pPortal == &g_OrangePortal
        ? PortalBrushOwner::Orange
        : PortalBrushOwner::Blue;
    if (!PortalBspPhase1::Restore("portal-close"))
    {
        U::LogError("[PortalBsp][BindingLifecycle] event=PortalClose owner=%s blocked=true reason=restore-failed.\n",
            owner == PortalBrushOwner::Blue ? "blue" : "orange");
        return;
    }
    G::PortalBspCollisionCarver.UnbindPortalBrush(owner);
    U::LogInfo("[PortalBsp][BindingLifecycle] event=PortalClose owner=%s bindingCleared=true destructiveWrites=false.\n",
        owner == PortalBrushOwner::Blue ? "blue" : "orange");

    // 使用新状态
    pPortal->animState = PORTAL_ANIM_CLOSING;
    pPortal->bIsClosing = true;
    pPortal->isAnimating = true;
    pPortal->closeAnimStartTime = I::EngineClient->OBSOLETE_Time();
}

bool L4D2_Portal::UpdatePortalScaleAnimation(PortalInfo_t* pPortal, const Vector& currentPos, C_BaseAnimating* pEntity)
{
    if (!pPortal || !I::EngineClient) {
        return false;
    }

    float flCurrentTime = I::EngineClient->OBSOLETE_Time();

    // === 使用统一的 animState 状态机 ===

    switch (pPortal->animState) {
        case PORTAL_ANIM_CLOSING:
        {
            // 关闭动画: 1.0 → 0.0 (线性)
            float flElapsedTime = flCurrentTime - pPortal->closeAnimStartTime;
            float t = flElapsedTime / pPortal->closeAnimDuration;

            float scale = 1.0f - t;
            if (scale < 0.0f) scale = 0.0f;
            if (scale > 1.0f) scale = 1.0f;
            pPortal->currentScale = scale;

            // 动画完成
            if (t >= 1.0f) {
                pPortal->currentScale = 0.0f;
                pPortal->animState = PORTAL_ANIM_CLOSED;
                pPortal->bIsActive = false;
                pPortal->bIsClosing = false;
                pPortal->isAnimating = false;
            }
            break;
        }

        case PORTAL_ANIM_OPENING:
        {
            // 打开动画: 0.0 → 1.0 (使用配置的曲线)
            float flElapsedTime = flCurrentTime - pPortal->animStartTime;
            float t = flElapsedTime / pPortal->animDuration;

            float scale = 0.0f;
            switch (pPortal->animType) {
                case SCALE_LINEAR:
                    scale = ScaleCurves::Linear(t);
                    break;
                case SCALE_EASE_IN:
                    scale = ScaleCurves::EaseInQuad(t);
                    break;
                case SCALE_EASE_OUT:
                    scale = ScaleCurves::EaseOutQuad(t);
                    break;
                case SCALE_EASE_IN_OUT:
                    scale = ScaleCurves::EaseInOut(t);
                    break;
                case SCALE_ELASTIC:
                    scale = ScaleCurves::Elastic(t);
                    break;
                default:
                    scale = ScaleCurves::Linear(t);
                    break;
            }

            if (scale < 0.0f) scale = 0.0f;
            if (scale > 1.0f) scale = 1.0f;
            pPortal->currentScale = scale;

            // 动画完成
            if (t >= 1.0f) {
                pPortal->currentScale = 1.0f;
                pPortal->animState = PORTAL_ANIM_OPEN;
                pPortal->isAnimating = false;
            }
            break;
        }

        case PORTAL_ANIM_IDLE:
        case PORTAL_ANIM_OPEN:
        case PORTAL_ANIM_CLOSED:
        default:
            // 空闲状态，不需要更新动画
            // 但保持兼容性：检测位置变化（旧的触发方式，仅在没有使用新触发方式时生效）
            float flDistToLast = currentPos.DistTo(pPortal->lastOrigin);
            if (flDistToLast > 1.0f && pPortal->bIsActive && !pPortal->bIsClosing) {
                // 位置变化，触发打开动画
                pPortal->animState = PORTAL_ANIM_OPENING;
                pPortal->isAnimating = true;
                pPortal->currentScale = 0.0f;
                pPortal->animStartTime = flCurrentTime;
            }
            break;
    }

    // 应用到模型
    if (pEntity) {
        float* pScale = (float*)((uintptr_t)pEntity + 0x728); // m_flModelScale offset
        if (pScale) {
            *pScale = pPortal->currentScale;
        }
    }

    // 保存位置状态
    pPortal->lastOrigin = currentPos;
    pPortal->origin = currentPos;
    pPortal->lastTime = flCurrentTime;

    return pPortal->isAnimating;
}
