#include "Entry.h"
#include <iostream>

void CGlobal_ModuleEntry::Run()
{
	if (!I::EngineClient->IsInGame() || I::EngineVGui->IsGameUIVisible())
		return;

	C_TerrorPlayer* pLocal = I::ClientEntityList->GetClientEntity(I::EngineClient->GetLocalPlayer())->As<C_TerrorPlayer*>();
	/*while (1) {*/
	if (pLocal && !pLocal->deadflag())
	{
		C_TerrorWeapon* pWeapon = pLocal->GetActiveWeapon()->As<C_TerrorWeapon*>();

		if (pWeapon)
		{
			const char* weapon_name = pWeapon->GetName();

			//G::Draw.String(EFonts::DEBUG, 5, 205, { 204, 204, 204, 255 }, TXT_DEFAULT, weapon_name);
		}
	}
	//}

}

void CGlobal_ModuleEntry::Load()
{
	AllocConsole();
	freopen("CONIN$", "r", stdin); // makes it possible to output to output to console with cout.
	freopen("CONOUT$", "w", stdout);

	while (!GetModuleHandleA("serverbrowser.dll"))
	    std::this_thread::sleep_for(std::chrono::seconds(1));

	U::Offsets.Init();

	//Interfaces
	{
		I::BaseClient       = U::Interface.Get<IBaseClientDLL*>("client.dll", "VClient016");
		std::cout << "BaseClient: " << I::BaseClient << std::endl;

		I::ClientEntityList = U::Interface.Get<IClientEntityList*>("client.dll", "VClientEntityList003");
        std::cout << "ClientEntityList: " << I::ClientEntityList << std::endl;

		I::Prediction       = U::Interface.Get<IPrediction*>("client.dll", "VClientPrediction001");
        std::cout << "Prediction: " << I::Prediction << std::endl;

		I::GameMovement     = U::Interface.Get<IGameMovement*>("client.dll", "GameMovement001");
        std::cout << "GameMovement: " << I::GameMovement << std::endl;

		I::EngineClient     = U::Interface.Get<IVEngineClient*>("engine.dll", "VEngineClient013");
        std::cout << "EngineClient: " << I::EngineClient << std::endl;

		I::EngineTrace      = U::Interface.Get<IEngineTrace*>("engine.dll", "EngineTraceClient003");
        std::cout << "EngineTrace: " << I::EngineTrace << std::endl;

		I::EngineVGui       = U::Interface.Get<IEngineVGui*>("engine.dll", "VEngineVGui001");
		std::cout << "EngineVGui: " << I::EngineVGui << std::endl;

		I::RenderView       = U::Interface.Get<IVRenderView*>("engine.dll", "VEngineRenderView013");
        std::cout << "RenderView: " << I::RenderView << std::endl;

		I::DebugOverlay     = U::Interface.Get<IVDebugOverlay*>("engine.dll", "VDebugOverlay003");
        std::cout << "DebugOverlay: " << I::DebugOverlay << std::endl;

		I::ModelInfo        = U::Interface.Get<IVModelInfo*>("engine.dll", "VModelInfoClient004");
        std::cout << "ModelInfo: " << I::ModelInfo << std::endl;

		I::ModelRender      = U::Interface.Get<IVModelRender*>("engine.dll", "VEngineModel016");
        std::cout << "ModelRender: " << I::ModelRender << std::endl;

		I::VGuiPanel        = U::Interface.Get<IVGuiPanel*>("vgui2.dll", "VGUI_Panel009");
        std::cout << "VGuiPanel: " << I::VGuiPanel << std::endl;

		I::VGuiSurface      = U::Interface.Get<IVGuiSurface*>("vgui2.dll", "VGUI_Surface031");
        std::cout << "VGuiSurface: " << I::VGuiSurface << std::endl;

		I::MatSystemSurface = U::Interface.Get<IMatSystemSurface*>("vguimatsurface.dll", "VGUI_Surface031");
		std::cout << "MatSystemSurface: " << I::MatSystemSurface << std::endl;

		I::MaterialSystem   = U::Interface.Get<IMaterialSystem*>("materialsystem.dll", "VMaterialSystem080");

		{
			I::ClientMode = **reinterpret_cast<void***>(U::Offsets.m_dwClientMode);
			XASSERT(I::ClientMode == nullptr);

			I::GlobalVars = **reinterpret_cast<CGlobalVarsBase***>(U::Offsets.m_dwGlobalVars);
			XASSERT(I::GlobalVars == nullptr);

			I::MoveHelper = **reinterpret_cast<IMoveHelper***>(U::Offsets.m_dwMoveHelper);
			XASSERT(I::MoveHelper == nullptr);
		}
	}

	G::Draw.Init();
	G::Hooks.Init();
	Run();
}