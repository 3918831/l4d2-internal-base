#include "BaseClient.h"
#include "../../Portal/L4D2_Portal.h"
#include "../../SDK/L4D2/Interfaces/RenderView.h"
#include "../../SDK/L4D2/Interfaces/EngineClient.h"
#include "../../SDK/L4D2/Interfaces/ModelInfo.h"
#include "../../SDK/L4D2/Interfaces/ModelRender.h"
#include "../../SDK/L4D2/Includes/const.h"
#include <memory>

CViewSetup g_ViewSetup;
CViewSetup g_hudViewSetup;
void* g_pClient_this_ptr = nullptr; // 用于存储 RenderView 的真实 this 指针
using namespace Hooks;

void __fastcall BaseClient::LevelInitPreEntity::Detour(void* ecx, void* edx, char const* pMapName)
{
	Table.Original<FN>(Index)(ecx, edx, pMapName);
}

void __fastcall BaseClient::LevelInitPostEntity::Detour(void* ecx, void* edx)
{
	Table.Original<FN>(Index)(ecx, edx);
}

void __fastcall BaseClient::LevelShutdown::Detour(void* ecx, void* edx)
{
	Table.Original<FN>(Index)(ecx, edx);
}

void __fastcall BaseClient::FrameStageNotify::Detour(void* ecx, void* edx, ClientFrameStage_t curStage)
{
	Table.Original<FN>(Index)(ecx, edx, curStage);
}

//void __fastcall BaseClient::RenderView::Detour(void* ecx, void* edx, CViewSetup& setup, int nClearFlags, int whatToDraw)
//{
//	printf("RenderView Hooked.\n");
//	Table.Original<FN>(Index)(ecx, edx, setup, nClearFlags, whatToDraw);
//}

// 全局标志，需要在ModelRender.cpp中声明
//extern bool g_bIsRenderingPortalTexture;
void __fastcall BaseClient::RenderView::Detour(void* ecx, void* edx, CViewSetup& setup, CViewSetup& hudViewSetup, int nClearFlags, int whatToDraw)
{
	// 如果我们还没有捕获过这个指针，就现在捕获它
	// 这个操作只会在游戏过程中发生一次

	if (!g_pClient_this_ptr) {
		g_pClient_this_ptr = ecx;
	}

	g_ViewSetup = setup;
	g_hudViewSetup = hudViewSetup;

#ifdef PLAN_B //PLAN_B: 指在RenderView中绘制纹理，在DrawModelExecute中使用模板测试和纹理来绘制传送门内的场景
	// 检查是否在游戏中且需要渲染portal
	if (I::EngineClient->IsInGame() && G::G_L4D2Portal.m_pMaterialSystem && G::G_L4D2Portal.m_pPortalTexture)
	{

		// 获取渲染上下文
		IMatRenderContext* pRenderContext = G::G_L4D2Portal.m_pMaterialSystem->GetRenderContext();
		if (pRenderContext)
		{
			// 保存当前渲染状态
			//pRenderContext->PushRenderTargetAndViewport(G::G_L4D2Portal.m_pPortalTexture);
			pRenderContext->PushRenderTargetAndViewport();			
			pRenderContext->SetRenderTarget(G::G_L4D2Portal.m_pPortalTexture);
			//pRenderContext->CopyRenderTargetToTextureEx(G::G_L4D2Portal.m_pPortalTexture, 0, NULL, NULL);
			//pRenderContext->ClearColor4ub(0, 0, 255, 255);
			//pRenderContext->ClearBuffers(true, false, false);

			CViewSetup portalView = g_ViewSetup; // 创建一个副本进行修改
			portalView.angles.y += 180.0f; // 仅修改副本
			portalView.origin.x = 0.0f;
			portalView.origin.y = 0.0f;
			portalView.origin.z = 0.0f;

			Func.Original<FN>()(ecx, edx, portalView, hudViewSetup, nClearFlags, whatToDraw & (~RENDERVIEW_DRAWVIEWMODEL) & (~RENDERVIEW_DRAWHUD));

			// 恢复渲染状态
			pRenderContext->PopRenderTargetAndViewport();
			//return; // 直接返回，不执行原函数的完整渲染
			//pRenderContext->DrawScreenSpaceQuad(G::G_L4D2Portal.m_pPortalMaterial);
		}		
	}
#endif

	// 正常情况下调用原函数
	Func.Original<FN>()(ecx, edx, setup, hudViewSetup, nClearFlags, whatToDraw);
}

void BaseClient::Init()
{
	XASSERT(Table.Init(I::BaseClient) == false);
	XASSERT(Table.Hook(&LevelInitPreEntity::Detour, LevelInitPreEntity::Index) == false);
	XASSERT(Table.Hook(&LevelInitPostEntity::Detour, LevelInitPostEntity::Index) == false);
	XASSERT(Table.Hook(&LevelShutdown::Detour, LevelShutdown::Index) == false);
	XASSERT(Table.Hook(&FrameStageNotify::Detour, FrameStageNotify::Index) == false);
	//XASSERT(Table.Hook(&RenderView::Detour, RenderView::Index) == false);

	//RenderView
	{
		using namespace RenderView;

		const FN pfRenderView = reinterpret_cast<FN>(U::Offsets.m_dwRenderView);
		printf("[BaseClient] RenderView: %p\n", pfRenderView);
		XASSERT(pfRenderView == nullptr);

		if (pfRenderView)
			XASSERT(Func.Init(pfRenderView, &Detour) == false);
	}
}