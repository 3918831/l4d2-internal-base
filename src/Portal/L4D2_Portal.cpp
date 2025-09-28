
#include <iostream>
#include "../SDK/L4D2/Includes/const.h"
#include "../SDK/L4D2/Interfaces/MaterialSystem.h"
#include "../SDK/L4D2/Interfaces/RenderView.h"
#include "../SDK/L4D2/Interfaces/MatRenderContext.h"
#include "../SDK/L4D2/Interfaces/ModelInfo.h"
#include "../SDK/L4D2/Interfaces/ModelRender.h"
//#include "../SDK/L4D2/KeyValues/KeyValues.h"
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
    m_pPortalTexture = m_pMaterialSystem->CreateNamedRenderTargetTextureEx("_rt_Portal1Texture",
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

    if (!m_pPortalTexture)
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

    // 初始化成员变量
    m_pStencilTexture = nullptr;
    m_bUseStencilMask = true; // 默认启用模板遮罩

    // 创建或查找材质
    CreatePortalTexture();
    
    // 创建模板缓冲区纹理
    CreateStencilTexture();
    
    // ��������Ҳ���
    CreatePortalMaterial();

    // 初始化完成后，可以调用RenderPortalFrame进行渲染
    printf("[Portal] Initialization completed\n\n");
    
    m_pPortalMaterial = g_pPortalMaterial;

    m_pWriteStencilMaterial = m_pMaterialSystem->FindMaterial("materials/dev/write_stencil", TEXTURE_GROUP_MODEL, true, nullptr);

    FindRenderableForModel();
    printf("[Portal] g_pPortalMaterial: %p\n", g_pPortalMaterial);
    printf("[Portal] m_pPortalTexture: %p\n", m_pPortalTexture);
    printf("[Portal] m_pStencilTexture: %p\n", m_pStencilTexture);
    printf("[Portal] m_pMaterialSystem: %p\n", m_pMaterialSystem);
    printf("[Portal] m_pPortalMaterial: %p\n", m_pPortalMaterial);
    printf("[Portal] m_pCustomMaterialSystem: %p\n", m_pCustomMaterialSystem);
    printf("[Portal] m_pWriteStencilMaterial: %p\n", m_pWriteStencilMaterial);

    //RenderPortalFrame();
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

    // 获取并保存当前原始渲染上下文的渲染目标
    ITexture* pOriginalRenderTarget = reinterpret_cast<ITexture*>(pRenderContext->GetRenderTarget());

    // 设置渲染目标为当前纹理
    pRenderContext->SetRenderTarget(m_pPortalTexture);

    // 清除渲染目标
    // pRenderContext->ClearColor4ub(0, 0, 0, 0);
    // pRenderContext->ClearBuffers(true, true);

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
    
    if (m_pStencilTexture)
    {
        // 释放模板缓冲区纹理
        m_pStencilTexture = nullptr;
    }
    
    m_pMaterialSystem = nullptr;
    m_pCustomMaterialSystem = nullptr;
    
    printf("[Portal] Shutdown completed\n");
}



// 创建模板缓冲区纹理
void L4D2_Portal::CreateStencilTexture()
{
    if (!m_pMaterialSystem)
    {
        printf("[Portal] Material system not initialized!\n");
        return;
    }

    m_pMaterialSystem->BeginRenderTargetAllocation();
    
    // 创建一个用于模板缓冲区的纹理
    m_pStencilTexture = m_pMaterialSystem->CreateNamedRenderTargetTextureEx("_rt_StencilTexture",
        1, 1,
        RT_SIZE_FULL_FRAME_BUFFER,
        I::MaterialSystem->GetBackBufferFormat(),
        MATERIAL_RT_DEPTH_ONLY,  // 使用深度纹理，不使用颜色缓冲区
        0,
        CREATERENDERTARGETFLAGS_HDR);
    
    m_pMaterialSystem->EndRenderTargetAllocation();

    if (!m_pStencilTexture)
    {
        printf("[Portal] Failed to create stencil texture!\n");
        return;
    } 
    else 
    {
        printf("[Portal] Created stencil texture successfully\n");
    }
}

// 设置模板缓冲区用于模型遮罩
void L4D2_Portal::SetupStencilForModelMask()
{
    if (!m_pMaterialSystem || !m_pStencilTexture)
    {
        printf("[Portal] Material system or stencil texture not initialized!\n");
        return;
    }

    IMatRenderContext* pRenderContext = m_pMaterialSystem->GetRenderContext();
    if (!pRenderContext)
    {
        printf("[Portal] Failed to get render context!\n");
        return;
    }

    // 保存当前渲染状态
    pRenderContext->PushRenderTargetAndViewport();
    
    try
    {
        // 设置渲染目标为模板纹理
        pRenderContext->SetRenderTarget(m_pStencilTexture);
        
        // 清除模板缓冲区
        pRenderContext->ClearColor4ub(0, 0, 0, 0);
        pRenderContext->ClearBuffers(true, true);
        
        // 创建并配置模板状态
        ShaderStencilState_t stencilState;
        stencilState.m_bEnable = true; // 启用模板测试
        stencilState.m_nReferenceValue = 1; // 参考值设为1
        stencilState.m_CompareFunc = STENCILCOMPARISONFUNCTION_ALWAYS; // 始终通过测试
        stencilState.m_PassOp = STENCILOPERATION_REPLACE; // 通过时替换为参考值
        stencilState.m_FailOp = STENCILOPERATION_KEEP; // 失败时保持原值
        stencilState.m_ZFailOp = STENCILOPERATION_KEEP; // Z失败时保持原值
        stencilState.m_nTestMask = 0xFFFFFFFF; // 测试掩码
        stencilState.m_nWriteMask = 0xFFFFFFFF; // 写入掩码
        
        // 应用模板状态
        pRenderContext->SetStencilState(stencilState);
        
        // 注意：模型加载和渲染由外部脚本处理，此处不再实现
        printf("[Portal] Setup stencil buffer for model mask (model loaded externally)\n");
        
        // 使用m_pPortalMaterial作为模板材质
        if (m_pPortalMaterial)
        {
            printf("[Portal] Using m_pPortalMaterial for stencil mask\n");
        }
        
        // 使用m_pPortalTexture作为纹理
        if (m_pPortalTexture)
        {
            printf("[Portal] Using m_pPortalTexture for stencil mask\n");
        }
        
        // 禁用模板测试
        stencilState.m_bEnable = false;
        pRenderContext->SetStencilState(stencilState);
    }
    catch (...)
    {
        printf("[Portal] Exception in SetupStencilForModelMask\n");
        // 确保禁用模板测试
        ShaderStencilState_t stencilState;
        stencilState.m_bEnable = false;
        pRenderContext->SetStencilState(stencilState);
    }
    
    // 恢复渲染状态
    pRenderContext->PopRenderTargetAndViewport();
}

// 应用模板遮罩到portal渲染
void L4D2_Portal::ApplyStencilMaskToPortal()
{
    if (!m_pMaterialSystem || !m_pPortalTexture || !m_pStencilTexture || !m_bUseStencilMask)
    {
        printf("[Portal] Material system, portal texture, stencil texture not initialized or mask disabled!\n");
        return;
    }

    IMatRenderContext* pRenderContext = m_pMaterialSystem->GetRenderContext();
    if (!pRenderContext)
    {
        printf("[Portal] Failed to get render context!\n");
        return;
    }

    // 保存当前渲染状态
    pRenderContext->PushRenderTargetAndViewport();
    
    try
    {
        // 设置渲染目标为portal纹理
        pRenderContext->SetRenderTarget(m_pPortalTexture);
        
        // 设置模板缓冲区用于模板纹理
        // 注意：在实际的Source引擎中，可能需要不同的方法来共享模板缓冲区
        // 这里只是一个概念性的实现
        
        // 创建并配置模板状态
        ShaderStencilState_t stencilState;
        stencilState.m_bEnable = true; // 启用模板测试
        stencilState.m_nReferenceValue = 1; // 参考值设为1
        stencilState.m_CompareFunc = STENCILCOMPARISONFUNCTION_EQUAL; // 只有当模板值等于参考值时才通过
        stencilState.m_PassOp = STENCILOPERATION_KEEP; // 通过时保持原值
        stencilState.m_FailOp = STENCILOPERATION_KEEP; // 失败时保持原值
        stencilState.m_ZFailOp = STENCILOPERATION_KEEP; // Z失败时保持原值
        stencilState.m_nTestMask = 0xFFFFFFFF; // 测试掩码
        stencilState.m_nWriteMask = 0xFFFFFFFF; // 写入掩码
        
        // 应用模板状态
        pRenderContext->SetStencilState(stencilState);
        
        printf("[Portal] Applied stencil mask to portal\n");
    }
    catch (...)
    {
        printf("[Portal] Exception in ApplyStencilMaskToPortal\n");
        // 确保禁用模板测试
        ShaderStencilState_t stencilState;
        stencilState.m_bEnable = false;
        pRenderContext->SetStencilState(stencilState);
    }
    
    // 注意：这里不恢复渲染状态，因为我们希望在渲染portal时应用遮罩
    // 恢复操作应该在渲染完成后进行
}



// 设置模板缓冲区用于渲染特定模型
void L4D2_Portal::SetStencilForSpecificModel()
{
    if (!m_pMaterialSystem)
    {
        printf("[Portal] Material system not initialized!\n");
        return;
    }

    IMatRenderContext* pRenderContext = m_pMaterialSystem->GetRenderContext();
    if (!pRenderContext)
    {
        printf("[Portal] Failed to get render context!\n");
        return;
    }

    try
    {
        // 清除模板缓冲区
        pRenderContext->ClearBuffers(false, true, false);
        
        // 创建并配置模板状态，用于只渲染特定模型
        ShaderStencilState_t stencilState;
        stencilState.m_bEnable = true; // 启用模板测试
        stencilState.m_nReferenceValue = 1; // 参考值设为1
        stencilState.m_CompareFunc = STENCILCOMPARISONFUNCTION_ALWAYS; // 始终通过模板测试，因为我们只想标记特定模型区域
        stencilState.m_PassOp = STENCILOPERATION_REPLACE; // 通过时替换为参考值
        stencilState.m_FailOp = STENCILOPERATION_KEEP; // 失败时保持原值
        stencilState.m_ZFailOp = STENCILOPERATION_KEEP; // Z失败时保持原值
        stencilState.m_nTestMask = 0xFFFFFFFF; // 测试掩码
        stencilState.m_nWriteMask = 0xFFFFFFFF; // 写入掩码
        
        // 应用模板状态
        pRenderContext->SetStencilState(stencilState);
        
        // 使用m_pPortalMaterial作为特定模型的材质
        if (m_pPortalMaterial)
        {
            printf("[Portal] Setting up stencil for specific model with material: %p\n", m_pPortalMaterial);
        }
        else
        {
            printf("[Portal] Portal material not available!\n");
        }
    }
    catch (...)
    {
        printf("[Portal] Exception in SetStencilForSpecificModel\n");
        // 确保禁用模板测试
        ShaderStencilState_t stencilState;
        stencilState.m_bEnable = false;
        pRenderContext->SetStencilState(stencilState);
    }
}

void L4D2_Portal::FindRenderableForModel()
{
    int modelIndex = I::ModelInfo->GetModelIndex("models/zimu/zimu1_hd.mdl");
    if (modelIndex == -1)
    {
        printf("[Portal] Failed to get model index for models/zimu/zimu1_hd.mdl\n");
        return;
    }

    IClientEntity* pEntity = I::ClientEntityList->GetClientEntity(modelIndex);
    if (!pEntity)
    {
        printf("[Portal] Failed to get client entity for model index %d\n", modelIndex);
        return;
    }

    IClientRenderable* pRenderable = pEntity->GetClientRenderable();
    if (!pRenderable)
    {
        printf("[Portal] Failed to get client renderable for model index %d\n", modelIndex);
        return;
    }

    m_pRenderable = pRenderable;
    printf("[Portal] Found renderable for models/zimu/zimu1_hd.mdl: %p\n", m_pRenderable);
}
