#pragma once

#include "../../SDK/SDK.h"

namespace Hooks
{
	namespace BaseClient
	{
		inline Hook::CTable Table;

		namespace LevelInitPreEntity
		{
			using FN = void(__fastcall*)(void*, void*, char const*);
			constexpr uint32_t Index = 4u;

			void __fastcall Detour(void* ecx, void* edx, char const* pMapName);
		}

		namespace LevelInitPostEntity
		{
			using FN = void(__fastcall*)(void*, void*);
			constexpr uint32_t Index = 5u;

			void __fastcall Detour(void* ecx, void* edx);
		}

		namespace LevelShutdown
		{
			using FN = void(__fastcall*)(void*, void*);
			constexpr uint32_t Index = 6u;

			void __fastcall Detour(void* ecx, void* edx);
		}

		namespace FrameStageNotify
		{
			using FN = void(__fastcall*)(void*, void*, ClientFrameStage_t);
			constexpr uint32_t Index = 34u;

			void __fastcall Detour(void* ecx, void* edx, ClientFrameStage_t curStage);
		}
		
		// add RenderView Hook 
		//namespace RenderView
		//{
		//	using FN = void(__fastcall*)(void*, void*, CViewSetup&, int , int);
		//	constexpr uint32_t Index = 26u;

		//	void __fastcall Detour(void *ecx, void *edx, CViewSetup &setup, int nClearFlags, int whatToDraw);
		//}

		// add RenderView Hook 
		namespace RenderView
		{
			inline Hook::CFunction Func;
			using FN = void(__fastcall*)(void*, void*, CViewSetup&, CViewSetup&, int, int);

			void __fastcall Detour(void* ecx, void* edx, CViewSetup& setup, CViewSetup& hudViewSetup, int nClearFlags, int whatToDraw);
		}
		
		void Init();
	}
}