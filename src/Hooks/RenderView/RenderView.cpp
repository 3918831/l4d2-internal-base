#include "RenderView.h"

#include <iostream>
#include <Windows.h>
#include <map>
#include "../../SDK/L4D2/Interfaces/RenderView.h"
#include "../../Portal/L4D2_Portal.h"
#include "../Hooks.h"

// g_bIsRenderingPortalTexture 已在 Hooks.h 中声明

// ============================================================================
// VMT 索引检测工具（用于调试未知的 VMT 函数索引）
// ============================================================================
// 用途：当需要 Hook 一个 VMT 函数但不确定其索引值时，使用此工具进行检测
//
// 启用方法：
//   1. 在 RenderView.h 中设置要测试的 Index 值
//   2. 取消下面 DETECT_VMT_INDEX 的注释
//   3. 重新编译并运行游戏
//   4. 查看控制台输出的统计报告
//   5. 如果 origin 参数有效率 > 50%，说明索引可能正确
//   6. 如果一直无效或崩溃，尝试其他索引值
//
// 完成后：
//   - 重新注释 DETECT_VMT_INDEX
//   - 删除调试输出（可选）
// ============================================================================
// #define DETECT_VMT_INDEX  // 取消此注释以启用索引检测模式

namespace Detail
{
#ifdef DETECT_VMT_INDEX
	static std::map<uint32_t, int> s_ValidCallCount;    // 有效调用次数（origin 非空）
	static std::map<uint32_t, int> s_InvalidCallCount;  // 无效调用次数（origin 为空）
	static std::map<uint32_t, int> s_TotalCallCount;    // 总调用次数
	static bool s_EnableDetection = true;               // 是否启用检测模式
	static int s_ReportInterval = 100;                  // 每多少次调用打印一次报告
#endif

	// 静态检查：函数指针是否指向有效代码内存
	static bool IsValidCodePtr(uintptr_t ptr)
	{
		if (ptr == 0 || ptr == 0xFFFFFFFF || ptr < 0x10000)
			return false;

		// 使用 Windows SEH 检查内存可读性
		__try
		{
			BYTE opcode = *(BYTE*)ptr;
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	// 检查指定索引的函数是否有效
	static bool IsIndexValid(uintptr_t* vmt, uint32_t index)
	{
		if (vmt == nullptr)
			return false;

		uintptr_t funcPtr = vmt[index];
		return IsValidCodePtr(funcPtr);
	}

#ifdef DETECT_VMT_INDEX
	// 打印统计报告
	static void PrintReport()
	{
		std::cout << "\n[RenderView] ===== Index Detection Report =====" << std::endl;
		std::cout << "[RenderView] Testing Index: " << Hooks::RenderView::ViewSetupVisEx::Index << std::endl;

		int total = s_TotalCallCount[Hooks::RenderView::ViewSetupVisEx::Index];
		int valid = s_ValidCallCount[Hooks::RenderView::ViewSetupVisEx::Index];
		int invalid = s_InvalidCallCount[Hooks::RenderView::ViewSetupVisEx::Index];

		std::cout << "[RenderView] Total calls: " << total << std::endl;
		std::cout << "[RenderView] Valid calls (origin != nullptr): " << valid << std::endl;
		std::cout << "[RenderView] Invalid calls (origin == nullptr): " << invalid << std::endl;

		if (total > 0)
		{
			float validRate = (float)valid / total * 100.0f;
			std::cout << "[RenderView] Valid rate: " << validRate << "%" << std::endl;

			if (validRate > 50.0f)
			{
				std::cout << "[RenderView] *** LIKELY CORRECT INDEX *** (High valid rate)" << std::endl;
			}
			else
			{
				std::cout << "[RenderView] *** INCORRECT INDEX *** (Low valid rate)" << std::endl;
			}
		}
		else
		{
			std::cout << "[RenderView] No calls recorded - function may not be invoked" << std::endl;
		}

		std::cout << "[RenderView] ===== End of Report =====\n" << std::endl;
	}
#endif
}

// ViewSetupVis Hook implementation
// 索引值22不确定是否是正确的
void __fastcall Hooks::RenderView::ViewSetupVis::Detour(void* ecx, void* edx, bool novis, int numorigins, const Vector origin[])
{
	// 1. 检查是否处于我们要欺骗的状态
    // if (g_bIsRenderingPortalTexture && numorigins > 0)
    // {
    //     Vector safeOrigin = G::G_L4D2Portal.g_BluePortal.origin + G::G_L4D2Portal.g_BluePortal.normal * 1.0f;
    //     Vector cheatOrigins[1];
    //     cheatOrigins[0] = safeOrigin;
    //     Table.Original<FN>(Index)(ecx, edx, novis, 1, cheatOrigins);
    // }

	// Call original function
	Table.Original<FN>(Index)(ecx, edx, novis, numorigins, origin);
}

// ViewSetupVisEx Hook implementation
// 索引值45确定是正确的
void __fastcall Hooks::RenderView::ViewSetupVisEx::Detour(void* ecx, void* edx, bool novis, int numorigins, const Vector origin[], unsigned int& returnFlags)
{
#ifdef DETECT_VMT_INDEX
	uint32_t currentIdx = Index;

	// 动态索引检测：记录调用参数有效性
	if (Detail::s_EnableDetection)
	{
		Detail::s_TotalCallCount[currentIdx]++;

		if (origin != nullptr)
		{
			Detail::s_ValidCallCount[currentIdx]++;
		}
		else
		{
			Detail::s_InvalidCallCount[currentIdx]++;
		}

		// 定期打印报告
		static int callCounter = 0;
		if (++callCounter >= Detail::s_ReportInterval)
		{
			Detail::PrintReport();
			callCounter = 0;
		}
	}
#endif

	// 参数保护：如果 origin 为空，不要调用原函数（会崩溃）
	if (ecx == nullptr)
	{
		std::cerr << "[RenderView::ViewSetupVisEx] ERROR: ecx is null!" << std::endl;
		return;
	}

	if (origin == nullptr && numorigins > 0)
	{
		// origin 为空但 numorigins > 0，说明索引错误，直接返回不调用原函数
		return;
	}

	// 1. 检查是否处于我们要欺骗的状态
    // if (g_bIsRenderingPortalTexture && numorigins > 0)
    // {
    //     Vector safeOrigin = G::G_L4D2Portal.g_BluePortal.origin + G::G_L4D2Portal.g_BluePortal.normal * 200.0f;
    //     Vector cheatOrigins[1];
    //     cheatOrigins[0] = safeOrigin;
    //     Table.Original<FN>(Index)(ecx, edx, true, 1, origin, returnFlags);
	// 	return;
	// }

	// Call original function
	Table.Original<FN>(Index)(ecx, edx, novis, numorigins, origin, returnFlags);
}

void Hooks::RenderView::Init()
{
	if (I::RenderView == nullptr)
	{
		std::cerr << "[RenderView::Init] ERROR: I::RenderView interface is null!" << std::endl;
		return;
	}

	// 获取 VMT 指针
	uintptr_t* vmt = *(uintptr_t**)I::RenderView;

#ifdef DETECT_VMT_INDEX
	// ========== 索引检测模式（仅在启用 DETECT_VMT_INDEX 时编译） ==========
	std::cout << "\n[RenderView] ===== Index Detection Mode =====" << std::endl;
	std::cout << "[RenderView] Testing ViewSetupVisEx Index: " << ViewSetupVisEx::Index << std::endl;

	// 静态检查：验证函数指针是否有效
	if (Detail::IsIndexValid(vmt, ViewSetupVisEx::Index))
	{
		std::cout << "[RenderView] Index " << ViewSetupVisEx::Index << " function pointer: VALID (0x"
			<< std::hex << vmt[ViewSetupVisEx::Index] << std::dec << ")" << std::endl;
	}
	else
	{
		std::cerr << "[RenderView::Init] ERROR: Index " << ViewSetupVisEx::Index
			<< " has INVALID function pointer! Skipping ViewSetupVisEx hook." << std::endl;
		std::cerr << "[RenderView::Init] Try a different index in RenderView.h" << std::endl;
	}

	std::cout << "[RenderView] Will print report every " << Detail::s_ReportInterval << " calls" << std::endl;
	std::cout << "[RenderView] ===== Detection Started =====\n" << std::endl;
	// ========================================================================
#endif

	if (Table.Init(I::RenderView) == false)
	{
		std::cerr << "[RenderView::Init] ERROR: Failed to initialize VMT table!" << std::endl;
		return;
	}

	if (Table.Hook(&ViewSetupVis::Detour, ViewSetupVis::Index) == false)
	{
		std::cerr << "[RenderView::Init] ERROR: Failed to hook ViewSetupVis!" << std::endl;
		return;
	}

#ifdef DETECT_VMT_INDEX
	// 只有当索引有效时才 Hook ViewSetupVisEx
	if (Detail::IsIndexValid(vmt, ViewSetupVisEx::Index))
#endif
	{
		if (Table.Hook(&ViewSetupVisEx::Detour, ViewSetupVisEx::Index) == false)
		{
			std::cerr << "[RenderView::Init] ERROR: Failed to hook ViewSetupVisEx!" << std::endl;
			return;
		}
	}

#ifdef DETECT_VMT_INDEX
	if (Detail::IsIndexValid(vmt, ViewSetupVisEx::Index))
	{
		std::cout << "[RenderView::Init] Successfully hooked ViewSetupVis (Index " << ViewSetupVis::Index
			<< ") and ViewSetupVisEx (Index " << ViewSetupVisEx::Index << ")" << std::endl;
	}
	else
	{
		std::cout << "[RenderView::Init] Successfully hooked ViewSetupVis (Index " << ViewSetupVis::Index
			<< ") - ViewSetupVisEx SKIPPED (invalid index)" << std::endl;
	}
#else
	std::cout << "[RenderView::Init] Successfully hooked ViewSetupVis (Index " << ViewSetupVis::Index
		<< ") and ViewSetupVisEx (Index " << ViewSetupVisEx::Index << ")" << std::endl;
#endif
}
