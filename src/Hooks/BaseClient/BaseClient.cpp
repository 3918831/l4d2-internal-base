#include "BaseClient.h"
#include "../../Portal/L4D2_Portal.h"
#include "../../SDK/L4D2/Interfaces/RenderView.h"
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
	// 添加静态标志防止递归渲染
	static bool bIsRenderingPortal = false;
	
	// 正常游戏渲染路径
	if (!bIsRenderingPortal)
	{
		// 检查是否在游戏中且需要渲染portal
		if (I::EngineClient->IsInGame() && G::G_L4D2Portal.m_pMaterialSystem && G::G_L4D2Portal.m_pPortalTexture)
		{
			// 获取渲染上下文
			IMatRenderContext* pRenderContext = G::G_L4D2Portal.m_pMaterialSystem->GetRenderContext();
			if (pRenderContext)
			{
				// 设置递归保护标志，准备渲染portal
				bIsRenderingPortal = true;
				
				// 保存当前渲染状态
				pRenderContext->PushRenderTargetAndViewport();

				try
					{
						// 设置渲染目标为portal纹理
						pRenderContext->SetRenderTarget(G::G_L4D2Portal.m_pPortalTexture);

						// 设置视口大小为纹理大小
						int textureWidth = G::G_L4D2Portal.m_pPortalTexture->GetActualWidth();
						int textureHeight = G::G_L4D2Portal.m_pPortalTexture->GetActualHeight();
						pRenderContext->Viewport(0, 0, textureWidth, textureHeight);

						// 确保深度缓冲区正确设置
						//pRenderContext->DepthRange(0.0f, 1.0f);
						//pRenderContext->OverrideDepthEnable(true, true);

						// 清除渲染目标和深度缓冲区
						//pRenderContext->ClearColor4ub(0, 0, 0, 0);
						//pRenderContext->ClearBuffers(true, true);

						// 使用游戏原始的whatToDraw参数确保渲染所有内容
						Func.Original<FN>()(ecx, edx, setup, hudViewSetup, nClearFlags, whatToDraw);
						
						// 重置强制材质覆盖
						I::ModelRender->ForcedMaterialOverride(nullptr);
					}
				catch (...)
				{
					// 发生异常时确保恢复状态
				}

				// 恢复渲染状态
				pRenderContext->PopRenderTargetAndViewport();
				
				// 清除递归保护标志
				bIsRenderingPortal = false;
			}
		}

		// 调用原函数渲染主场景
		Func.Original<FN>()(ecx, edx, setup, hudViewSetup, nClearFlags, whatToDraw);
	}
	else
	{
		// 递归调用路径 - 直接调用原始函数不进行portal渲染
		Func.Original<FN>()(ecx, edx, setup, hudViewSetup, nClearFlags, whatToDraw);
	}

		// 清除渲染目标
		// pRenderContext->ClearColor4ub(0, 0, 0, 0);
		// pRenderContext->ClearBuffers(true, true);

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
	// } else {
	// 	// printf("[Portal] Not in game.\n");
	// }
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