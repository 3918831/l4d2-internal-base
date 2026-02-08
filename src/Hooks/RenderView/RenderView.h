#pragma once

#include "../../SDK/SDK.h"

namespace Hooks
{
	namespace RenderView
	{
		inline Hook::CTable Table;

		namespace ViewSetupVis
		{
			// Function signature matching IVRenderView::ViewSetupVis
			// virtual void ViewSetupVis(bool novis, int numorigins, const Vector origin[]) = 0; // Index 22
			using FN = void(__fastcall*)(void*, void*, bool, int, const Vector[]);
			constexpr uint32_t Index = 22u;

			void __fastcall Detour(void* ecx, void* edx, bool novis, int numorigins, const Vector origin[]);
		}

		namespace ViewSetupVisEx
		{
			// Function signature matching IVRenderView::ViewSetupVisEx
			// virtual void ViewSetupVisEx(bool novis, int numorigins, const Vector origin[], unsigned int& returnFlags) = 0;
			// Index confirmed: 45 (verified via runtime detection)
			using FN = void(__fastcall*)(void*, void*, bool, int, const Vector[], unsigned int&);
			constexpr uint32_t Index = 45u;

			void __fastcall Detour(void* ecx, void* edx, bool novis, int numorigins, const Vector origin[], unsigned int& returnFlags);
		}

		void Init();
	}
}
