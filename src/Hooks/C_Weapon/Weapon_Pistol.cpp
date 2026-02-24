#include "Weapon_Pistol.h"
#include "../../Portal/L4D2_Portal.h"
#include "../../Util/Logger/Logger.h"

using namespace Hooks;

void __fastcall Pistol::Reload::Detour(C_TerrorWeapon* pThis, void* edx)
{
	U::LogDebug("Pistol::Reload::Detour is called.\n");

	// 触发两个传送门的关闭动画
	G::G_L4D2Portal.StartPortalCloseAnimation(&G::G_L4D2Portal.g_BluePortal);
	G::G_L4D2Portal.StartPortalCloseAnimation(&G::G_L4D2Portal.g_OrangePortal);

	Func.Original<FN>()(pThis, edx);
}

void __fastcall Pistol::PrimaryAttack::Detour(C_TerrorWeapon* pThis, void* edx)
{
	U::LogDebug("Pistol::PrimaryAttack::Detour is called.\n");

	/*
	C_TerrorPlayer* pLocal = I::ClientEntityList->GetClientEntity(I::EngineClient->GetLocalPlayer())->As<C_TerrorPlayer*>();

	if (pLocal && !pLocal->deadflag())
	{
		Vector eyePosition = pLocal->EyePosition();

		// �������ӽ�Ŀǰ�Ƕȶ�Ӧ����������
		Vector forward, right, up;

		Vector eyeAngles = pLocal->EyeAngles();
		U::Math.AngleVectors(reinterpret_cast<QAngle&>(eyeAngles), &forward, &right, &up);

		Vector vTracerOrigin = eyePosition
			+ forward * 30.0f
			+ right * 4.0f
			+ up * (-5.0f);
		Vector vTracerEnd;
		float z_distance = 4096.0f;
		vTracerEnd.x = vTracerOrigin.x + forward.x * z_distance;
		vTracerEnd.y = vTracerOrigin.y + forward.y * z_distance;
		vTracerEnd.z = vTracerOrigin.z + forward.z * z_distance;

		Ray_t ray;
		ray.Init(vTracerOrigin, vTracerEnd);
		unsigned int fMask = MASK_SHOT | CONTENTS_GRATE;
		CTraceFilter pTraceFilter;
		trace_t pTrace;
		I::EngineTrace->TraceRay(ray, fMask, &pTraceFilter, &pTrace);
		if (pTrace.DidHit()) {
			int index = pTrace.m_pEnt->entindex(); //Ϊ0�Ļ�����worldspawn
			U::LogDebug("[Hook] Hit index: %d\n", index);
		} else {
			U::LogDebug("[Hook] Hit nothing\n");
		}

	}
	//*/
	G::G_L4D2Portal.m_pWeaponPortalgun->FirePortal1();
	Func.Original<FN>()(pThis, edx);
}

void __fastcall Pistol::SecondaryAttack::Detour(C_TerrorWeapon* pThis, void* edx)
{
	U::LogDebug("Pistol::SecondaryAttack::Detour is called.\n");
	G::G_L4D2Portal.m_pWeaponPortalgun->FirePortal2();
	Func.Original<FN>()(pThis, edx);
}

void Pistol::Init()
{
	//Reload
	{
		using namespace Reload;
		const FN pfReload = reinterpret_cast<FN>(U::Offsets.m_dwReload);
		XASSERT(pfReload == nullptr);

		if (pfReload)
			XASSERT(Func.Init(pfReload, &Detour) == false);
	}

	//PrimaryAttack
	{
		using namespace PrimaryAttack;
		const FN pfPrimaryAttack = reinterpret_cast<FN>(U::Offsets.m_dwPrimaryAttack);
		XASSERT(pfPrimaryAttack == nullptr);

		if (pfPrimaryAttack)
			XASSERT(Func.Init(pfPrimaryAttack, &Detour) == false);
	}

	//SecondaryAttack
	{
		using namespace SecondaryAttack;
		const FN pfSecondaryAttack = reinterpret_cast<FN>(U::Offsets.m_dwSecondaryAttack);
		XASSERT(pfSecondaryAttack == nullptr);

		if (pfSecondaryAttack)
			XASSERT(Func.Init(pfSecondaryAttack, &Detour) == false);
	}
}