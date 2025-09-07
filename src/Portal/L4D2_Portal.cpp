
#include <iostream>
#include "../SDK/L4D2/Includes/const.h"
//#include "../SDK/L4D2/Interfaces/IMaterial.h"
#include "../SDK/L4D2/Interfaces/MaterialSystem.h"
//#include "../SDK/L4D2/Interfaces/Texture.h"
#include "../SDK/L4D2/Interfaces/RenderView.h"
#include "../SDK/L4D2/Interfaces/MatRenderContext.h"

#include "../SDK/L4D2/Entities/C_BasePlayer.h"

#include "public/qAngle.h"
#include "L4D2_Portal.h"

// ITexture* g_pPortalTexture = nullptr;
 //IMaterialSystem* g_pPortalMaterialSystem = nullptr;
 IMaterial* g_pPortalMaterial = nullptr;

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
    // 创建512x512的渲染目标纹理
    m_pPortalTexture = m_pMaterialSystem->CreateNamedRenderTargetTextureEx("_rt_PortalTexture", 
       512, 512, 
       RT_SIZE_DEFAULT, 
       IMAGE_FORMAT_RGBA8888, 
       MATERIAL_RT_DEPTH_SHARED, 
       TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT, 
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

    if (!m_pPortalTexture)
    {
        printf("[Portal] Failed to create portal texture!\n");
        return;
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

    if (!g_pPortalMaterial)
    {
        printf("[Portal] Failed to find portal material!\n");
        return;
    }

    printf("[Portal] Found portal material successfully\n");

    // 设置材质为可绘制状态
    g_pPortalMaterial->IncrementReferenceCount();

    // 查找并设置基础纹理参数
    IMaterialVar* pBaseTextureVar = g_pPortalMaterial->FindVar("$basetexture", NULL, false);
    if (pBaseTextureVar)
    {
        if (m_pPortalTexture) {
            pBaseTextureVar->SetTextureValue(m_pPortalTexture);
            printf("[Portal] Set base texture to portal render target\n");
        } else {
            printf("[Portal] Failed to set base texture because portal texture is null\n");
        }
    }
    else
    {
        // 尝试查找diffusemap作为替代
        pBaseTextureVar = g_pPortalMaterial->FindVar("$diffusemap", NULL, false);
        if (pBaseTextureVar)
        {
            pBaseTextureVar->SetTextureValue(m_pPortalTexture);
            printf("[Portal] Set diffuse map to portal render target\n");
        }
        else
        {
            printf("[Portal] Failed to find texture variables\n");
        }
    }

    // 设置材质为半透明
    IMaterialVar* pTranslucentVar = g_pPortalMaterial->FindVar("$translucent", NULL, false);
    if (pTranslucentVar)
    {
        pTranslucentVar->SetIntValue(1);
    }

    // 禁用Z缓冲区测试，允许透过其他物体看到传送门内容
    IMaterialVar* pIgnoreZVar = g_pPortalMaterial->FindVar("$ignorez", NULL, false);
    if (pIgnoreZVar)
    {
        pIgnoreZVar->SetIntValue(1);
    }
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

    // 创建渲染目标纹理
    CreatePortalTexture();
    
    // 创建或查找材质
    CreatePortalMaterial();

    // 初始化完成后，可以调用RenderPortalFrame进行渲染
    printf("[Portal] Initialization completed\n");

    RenderPortalFrame();
}

// 渲染场景到传送门纹理
void L4D2_Portal::RenderPortalFrame()
{
    // 检查材质系统和纹理是否有效
    if (!m_pMaterialSystem || !m_pPortalTexture)
    {
        printf("[Portal] Material system or texture not initialized, skipping render\n");
        return;
    }

    // 获取渲染上下文
    IMatRenderContext* pRenderContext = m_pMaterialSystem->GetRenderContext();
    if (!pRenderContext)
    {
        printf("[Portal] Failed to get render context\n");
        return;
    }
    // 保存当前渲染状态
    //pRenderContext->PushRenderTargetAndViewport();

    // 创建临时的CViewSetup结构
    CViewSetup viewSetup;
    viewSetup.x = 0;
    viewSetup.y = 0;
    viewSetup.width = 512;
    viewSetup.height = 512;
    viewSetup.fov = 90;
    viewSetup.origin = { 0, 0, 0 };
    viewSetup.angles = { 0, 0, 0 };
    viewSetup.zNear = 6;
    viewSetup.zFar = 4096;

    // 设置渲染目标为传送门纹理
    //pRenderContext->SetRenderTarget(m_pPortalTexture);
    //I::BaseClient->RenderView(&viewSetup, 2 | 1, 0);
    
    // 恢复渲染状态
    //pRenderContext->PopRenderTargetAndViewport();
    //pRenderContext->Release();
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


