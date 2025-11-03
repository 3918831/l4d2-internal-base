#include "Weapon_Pistol.h"

using namespace Hooks;

void __fastcall Pistol::Reload::Detour(C_TerrorWeapon* pThis, void* edx)
{
	printf("Pistol::Reload::Detour is called.\n");
	Func.Original<FN>()(pThis, edx);
}

void __fastcall Pistol::PrimaryAttack::Detour(C_TerrorWeapon* pThis, void* edx)
{
	printf("Pistol::PrimaryAttack::Detour is called.\n");
	Func.Original<FN>()(pThis, edx);
}

void __fastcall Pistol::SecondaryAttack::Detour(C_TerrorWeapon* pThis, void* edx)
{
	printf("Pistol::SecondaryAttack::Detour is called.\n");
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