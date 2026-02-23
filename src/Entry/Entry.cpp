#include <iostream>
#include "Entry.h"
#include "../SDK/L4D2/Interfaces/ICvar.h"
#include "../SDK/L4D2/Interfaces/IInput.h"
#include "../SDK/L4D2/Interfaces/Vphysics.h"
#include "../SDK/L4D2/Interfaces/CServerTools.h"
#include "../SDK/L4D2/Interfaces/IVEngineServer.h"
#include "../Portal/L4D2_Portal.h"
#include "../Util/CVar/CVarManager.h"
#include "../Util/Logger/Logger.h"
#include "../Features/TestCVar/TestCVar.h"


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
	auto CvarPtr = I::Cvar->FindCommandBase("sv_cheats");
	U::LogDebug("I::Cvar->FindCommandBase of sv_cheats: %p\n", CvarPtr);

	auto CvarPtr2 = I::Cvar->FindCommandBase("god");
	U::LogDebug("I::Cvar->FindCommandBase of god: %p\n", CvarPtr2);
	//I::Cvar->FindVar("sv_cheats")->GetInt();

	//if (!I::EngineClient->IsInGame()) {
		for (MaterialHandle_t i = I::MaterialSystem->FirstMaterial(); i != I::MaterialSystem->InvalidMaterial(); i = I::MaterialSystem->NextMaterial(i))
		{
			IMaterial* pMaterial = I::MaterialSystem->GetMaterial(i);
			if (pMaterial->IsErrorMaterial())continue;

			//std::cout << pMaterial->GetName() << std::endl;

			if (strstr("models/zimu/zimu1_hd/zimu1_hd", pMaterial->GetName()))
			{
				U::LogDebug("material: zimu1_hd found\n");
				//I::ModelRender->ForcedMaterialOverride(pMaterial, OVERRIDE_NORMAL);
				bool btemp = false;
				IMaterialVar* iMaterialVar = pMaterial->FindVar("$basetexture", &btemp, false);
				if (iMaterialVar) {
					auto materialvar = iMaterialVar->GetName();
					U::LogDebug("materialvar: %s\n", materialvar);

					auto materialvar_string_value = iMaterialVar->GetStringValue();
					U::LogDebug("materialvar_string_value: %s\n", materialvar_string_value);

					U::LogDebug("iMaterialVar->GetTextureValue->name: %s\n", iMaterialVar->GetTextureValue()->GetName());


					// 查找material
					IMaterial* zimu2 = I::MaterialSystem->FindMaterial("models/zimu/zimu2_hd/zimu2_hd", TEXTURE_GROUP_MODEL, false);
					IMaterial* zimu3 = I::MaterialSystem->FindMaterial("models/zimu/zimu3_hd/zimu3_hd", TEXTURE_GROUP_MODEL, false);
					ITexture* zimu2_texture = nullptr;
					ITexture* zimu3_texture = nullptr;
					if (zimu2 != nullptr) {
						zimu2_texture = zimu2->FindVar("$basetexture", &btemp, false)->GetTextureValue();
						//iMaterialVar->SetTextureValue(zimu2_texture);
					} else {
                        U::LogDebug("models/zimu/zimu2_hd/zimu2_hd not found\n");
					}

					if (zimu3 != nullptr) {
						zimu3_texture = zimu3->FindVar("$basetexture", &btemp, false)->GetTextureValue();
						//iMaterialVar->SetTextureValue(zimu3_texture);
					}
					else {
						U::LogDebug("models/zimu/zimu3_hd/zimu3_hd not found\n");
					}

					//while (true) {
					//	iMaterialVar->SetTextureValue(zimu2_texture);
					//	Sleep(1000);
					//	iMaterialVar->SetTextureValue(zimu3_texture);
					//	Sleep(1000);
					//}


					//iMaterialVar->SetStringValue("zimu/zimu2_hd"); //直接这么设置会白屏
					//U::LogDebug("materialvar_string_new_value: %s\n", iMaterialVar->GetStringValue());
				} else {
					U::LogDebug("iMaterialVar not found\n");
				}
			}

			//const char* mat = pMaterial->GetTextureGroupName();
			
			//if (strstr(TEXTURE_GROUP_WORLD, pMaterial->GetTextureGroupName()))
			//{
			//	//pMaterial->AlphaModulate(alpha);
			//}
			////20220905, 空气墙材质渲染为半透明
			//if (strstr(TEXTURE_GROUP_OTHER, pMaterial->GetTextureGroupName()))
			//{
			//	pMaterial->AlphaModulate(0.5);
			//}
		}
	//}
	//else {
	//	std::cout << "not in game." << std::endl;
	//}

	//ITexture* ret =  I::MaterialSystem->CreateNamedRenderTargetTextureEx("_rt_PortalTexture",
	//		512, 512,
	//		RT_SIZE_DEFAULT,
	//		IMAGE_FORMAT_RGBA8888,
	//		MATERIAL_RT_DEPTH_SHARED,
	//		TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT,
	//		CREATERENDERTARGETFLAGS_HDR);

	

	//传送门实验代码
	// L4D2_Portal l4d2_Portal;
	// l4d2_Portal.PortalInit();
}

void CGlobal_ModuleEntry::Func_TraceRay_Test()
{
	if (!I::EngineClient->IsInGame() || I::EngineVGui->IsGameUIVisible())
		return;

	bool hit = false;

	C_TerrorPlayer* pLocal = I::ClientEntityList->GetClientEntity(I::EngineClient->GetLocalPlayer())->As<C_TerrorPlayer*>();
	if (1) {
		if (pLocal && !pLocal->deadflag())
		{
			Vector eyeAngles =  pLocal->EyeAngles();
			Vector eyePosition = pLocal->EyePosition();
			Vector pos = pLocal->GetAbsOrigin();
			Ray_t ray;
			ray.Init(pos, Vector(0, 0, 0));
			unsigned int fMask = MASK_SHOT | CONTENTS_GRATE;
			CTraceFilter pTraceFilter;
			trace_t pTrace;
			I::EngineTrace->TraceRay(ray, fMask, &pTraceFilter, &pTrace);
			auto ret1 = pTrace.m_pEnt->ShouldDrawUnderwaterBulletBubbles();
			auto ret2 = pTrace.m_pEnt->ShouldDrawWaterImpacts();
			if (pTrace.fraction < 1.0) {
					U::LogDebug("Trace Hit\n");
			} else {
				U::LogDebug("Trace Not Hit\n");
			}
			Sleep(1000);
		}
	}
}

class IVPhysicsDebugOverlay
{
public:
	virtual void AddEntityTextOverlay(int ent_index, int line_offset, float duration, int r, int g, int b, int a, const char* format, ...) = 0;
	virtual void AddBoxOverlay(const Vector& origin, const Vector& mins, const Vector& max, QAngle const& orientation, int r, int g, int b, int a, float duration) = 0;
	virtual void AddTriangleOverlay(const Vector& p1, const Vector& p2, const Vector& p3, int r, int g, int b, int a, bool noDepthTest, float duration) = 0;
	virtual void AddLineOverlay(const Vector& origin, const Vector& dest, int r, int g, int b, bool noDepthTest, float duration) = 0;
	virtual void AddTextOverlay(const Vector& origin, float duration, const char* format, ...) = 0;
	virtual void AddTextOverlay(const Vector& origin, int line_offset, float duration, const char* format, ...) = 0;
	virtual void AddScreenTextOverlay(float flXPos, float flYPos, float flDuration, int r, int g, int b, int a, const char* text) = 0;
	virtual void AddSweptBoxOverlay(const Vector& start, const Vector& end, const Vector& mins, const Vector& max, const QAngle& angles, int r, int g, int b, int a, float flDuration) = 0;
	virtual void AddTextOverlayRGB(const Vector& origin, int line_offset, float duration, float r, float g, float b, float alpha, const char* format, ...) = 0;
};
typedef void* (*CreateInterfaceFn)(const char* pName, int* pReturnCode);
typedef void* (*InstantiateInterfaceFn)();

class IPhysicsEnvironment
{
public:
	virtual ~IPhysicsEnvironment(void) {}

	virtual void SetDebugOverlay(CreateInterfaceFn debugOverlayFactory) = 0;
	virtual IVPhysicsDebugOverlay* GetDebugOverlay(void) = 0;

	// gravity is a 3-vector in in/s^2
	virtual void			SetGravity(const Vector& gravityVector) = 0;
	virtual void			GetGravity(Vector* pGravityVector) const = 0;

	// air density is in kg / m^3 (water is 1000)
	// This controls drag, air that is more dense has more drag.
	virtual void			SetAirDensity(float density) = 0;
	virtual float			GetAirDensity(void) const = 0;

};

void CGlobal_ModuleEntry::Func_IPhysicsEnvironment_Test()
{
	HMODULE hModule = GetModuleHandle(L"server.dll");
	// 正确计算全局变量地址
	// 偏移量0x107FE19C是从IDA Pro中获取到的偏移地址，减去0x10000000得到实际的physenv指针地址
	// 具体原理参考https://www.cnblogs.com/blogwr/p/18725779/how-to-make-your-ida-address-rva-z1az4mx
	DWORD* physenv_ptr_ptr = (DWORD*)((BYTE*)hModule + 0x107FE19C - 0x10000000);

	// 先读取指针，再调用方法
	IPhysicsEnvironment* physenv = (IPhysicsEnvironment*)(*physenv_ptr_ptr);
	if (physenv) {
		float airDensity = physenv->GetAirDensity(); //这个调用正确,默认返回值是2.0
		// 使用name...
		U::LogDebug("airDensity = %f\n", airDensity);
	}
	
}

void CGlobal_ModuleEntry::Func_Pistol_Fire_Test()
{
	if (!I::EngineClient->IsInGame() || I::EngineVGui->IsGameUIVisible())
		return;

	C_TerrorPlayer* pLocal = I::ClientEntityList->GetClientEntity(I::EngineClient->GetLocalPlayer())->As<C_TerrorPlayer*>();
	/*while (1) {*/
	while (pLocal && !pLocal->deadflag())
	{
		C_TerrorWeapon* pWeapon = pLocal->GetActiveWeapon()->As<C_TerrorWeapon*>();
		if (pWeapon)
		{
			const char* weapon_name = pWeapon->GetName();
			U::LogDebug("weapon_name = %s\n", weapon_name);
			if (strcmp(weapon_name, "weapon_pistol") == 0)
			//if (strcmp(weapon_name, "weapon_shotgun_chrome") == 0)
			{
				// 或者通过代码获取
				HMODULE hModule = GetModuleHandle(L"client.dll");
				DWORD baseAddress = (DWORD)hModule;
				U::LogDebug("DLL基址: 0x%08X\n", baseAddress);

				typedef void(__thiscall* PrimaryAttackFn)(void*);
				typedef void(__thiscall* SecondaryAttackFn)(void*);
				// 1. 获取虚函数表指针
				void** vtable_ptr = *reinterpret_cast<void***>(pWeapon);

				// 2. 调用PrimaryAttack (索引270)
				PrimaryAttackFn primaryFunc = reinterpret_cast<PrimaryAttackFn>(vtable_ptr[270]);
				//primaryFunc(pWeapon);
				Sleep(1000);

				// 3. 调用SecondaryAttack (索引271)
				SecondaryAttackFn secondaryFunc = reinterpret_cast<SecondaryAttackFn>(vtable_ptr[271]);
				//secondaryFunc(pWeapon);
				Sleep(1000);


				pWeapon->PrimaryAttack();
				//Sleep(1000);
				//pWeapon->SecondaryAttack();
                
			}
		}
	}
}

void CGlobal_ModuleEntry::Func_CServerTools_Test()
{
	class CBaseEntity;
	CBaseEntity* prop_obj = (CBaseEntity*)I::CServerTools->CreateEntityByName("prop_dynamic");
	U::LogDebug("prop_obj = %p\n", prop_obj);

	typedef void(__thiscall* TeleportFn)(void*, Vector const*, QAngle const*, Vector const*);
	typedef void(__thiscall* SetModelFn)(void*, const char*);
	// 1. 获取虚函数表指针
	void** vtable_ptr = *reinterpret_cast<void***>(prop_obj);
	TeleportFn primaryFunc = reinterpret_cast<TeleportFn>(vtable_ptr[118]);  //118 = CBaseEntity::Teleport(Vector const*, QAngle const*, Vector const*)
	const Vector* newPosition = new Vector(0, 0, 0);
	QAngle* newAngles = new QAngle;
	newAngles->x = newAngles->y = newAngles->z = 0;
	const Vector* newVelocity = new Vector(0, 0, 0);
	primaryFunc(prop_obj, newPosition, newAngles, newVelocity);

	const char* model_name = "models/blackops/portal.mdl";
	SetModelFn setModelFunc = reinterpret_cast<SetModelFn>(vtable_ptr[27]); //27 = CBaseEntity::SetModel(char const*)
	setModelFunc(prop_obj, model_name);

	I::CServerTools->DispatchSpawn(prop_obj);

}

void CGlobal_ModuleEntry::Load()
{
	// 由于打印信息现在都输出到游戏内控制台，不再需要额外的控制台窗口
	//AllocConsole();
	//freopen("CONIN$", "r", stdin); // makes it possible to output to output to console with cout.
	//freopen("CONOUT$", "w", stdout);

	// 最小化控制台窗口到后台
	//ShowWindow(GetConsoleWindow(), SW_MINIMIZE);

	while (!GetModuleHandleA("serverbrowser.dll"))
	    std::this_thread::sleep_for(std::chrono::seconds(1));

	U::Offsets.Init();

	//Interfaces
	{
		I::BaseClient       = U::Interface.Get<IBaseClientDLL*>("client.dll", "VClient016");
		U::LogInfo("BaseClient: %p\n", I::BaseClient);

		I::ClientEntityList = U::Interface.Get<IClientEntityList*>("client.dll", "VClientEntityList003");
        U::LogInfo("ClientEntityList: %p\n", I::ClientEntityList);

		I::Prediction       = U::Interface.Get<IPrediction*>("client.dll", "VClientPrediction001");
        U::LogInfo("Prediction: %p\n", I::Prediction);

		I::GameMovement     = U::Interface.Get<IGameMovement*>("client.dll", "GameMovement001");
        U::LogInfo("Client::GameMovement: %p\n", I::GameMovement);

		I::EngineClient     = U::Interface.Get<IVEngineClient*>("engine.dll", "VEngineClient013");
        U::LogInfo("EngineClient: %p\n", I::EngineClient);

		I::EngineTrace      = U::Interface.Get<IEngineTrace*>("engine.dll", "EngineTraceClient003");
        U::LogInfo("EngineTrace: %p\n", I::EngineTrace);

		I::EngineVGui       = U::Interface.Get<IEngineVGui*>("engine.dll", "VEngineVGui001");
		U::LogInfo("EngineVGui: %p\n", I::EngineVGui);

		I::RenderView       = U::Interface.Get<IVRenderView*>("engine.dll", "VEngineRenderView013");
        U::LogInfo("RenderView: %p\n", I::RenderView);

		I::DebugOverlay     = U::Interface.Get<IVDebugOverlay*>("engine.dll", "VDebugOverlay003");
        U::LogInfo("DebugOverlay: %p\n", I::DebugOverlay);

		I::ModelInfo        = U::Interface.Get<IVModelInfo*>("engine.dll", "VModelInfoClient004");
        U::LogInfo("ModelInfo: %p\n", I::ModelInfo);

		I::ModelRender      = U::Interface.Get<IVModelRender*>("engine.dll", "VEngineModel016");
        U::LogInfo("ModelRender: %p\n", I::ModelRender);

		I::VGuiPanel        = U::Interface.Get<IVGuiPanel*>("vgui2.dll", "VGUI_Panel009");
        U::LogInfo("VGuiPanel: %p\n", I::VGuiPanel);

		I::VGuiSurface      = U::Interface.Get<IVGuiSurface*>("vgui2.dll", "VGUI_Surface031");
        U::LogInfo("VGuiSurface: %p\n", I::VGuiSurface);

		I::MatSystemSurface = U::Interface.Get<IMatSystemSurface*>("vguimatsurface.dll", "VGUI_Surface031");
		U::LogInfo("MatSystemSurface: %p\n", I::MatSystemSurface);

		I::MaterialSystem   = U::Interface.Get<IMaterialSystem*>("materialsystem.dll", "VMaterialSystem080");
        U::LogInfo("MaterialSystem: %p\n", I::MaterialSystem);

		I::Cvar = U::Interface.Get<ICvar*>("materialsystem.dll", "VEngineCvar007");
		U::LogInfo("Cvar: %p\n", I::Cvar);

		// Initialize CVar manager after I::Cvar is ready
		U::CVarManager::Initialize();

		{
			I::ClientMode = **reinterpret_cast<void***>(U::Offsets.m_dwClientMode);
			XASSERT(I::ClientMode == nullptr);

			I::GlobalVars = **reinterpret_cast<CGlobalVarsBase***>(U::Offsets.m_dwGlobalVars);
			XASSERT(I::GlobalVars == nullptr);

			I::MoveHelper = **reinterpret_cast<IMoveHelper***>(U::Offsets.m_dwMoveHelper);
			XASSERT(I::MoveHelper == nullptr);

			I::IInput = **reinterpret_cast<IInput_t***>(U::Offsets.m_dwIInput);
			XASSERT(I::IInput == nullptr);
		}

		I::PhysicsCollision = U::Interface.Get<IPhysicsCollision*>("vphysics.dll", "VPhysicsCollision007");
		U::LogInfo("PhysicsCollision: %p\n", I::PhysicsCollision);

		I::CServerTools = U::Interface.Get<IServerTools*>("server.dll", "VSERVERTOOLS001");
		U::LogInfo("CServerTools: %p\n", I::CServerTools);

		I::EngineServer = U::Interface.Get<IVEngineServer*>("engine.dll", "VEngineServer022");
		U::LogInfo("EngineServer: %p\n", I::EngineServer);

	}

	// Initialize test cvars
	F::TestCVar::Initialize();

	G::Draw.Init();
	// PortalInit() 已移至 LevelInitPostEntity，在地图加载时调用
	// 这样可以支持地图切换时重新初始化
	// G::G_L4D2Portal.PortalInit();
	G::Hooks.Init();
	//Run();
	//Func_TraceRay_Test();
	//Func_IPhysicsEnvironment_Test();
	//Func_Pistol_Fire_Test();
	//Func_CServerTools_Test();
}