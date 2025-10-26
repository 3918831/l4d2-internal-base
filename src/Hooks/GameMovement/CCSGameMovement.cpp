#include "CCSGameMovement.h"
#include "../../Portal/L4D2_Portal.h"

using namespace Hooks;

void __fastcall CCSGameMovement::TracePlayerBBox::Detour(void* ecx, void* edx, const Vector& start, const Vector& end, unsigned int fMask, int collisionGroup, trace_t* pm)
{
	// 打印调试信息
	//printf("[GameMovement] TracePlayerBBox called!\n");
	//printf("[GameMovement] Parameters: start=(%f,%f,%f), end=(%f,%f,%f)\n",
	//	start.x, start.y, start.z,
	//	start.x, start.y, start.z);
	//printf("[GameMovement] pTrace: %p\n", &pm);

	// 调用原始函数
	Func.Original<FN>()(ecx, edx, start, end, fMask, collisionGroup, pm);
	//pm->fraction = 1.0f;  // 设置为1.0表示射线到达终点，没有发生碰撞
	//pm->allsolid = true;     // 不是完全固体
	//pm->startsolid = true;   // 起始点不在固体中
	//pm->contents = 0;         // 无特殊内容标志
	//pm->endpos = end;         // 结束位置设为目标位置

	//// 如果想更真实，可以保留原始起点
	//pm->startpos = start;

	//// 清除命中实体信息
	//pm->m_pEnt = NULL;
	pm->startsolid = false;
	pm->allsolid = false; // 同样清除allsolid标志
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