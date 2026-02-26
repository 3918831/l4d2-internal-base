#include "Weapon_Pistol.h"
#include "../../Portal/L4D2_Portal.h"
#include "../../Util/Logger/Logger.h"

using namespace Hooks;

void __fastcall Pistol::Reload::Detour(C_TerrorWeapon* pThis, void* edx)
{
	// 只保留打印，实际逻辑已在 CreateMove 中处理
	U::LogDebug("Pistol::Reload::Detour is called (handled in CreateMove).\n");

	Func.Original<FN>()(pThis, edx);
}

void __fastcall Pistol::PrimaryAttack::Detour(C_TerrorWeapon* pThis, void* edx)
{
	// 只保留打印，实际逻辑已在 CreateMove 中处理
	U::LogDebug("Pistol::PrimaryAttack::Detour is called (handled in CreateMove).\n");

	Func.Original<FN>()(pThis, edx);
}

void __fastcall Pistol::SecondaryAttack::Detour(C_TerrorWeapon* pThis, void* edx)
{
	// 只保留打印，实际逻辑已在 CreateMove 中处理
	U::LogDebug("Pistol::SecondaryAttack::Detour is called (handled in CreateMove).\n");

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