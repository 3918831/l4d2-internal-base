
#include <iostream>
#include "../SDK/L4D2/Includes/const.h"
#include "../SDK/L4D2/Interfaces/MaterialSystem.h"
#include "../SDK/L4D2/Interfaces/RenderView.h"
#include "../SDK/L4D2/Interfaces/MatRenderContext.h"
#include "../SDK/L4D2/Interfaces/ModelInfo.h"
#include "../SDK/L4D2/Interfaces/ModelRender.h"
//#include "../SDK/L4D2/KeyValues/KeyValues.h"
#include "../SDK/L4D2/Entities/C_BasePlayer.h"
//#include "public/qAngle.h"
#include "L4D2_Portal.h"

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
    // m_LeftEyeTexture = m_Game->m_MaterialSystem->CreateNamedRenderTargetTextureEx("leftEye0", m_RenderWidth, m_RenderHeight, RT_SIZE_NO_CHANGE, m_Game->m_MaterialSystem->GetBackBufferFormat(), MATERIAL_RT_DEPTH_SHARED, TEXTUREFLAGS_NOMIP);
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
    m_pMaterialSystem->EndRenderTargetAllocation();

    //m_pPortalTexture = m_pCustomMaterialSystem->CreateNamedRenderTargetEx("_rt_PortalTexture",
    //        512, 512, 
    //        RT_SIZE_DEFAULT, 
    //        IMAGE_FORMAT_RGBA8888, 
    //        MATERIAL_RT_DEPTH_SHARED, 
    //        TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT, 
    //        CREATERENDERTARGETFLAGS_HDR);

    // m_pPortalTexture = m_pCustomMaterialSystem->CreateNamedRenderTargetEx("rt_test1", 4096, 4096, 0, 1, 0, true, false);

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

    // 查找并设置基础纹理参数
    IMaterialVar* pBaseTextureVar = g_pPortalMaterial->FindVar("$basetexture", NULL, false);
    IMaterialVar* pBaseTextureVar_2 = g_pPortalMaterial_2->FindVar("$basetexture", NULL, false);
    if (pBaseTextureVar && pBaseTextureVar_2)
    {
        if (m_pPortalTexture) {
            pBaseTextureVar->SetTextureValue(m_pPortalTexture);
            pBaseTextureVar_2->SetTextureValue(m_pPortalTexture_2);
            printf("[Portal] Set base texture to portal render target\n");
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
void L4D2_Portal::RenderPortalViewRecursive(const CViewSetup& previousView, Portal* entryPortal, Portal* exitPortal)
{
    // 检查递归深度是否超限
    if (g_nPortalRenderDepth >= MAX_PORTAL_RECURSION_DEPTH) {
        return;
    }

    // --- 核心逻辑：入栈 ---
    g_nPortalRenderDepth++;
    CViewSetup newPortalView = CalculatePortalView(previousView, entryPortal, exitPortal);
    g_vViewStack.push_back(newPortalView);

    // ... 设置渲染目标为 g_vPortalTextures[g_nPortalRenderDepth - 1] ...
    // ... 设置裁剪平面等 ...

    // 调用引擎的渲染函数。
    // 如果这个调用中又遇到了传送门，它触发的 DrawModelExecute
    // 将会从栈顶读取到我们刚刚 push 进去的 newPortalView，从而实现正确的递归。
    I::CustomView->DrawWorldAndEntities(true, newPortalView, ...);

    // ... 清理渲染目标和裁剪平面 ...

    // --- 核心逻辑：出栈 ---
    // 当前深度的渲染任务已完成，必须将自己的状态从栈中移除，
    // 以便上层递归或其他同级渲染任务能够正确工作。
    g_vViewStack.pop_back();
    g_nPortalRenderDepth--;
}
#endif