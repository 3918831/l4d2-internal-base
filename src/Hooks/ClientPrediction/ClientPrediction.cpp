#include "ClientPrediction.h"
#include "../../Portal/L4D2_Portal.h"
#include "../../Portal/PortalTransitionDecision.h"
#include "../../Util/Logger/Logger.h"

using namespace Hooks;

namespace
{
	bool TrySyncCommittedMoveBeforeControlledPrediction(const char* phaseName, CUserCmd* ucmd, CMoveData* move)
	{
		if (!ucmd || !move)
			return false;

		const PortalTransitionContext& context = G::G_L4D2Portal.m_PortalTransitionSimulator.GetContext();
		Vector committedOrigin;
		Vector committedVelocity;
		QAngle committedAngles;
		if (!G::G_L4D2Portal.m_PortalTransitionSimulator.TryGetCommittedMovementForCommand(
			ucmd->command_number,
			&committedOrigin,
			&committedVelocity,
			&committedAngles))
		{
			return false;
		}

		const Vector originBefore = move->GetAbsOrigin();
		const Vector velocityBefore = move->m_vecVelocity;
		const Vector commandAnglesBefore = ucmd->viewangles;
		const float originDeltaSqr = (originBefore - committedOrigin).LenghtSqr();
		if (!PortalTransitionDecision::ShouldSyncCommittedMoveBeforeControlledMove(context.phase, originDeltaSqr))
			return false;

		move->SetAbsOrigin(committedOrigin);
		move->m_vecVelocity = committedVelocity;
		move->m_vecViewAngles = committedAngles;
		move->m_vecAbsViewAngles = committedAngles;
		move->m_vecAngles = committedAngles;
		if (PortalTransitionDecision::ShouldSyncCommittedViewAnglesWithMove(context.phase, true))
			ucmd->viewangles = Vector(committedAngles.x, committedAngles.y, committedAngles.z);
		if (I::EngineClient)
		{
			Vector engineAngles(committedAngles.x, committedAngles.y, committedAngles.z);
			I::EngineClient->SetViewAngles(engineAngles);
		}

		U::LogInfo("[PortalPredictionSync] phase=%s cmd=%d ctxPhase=%d origin=(%.1f %.1f %.1f)->(%.1f %.1f %.1f) vel=(%.1f %.1f %.1f)->(%.1f %.1f %.1f) delta=%.1f cmdAngles=(%.1f %.1f %.1f)->(%.1f %.1f %.1f) angles=(%.1f %.1f %.1f).\n",
			phaseName ? phaseName : "unknown",
			ucmd->command_number,
			static_cast<int>(context.phase),
			originBefore.x, originBefore.y, originBefore.z,
			committedOrigin.x, committedOrigin.y, committedOrigin.z,
			velocityBefore.x, velocityBefore.y, velocityBefore.z,
			committedVelocity.x, committedVelocity.y, committedVelocity.z,
			std::sqrt(originDeltaSqr),
			commandAnglesBefore.x, commandAnglesBefore.y, commandAnglesBefore.z,
			ucmd->viewangles.x, ucmd->viewangles.y, ucmd->viewangles.z,
			committedAngles.x, committedAngles.y, committedAngles.z);
		return true;
	}
}

void __fastcall ClientPrediction::RunCommand::Detour(void* ecx, void* edx, C_BasePlayer* player, CUserCmd* ucmd, IMoveHelper* moveHelper)
{
	Table.Original<FN>(Index)(ecx, edx, player, ucmd, moveHelper);
}

void __fastcall ClientPrediction::SetupMove::Detour(void* ecx, void* edx, C_BasePlayer* player, CUserCmd* ucmd, IMoveHelper* pHelper, CMoveData* move)
{
	Table.Original<FN>(Index)(ecx, edx, player, ucmd, pHelper, move);
	TrySyncCommittedMoveBeforeControlledPrediction("setupmove", ucmd, move);
}

void __fastcall ClientPrediction::FinishMove::Detour(void* ecx, void* edx, C_BasePlayer* player, CUserCmd* ucmd, CMoveData* move)
{
	const PortalMoveFrameDiagnostics before = G::G_L4D2Portal.m_PortalStage1Probe.CaptureMoveFrame(
		player,
		ucmd,
		move,
		G::G_L4D2Portal.m_PortalTransitionSimulator);
	Table.Original<FN>(Index)(ecx, edx, player, ucmd, move);
	static float nextMoveTypeProbeTime = 0.0f;
	const float currentTime = I::EngineClient ? I::EngineClient->OBSOLETE_Time() : 0.0f;
	if (currentTime >= nextMoveTypeProbeTime)
	{
		nextMoveTypeProbeTime = currentTime + 0.35f;
		G::G_L4D2Portal.m_PortalTransition.LogMoveTypeProbe("client-prediction-finishmove", player, move);
	}
	const PortalMoveFrameDiagnostics after = G::G_L4D2Portal.m_PortalStage1Probe.CaptureMoveFrame(
		player,
		ucmd,
		move,
		G::G_L4D2Portal.m_PortalTransitionSimulator);
	G::G_L4D2Portal.m_PortalStage1Probe.LogFinishMoveDiagnostics(before, after);
	G::G_L4D2Portal.m_PortalStage1Probe.LogFrameTraceDiagnostics(
		G::G_L4D2Portal.m_PortalCollisionBridge,
		after);
}

void ClientPrediction::Init()
{
	XASSERT(Table.Init(I::Prediction) == false);
	XASSERT(Table.Hook(&RunCommand::Detour, RunCommand::Index) == false);
	XASSERT(Table.Hook(&SetupMove::Detour, SetupMove::Index) == false);
	XASSERT(Table.Hook(&FinishMove::Detour, FinishMove::Index) == false);
}
