#include "CCSGameMovement.h"
#include "../../Portal/L4D2_Portal.h"

using namespace Hooks;

void __fastcall CCSGameMovement::TracePlayerBBox::Detour(void* ecx, void* edx, const Vector& start, const Vector& end, unsigned int fMask, int collisionGroup, void* pm)
{
	// 打印调试信息
	//printf("[GameMovement] TracePlayerBBox called!\n");
	//printf("[GameMovement] Parameters: start=(%f,%f,%f), end=(%f,%f,%f)\n",
	//	start.x, start.y, start.z,
	//	start.x, start.y, start.z);
	//printf("[GameMovement] pTrace: %p\n", &pm);

	// 调用原始函数
	Func.Original<FN>()(ecx, edx, start, end, fMask, collisionGroup, pm);	
	return;
}

void CCSGameMovement::Init()
{
	//TracePlayerBBox
	{
		using namespace TracePlayerBBox;

		const FN ctracePlayerBBox = reinterpret_cast<FN>(U::Offsets.m_dwTracePlayerBBox);
		printf("[CCSGameMovement] TracePlayerBBox: %p\n", ctracePlayerBBox);
		XASSERT(ctracePlayerBBox == nullptr);

		if (ctracePlayerBBox)
			XASSERT(Func.Init(ctracePlayerBBox, &Detour) == false);
	}
}