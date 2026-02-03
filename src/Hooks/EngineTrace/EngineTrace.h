#pragma once

#include "../../SDK/SDK.h"

namespace Hooks
{
	namespace EngineTrace
	{
		inline Hook::CTable Table;

		namespace GetLeafContainingPoint
		{
			// Function signature matching IEngineTrace::GetLeafContainingPoint
			// virtual int GetLeafContainingPoint(const Vector& ptTest) = 0; // Index 18
			using FN = int(__fastcall*)(void*, void*, const Vector&);
			constexpr uint32_t Index = 18u;

			int __fastcall Detour(void* ecx, void* edx, const Vector& ptTest);
		}

		void Init();
	}
}
