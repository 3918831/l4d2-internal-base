#include <memory>
#include <cassert>
#include "BaseClient.h"
#include "../../Portal/L4D2_Portal.h"
#include "../../SDK/L4D2/Interfaces/RenderView.h"
#include "../../SDK/L4D2/Interfaces/EngineClient.h"
#include "../../SDK/L4D2/Interfaces/ModelInfo.h"
#include "../../SDK/L4D2/Interfaces/ModelRender.h"
#include "../../SDK/L4D2/Includes/const.h"
#include "../../Util/Math/Math.h"
//#include "../../Portal/public/mathlib.h"

//CViewSetup g_ViewSetup;
//CViewSetup g_hudViewSetup;
void* g_pClient_this_ptr = nullptr; // 用于存储 RenderView 的真实 this 指针


// 在你的代码顶部或常量定义文件中，定义一个偏移量
const float PORTAL_CAMERA_OFFSET = 0.0f; // 你可以调整这个值，比如1.0, 2.0, 5.0等，找到最合适的大小

/*
 * 计算传送门虚拟摄像机的视角
 * @param playerView      - 当前玩家的原始CViewSetup
 * @param pEntrancePortal - 玩家正在“看向”的传送门 (入口)
 * @param pExitPortal     - 应该从哪个传送门“看出去” (出口)
 * @return                - 一个新的、计算好的CViewSetup，用于渲染
 */
CViewSetup CalculatePortalView(const CViewSetup& playerView, const PortalInfo_t* pEntrancePortal, const PortalInfo_t* pExitPortal)
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
	portalView.origin += exitPortalNormal * 2.0f;

	return portalView;
}

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
	if (!g_pClient_this_ptr) {
		g_pClient_this_ptr = ecx;
	}
#ifdef PLAN_B
	// 只有在游戏内且传送门系统初始化后才执行
	if (I::EngineClient->IsInGame() && G::G_L4D2Portal.m_pMaterialSystem
		&& G::G_L4D2Portal.m_pPortalTexture && G::G_L4D2Portal.m_pPortalTexture_2
		&& G::G_L4D2Portal.g_BluePortal.bIsActive && G::G_L4D2Portal.g_OrangePortal.bIsActive)
	{
		IMatRenderContext* pRenderContext = G::G_L4D2Portal.m_pMaterialSystem->GetRenderContext();
		if (pRenderContext)
		{
			// --- 渲染蓝门内的场景 (裁剪平面在橙门) ---
			{
				// 边界条件检查: 玩家必须在蓝门前方且大致朝向蓝门
				// (这部分逻辑是好的优化，保持)
				Vector vPlayerFwd, vBluePortalFwd, vToPlayer;
				U::Math.AngleVectors(setup.angles, &vPlayerFwd, nullptr, nullptr);
				U::Math.AngleVectors(G::G_L4D2Portal.g_BluePortal.angles, &vBluePortalFwd, nullptr, nullptr);
				vToPlayer = setup.origin - G::G_L4D2Portal.g_BluePortal.origin;
				U::Math.VectorNormalize(vToPlayer); // 归一化以获得纯方向

				if (/*DotProduct(vToPlayer, vBluePortalFwd) >= 0.0f*/ true)
				{
					pRenderContext->PushRenderTargetAndViewport();
					pRenderContext->SetRenderTarget(G::G_L4D2Portal.m_pPortalTexture);
					pRenderContext->Viewport(0, 0, setup.width, setup.height);
					pRenderContext->ClearBuffers(true, true, true);

					// --- 定义出口（橙门）的裁剪平面 ---
					Vector exitNormal_O;
					U::Math.AngleVectors(G::G_L4D2Portal.g_OrangePortal.angles, &exitNormal_O, nullptr, nullptr);

					float clipPlane_O[4];
					clipPlane_O[0] = exitNormal_O.x;
					clipPlane_O[1] = exitNormal_O.y;
					clipPlane_O[2] = exitNormal_O.z;

					// 【核心修正】修正 D 值的计算，遵循 n·p - d = 0 的形式
					// d = n·p，并加入一个小的偏移量防止瑕疵
					clipPlane_O[3] = DotProduct(G::G_L4D2Portal.g_OrangePortal.origin, exitNormal_O) - 0.5f;

					CViewSetup portalView = CalculatePortalView(setup, &G::G_L4D2Portal.g_BluePortal, &G::G_L4D2Portal.g_OrangePortal);

					pRenderContext->PushCustomClipPlane(clipPlane_O);
					pRenderContext->EnableClipping(true);


					VisibleFogVolumeInfo_t fog_1;
					I::CustomRender->GetVisibleFogVolumeInfo(G::G_L4D2Portal.g_OrangePortal.origin, fog_1);
					WaterRenderInfo_t water_1;
					I::CustomView->DetermineWaterRenderInfo(&fog_1, &water_1);


					//I::RenderView->Push3DView(pRenderContext, &portalView, 0, G::G_L4D2Portal.m_pPortalTexture, &G::G_L4D2Portal.m_Frustum);
					I::CustomRender->Push3DView(portalView, 0, G::G_L4D2Portal.m_pPortalTexture, I::CustomView->GetFrustum(), nullptr);
					// 递归调用原函数进行渲染, 不渲染玩家模型和HUD
					//Func.Original<FN>()(ecx, edx, portalView, hudViewSetup, nClearFlags, whatToDraw & (~RENDERVIEW_DRAWVIEWMODEL) & (~RENDERVIEW_DRAWHUD));
					//Func.Original<FN>()(ecx, edx, portalView, hudViewSetup, 0, RENDERVIEW_UNSPECIFIED);
					I::CustomView->DrawWorldAndEntities(true, portalView, nClearFlags, &fog_1, &water_1);
					//I::RenderView->PopView(pRenderContext, &G::G_L4D2Portal.m_Frustum);
					I::CustomRender->PopView(I::CustomView->GetFrustum());


					pRenderContext->EnableClipping(false);
					pRenderContext->PopCustomClipPlane();
					pRenderContext->PopRenderTargetAndViewport();
				}
			}

			// --- 渲染橙门内的场景 (裁剪平面在蓝门) ---
			{
				// 对橙门执行相同的边界条件检查
				Vector vPlayerFwd, vOrangePortalFwd, vToPlayer;
				U::Math.AngleVectors(setup.angles, &vPlayerFwd, nullptr, nullptr);
				U::Math.AngleVectors(G::G_L4D2Portal.g_OrangePortal.angles, &vOrangePortalFwd, nullptr, nullptr);
				vToPlayer = setup.origin - G::G_L4D2Portal.g_OrangePortal.origin;
				U::Math.VectorNormalize(vToPlayer);

				if (/*DotProduct(vToPlayer, vOrangePortalFwd) >= 0.0f*/ true)
				{
					pRenderContext->PushRenderTargetAndViewport();
					pRenderContext->SetRenderTarget(G::G_L4D2Portal.m_pPortalTexture_2);
					pRenderContext->Viewport(0, 0, setup.width, setup.height);
					pRenderContext->ClearBuffers(true, true, true);

					// --- 定义出口（蓝门）的裁剪平面 ---
					Vector exitNormal_B;
					U::Math.AngleVectors(G::G_L4D2Portal.g_BluePortal.angles, &exitNormal_B, nullptr, nullptr);

					float clipPlane_B[4];
					clipPlane_B[0] = exitNormal_B.x;
					clipPlane_B[1] = exitNormal_B.y;
					clipPlane_B[2] = exitNormal_B.z;

					// 【核心修正】修正 D 值的计算
					clipPlane_B[3] = DotProduct(G::G_L4D2Portal.g_BluePortal.origin, exitNormal_B) - 0.5f;

					CViewSetup portalView2 = CalculatePortalView(setup, &G::G_L4D2Portal.g_OrangePortal, &G::G_L4D2Portal.g_BluePortal);


					pRenderContext->PushCustomClipPlane(clipPlane_B);
					pRenderContext->EnableClipping(true);

					VisibleFogVolumeInfo_t fog_2;
					I::CustomRender->GetVisibleFogVolumeInfo(G::G_L4D2Portal.g_OrangePortal.origin, fog_2);
					WaterRenderInfo_t water_2;
					I::CustomView->DetermineWaterRenderInfo(&fog_2, &water_2);

					//I::RenderView->Push3DView(pRenderContext, &portalView2, 0, G::G_L4D2Portal.m_pPortalTexture_2, &G::G_L4D2Portal.m_Frustum);
					I::CustomRender->Push3DView(portalView2, 0, G::G_L4D2Portal.m_pPortalTexture_2, I::CustomView->GetFrustum(), nullptr);
					//Func.Original<FN>()(ecx, edx, portalView2, hudViewSetup, nClearFlags, whatToDraw & (~RENDERVIEW_DRAWVIEWMODEL) & (~RENDERVIEW_DRAWHUD));
					//Func.Original<FN>()(ecx, edx, portalView2, hudViewSetup, 0, RENDERVIEW_UNSPECIFIED);
					I::CustomView->DrawWorldAndEntities(true, portalView2, nClearFlags, &fog_2, &water_2);
					//I::RenderView->PopView(pRenderContext, &G::G_L4D2Portal.m_Frustum);
					I::CustomRender->PopView(I::CustomView->GetFrustum());

					pRenderContext->EnableClipping(false);
					pRenderContext->PopCustomClipPlane();
					pRenderContext->PopRenderTargetAndViewport();
				}
			}
		}
	}
#endif
	// 正常情况下调用原函数，渲染玩家的主视角
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