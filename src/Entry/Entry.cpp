#include <iostream>
#include "Entry.h"
#include "../SDK/L4D2/Interfaces/ICvar.h"
#include "../SDK/L4D2/Interfaces/IInput.h"
#include "../SDK/L4D2/Interfaces/Vphysics.h"
#include "../Portal/L4D2_Portal.h"


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
	std::cout << "I::Cvar->FindCommandBase of sv_cheats: " << CvarPtr << std::endl;

	auto CvarPtr2 = I::Cvar->FindCommandBase("god");
	std::cout << "I::Cvar->FindCommandBase of god: " << CvarPtr2 << std::endl;
	//I::Cvar->FindVar("sv_cheats")->GetInt();

	//if (!I::EngineClient->IsInGame()) {
		for (MaterialHandle_t i = I::MaterialSystem->FirstMaterial(); i != I::MaterialSystem->InvalidMaterial(); i = I::MaterialSystem->NextMaterial(i))
		{
			IMaterial* pMaterial = I::MaterialSystem->GetMaterial(i);
			if (pMaterial->IsErrorMaterial())continue;

			//std::cout << pMaterial->GetName() << std::endl;

			if (strstr("models/zimu/zimu1_hd/zimu1_hd", pMaterial->GetName()))
			{
				std::cout << "material: zimu1_hd found" << std::endl;
				//I::ModelRender->ForcedMaterialOverride(pMaterial, OVERRIDE_NORMAL);
				bool btemp = false;
				IMaterialVar* iMaterialVar = pMaterial->FindVar("$basetexture", &btemp, false);
				if (iMaterialVar) {
					auto materialvar = iMaterialVar->GetName();
					std::cout << "materialvar: " << materialvar << std::endl;

					auto materialvar_string_value = iMaterialVar->GetStringValue();
					std::cout << "materialvar_string_value: " << materialvar_string_value << std::endl;

					std::cout << "iMaterialVar->GetTextureValue->name: " << iMaterialVar->GetTextureValue()->GetName() << std::endl;


					// 查找material
					IMaterial* zimu2 = I::MaterialSystem->FindMaterial("models/zimu/zimu2_hd/zimu2_hd", TEXTURE_GROUP_MODEL, false);
					IMaterial* zimu3 = I::MaterialSystem->FindMaterial("models/zimu/zimu3_hd/zimu3_hd", TEXTURE_GROUP_MODEL, false);
					ITexture* zimu2_texture = nullptr;
					ITexture* zimu3_texture = nullptr;
					if (zimu2 != nullptr) {
						zimu2_texture = zimu2->FindVar("$basetexture", &btemp, false)->GetTextureValue();
						//iMaterialVar->SetTextureValue(zimu2_texture);
					} else {
                        std::cout << "models/zimu/zimu2_hd/zimu2_hd not found" << std::endl;
					}

					if (zimu3 != nullptr) {
						zimu3_texture = zimu3->FindVar("$basetexture", &btemp, false)->GetTextureValue();
						//iMaterialVar->SetTextureValue(zimu3_texture);
					}
					else {
						std::cout << "models/zimu/zimu3_hd/zimu3_hd not found" << std::endl;
					}

					//while (true) {
					//	iMaterialVar->SetTextureValue(zimu2_texture);
					//	Sleep(1000);
					//	iMaterialVar->SetTextureValue(zimu3_texture);
					//	Sleep(1000);
					//}


					//iMaterialVar->SetStringValue("zimu/zimu2_hd"); //直接这么设置会白屏
					//std::cout << "materialvar_string_new_value: " << iMaterialVar->GetStringValue() << std::endl;
				} else {
					std::cout << "iMaterialVar not found" << std::endl;
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
	while (1) {
		if (pLocal && !pLocal->deadflag())
		{
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
					std::cout << "Trace Hit" << std::endl;
			} else {
				std::cout << "Trace Not Hit" << std::endl;				
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
		float airDensity = physenv->GetAirDensity();
		// 使用name...
		printf("airDensity = %f\n", airDensity);
	}
	
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
        std::cout << "Client::GameMovement: " << I::GameMovement << std::endl;

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
        std::cout << "MaterialSystem: " << I::MaterialSystem << std::endl;

		I::Cvar = U::Interface.Get<ICvar*>("materialsystem.dll", "VEngineCvar007");
		std::cout << "Cvar: " << I::Cvar << std::endl;

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
		std::cout << "PhysicsCollision: " << I::PhysicsCollision << std::endl;
	}

	G::Draw.Init();
	G::G_L4D2Portal.PortalInit();
	G::Hooks.Init();
	//Run();
	Func_TraceRay_Test();
	//Func_IPhysicsEnvironment_Test();
}