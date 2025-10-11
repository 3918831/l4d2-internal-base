
#include <iostream>
#include "../SDK/L4D2/Includes/const.h"
#include "../SDK/L4D2/Interfaces/MaterialSystem.h"
#include "../SDK/L4D2/Interfaces/RenderView.h"
#include "../SDK/L4D2/Interfaces/MatRenderContext.h"
#include "../SDK/L4D2/Interfaces/ModelInfo.h"
#include "../SDK/L4D2/Interfaces/ModelRender.h"
//#include "../SDK/L4D2/KeyValues/KeyValues.h"
#include "../SDK/L4D2/Entities/C_BasePlayer.h"
#include "../Util/Math/Math.h"
#include "L4D2_Portal.h"
#include "CustomRender.h"

// ITexture* g_pPortalTexture = nullptr;
//IMaterialSystem* g_pPortalMaterialSystem = nullptr;
IMaterial* g_pPortalMaterial = nullptr;
IMaterial* g_pPortalMaterial_2 = nullptr;
IMaterial* g_pPortalMaterial_3 = nullptr;


void L4D2_Portal::CreatePortalTexture()
{
    if (!m_pCustomMaterialSystem)
    {
        printf("[Portal] Material system not initialized!\n");
        return;
    }

    m_pCustomMaterialSystem->UnLockRTAllocation();
    m_pMaterialSystem->BeginRenderTargetAllocation();
    m_pPortalTexture = m_pMaterialSystem->CreateNamedRenderTargetTextureEx("_rt_Portal1Texture",
        1, 1,
        RT_SIZE_FULL_FRAME_BUFFER,
        I::MaterialSystem->GetBackBufferFormat(), // or IMAGE_FORMAT_RGBA8888, 
        MATERIAL_RT_DEPTH_SHARED,
        0,//TEXTUREFLAGS_NOMIP,
       CREATERENDERTARGETFLAGS_HDR);

    m_pPortalTexture_2 = m_pMaterialSystem->CreateNamedRenderTargetTextureEx("_rt_Portal1Texture_2",
        1, 1,
        RT_SIZE_FULL_FRAME_BUFFER,
        I::MaterialSystem->GetBackBufferFormat(), // or IMAGE_FORMAT_RGBA8888, 
        MATERIAL_RT_DEPTH_SHARED,
        0,//TEXTUREFLAGS_NOMIP,
        CREATERENDERTARGETFLAGS_HDR);

#ifdef RECURSIVE_RENDERING
    // 预分配内存
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
            printf("[Portal] Error: Failed to create portal texture %d\n", i);
        }
    }

#endif
    m_pMaterialSystem->EndRenderTargetAllocation();

    if (!m_pPortalTexture || !m_pPortalTexture_2)
    {
        printf("[Portal] Failed to create portal texture!\n");
        return;
    } else {
        printf("[Portal] m_pPortalTexture Name: %s\n", m_pPortalTexture->GetName());
    }

    printf("[Portal] Created portal texture successfully\n");
}

void L4D2_Portal::CreatePortalMaterial()
{
    if (!m_pMaterialSystem)
    {
        printf("[Portal] Material system not initialized!\n");
        return;
    }

    // 使用FindMaterial查找游戏内置材质
    g_pPortalMaterial = m_pMaterialSystem->FindMaterial("models/zimu/zimu1_hd/zimu1_hd", TEXTURE_GROUP_MODEL, true, nullptr);
    g_pPortalMaterial_2 = m_pMaterialSystem->FindMaterial("models/zimu/zimu2_hd/zimu2_hd", TEXTURE_GROUP_MODEL, true, nullptr);
    g_pPortalMaterial_3 = m_pMaterialSystem->FindMaterial("dev/write_stencil", TEXTURE_GROUP_OTHER);
#ifdef RECURSIVE_RENDERING
    m_pWriteStencilMaterial = m_pMaterialSystem->FindMaterial("dev/write_stencil", TEXTURE_GROUP_OTHER);
    m_pDynamicPortalMaterial = m_pMaterialSystem->FindMaterial("dev/portal_content", TEXTURE_GROUP_OTHER);
    m_pBlackoutMaterial = m_pMaterialSystem->FindMaterial("dev/portal_blackout", TEXTURE_GROUP_OTHER);

    if (!m_pDynamicPortalMaterial || !m_pWriteStencilMaterial || !m_pBlackoutMaterial) {
        printf("[Portal] m_pDynamicPortalMaterial::Failed to find dynamic portal material!\n");
        return;
    }
#endif

    if (!g_pPortalMaterial || !g_pPortalMaterial_2 || !g_pPortalMaterial_3)
    {
        printf("[Portal] Failed to find portal material!\n");
        return;
    }

    printf("[Portal] Found portal material successfully\n");

    // 设置材质为可绘制状态
    g_pPortalMaterial->IncrementReferenceCount();
    g_pPortalMaterial_2->IncrementReferenceCount();
    g_pPortalMaterial_3->IncrementReferenceCount();
#ifdef RECURSIVE_RENDERING
    m_pWriteStencilMaterial->IncrementReferenceCount();
    m_pDynamicPortalMaterial->IncrementReferenceCount();
    m_pBlackoutMaterial->IncrementReferenceCount();
#endif
    // 查找并设置基础纹理参数
    IMaterialVar* pBaseTextureVar = g_pPortalMaterial->FindVar("$basetexture", NULL, false);
    IMaterialVar* pBaseTextureVar_2 = g_pPortalMaterial_2->FindVar("$basetexture", NULL, false);
    if (pBaseTextureVar && pBaseTextureVar_2)
    {
        if (m_pPortalTexture) {
            pBaseTextureVar->SetTextureValue(m_pPortalTexture);
            pBaseTextureVar_2->SetTextureValue(m_pPortalTexture_2);
            printf("[Portal] Set base texture to portal render target\n");

            //IMaterialVar* pBaseTextureVarofDynamic = m_pDynamicPortalMaterial->FindVar("$basetexture", NULL, false);
            //if (pBaseTextureVarofDynamic) {
            //    pBaseTextureVarofDynamic->SetTextureValue(pBaseTextureVar->GetTextureValue());
            //}
        } else {
            printf("[Portal] Failed to set base texture because portal texture is null\n");
        }        
    }
    else
    {
        // 尝试查找diffusemap作为替代
        pBaseTextureVar = g_pPortalMaterial->FindVar("$diffusemap", NULL, false);
        pBaseTextureVar_2 = g_pPortalMaterial_2->FindVar("$diffusemap", NULL, false);
        if (pBaseTextureVar && pBaseTextureVar_2)
        {
            pBaseTextureVar->SetTextureValue(m_pPortalTexture);
            pBaseTextureVar_2->SetTextureValue(m_pPortalTexture_2);
            printf("[Portal] Set diffuse map to portal render target\n");
        }
        else
        {
            printf("[Portal] Failed to find texture variables\n");
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
        printf("[Portal] Failed to get MaterialSystem interface!\n");
        return;
    }

    std::cout << "[Portal] I::MaterialSystem:" << (INT32)(I::MaterialSystem) << std::endl;
    std::cout << "[Portal] m_pMaterialSystem:" << (INT32)m_pMaterialSystem << std::endl;
    std::cout << "[Portal] m_pCustomMaterialSystem:" << (INT32)m_pCustomMaterialSystem << std::endl;

    printf("[Portal] Got MaterialSystem interface success\n");

    // 创建或查找材质
    CreatePortalTexture();
    
    // 创建测试用材质,本质上是查找zimu的材质,实际业务暂时用不到
    CreatePortalMaterial();

    // 初始化完成后，可以调用RenderPortalFrame进行渲染
    printf("[Portal] Initialization completed\n\n");
    
    m_pPortalMaterial = g_pPortalMaterial;
    m_pPortalMaterial_2 = g_pPortalMaterial_2;
    
    printf("[Portal] g_pPortalMaterial: %p\n", g_pPortalMaterial);
    printf("[Portal] g_pPortalMaterial_2: %p\n", g_pPortalMaterial_2);
    printf("[Portal] m_pPortalTexture: %p\n", m_pPortalTexture);
    printf("[Portal] m_pMaterialSystem: %p\n", m_pMaterialSystem);
    printf("[Portal] m_pPortalMaterial: %p\n", m_pPortalMaterial);
    printf("[Portal] m_pCustomMaterialSystem: %p\n", m_pCustomMaterialSystem);

#ifdef RECURSIVE_RENDERING
    m_nClearFlags = 0;
    I::EngineClient->GetScreenSize(screenWidth, screenHeight);
#endif
}

// 清理函数，在不需要传送门时调用
void L4D2_Portal::PortalShutdown()
{
    if (g_pPortalMaterial)
    {
        g_pPortalMaterial->DecrementReferenceCount();
        g_pPortalMaterial = nullptr;
    }
    
    if (m_pPortalTexture)
    {
        // Source引擎中的纹理通常由材质系统管理，不需要手动释放
        m_pPortalTexture = nullptr;
    }

#ifdef RECURSIVE_RENDERING
    if (m_pDynamicPortalMaterial) {
        m_pDynamicPortalMaterial->DecrementReferenceCount();
        m_pDynamicPortalMaterial = nullptr;
    }
    if (m_pWriteStencilMaterial) {
        m_pWriteStencilMaterial->DecrementReferenceCount();
        m_pWriteStencilMaterial = nullptr;
    }
    if (m_pBlackoutMaterial) {
        m_pBlackoutMaterial->DecrementReferenceCount();
        m_pBlackoutMaterial = nullptr;
    }
#endif
    
    m_pMaterialSystem = nullptr;
    m_pCustomMaterialSystem = nullptr;
    
    printf("[Portal] Shutdown completed\n");
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

    // 6. 【核心修正】将虚拟摄像机沿出口法线方向稍微向前推，以进入有效的Visleaf
    // 这是解决黑天、全亮模型和渲染缺失问题的关键
    Vector exitPortalNormal;
    U::Math.AngleVectors(pExitPortal->angles, &exitPortalNormal, nullptr, nullptr);

    // 将摄像机向前推动一个很小的单位（例如 1.0f），确保它在传送门“外面”
    portalView.origin += exitPortalNormal * 1.0f;

    return portalView;
}

#ifdef RECURSIVE_RENDERING
bool L4D2_Portal::RenderPortalViewRecursive(const CViewSetup& previousView, PortalInfo_t* entryPortal, PortalInfo_t* exitPortal)
{
    // 检查递归深度是否超限
    if (m_nPortalRenderDepth >= MAX_PORTAL_RECURSION_DEPTH) {
        // 已经到了最深处，我们不再继续渲染世界，
        // 而是将这一层的纹理清空为一个指定的颜色。

        //IMatRenderContext* pRenderContext = I::MaterialSystem->GetRenderContext();
        //if (pRenderContext)
        //{
        //    // 获取当前深度对应的纹理
        //    ITexture* pFinalTexture = m_vPortalTextures[m_nPortalRenderDepth - 1];

        //    pRenderContext->PushRenderTargetAndViewport();
        //    pRenderContext->SetRenderTarget(pFinalTexture);

        //    // 设置你想要的颜色。例如，半透明的黑色，或者传送门的边框颜色
        //    // Valve 在《传送门》里用的就是传送门自身的颜色
        //    if (/*entryPortal->IsBlue()*/ true) // 假设有这样的函数
        //        pRenderContext->ClearColor4ub(75, 125, 255, 255); // 蓝色
        //    else
        //        pRenderContext->ClearColor4ub(255, 150, 0, 255);   // 橙色

        //    // 执行清空操作 (只清空颜色，不需要深度)
        //    pRenderContext->ClearBuffers(true, false);

        //    pRenderContext->PopRenderTargetAndViewport();
        //}

        // 工作完成，返回。
        //return;



        // 由于我们已经没有更深的纹理可用，这里我们“假装”已经渲染了最深层。
        // 但我们实际上什么都不做，因为没有纹理可以写入。
        // 调用者 DrawModelExecute 将会使用一个未被渲染的纹理，
        // 这个问题我们将在 DrawModelExecute 中处理。
        return false;
    }

    // --- 核心逻辑：入栈 ---
    m_nPortalRenderDepth++;
    // 【新增】设置当前出口，用于递归保护
    m_pCurrentExitPortal = exitPortal;

    CViewSetup newPortalView = CalculatePortalView(previousView, entryPortal, exitPortal);
    m_vViewStack.push_back(newPortalView);

    IMatRenderContext* pRenderContext = G::G_L4D2Portal.m_pMaterialSystem->GetRenderContext();
    if (pRenderContext)
    {

        // 从池中获取当前深度的渲染目标纹理
        // 此时 m_nPortalRenderDepth 最小为 1, 所以索引是安全的 [0]
        // 从池中获取当前深度的渲染目标纹理
        ITexture* pRenderTarget = m_vPortalTextures[m_nPortalRenderDepth - 1];
        pRenderContext->PushRenderTargetAndViewport();
        pRenderContext->SetRenderTarget(pRenderTarget);
        pRenderContext->Viewport(0, 0, screenWidth, screenHeight);
        pRenderContext->ClearBuffers(true, true, true);

        // --- 定义出口（橙门）的裁剪平面 ---
        Vector exitNormal;
        U::Math.AngleVectors(exitPortal->angles, &exitNormal, nullptr, nullptr);

        float clipPlane[4];
        clipPlane[0] = exitNormal.x;
        clipPlane[1] = exitNormal.y;
        clipPlane[2] = exitNormal.z;

        // 【核心修正】修正 D 值的计算，遵循 n·p - d = 0 的形式
        // d = n·p，并加入一个小的偏移量防止瑕疵
        clipPlane[3] = DotProduct(exitPortal->origin, exitNormal) - 0.5f;

        //CViewSetup portalView = G::G_L4D2Portal.CalculatePortalView(previousView, &G::G_L4D2Portal.g_BluePortal, &G::G_L4D2Portal.g_OrangePortal);

        pRenderContext->PushCustomClipPlane(clipPlane);
        pRenderContext->EnableClipping(true);

        // 【修正 5】Push3DView 和 DrawWorldAndEntities 必须使用 newPortalView
        // 并且 Push3DView 的纹理参数应该是 pRenderTarget
        VisibleFogVolumeInfo_t fog_1;
        I::CustomRender->GetVisibleFogVolumeInfo(exitPortal->origin, fog_1);
        WaterRenderInfo_t water_1;
        I::CustomView->DetermineWaterRenderInfo(&fog_1, &water_1);

        I::CustomRender->Push3DView(newPortalView, 0, pRenderTarget, I::CustomView->GetFrustum(), nullptr);
        // 递归调用原函数进行渲染, 不渲染玩家模型和HUD
        I::CustomView->DrawWorldAndEntities(true, newPortalView, m_nClearFlags, &fog_1, &water_1);
        I::CustomRender->PopView(I::CustomView->GetFrustum());

        pRenderContext->EnableClipping(false);
        pRenderContext->PopCustomClipPlane();
        pRenderContext->PopRenderTargetAndViewport();
        
    }
    m_vViewStack.pop_back();
    m_nPortalRenderDepth--;
    return true;
}
#endif