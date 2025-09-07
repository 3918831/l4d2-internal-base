#include "BaseClient.h"
#include "../../Portal/L4D2_Portal.h"
#include <memory>

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

void __fastcall BaseClient::RenderView::Detour(void* ecx, void* edx, CViewSetup& setup, CViewSetup& hudViewSetup, int nClearFlags, int whatToDraw)
{
	//printf("RenderView Hooked.\n"); //Ŀǰ�Ѿ�hook�ɹ�
	//printf("[Hooked RenderView]: original fov: %f\n", setup.fov);
	//setup.fov = 160.0;
	//printf("[Hooked RenderView]: new fov: %f\n", setup.fov);

	//先调用原函数保证正常游戏场景的渲染
	//Func.Original<FN>()(ecx, edx, setup, hudViewSetup, nClearFlags, whatToDraw);

	if(I::EngineClient->IsInGame()) {
		// 检查材质系统和纹理是否有效
		if (!G::G_L4D2Portal.m_pMaterialSystem || !G::G_L4D2Portal.m_pPortalTexture)
		{
			printf("[Portal] Material system or texture not initialized, skipping render\n");
			return;
		}

		// 获取渲染上下文
		IMatRenderContext* pRenderContext = G::G_L4D2Portal.m_pMaterialSystem->GetRenderContext();
		if (!pRenderContext)
		{
			printf("[Portal] Failed to get render context\n");
			return;
		}
		// 解锁渲染目标分配
        G::G_L4D2Portal.m_pCustomMaterialSystem->UnLockRTAllocation();

		// 保存当前渲染状态
    	pRenderContext->PushRenderTargetAndViewport();

		// 获取并保存当前原始渲染上下文的渲染目标
		ITexture* pOriginalRenderTarget = reinterpret_cast<ITexture*>(pRenderContext->GetRenderTarget()); // 20250907:返回空指针

		// 设置渲染目标为当前纹理
		pRenderContext->SetRenderTarget(G::G_L4D2Portal.m_pPortalTexture);

		// 对目标纹理进行渲染
		Func.Original<FN>()(ecx, edx, setup, hudViewSetup, nClearFlags, whatToDraw);
		
		// 恢复渲染目标(是否有必要?)
		pRenderContext->SetRenderTarget(pOriginalRenderTarget);


		// 恢复渲染状态
    	pRenderContext->PopRenderTargetAndViewport();

		// 清除渲染目标
		// pRenderContext->ClearColor4ub(0, 0, 0, 0);
		// pRenderContext->ClearBuffers(true, true);

		// 保存当前渲染状态
		//pRenderContext->PushRenderTargetAndViewport();

		// 创建临时的CViewSetup结构
		// CViewSetup viewSetup;
		// viewSetup.x = 0;
		// viewSetup.y = 0;
		// viewSetup.width = 512;
		// viewSetup.height = 512;
		// viewSetup.fov = 90;
		// viewSetup.origin = { 0, 0, 0 };
		// viewSetup.angles = { 0, 0, 0 };
		// viewSetup.zNear = 6;
		// viewSetup.zFar = 4096;
	} else {
		printf("[Portal] Not in game.\n");
	}
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