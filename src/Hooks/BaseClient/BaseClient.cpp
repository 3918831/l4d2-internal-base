#include <memory>
#include <cassert>
#include "BaseClient.h"
// #include "../ModelRender/ModelRender.h"
#include "../Hooks.h"
#include "../../Portal/L4D2_Portal.h"
#include "../../SDK/L4D2/Interfaces/RenderView.h"
#include "../../SDK/L4D2/Interfaces/EngineClient.h"
#include "../../SDK/L4D2/Interfaces/ModelInfo.h"
#include "../../SDK/L4D2/Interfaces/ModelRender.h"
#include "../../SDK/L4D2/Interfaces/IVEngineServer.h"
#include "../../SDK/L4D2/Includes/const.h"
#include "../../Util/Math/Math.h"
//#include "../../Portal/public/mathlib.h"

//CViewSetup g_ViewSetup;
//CViewSetup g_hudViewSetup;
void* g_pClient_this_ptr = nullptr; // 用于存储 RenderView 的真实 this 指针

using namespace Hooks;

void __fastcall BaseClient::LevelInitPreEntity::Detour(void* ecx, void* edx, char const* pMapName)
{
	// 先调用原始函数
	Table.Original<FN>(Index)(ecx, edx, pMapName);

	printf("[BaseClient] LevelInitPreEntity: %s\n", pMapName);

	// 预载传送门模型
	if (I::EngineServer)
	{
		printf("[BaseClient] Precaching portal models...\n");

		// 预载橙色传送门模型
		int index1 = I::EngineServer->PrecacheModel("models/blackops/portal_og.mdl", true);
		printf("[BaseClient] Precached models/blackops/portal_og.mdl, index: %d\n", index1);

		// 预载蓝色传送门模型
		int index2 = I::EngineServer->PrecacheModel("models/blackops/portal.mdl", true);
		printf("[BaseClient] Precached models/blackops/portal.mdl, index: %d\n", index2);

		if (index1 > 0 && index2 > 0)
		{
			printf("[BaseClient] Both portal models precached successfully!\n");
		}
		else
		{
			printf("[BaseClient] WARNING: Failed to precache one or more portal models!\n");
		}
	}
	else
	{
		printf("[BaseClient] WARNING: EngineServer interface is null, cannot precache models!\n");
	}
}

void __fastcall BaseClient::LevelInitPostEntity::Detour(void* ecx, void* edx)
{
	// 先调用原始函数
	Table.Original<FN>(Index)(ecx, edx);

	printf("[BaseClient] LevelInitPostEntity: Initializing portal system...\n");

	// 地图加载完成后初始化传送门系统
	// 这样每次切换地图都会重新初始化
	G::G_L4D2Portal.PortalInit();

	printf("[BaseClient] Portal system initialized.\n");
}

void __fastcall BaseClient::LevelShutdown::Detour(void* ecx, void* edx)
{
	printf("[BaseClient] LevelShutdown: Cleaning up portal system...\n");

	// 地图退出前清理传送门资源
	// 这样可以释放旧地图的资源，避免泄漏
	G::G_L4D2Portal.PortalShutdown();

	// 调用原始函数
	Table.Original<FN>(Index)(ecx, edx);

	printf("[BaseClient] Portal system cleaned up.\n");
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

#ifdef RECURSIVE_RENDERING
// RECURSIVE_RENDERING_0: 这个实现是为了配合在DrawModelExecute中绘制传送门纹理的写法, 但是在DrawModelExecute中绘制纹理存在的问题就是
// 到了某个模型绘制阶段才去绘制整张纹理, 是有点晚了的, 现象就是第一帧传送门绘制会卡顿, 新的方案改为从RenderView阶段就去绘制传送门纹理(如果两个传送门都被创建了的话)
// 本质上, 绘制传送门只需要两个要素: 两扇传送门均被创建& 人物当前视角, 此时就可以唯一确定传送门后的纹理内容了
// 当前可以使用, 但是在RenderPortalViewRecursive存在一定视角丢模型问题待解决
void __fastcall BaseClient::RenderView::Detour(void* ecx, void* edx, CViewSetup& setup, CViewSetup& hudViewSetup, int nClearFlags, int whatToDraw)
{
	// 每帧开始时，重置状态
	G::G_L4D2Portal.m_nPortalRenderDepth = 0;
	G::G_L4D2Portal.m_vViewStack.clear();

	// 将玩家的主视角作为栈底（第0层）
	G::G_L4D2Portal.m_vViewStack.push_back(setup);

	// 保存当前帧的标志
	G::G_L4D2Portal.m_nClearFlags = nClearFlags;

	// 调用原始函数。当它内部渲染到传送门时，会触发 DrawModelExecute
	Func.Original<FN>()(ecx, edx, setup, hudViewSetup, nClearFlags, whatToDraw);
}
#endif

#ifdef RECURSIVE_RENDERING_0 //Recursive-Rendering指的是支持迭代式计算门中门场景的方式,主要绘制逻辑已经不能放在RenderView函数中了
// g_bIsRenderingPortalTexture 已在 Hooks.h 中声明

void __fastcall Hooks::BaseClient::RenderView::Detour(void* ecx, void* edx, CViewSetup& setup, CViewSetup& hudViewSetup, int nClearFlags, int whatToDraw)
{
    // 1. 如果已经在渲染传送门纹理，或者是递归保护，直接调用原始函数
    // if (g_bIsRenderingPortalTexture || !I::EngineClient->IsInGame()) {
    //     Func.Original<FN>()(ecx, edx, setup, hudViewSetup, nClearFlags, whatToDraw);
    //     return;
    // }

    // 2. 每帧初始化状态
    G::G_L4D2Portal.m_nPortalRenderDepth = 0;
    G::G_L4D2Portal.m_vViewStack.clear();
    G::G_L4D2Portal.m_vViewStack.push_back(setup);
    G::G_L4D2Portal.m_nClearFlags = nClearFlags;

    // 3. 方案 B：反馈回路预渲染
    // 只有当两扇门都激活时才进行渲染
    if (G::G_L4D2Portal.g_BluePortal.bIsActive && G::G_L4D2Portal.g_OrangePortal.bIsActive) 
    {
        g_bIsRenderingPortalTexture = true; // 开启保护

        // 渲染流程：
        // A. 计算并渲染从“橙门”看出去，显示在“蓝门”表面的画面
        G::G_L4D2Portal.RenderViewToTexture(ecx, edx, setup, 
            &G::G_L4D2Portal.g_BluePortal,   // 入口 (我们要画在这个门上)
            &G::G_L4D2Portal.g_OrangePortal, // 出口 (相机从这里射出去)
            G::G_L4D2Portal.m_pPortalTexture_Blue);

        // B. 计算并渲染从“蓝门”看出去，显示在“橙门”表面的画面
        G::G_L4D2Portal.RenderViewToTexture(ecx, edx, setup, 
            &G::G_L4D2Portal.g_OrangePortal, // 入口
            &G::G_L4D2Portal.g_BluePortal,   // 出口
            G::G_L4D2Portal.m_pPortalTexture_Orange);

        g_bIsRenderingPortalTexture = false; // 解除保护
    }


	// =================================================================================================
	// 以下代码用于测试近平面裁剪

	// IMatRenderContext* pRenderContext = I::MaterialSystem->GetRenderContext();
    // if (!pRenderContext) return;
    // Vector exitNormal;
    // U::Math.AngleVectors(setup.angles, &exitNormal, nullptr, nullptr);
    // float clipPlane[4];
    // clipPlane[0] = exitNormal.x;
    // clipPlane[1] = exitNormal.y;
    // clipPlane[2] = exitNormal.z;
    // clipPlane[3] = DotProduct(setup.origin, exitNormal) + 200.0f;
	// pRenderContext->PushCustomClipPlane(clipPlane);
    // pRenderContext->EnableClipping(true);

    // Func.Original<FN>()(ecx, edx, setup, hudViewSetup, nClearFlags, whatToDraw);

	// pRenderContext->EnableClipping(false);
    // pRenderContext->PopCustomClipPlane();
	// =================================================================================================

	Func.Original<FN>()(ecx, edx, setup, hudViewSetup, nClearFlags, whatToDraw);
}
#endif

void BaseClient::Init()
{
	printf("[BaseClient] Initializing BaseClient hooks...\n");

	XASSERT(Table.Init(I::BaseClient) == false);
	XASSERT(Table.Hook(&LevelInitPreEntity::Detour, LevelInitPreEntity::Index) == false);
	XASSERT(Table.Hook(&LevelInitPostEntity::Detour, LevelInitPostEntity::Index) == false);
	XASSERT(Table.Hook(&LevelShutdown::Detour, LevelShutdown::Index) == false);
	XASSERT(Table.Hook(&FrameStageNotify::Detour, FrameStageNotify::Index) == false);
	//XASSERT(Table.Hook(&RenderView::Detour, RenderView::Index) == false);

	printf("[BaseClient] Hooks installed successfully.\n");

	//RenderView
	{
		using namespace RenderView;

		const FN pfRenderView = reinterpret_cast<FN>(U::Offsets.m_dwRenderView);
		printf("[BaseClient] RenderView: %p\n", pfRenderView);
		XASSERT(pfRenderView == nullptr);

		if (pfRenderView)
			XASSERT(Func.Init(pfRenderView, &Detour) == false);
	}

	// 【关键修复】检查游戏是否已经在地图中
	// 如果 DLL 是在游戏运行时注入的，LevelInitPostEntity 已经不会被调用了
	// 所以需要检查并手动初始化传送门系统
	if (I::EngineClient)
	{
		if (I::EngineClient->IsInGame())
		{
			printf("[BaseClient] Detected injection during gameplay. Manually initializing portal system...\n");
			printf("[BaseClient] Current map: %s\n", I::EngineClient->GetLevelName());
			G::G_L4D2Portal.PortalInit();
			printf("[BaseClient] Portal system manually initialized.\n");
		}
		else
		{
			printf("[BaseClient] Not in game. Portal system will be initialized when map loads.\n");
		}
	}
	else
	{
		printf("[BaseClient] WARNING: EngineClient not available during Init().\n");
	}
}