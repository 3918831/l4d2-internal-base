
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
    // ����512x512����ȾĿ������
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

    // ʹ��FindMaterial������Ϸ���ò���
    g_pPortalMaterial = m_pMaterialSystem->FindMaterial("models/zimu/zimu1_hd/zimu1_hd", TEXTURE_GROUP_MODEL, true, nullptr);

    if (!g_pPortalMaterial)
    {
        printf("[Portal] Failed to find portal material!\n");
        return;
    }

    printf("[Portal] Found portal material successfully\n");

    // ���ò���Ϊ�ɻ���״̬
    g_pPortalMaterial->IncrementReferenceCount();

    // ���Ҳ����û����������
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
        // ���Բ���diffusemap��Ϊ���
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

    // ���ò���Ϊ��͸��
    IMaterialVar* pTranslucentVar = g_pPortalMaterial->FindVar("$translucent", NULL, false);
    if (pTranslucentVar)
    {
        pTranslucentVar->SetIntValue(1);
    }

    // ����Z���������ԣ�����͸���������忴������������
    IMaterialVar* pIgnoreZVar = g_pPortalMaterial->FindVar("$ignorez", NULL, false);
    if (pIgnoreZVar)
    {
        pIgnoreZVar->SetIntValue(1);
    }
}

void L4D2_Portal::PortalInit()
{
    // ��ȡ����ϵͳ�ӿ�
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

    // ������ȾĿ������
    CreatePortalTexture();
    
    // ��������Ҳ���
    CreatePortalMaterial();

    // ��ʼ����ɺ󣬿��Ե���RenderPortalFrame������Ⱦ
    printf("[Portal] Initialization completed\n");

    RenderPortalFrame();
}

// ��Ⱦ����������������
void L4D2_Portal::RenderPortalFrame()
{
    // ������ϵͳ�������Ƿ���Ч
    if (!m_pMaterialSystem || !m_pPortalTexture)
    {
        printf("[Portal] Material system or texture not initialized, skipping render\n");
        return;
    }

    // ��ȡ��Ⱦ������
    IMatRenderContext* pRenderContext = m_pMaterialSystem->GetRenderContext();
    if (!pRenderContext)
    {
        printf("[Portal] Failed to get render context\n");
        return;
    }
    // ���浱ǰ��Ⱦ״̬
    //pRenderContext->PushRenderTargetAndViewport();

    // ������ʱ��CViewSetup�ṹ
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

    // ������ȾĿ��Ϊ����������
    //pRenderContext->SetRenderTarget(m_pPortalTexture);
    //I::BaseClient->RenderView(&viewSetup, 2 | 1, 0);
    
    // �ָ���Ⱦ״̬
    //pRenderContext->PopRenderTargetAndViewport();
    //pRenderContext->Release();
}

// ���������ڲ���Ҫ������ʱ����
void L4D2_Portal::PortalShutdown()
{
    if (g_pPortalMaterial)
    {
        g_pPortalMaterial->DecrementReferenceCount();
        g_pPortalMaterial = nullptr;
    }
    
    if (m_pPortalTexture)
    {
        // Source�����е�����ͨ���ɲ���ϵͳ��������Ҫ�ֶ��ͷ�
        m_pPortalTexture = nullptr;
    }
    
    m_pMaterialSystem = nullptr;
    m_pCustomMaterialSystem = nullptr;
    
    printf("[Portal] Shutdown completed\n");
}


