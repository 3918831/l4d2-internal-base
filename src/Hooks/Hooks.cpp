#include "Hooks.h"

using namespace Hooks;

void CGlobal_Hooks::Init()
{
	const MH_STATUS MH_INIT_STATUS = MH_Initialize();
	XASSERT(MH_INIT_STATUS != MH_STATUS::MH_OK);

	if (MH_INIT_STATUS == MH_STATUS::MH_OK)
	{
		BaseClient::Init();
		BasePlayer::Init();
		CL_Main::Init();
		ClientMode::Init(); //启用这条hook目前导致游戏出现异常:人物不停跳跃和右键
		ClientPrediction::Init();
		EngineVGui::Init();
		ModelRender::Init();
		ModelRenderSystem::Init();
		SequenceTransitioner::Init();
		TerrorGameRules::Init();
		TerrorPlayer::Init();
		WndProc::Init();
		CCSGameMovement::Init();

		//增加对Pistol的Hook
		Pistol::Init();

		//增加对EngineServer的Hook
		EngineServer::Init();

		//增加对EngineTrace的Hook
		EngineTrace::Init();

		//增加对RenderView的Hook
		RenderView::Init();
	}

	XASSERT(MH_EnableHook(MH_ALL_HOOKS) != MH_STATUS::MH_OK);
}

#ifdef RECURSIVE_RENDERING
// 全局标志定义
bool g_bIsRenderingPortalTexture = false;
#endif