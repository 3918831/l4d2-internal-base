
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

// 定义CViewSetup结构体以用于渲染视图设置
struct CViewSetup
{
    int            x, y, width, height;
    int            m_nUnscaledX, m_nUnscaledY, m_nUnscaledWidth, m_nUnscaledHeight;
    bool           m_bOrtho;
    float          m_OrthoLeft;
    float          m_OrthoTop;
    float          m_OrthoRight;
    float          m_OrthoBottom;
    float          m_flAspectRatio;
    float          m_flNearZ;
    float          m_flFarZ;
    float          m_flNearViewmodelZ;
    float          m_flFarViewmodelZ;
    float          m_flFOV;
    float          m_flViewmodelFOV;
    Vector         m_origin;
    QAngle         m_angles;
    Vector         m_viewSize;
    float          m_flZNear;
    float          m_flZFar;
};

void L4D2_Portal::CreatePortalTexture()
{
    if (!m_pCustomMaterialSystem)
    {
        printf("[Portal] Material system not initialized!\n");
        return;
    }

    // 创建512x512的渲染目标纹理
    //g_pPortalTexture = g_pPortalMaterialSystem->CreateNamedRenderTargetTextureEx("_rt_PortalTexture", 
    //    512, 512, 
    //    RT_SIZE_DEFAULT, 
    //    IMAGE_FORMAT_RGBA8888, 
    //    MATERIAL_RT_DEPTH_SHARED, 
    //    TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT, 
    //    CREATERENDERTARGETFLAGS_HDR);


    //m_pPortalTexture = m_pCustomMaterialSystem->CreateNamedRenderTargetEx("_rt_PortalTexture",
    //        512, 512, 
    //        RT_SIZE_DEFAULT, 
    //        IMAGE_FORMAT_RGBA8888, 
    //        MATERIAL_RT_DEPTH_SHARED, 
    //        TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT, 
    //        CREATERENDERTARGETFLAGS_HDR);

    m_pPortalTexture = m_pCustomMaterialSystem->CreateNamedRenderTargetEx("rt_test1", 4096, 4096, 0, 1, 0, true, false);

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

    try
    {
        // 推送渲染目标和视口
        pRenderContext->PushRenderTargetAndViewport(m_pPortalTexture, 0, 0, 512, 512);
        
        // 清除缓冲区
        pRenderContext->ClearBuffers(true, true, true);
        
        // 设置CViewSetup结构体（模拟传送门后的视角）
        CViewSetup viewSetup;
        memset(&viewSetup, 0, sizeof(CViewSetup));
        
        // 设置视口大小
        viewSetup.x = 0;
        viewSetup.y = 0;
        viewSetup.width = 512;
        viewSetup.height = 512;
        viewSetup.m_nUnscaledX = 0;
        viewSetup.m_nUnscaledY = 0;
        viewSetup.m_nUnscaledWidth = 512;
        viewSetup.m_nUnscaledHeight = 512;
        
        // 设置透视参数
        viewSetup.m_bOrtho = false;
        viewSetup.m_flAspectRatio = 1.0f; // 512x512的宽高比为1:1
        viewSetup.m_flFOV = 90.0f; // 标准FOV
        viewSetup.m_flNearZ = 4.0f;
        viewSetup.m_flFarZ = 16384.0f;
        
        // 获取当前玩家视角，并模拟传送门后的视角
        // 这里简单地使用当前玩家的位置和角度，但在实际实现中需要根据传送门的位置和方向进行转换
        //C_BasePlayer* pLocalPlayer = I::EngineClient->GetLocalPlayer();
        //C_BasePlayer* pLocalPlayer = I::ClientEntityList->GetClientEntity(I::EngineClient->GetLocalPlayer())->As<C_BasePlayer*>();
        C_BasePlayer* pLocalPlayer = nullptr;
        if (pLocalPlayer)
        {
            Vector eyePos;
            QAngle eyeAng {0.0, 0.0, 0.0};
            pLocalPlayer->ViewPunch(eyePos);
            //pLocalPlayer->GetEyeAngles(&eyeAng);
            
            // 这里只是一个示例，实际实现中需要根据传送门的位置和方向调整视角
            // 例如：将玩家视角旋转180度，模拟从传送门另一侧看到的场景
            viewSetup.m_origin = eyePos;
            viewSetup.m_angles = eyeAng;
            
            // 为了演示效果，将视角稍微偏移一点
            Vector forward;
            //U::Math.AngleVectors(eyeAng, &forward);
            viewSetup.m_origin += forward * 100.0f; // 向前移动100单位
        }
        else
        {
            // 如果没有本地玩家，使用默认位置
            viewSetup.m_origin = Vector(0, 0, 0);
            viewSetup.m_angles = QAngle(0, 0, 0);
        }
        
        // 开始场景渲染
        I::RenderView->SceneBegin();
        
        // 推送3D视图
        I::RenderView->Push3DView(pRenderContext, &viewSetup, 0, nullptr, nullptr);
        
        // 渲染世界（这里只是基本实现，实际需要更复杂的渲染逻辑）
        // 在实际Source引擎中，你需要调用DrawWorldLists等函数
        // 这里为了示例，我们使用简化的方法
        
        // 结束场景渲染
        I::RenderView->PopView(pRenderContext, nullptr);
        I::RenderView->SceneEnd();
        
        // 弹出渲染目标和视口
        pRenderContext->PopRenderTargetAndViewport();
        
        printf("[Portal] Successfully rendered frame to portal texture\n");
    }
    catch (...)
    {
        // 确保在发生异常时也弹出渲染目标和视口
        pRenderContext->PopRenderTargetAndViewport();
        printf("[Portal] Exception occurred during rendering\n");
    }
    
    // 释放渲染上下文
    pRenderContext->Release();
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


