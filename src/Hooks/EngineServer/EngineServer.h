#pragma once

#include "../../SDK/SDK.h"

namespace Hooks
{
	namespace EngineServer
	{
		inline Hook::CTable Table;

		namespace GetArea
		{
			// Function signature matching IVEngineServer::GetArea
			// virtual int GetArea(const Vector& origin) = 0; // Index 65
			using FN = int(__fastcall*)(void*, void*, const Vector&);
			constexpr uint32_t Index = 65u;

			int __fastcall Detour(void* ecx, void* edx, const Vector& origin);
		}

		void Init();
	}
}
