
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

// ����CViewSetup�ṹ����������Ⱦ��ͼ����
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

    // ����512x512����ȾĿ������
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

    try
    {
        // ������ȾĿ����ӿ�
        pRenderContext->PushRenderTargetAndViewport(m_pPortalTexture, 0, 0, 512, 512);
        
        // ���������
        pRenderContext->ClearBuffers(true, true, true);
        
        // ����CViewSetup�ṹ�壨ģ�⴫���ź���ӽǣ�
        CViewSetup viewSetup;
        memset(&viewSetup, 0, sizeof(CViewSetup));
        
        // �����ӿڴ�С
        viewSetup.x = 0;
        viewSetup.y = 0;
        viewSetup.width = 512;
        viewSetup.height = 512;
        viewSetup.m_nUnscaledX = 0;
        viewSetup.m_nUnscaledY = 0;
        viewSetup.m_nUnscaledWidth = 512;
        viewSetup.m_nUnscaledHeight = 512;
        
        // ����͸�Ӳ���
        viewSetup.m_bOrtho = false;
        viewSetup.m_flAspectRatio = 1.0f; // 512x512�Ŀ�߱�Ϊ1:1
        viewSetup.m_flFOV = 90.0f; // ��׼FOV
        viewSetup.m_flNearZ = 4.0f;
        viewSetup.m_flFarZ = 16384.0f;
        
        // ��ȡ��ǰ����ӽǣ���ģ�⴫���ź���ӽ�
        // ����򵥵�ʹ�õ�ǰ��ҵ�λ�úͽǶȣ�����ʵ��ʵ������Ҫ���ݴ����ŵ�λ�úͷ������ת��
        //C_BasePlayer* pLocalPlayer = I::EngineClient->GetLocalPlayer();
        //C_BasePlayer* pLocalPlayer = I::ClientEntityList->GetClientEntity(I::EngineClient->GetLocalPlayer())->As<C_BasePlayer*>();
        C_BasePlayer* pLocalPlayer = nullptr;
        if (pLocalPlayer)
        {
            Vector eyePos;
            QAngle eyeAng {0.0, 0.0, 0.0};
            pLocalPlayer->ViewPunch(eyePos);
            //pLocalPlayer->GetEyeAngles(&eyeAng);
            
            // ����ֻ��һ��ʾ����ʵ��ʵ������Ҫ���ݴ����ŵ�λ�úͷ�������ӽ�
            // ���磺������ӽ���ת180�ȣ�ģ��Ӵ�������һ�࿴���ĳ���
            viewSetup.m_origin = eyePos;
            viewSetup.m_angles = eyeAng;
            
            // Ϊ����ʾЧ�������ӽ���΢ƫ��һ��
            Vector forward;
            //U::Math.AngleVectors(eyeAng, &forward);
            viewSetup.m_origin += forward * 100.0f; // ��ǰ�ƶ�100��λ
        }
        else
        {
            // ���û�б�����ң�ʹ��Ĭ��λ��
            viewSetup.m_origin = Vector(0, 0, 0);
            viewSetup.m_angles = QAngle(0, 0, 0);
        }
        
        // ��ʼ������Ⱦ
        I::RenderView->SceneBegin();
        
        // ����3D��ͼ
        I::RenderView->Push3DView(pRenderContext, &viewSetup, 0, nullptr, nullptr);
        
        // ��Ⱦ���磨����ֻ�ǻ���ʵ�֣�ʵ����Ҫ�����ӵ���Ⱦ�߼���
        // ��ʵ��Source�����У�����Ҫ����DrawWorldLists�Ⱥ���
        // ����Ϊ��ʾ��������ʹ�ü򻯵ķ���
        
        // ����������Ⱦ
        I::RenderView->PopView(pRenderContext, nullptr);
        I::RenderView->SceneEnd();
        
        // ������ȾĿ����ӿ�
        pRenderContext->PopRenderTargetAndViewport();
        
        printf("[Portal] Successfully rendered frame to portal texture\n");
    }
    catch (...)
    {
        // ȷ���ڷ����쳣ʱҲ������ȾĿ����ӿ�
        pRenderContext->PopRenderTargetAndViewport();
        printf("[Portal] Exception occurred during rendering\n");
    }
    
    // �ͷ���Ⱦ������
    pRenderContext->Release();
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


