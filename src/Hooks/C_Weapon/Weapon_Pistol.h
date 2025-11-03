#pragma once

#include "../../SDK/SDK.h"

namespace Hooks
{
	namespace Pistol
	{
		namespace Reload
		{
			inline Hook::CFunction Func;
			using FN = void(__fastcall*)(C_TerrorWeapon*, void*);

			void __fastcall Detour(C_TerrorWeapon* pThis, void* edx);
		}

		namespace PrimaryAttack
		{
			inline Hook::CFunction Func;
			using FN = void(__fastcall*)(C_TerrorWeapon*, void*);

			void __fastcall Detour(C_TerrorWeapon* pThis, void* edx);
		}

		namespace SecondaryAttack
		{
			inline Hook::CFunction Func;
			using FN = void(__fastcall*)(C_TerrorWeapon*, void*);

			void __fastcall Detour(C_TerrorWeapon* pThis, void* edx);
		}

		void Init();
	}
}