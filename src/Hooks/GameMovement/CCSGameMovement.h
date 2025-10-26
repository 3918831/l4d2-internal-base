#pragma once

#include "../../SDK/SDK.h"

namespace Hooks
{
	namespace CCSGameMovement
	{

		namespace TracePlayerBBox
		{
			inline Hook::CFunction Func;
			using FN = void(__fastcall*)(void*, void*, const Vector&, const Vector&, unsigned int, int, trace_t*);
			void __fastcall Detour(void* ecx, void* edx, const Vector& start, const Vector& end, unsigned int fMask, int collisionGroup, trace_t* pm);
		}

		void Init();
	}
}