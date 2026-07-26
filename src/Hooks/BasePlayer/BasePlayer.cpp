#include "BasePlayer.h"

#include "../../Features/Vars.h"
#pragma warning(push)
#pragma warning(disable: 4819)
#include "../../Portal/L4D2_Portal.h"
#pragma warning(pop)

using namespace Hooks;

void __fastcall BasePlayer::CalcPlayerView::Detour(C_BasePlayer* pThis, void* edx, Vector& eyeOrigin, Vector& eyeAngles, float& fov)
{
	if (pThis && !pThis->deadflag()) //Thanks Spook for telling me to do it here.
	{
		const Vector vOldPunch = pThis->GetPunchAngle();

		pThis->m_vecPunchAngle().Init();
		Func.Original<FN>()(pThis, edx, eyeOrigin, eyeAngles, fov);
		pThis->m_vecPunchAngle() = vOldPunch;
	}
	else
	{
		Func.Original<FN>()(pThis, edx, eyeOrigin, eyeAngles, fov);
	}

	// Match Portal's client-side CalcPortalView handoff: once the local eye has
	// crossed the entry plane, render from its linked-space transform while the
	// physical movement command is still waiting to commit Teleport.
	G::G_L4D2Portal.m_PortalTransition.ApplyEntryCameraHandoff(pThis, eyeOrigin, eyeAngles);
}

void BasePlayer::Init()
{
	//CalcPlayerView
	{
		using namespace CalcPlayerView;

		const FN pfCalcPlayerView = reinterpret_cast<FN>(U::Offsets.m_dwCalcPlayerView);
		XASSERT(pfCalcPlayerView == nullptr);

		if (pfCalcPlayerView)
			XASSERT(Func.Init(pfCalcPlayerView, &Detour) == false);
	}
}
