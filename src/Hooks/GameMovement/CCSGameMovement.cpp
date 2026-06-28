#include "CCSGameMovement.h"
#include "../../Portal/L4D2_Portal.h"
#include "../../Util/Logger/Logger.h"
#include "../../Util/Logger/PortalFileLog.h"
#include <intrin.h>
#include <cmath>
#include <cstring>

using namespace Hooks;

namespace
{
	const char* PortalTraceClassName(PortalTraceClass traceClass)
	{
		switch (traceClass)
		{
		case PortalTraceClass::HorizontalMove: return "HorizontalMove";
		case PortalTraceClass::ZeroLengthPositionTest: return "ZeroLengthPositionTest";
		case PortalTraceClass::VerticalGroundProbe: return "VerticalGroundProbe";
		case PortalTraceClass::StepUpDownProbe: return "StepUpDownProbe";
		case PortalTraceClass::Other:
		default:
			return "Other";
		}
	}

	const char* PortalPhaseName(PortalTransitionPhase phase)
	{
		switch (phase)
		{
		case PortalTransitionPhase::Idle: return "Idle";
		case PortalTransitionPhase::ApproachingPortal: return "ApproachingPortal";
		case PortalTransitionPhase::IntersectingPortal: return "IntersectingPortal";
		case PortalTransitionPhase::CommittingTeleport: return "CommittingTeleport";
		case PortalTransitionPhase::ExitingPortal: return "ExitingPortal";
		case PortalTransitionPhase::Cooldown: return "Cooldown";
		default: return "Unknown";
		}
	}

	const char* PortalSideName(PortalTransitionSide side)
	{
		switch (side)
		{
		case PortalTransitionSide::Blue: return "Blue";
		case PortalTransitionSide::Orange: return "Orange";
		case PortalTransitionSide::None:
		default:
			return "None";
		}
	}

	const char* BoolText(bool value)
	{
		return value ? "true" : "false";
	}

	uintptr_t ModuleRelativeAddress(const char* moduleName, const void* address)
	{
		const HMODULE module = GetModuleHandleA(moduleName);
		if (!module || !address)
			return 0u;

		const uintptr_t absolute = reinterpret_cast<uintptr_t>(address);
		const uintptr_t base = reinterpret_cast<uintptr_t>(module);
		return absolute >= base ? absolute - base : 0u;
	}

	enum class MovementStage
	{
		None,
		PlayerMove,
		FullWalkMove,
		WalkMove,
	};

	const char* MovementStageName(MovementStage stage)
	{
		switch (stage)
		{
		case MovementStage::PlayerMove: return "PlayerMove";
		case MovementStage::FullWalkMove: return "FullWalkMove";
		case MovementStage::WalkMove: return "WalkMove";
		case MovementStage::None:
		default:
			return "None";
		}
	}

	thread_local MovementStage g_CurrentMovementStage = MovementStage::None;
	thread_local const char* g_CurrentMovementDomain = "none";
	void* g_ServerStayOnGroundCandidateA = nullptr;

	bool IsReadableAddressRange(const void* address, size_t size)
	{
		if (!address || size == 0)
			return false;

		const uintptr_t start = reinterpret_cast<uintptr_t>(address);
		const uintptr_t end = start + size - 1u;
		MEMORY_BASIC_INFORMATION info = {};
		if (!VirtualQuery(reinterpret_cast<const void*>(start), &info, sizeof(info)))
			return false;

		const bool firstReadable = info.State == MEM_COMMIT
			&& !(info.Protect & PAGE_NOACCESS)
			&& !(info.Protect & PAGE_GUARD);
		if (!firstReadable)
			return false;

		if (end < reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize)
			return true;

		MEMORY_BASIC_INFORMATION endInfo = {};
		if (!VirtualQuery(reinterpret_cast<const void*>(end), &endInfo, sizeof(endInfo)))
			return false;

		return endInfo.State == MEM_COMMIT
			&& !(endInfo.Protect & PAGE_NOACCESS)
			&& !(endInfo.Protect & PAGE_GUARD);
	}

	bool LooksLikeFunctionPrologue(const unsigned char* bytes)
	{
		return (bytes[0] == 0x55 && bytes[1] == 0x8B && bytes[2] == 0xEC)
			|| (bytes[0] == 0x55 && bytes[1] == 0x89 && bytes[2] == 0xE5)
			|| (bytes[0] == 0x53 && bytes[1] == 0x8B && bytes[2] == 0xDC);
	}

	void* FindNearbyFunctionPrologue(const void* address, size_t maxBack)
	{
		if (!address)
			return nullptr;

		const uintptr_t start = reinterpret_cast<uintptr_t>(address);
		for (size_t back = 0; back <= maxBack; ++back)
		{
			const uintptr_t probe = start - back;
			if (!IsReadableAddressRange(reinterpret_cast<const void*>(probe), 8u))
				continue;

			const unsigned char* bytes = reinterpret_cast<const unsigned char*>(probe);
			if (LooksLikeFunctionPrologue(bytes))
				return reinterpret_cast<void*>(probe);
		}

		return nullptr;
	}

	class MovementStageScope
	{
	public:
		MovementStageScope(const char* domain, MovementStage stage)
			: m_PreviousStage(g_CurrentMovementStage),
			m_PreviousDomain(g_CurrentMovementDomain)
		{
			g_CurrentMovementStage = stage;
			g_CurrentMovementDomain = domain;
		}

		~MovementStageScope()
		{
			g_CurrentMovementStage = m_PreviousStage;
			g_CurrentMovementDomain = m_PreviousDomain;
		}

	private:
		MovementStage m_PreviousStage;
		const char* m_PreviousDomain;
	};

	CMoveData* TryGetMoveDataFromGameMovement(void* gameMovement)
	{
		if (!gameMovement)
			return nullptr;

		// Official CGameMovement layout has the vptr, then player, then mv.
		// Treat this as diagnostic-only until verified against L4D2.
		return *reinterpret_cast<CMoveData**>(reinterpret_cast<uintptr_t>(gameMovement) + (sizeof(void*) * 2u));
	}

	void LogServerMoveData(const char* tag, void* gameMovement, const CMoveData* move)
	{
		if (!move)
		{
			U::LogWarning("[PortalBridge][ServerMove][%s] gm=%p mv=null.\n", tag, gameMovement);
			return;
		}

		const Vector& origin = move->GetAbsOrigin();
		const Vector& velocity = move->m_vecVelocity;
		U::LogWarning("[PortalBridge][ServerMove][%s] gm=%p mv=%p origin=(%.1f %.1f %.1f) vel=(%.1f %.1f %.1f) buttons=0x%X fmove=%.1f smove=%.1f gameCodeMoved=%s.\n",
			tag,
			gameMovement,
			move,
			origin.x, origin.y, origin.z,
			velocity.x, velocity.y, velocity.z,
			move->m_nButtons,
			move->m_flForwardMove,
			move->m_flSideMove,
			BoolText(move->m_bGameCodeMovedPlayer));
	}

	bool ShouldLogServerMovement()
	{
		return false;
	}

	bool ShouldLogMovementHeartbeat(uint32_t counter)
	{
		(void)counter;
		return false;
	}

	void LogTryPlayerMoveEnter(const char* domain, uint32_t heartbeat, void* gameMovement, Vector* pFirstDest, trace_t* pFirstTrace, CMoveData* move, bool detailed)
	{
		if (!detailed && !ShouldLogMovementHeartbeat(heartbeat))
			return;

		const PortalCollisionBridgeDiagnostics& diag = G::G_L4D2Portal.m_PortalCollisionBridge.GetDiagnostics();
		U::LogWarning("[PortalBridge][%s][TryPlayerMove][Enter] heartbeat=%u detailed=%s cmd=%d gm=%p firstDest=%p firstTrace=%p dest=(%.1f %.1f %.1f).\n",
			domain,
			heartbeat,
			BoolText(detailed),
			diag.frameCommandNumber,
			gameMovement,
			pFirstDest,
			pFirstTrace,
			pFirstDest ? pFirstDest->x : 0.0f,
			pFirstDest ? pFirstDest->y : 0.0f,
			pFirstDest ? pFirstDest->z : 0.0f);
		LogServerMoveData(domain, gameMovement, move);
	}

	void LogTryPlayerMoveExit(const char* domain, uint32_t heartbeat, int result, void* gameMovement, Vector* pFirstDest, trace_t* pFirstTrace, CMoveData* move, bool detailed)
	{
		if (!detailed && !ShouldLogMovementHeartbeat(heartbeat))
			return;

		U::LogWarning("[PortalBridge][%s][TryPlayerMove][Exit] heartbeat=%u detailed=%s result=%d firstDest=%p firstTrace=%p traceEnd=(%.1f %.1f %.1f) tracePlane=(%.2f %.2f %.2f) traceFrac=%.3f startsolid=%s allsolid=%s.\n",
			domain,
			heartbeat,
			BoolText(detailed),
			result,
			pFirstDest,
			pFirstTrace,
			pFirstTrace ? pFirstTrace->endpos.x : 0.0f,
			pFirstTrace ? pFirstTrace->endpos.y : 0.0f,
			pFirstTrace ? pFirstTrace->endpos.z : 0.0f,
			pFirstTrace ? pFirstTrace->plane.normal.x : 0.0f,
			pFirstTrace ? pFirstTrace->plane.normal.y : 0.0f,
			pFirstTrace ? pFirstTrace->plane.normal.z : 0.0f,
			pFirstTrace ? pFirstTrace->fraction : 0.0f,
			BoolText(pFirstTrace ? pFirstTrace->startsolid : false),
			BoolText(pFirstTrace ? pFirstTrace->allsolid : false));
		LogServerMoveData(domain, gameMovement, move);
	}

	void LogStepMoveEnter(const char* domain, uint32_t heartbeat, void* gameMovement, Vector& vecDestination, trace_t& trace, CMoveData* move, bool detailed)
	{
		if (!detailed && !ShouldLogMovementHeartbeat(heartbeat))
			return;

		const PortalCollisionBridgeDiagnostics& diag = G::G_L4D2Portal.m_PortalCollisionBridge.GetDiagnostics();
		U::LogWarning("[PortalBridge][%s][StepMove][Enter] heartbeat=%u detailed=%s cmd=%d gm=%p dest=(%.1f %.1f %.1f) traceEnd=(%.1f %.1f %.1f) tracePlane=(%.2f %.2f %.2f) traceFrac=%.3f startsolid=%s allsolid=%s.\n",
			domain,
			heartbeat,
			BoolText(detailed),
			diag.frameCommandNumber,
			gameMovement,
			vecDestination.x, vecDestination.y, vecDestination.z,
			trace.endpos.x, trace.endpos.y, trace.endpos.z,
			trace.plane.normal.x, trace.plane.normal.y, trace.plane.normal.z,
			trace.fraction,
			BoolText(trace.startsolid),
			BoolText(trace.allsolid));
		LogServerMoveData(domain, gameMovement, move);
	}

	void LogStepMoveExit(const char* domain, void* gameMovement, Vector& vecDestination, trace_t& trace, CMoveData* move, bool detailed)
	{
		if (!detailed)
			return;

		U::LogWarning("[PortalBridge][%s][StepMove][Exit] gm=%p dest=(%.1f %.1f %.1f) traceEnd=(%.1f %.1f %.1f) tracePlane=(%.2f %.2f %.2f) traceFrac=%.3f startsolid=%s allsolid=%s.\n",
			domain,
			gameMovement,
			vecDestination.x, vecDestination.y, vecDestination.z,
			trace.endpos.x, trace.endpos.y, trace.endpos.z,
			trace.plane.normal.x, trace.plane.normal.y, trace.plane.normal.z,
			trace.fraction,
			BoolText(trace.startsolid),
			BoolText(trace.allsolid));
		LogServerMoveData(domain, gameMovement, move);
	}

	void LogMovementStageSnapshot(const char* domain, const char* stage, const char* point, uint32_t heartbeat, void* gameMovement, bool detailed)
	{
		if (!detailed && !ShouldLogMovementHeartbeat(heartbeat))
			return;

		const PortalCollisionBridgeDiagnostics& diag = G::G_L4D2Portal.m_PortalCollisionBridge.GetDiagnostics();
		CMoveData* move = TryGetMoveDataFromGameMovement(gameMovement);
		if (!move)
		{
			U::LogWarning("[PortalBridge][StageProbe][%s][%s][%s] heartbeat=%u detailed=%s cmd=%d phase=%s entry=%s gm=%p mv=null.\n",
				domain,
				stage,
				point,
				heartbeat,
				BoolText(detailed),
				diag.frameCommandNumber,
				PortalPhaseName(diag.lastPhase),
				PortalSideName(diag.lastEntrySide),
				gameMovement);
			return;
		}

		const Vector& origin = move->GetAbsOrigin();
		const Vector& velocity = move->m_vecVelocity;
		U::LogWarning("[PortalBridge][StageProbe][%s][%s][%s] heartbeat=%u detailed=%s cmd=%d phase=%s entry=%s bridge=%s gm=%p mv=%p origin=(%.1f %.1f %.1f) vel=(%.1f %.1f %.1f) buttons=0x%X fmove=%.1f smove=%.1f step=%.2f gameMoved=%s.\n",
			domain,
			stage,
			point,
			heartbeat,
			BoolText(detailed),
			diag.frameCommandNumber,
			PortalPhaseName(diag.lastPhase),
			PortalSideName(diag.lastEntrySide),
			BoolText(G::G_L4D2Portal.m_PortalTransitionSimulator.IsInCollisionBridgePhase()),
			gameMovement,
			move,
			origin.x, origin.y, origin.z,
			velocity.x, velocity.y, velocity.z,
			move->m_nButtons,
			move->m_flForwardMove,
			move->m_flSideMove,
			move->m_outStepHeight,
			BoolText(move->m_bGameCodeMovedPlayer));
	}

	const PortalInfo_t* GetPortalInfoForSide(PortalTransitionSide side)
	{
		switch (side)
		{
		case PortalTransitionSide::Blue:
			return &G::G_L4D2Portal.g_BluePortal;
		case PortalTransitionSide::Orange:
			return &G::G_L4D2Portal.g_OrangePortal;
		case PortalTransitionSide::None:
		default:
			return nullptr;
		}
	}

	float ClampFloat(float value, float minValue, float maxValue)
	{
		return value < minValue ? minValue : (value > maxValue ? maxValue : value);
	}

	struct PortalApproachVelocitySample
	{
		PortalTransitionSide side = PortalTransitionSide::None;
		int commandNumber = 0;
		float inwardNormalVelocity = 0.0f;
		Vector velocity;
	};

	PortalApproachVelocitySample g_serverApproachVelocity;
	PortalApproachVelocitySample g_clientApproachVelocity;

	PortalApproachVelocitySample& ApproachVelocitySampleForDomain(const char* domain)
	{
		return domain && std::strcmp(domain, "client") == 0 ? g_clientApproachVelocity : g_serverApproachVelocity;
	}

	void UpdatePortalApproachVelocitySample(const char* domain, const Vector& velocityAfterOriginal)
	{
		const PortalTransitionContext& context = G::G_L4D2Portal.m_PortalTransitionSimulator.GetContext();
		if (context.phase != PortalTransitionPhase::ApproachingPortal
			|| !context.insideAperture
			|| !context.movingIntoPortal
			|| context.entrySide == PortalTransitionSide::None)
		{
			return;
		}

		const PortalInfo_t* entry = GetPortalInfoForSide(context.entrySide);
		if (!entry || !entry->bIsActive || entry->normal.LenghtSqr() < 0.25f)
			return;

		const float inwardNormalVelocity = velocityAfterOriginal.Dot(entry->normal);
		if (inwardNormalVelocity >= -20.0f)
			return;

		PortalApproachVelocitySample& sample = ApproachVelocitySampleForDomain(domain);
		sample.side = context.entrySide;
		sample.commandNumber = G::G_L4D2Portal.m_PortalCollisionBridge.GetDiagnostics().frameCommandNumber;
		sample.inwardNormalVelocity = inwardNormalVelocity;
		sample.velocity = velocityAfterOriginal;
	}

	bool TryGetRecentPortalApproachVelocity(const char* domain, PortalTransitionSide side, Vector* velocity)
	{
		const PortalApproachVelocitySample& sample = ApproachVelocitySampleForDomain(domain);
		const int commandNumber = G::G_L4D2Portal.m_PortalCollisionBridge.GetDiagnostics().frameCommandNumber;
		if (sample.side != side
			|| sample.inwardNormalVelocity >= -20.0f
			|| commandNumber < sample.commandNumber
			|| commandNumber - sample.commandNumber > 16)
		{
			return false;
		}

		if (velocity)
			*velocity = sample.velocity;
		return true;
	}

	float SelectPortalTargetNormalVelocity(const char* domain, PortalTransitionSide side, const Vector& fallbackVelocity, const Vector& normal)
	{
		float targetNormalVelocity = fallbackVelocity.Dot(normal);
		Vector sampledVelocity;
		if (TryGetRecentPortalApproachVelocity(domain, side, &sampledVelocity))
			targetNormalVelocity = sampledVelocity.Dot(normal);

		if (targetNormalVelocity > -80.0f)
			targetNormalVelocity = -80.0f;
		return ClampFloat(targetNormalVelocity, -260.0f, -80.0f);
	}

	Vector SelectPortalTargetVelocity(const char* domain, PortalTransitionSide side, const Vector& fallbackVelocity, const Vector& normal)
	{
		Vector targetVelocity = fallbackVelocity;
		Vector sampledVelocity;
		if (TryGetRecentPortalApproachVelocity(domain, side, &sampledVelocity))
			targetVelocity = sampledVelocity;

		const float targetNormalVelocity = SelectPortalTargetNormalVelocity(domain, side, fallbackVelocity, normal);
		const float normalVelocity = targetVelocity.Dot(normal);
		if (normalVelocity > targetNormalVelocity)
			targetVelocity = targetVelocity + (normal * (targetNormalVelocity - normalVelocity));

		return targetVelocity;
	}

	bool TryApplyPortalWalkMoveNudge(const char* domain, uint32_t heartbeat, void* gameMovement, const Vector& velocityBeforeOriginal, bool detailed)
	{
		const PortalTransitionContext& context = G::G_L4D2Portal.m_PortalTransitionSimulator.GetContext();
		if (!G::G_L4D2Portal.m_PortalTransitionSimulator.IsInCollisionBridgePhase()
			|| context.phase != PortalTransitionPhase::IntersectingPortal
			|| !context.insideAperture
			|| !context.movingIntoPortal
			|| context.entrySide == PortalTransitionSide::None)
		{
			return false;
		}

		const PortalCollisionBridgeDiagnostics& diag = G::G_L4D2Portal.m_PortalCollisionBridge.GetDiagnostics();
		const PortalFrameTraceSnapshot& horizontalTrace = diag.frameLastHorizontalTrace;
		if (!horizontalTrace.hasTrace || !horizontalTrace.accepted)
			return false;

		const PortalInfo_t* entry = GetPortalInfoForSide(context.entrySide);
		if (!entry || !entry->bIsActive || entry->normal.LenghtSqr() < 0.25f)
			return false;

		CMoveData* move = TryGetMoveDataFromGameMovement(gameMovement);
		if (!move)
			return false;

		const Vector originBefore = move->GetAbsOrigin();
		const Vector velocityBefore = move->m_vecVelocity;
		const float currentDepth = (originBefore - entry->origin).Dot(entry->normal);
		if (currentDepth < -8.0f || currentDepth > 32.0f)
			return false;

		const float targetNormalVelocity = SelectPortalTargetNormalVelocity(domain, context.entrySide, velocityBeforeOriginal, entry->normal);

		const float nudgeDistance = ClampFloat(-targetNormalVelocity * (1.0f / 30.0f), 3.0f, 8.0f);
		const Vector originAfter = originBefore - (entry->normal * nudgeDistance);
		const Vector velocityAfter = SelectPortalTargetVelocity(domain, context.entrySide, velocityBeforeOriginal, entry->normal);

		move->SetAbsOrigin(originAfter);
		move->m_vecVelocity = velocityAfter;

		Vector committedOrigin;
		Vector committedVelocity;
		const bool predictedTeleport = G::G_L4D2Portal.m_PortalTransitionSimulator.TryCommitMovementCrossing(
			diag.frameCommandNumber,
			originAfter,
			velocityAfter,
			&committedOrigin,
			&committedVelocity);
		if (predictedTeleport)
		{
			move->SetAbsOrigin(committedOrigin);
			move->m_vecVelocity = committedVelocity;
		}

		{
			U::LogWarning("[PortalBridge][WalkMoveNudge][%s] heartbeat=%u cmd=%d phase=%s entry=%s ctxDepth=%.2f curDepth=%.2f targetNorm=%.1f nudge=%.2f predictedTeleport=%s traceEnd=(%.1f %.1f %.1f) origin=(%.1f %.1f %.1f)->(%.1f %.1f %.1f) vel=(%.1f %.1f %.1f)->(%.1f %.1f %.1f) normal=(%.2f %.2f %.2f).\n",
				domain,
				heartbeat,
				diag.frameCommandNumber,
				PortalPhaseName(context.phase),
				PortalSideName(context.entrySide),
				context.signedDepth,
				currentDepth,
				targetNormalVelocity,
				nudgeDistance,
				BoolText(predictedTeleport),
				horizontalTrace.end.x, horizontalTrace.end.y, horizontalTrace.end.z,
				originBefore.x, originBefore.y, originBefore.z,
				originAfter.x, originAfter.y, originAfter.z,
				velocityBefore.x, velocityBefore.y, velocityBefore.z,
				velocityAfter.x, velocityAfter.y, velocityAfter.z,
				entry->normal.x, entry->normal.y, entry->normal.z);
		}

		return true;
	}

	bool TrySyncPortalCommittedMovement(const char* domain, uint32_t heartbeat, void* gameMovement)
	{
		CMoveData* move = TryGetMoveDataFromGameMovement(gameMovement);
		if (!move)
			return false;

		const PortalCollisionBridgeDiagnostics& diag = G::G_L4D2Portal.m_PortalCollisionBridge.GetDiagnostics();
		Vector committedOrigin;
		Vector committedVelocity;
		QAngle committedAngles;
		if (!G::G_L4D2Portal.m_PortalTransitionSimulator.TryGetCommittedMovementForCommand(
			diag.frameCommandNumber,
			&committedOrigin,
			&committedVelocity,
			&committedAngles))
		{
			return false;
		}

		const Vector originBefore = move->GetAbsOrigin();
		const Vector velocityBefore = move->m_vecVelocity;
		if ((originBefore - committedOrigin).LenghtSqr() < (48.0f * 48.0f))
			return false;

		move->SetAbsOrigin(committedOrigin);
		move->m_vecVelocity = committedVelocity;
		if (I::EngineClient)
		{
			Vector engineAngles(committedAngles.x, committedAngles.y, committedAngles.z);
			I::EngineClient->SetViewAngles(engineAngles);
		}

		U::LogWarning("[PortalBridge][CommittedMoveSync][%s] heartbeat=%u cmd=%d origin=(%.1f %.1f %.1f)->(%.1f %.1f %.1f) vel=(%.1f %.1f %.1f)->(%.1f %.1f %.1f) angles=(%.1f %.1f %.1f).\n",
			domain,
			heartbeat,
			diag.frameCommandNumber,
			originBefore.x, originBefore.y, originBefore.z,
			committedOrigin.x, committedOrigin.y, committedOrigin.z,
			velocityBefore.x, velocityBefore.y, velocityBefore.z,
			committedVelocity.x, committedVelocity.y, committedVelocity.z,
			committedAngles.x, committedAngles.y, committedAngles.z);
		return true;
	}

	bool TryPreservePortalExitVelocity(const char* domain, uint32_t heartbeat, void* gameMovement, const Vector& velocityBeforeOriginal)
	{
		const PortalTransitionContext& context = G::G_L4D2Portal.m_PortalTransitionSimulator.GetContext();
		if (context.phase != PortalTransitionPhase::ExitingPortal || context.exitSide == PortalTransitionSide::None)
			return false;

		const PortalInfo_t* exit = GetPortalInfoForSide(context.exitSide);
		if (!exit || !exit->bIsActive || exit->normal.LenghtSqr() < 0.25f)
			return false;

		CMoveData* move = TryGetMoveDataFromGameMovement(gameMovement);
		if (!move)
			return false;

		const float targetNormalVelocity = velocityBeforeOriginal.Dot(exit->normal);
		if (targetNormalVelocity < 20.0f)
			return false;

		const Vector velocityBefore = move->m_vecVelocity;
		Vector velocityAfter = velocityBefore;
		const float currentNormalVelocity = velocityAfter.Dot(exit->normal);
		if (currentNormalVelocity >= targetNormalVelocity * 0.75f)
			return false;

		velocityAfter = velocityAfter + (exit->normal * (targetNormalVelocity - currentNormalVelocity));
		move->m_vecVelocity = velocityAfter;

		const PortalCollisionBridgeDiagnostics& diag = G::G_L4D2Portal.m_PortalCollisionBridge.GetDiagnostics();
		U::LogWarning("[PortalBridge][ExitVelocityPreserve][%s] heartbeat=%u cmd=%d exit=%s targetNorm=%.1f currentNorm=%.1f vel=(%.1f %.1f %.1f)->(%.1f %.1f %.1f).\n",
			domain,
			heartbeat,
			diag.frameCommandNumber,
			PortalSideName(context.exitSide),
			targetNormalVelocity,
			currentNormalVelocity,
			velocityBefore.x, velocityBefore.y, velocityBefore.z,
			velocityAfter.x, velocityAfter.y, velocityAfter.z);
		return true;
	}

	void LogPortalWalkMoveFrame(
        const char* domain,
        uint32_t heartbeat,
        void* gameMovement,
        const Vector& originBefore,
        const Vector& velocityBefore,
        const Vector& originAfterOriginal,
        const Vector& velocityAfterOriginal,
        bool bridgeApplied)
    {
        if (!G::G_L4D2Portal.m_PortalTransitionSimulator.IsInCollisionBridgePhase())
            return;

        CMoveData* move = TryGetMoveDataFromGameMovement(gameMovement);
        if (!move)
            return;

        const PortalCollisionBridgeDiagnostics& diag = G::G_L4D2Portal.m_PortalCollisionBridge.GetDiagnostics();
        const PortalTransitionContext& context = G::G_L4D2Portal.m_PortalTransitionSimulator.GetContext();
        const Vector originAfterBridge = move->GetAbsOrigin();
        const Vector velocityAfterBridge = move->m_vecVelocity;
        U::PortalFileLog::WriteFormat(
            "[PortalWalkMoveFrame] domain=%s heartbeat=%u cmd=%d phase=%s side=%s bridgeApplied=%s originBefore=(%.2f %.2f %.2f) originAfterOriginal=(%.2f %.2f %.2f) originAfterBridge=(%.2f %.2f %.2f) velocityBefore=(%.2f %.2f %.2f) velocityAfterOriginal=(%.2f %.2f %.2f) velocityAfterBridge=(%.2f %.2f %.2f) speedBefore=%.2f speedAfterOriginal=%.2f speedAfterBridge=%.2f\n",
            domain,
            heartbeat,
            diag.frameCommandNumber,
            PortalPhaseName(context.phase),
            PortalSideName(G::G_L4D2Portal.m_PortalTransitionSimulator.GetCollisionBridgeSide()),
            BoolText(bridgeApplied),
            originBefore.x, originBefore.y, originBefore.z,
            originAfterOriginal.x, originAfterOriginal.y, originAfterOriginal.z,
            originAfterBridge.x, originAfterBridge.y, originAfterBridge.z,
            velocityBefore.x, velocityBefore.y, velocityBefore.z,
            velocityAfterOriginal.x, velocityAfterOriginal.y, velocityAfterOriginal.z,
            velocityAfterBridge.x, velocityAfterBridge.y, velocityAfterBridge.z,
            std::sqrt(velocityBefore.LenghtSqr()),
            std::sqrt(velocityAfterOriginal.LenghtSqr()),
            std::sqrt(velocityAfterBridge.LenghtSqr()));
    }
void LogCategorizeSnapshot(const char* domain, const char* point, uint32_t heartbeat, void* gameMovement, bool detailed)
	{
		if (!detailed && !ShouldLogMovementHeartbeat(heartbeat))
			return;

		const PortalCollisionBridgeDiagnostics& diag = G::G_L4D2Portal.m_PortalCollisionBridge.GetDiagnostics();
		const PortalTransitionContext& context = G::G_L4D2Portal.m_PortalTransitionSimulator.GetContext();
		CMoveData* move = TryGetMoveDataFromGameMovement(gameMovement);
		if (!move)
		{
			U::LogWarning("[PortalBridge][PositionProbe][%s][CategorizePosition][%s] heartbeat=%u detailed=%s cmd=%d phase=%s entry=%s ctxDepth=%.2f inside=%s moving=%s gm=%p mv=null.\n",
				domain,
				point,
				heartbeat,
				BoolText(detailed),
				diag.frameCommandNumber,
				PortalPhaseName(diag.lastPhase),
				PortalSideName(diag.lastEntrySide),
				context.signedDepth,
				BoolText(context.insideAperture),
				BoolText(context.movingIntoPortal),
				gameMovement);
			return;
		}

		const Vector& moveOrigin = move->GetAbsOrigin();
		const Vector& moveVelocity = move->m_vecVelocity;
		U::LogWarning("[PortalBridge][PositionProbe][%s][CategorizePosition][%s] heartbeat=%u detailed=%s cmd=%d phase=%s entry=%s bridge=%s ctxDepth=%.2f inside=%s moving=%s gm=%p mv=%p moveOrigin=(%.1f %.1f %.1f) moveVel=(%.1f %.1f %.1f) step=%.2f gameMoved=%s.\n",
			domain,
			point,
			heartbeat,
			BoolText(detailed),
			diag.frameCommandNumber,
			PortalPhaseName(diag.lastPhase),
			PortalSideName(diag.lastEntrySide),
			BoolText(G::G_L4D2Portal.m_PortalTransitionSimulator.IsInCollisionBridgePhase()),
			context.signedDepth,
			BoolText(context.insideAperture),
			BoolText(context.movingIntoPortal),
			gameMovement,
			move,
			moveOrigin.x, moveOrigin.y, moveOrigin.z,
			moveVelocity.x, moveVelocity.y, moveVelocity.z,
			move->m_outStepHeight,
			BoolText(move->m_bGameCodeMovedPlayer));
	}

	void LogTestPlayerPositionExit(const char* domain, uint32_t heartbeat, void* gameMovement, const Vector& pos, int collisionGroup, trace_t* pm, unsigned long result, bool detailed)
	{
		if (!detailed && !(pm && (pm->startsolid || pm->allsolid)))
			return;

		const PortalCollisionBridgeDiagnostics& diag = G::G_L4D2Portal.m_PortalCollisionBridge.GetDiagnostics();
		CMoveData* move = TryGetMoveDataFromGameMovement(gameMovement);
		const Vector moveOrigin = move ? move->GetAbsOrigin() : Vector{};
		const Vector moveVelocity = move ? move->m_vecVelocity : Vector{};
		U::LogWarning("[PortalBridge][PositionProbe][%s][TestPlayerPosition][Exit] heartbeat=%u detailed=%s cmd=%d phase=%s entry=%s bridge=%s gm=%p mv=%p pos=(%.1f %.1f %.1f) collisionGroup=%d result=0x%08X trace=%p end=(%.1f %.1f %.1f) plane=(%.2f %.2f %.2f) frac=%.3f startsolid=%s allsolid=%s contents=0x%X ent=%p moveOrigin=(%.1f %.1f %.1f) moveVel=(%.1f %.1f %.1f).\n",
			domain,
			heartbeat,
			BoolText(detailed),
			diag.frameCommandNumber,
			PortalPhaseName(diag.lastPhase),
			PortalSideName(diag.lastEntrySide),
			BoolText(G::G_L4D2Portal.m_PortalTransitionSimulator.IsInCollisionBridgePhase()),
			gameMovement,
			move,
			pos.x, pos.y, pos.z,
			collisionGroup,
			static_cast<unsigned int>(result),
			pm,
			pm ? pm->endpos.x : 0.0f, pm ? pm->endpos.y : 0.0f, pm ? pm->endpos.z : 0.0f,
			pm ? pm->plane.normal.x : 0.0f, pm ? pm->plane.normal.y : 0.0f, pm ? pm->plane.normal.z : 0.0f,
			pm ? pm->fraction : 0.0f,
			BoolText(pm ? pm->startsolid : false),
			BoolText(pm ? pm->allsolid : false),
			pm ? pm->contents : 0,
			pm ? pm->m_pEnt : nullptr,
			moveOrigin.x, moveOrigin.y, moveOrigin.z,
			moveVelocity.x, moveVelocity.y, moveVelocity.z);
	}


}

static void HandleTracePlayerBBox(const char* domain, const void* caller, CCSGameMovement::TracePlayerBBox::FN original, void* ecx, void* edx, const Vector& start, const Vector& end, unsigned int fMask, int collisionGroup, trace_t* pm)
{
	// 打印调试信息
	//U::LogDebug("[GameMovement] TracePlayerBBox called!\n");
	//U::LogDebug("[GameMovement] Parameters: start=(%f,%f,%f), end=(%f,%f,%f)\n",
	//	start.x, start.y, start.z,
	//	start.x, start.y, start.z);
	//U::LogDebug("[GameMovement] pTrace: %p\n", &pm);

	const bool shouldInspectMoveData = G::G_L4D2Portal.m_PortalTransitionSimulator.IsInCollisionBridgePhase();

	original(ecx, edx, start, end, fMask, collisionGroup, pm);
	if (!shouldInspectMoveData)
		return;

	PortalTraceRequest request;
	request.start = start;
	request.end = end;
	request.mask = fMask;
	request.collisionGroup = collisionGroup;
	request.trace = pm;
	(void)G::G_L4D2Portal.m_PortalCollisionBridge.TryBypassPlayerBBoxTrace(
		request,
		G::G_L4D2Portal.m_PortalTransitionSimulator);
	//pm->fraction = 1.0f;  // 设置�?.0表示射线到达终点，没有发生碰�?	//pm->allsolid = true;     // 不是完全固体
	//pm->startsolid = true;   // 起始点不在固体中
	//pm->contents = 0;         // 无特殊内容标�?	//pm->endpos = end;         // 结束位置设为目标位置

	//// 如果想更真实，可以保留原始起�?	//pm->startpos = start;

	//// 清除命中实体信息
	//pm->m_pEnt = NULL;

	(void)start;
	(void)end;
	(void)fMask;
	(void)collisionGroup;
	(void)pm;
	return;
}

void __fastcall CCSGameMovement::TracePlayerBBox::Detour(void* ecx, void* edx, const Vector& start, const Vector& end, unsigned int fMask, int collisionGroup, trace_t* pm)
{
	HandleTracePlayerBBox("server-signature", _ReturnAddress(), Func.Original<FN>(), ecx, edx, start, end, fMask, collisionGroup, pm);
}

void __fastcall CCSGameMovement::ServerTracePlayerBBox::Detour(void* ecx, void* edx, const Vector& start, const Vector& end, unsigned int fMask, int collisionGroup, trace_t* pm)
{
	HandleTracePlayerBBox("server", _ReturnAddress(), ServerTable.Original<FN>(Index), ecx, edx, start, end, fMask, collisionGroup, pm);
}

void __fastcall CCSGameMovement::ClientTracePlayerBBox::Detour(void* ecx, void* edx, const Vector& start, const Vector& end, unsigned int fMask, int collisionGroup, trace_t* pm)
{
	HandleTracePlayerBBox("client", _ReturnAddress(), ClientTable.Original<FN>(Index), ecx, edx, start, end, fMask, collisionGroup, pm);
}

int __fastcall CCSGameMovement::TryPlayerMove::Detour(void* ecx, void* edx, Vector* pFirstDest, trace_t* pFirstTrace)
{
	const bool log = ShouldLogServerMovement();
	static uint32_t heartbeat = 0u;
	++heartbeat;
	CMoveData* move = (log || ShouldLogMovementHeartbeat(heartbeat)) ? TryGetMoveDataFromGameMovement(ecx) : nullptr;
	LogTryPlayerMoveEnter("server", heartbeat, ecx, pFirstDest, pFirstTrace, move, log);

	const int result = ServerTable.Original<FN>(Index)(ecx, edx, pFirstDest, pFirstTrace);

	if (log)
	{
		move = TryGetMoveDataFromGameMovement(ecx);
		LogTryPlayerMoveExit("server", heartbeat, result, ecx, pFirstDest, pFirstTrace, move, log);
	}

	return result;
}

void __fastcall CCSGameMovement::StepMove::Detour(void* ecx, void* edx, Vector& vecDestination, trace_t& trace)
{
	const bool log = ShouldLogServerMovement();
	static uint32_t heartbeat = 0u;
	++heartbeat;
	CMoveData* move = (log || ShouldLogMovementHeartbeat(heartbeat)) ? TryGetMoveDataFromGameMovement(ecx) : nullptr;
	LogStepMoveEnter("server", heartbeat, ecx, vecDestination, trace, move, log);

	ServerTable.Original<FN>(Index)(ecx, edx, vecDestination, trace);

	if (log)
	{
		move = TryGetMoveDataFromGameMovement(ecx);
		LogStepMoveExit("server", ecx, vecDestination, trace, move, log);
	}
}

int __fastcall CCSGameMovement::ClientTryPlayerMove::Detour(void* ecx, void* edx, Vector* pFirstDest, trace_t* pFirstTrace)
{
	const bool log = ShouldLogServerMovement();
	static uint32_t heartbeat = 0u;
	++heartbeat;
	CMoveData* move = TryGetMoveDataFromGameMovement(ecx);
	LogTryPlayerMoveEnter("client", heartbeat, ecx, pFirstDest, pFirstTrace, move, log);

	const int result = ClientTable.Original<FN>(Index)(ecx, edx, pFirstDest, pFirstTrace);

	if (log)
	{
		move = TryGetMoveDataFromGameMovement(ecx);
		LogTryPlayerMoveExit("client", heartbeat, result, ecx, pFirstDest, pFirstTrace, move, log);
	}

	return result;
}

void __fastcall CCSGameMovement::ClientStepMove::Detour(void* ecx, void* edx, Vector& vecDestination, trace_t& trace)
{
	const bool log = ShouldLogServerMovement();
	static uint32_t heartbeat = 0u;
	++heartbeat;
	CMoveData* move = TryGetMoveDataFromGameMovement(ecx);
	LogStepMoveEnter("client", heartbeat, ecx, vecDestination, trace, move, log);

	ClientTable.Original<FN>(Index)(ecx, edx, vecDestination, trace);

	if (log)
	{
		move = TryGetMoveDataFromGameMovement(ecx);
		LogStepMoveExit("client", ecx, vecDestination, trace, move, log);
	}
}

void __fastcall CCSGameMovement::PlayerMove::Detour(void* ecx, void* edx)
{
	MovementStageScope stageScope("server", MovementStage::PlayerMove);
	const bool log = ShouldLogServerMovement();
	static uint32_t heartbeat = 0u;
	++heartbeat;
	LogMovementStageSnapshot("server", "PlayerMove", "Enter", heartbeat, ecx, log);
	ServerTable.Original<FN>(Index)(ecx, edx);
	LogMovementStageSnapshot("server", "PlayerMove", "Exit", heartbeat, ecx, log);
}

void __fastcall CCSGameMovement::WalkMove::Detour(void* ecx, void* edx)
{
	MovementStageScope stageScope("server", MovementStage::WalkMove);
	const bool log = ShouldLogServerMovement();
	static uint32_t heartbeat = 0u;
	++heartbeat;
	LogMovementStageSnapshot("server", "WalkMove", "Enter", heartbeat, ecx, log);
    CMoveData* move = TryGetMoveDataFromGameMovement(ecx);
    const Vector originBefore = move ? move->GetAbsOrigin() : Vector();
    const Vector velocityBefore = move ? move->m_vecVelocity : Vector();
    ServerTable.Original<FN>(Index)(ecx, edx);
    const Vector originAfterOriginal = move ? move->GetAbsOrigin() : Vector();
    const Vector velocityAfterOriginal = move ? move->m_vecVelocity : Vector();
    UpdatePortalApproachVelocitySample("server", velocityAfterOriginal);
    bool bridgeApplied = TrySyncPortalCommittedMovement("server", heartbeat, ecx);
    if (!bridgeApplied)
        bridgeApplied = TryApplyPortalWalkMoveNudge("server", heartbeat, ecx, velocityBefore, log);
    if (!bridgeApplied)
        bridgeApplied = TryPreservePortalExitVelocity("server", heartbeat, ecx, velocityBefore);
    LogPortalWalkMoveFrame("server", heartbeat, ecx, originBefore, velocityBefore, originAfterOriginal, velocityAfterOriginal, bridgeApplied);
    LogMovementStageSnapshot("server", "WalkMove", "Exit", heartbeat, ecx, log);
}

void __fastcall CCSGameMovement::FullWalkMove::Detour(void* ecx, void* edx)
{
	MovementStageScope stageScope("server", MovementStage::FullWalkMove);
	const bool log = ShouldLogServerMovement();
	static uint32_t heartbeat = 0u;
	++heartbeat;
	LogMovementStageSnapshot("server", "FullWalkMove", "Enter", heartbeat, ecx, log);
	ServerTable.Original<FN>(Index)(ecx, edx);
	LogMovementStageSnapshot("server", "FullWalkMove", "Exit", heartbeat, ecx, log);
}

void __fastcall CCSGameMovement::CategorizePosition::Detour(void* ecx, void* edx)
{
	const bool log = ShouldLogServerMovement();
	static uint32_t heartbeat = 0u;
	++heartbeat;
	LogCategorizeSnapshot("server-signature", "Enter", heartbeat, ecx, log);

	const FN original = Func.Original<FN>();
	if (original)
		original(ecx, edx);
	else
		U::LogError("[PortalBridge][PositionProbe][server-signature][CategorizePosition] original function is null.\n");

	LogCategorizeSnapshot("server-signature", "Exit", heartbeat, ecx, log);
}

void __fastcall CCSGameMovement::StayOnGround::Detour(void* ecx, void* edx)
{
	const bool log = ShouldLogServerMovement();
	static uint32_t heartbeat = 0u;
	++heartbeat;
	LogMovementStageSnapshot("server-signature", "StayOnGround", "Enter", heartbeat, ecx, log);

	const PortalTransitionContext& context = G::G_L4D2Portal.m_PortalTransitionSimulator.GetContext();
	const bool skipForPortalBridge = G::G_L4D2Portal.m_PortalTransitionSimulator.IsInCollisionBridgePhase()
		&& context.insideAperture
		&& context.entrySide != PortalTransitionSide::None;

	if (skipForPortalBridge)
	{
		static uint32_t skippedLogCount = 0u;
		++skippedLogCount;
		if (skippedLogCount <= 3u || (skippedLogCount % 120u) == 0u)
		{
			U::LogInfo("[PortalBridge][StayOnGround] skipped during portal bridge count=%u heartbeat=%u phase=%s entry=%s ctxDepth=%.2f moving=%s.\n",
				skippedLogCount,
				heartbeat,
				PortalPhaseName(context.phase),
				PortalSideName(context.entrySide),
				context.signedDepth,
				BoolText(context.movingIntoPortal));
		}
		return;
	}

	const FN original = Func.Original<FN>();
	if (original)
		original(ecx, edx);
	else
		U::LogError("[PortalBridge][PositionProbe][server-signature][StayOnGround] original function is null.\n");

	LogMovementStageSnapshot("server-signature", "StayOnGround", "Exit", heartbeat, ecx, log);
}
unsigned long __fastcall CCSGameMovement::TestPlayerPosition::Detour(void* ecx, void* edx, const Vector& pos, int collisionGroup, trace_t* pm)
{
	const bool log = ShouldLogServerMovement();
	static uint32_t heartbeat = 0u;
	++heartbeat;
	const unsigned long result = ServerTable.Original<FN>(Index)(ecx, edx, pos, collisionGroup, pm);
	LogTestPlayerPositionExit("server", heartbeat, ecx, pos, collisionGroup, pm, result, log);
	return result;
}

void __fastcall CCSGameMovement::ClientPlayerMove::Detour(void* ecx, void* edx)
{
	MovementStageScope stageScope("client", MovementStage::PlayerMove);
	const bool log = ShouldLogServerMovement();
	static uint32_t heartbeat = 0u;
	++heartbeat;
	LogMovementStageSnapshot("client", "PlayerMove", "Enter", heartbeat, ecx, log);
	ClientTable.Original<FN>(Index)(ecx, edx);
	LogMovementStageSnapshot("client", "PlayerMove", "Exit", heartbeat, ecx, log);
}

void __fastcall CCSGameMovement::ClientWalkMove::Detour(void* ecx, void* edx)
{
	MovementStageScope stageScope("client", MovementStage::WalkMove);
	const bool log = ShouldLogServerMovement();
	static uint32_t heartbeat = 0u;
	++heartbeat;
	LogMovementStageSnapshot("client", "WalkMove", "Enter", heartbeat, ecx, log);
    CMoveData* move = TryGetMoveDataFromGameMovement(ecx);
    const Vector originBefore = move ? move->GetAbsOrigin() : Vector();
    const Vector velocityBefore = move ? move->m_vecVelocity : Vector();
    ClientTable.Original<FN>(Index)(ecx, edx);
    const Vector originAfterOriginal = move ? move->GetAbsOrigin() : Vector();
    const Vector velocityAfterOriginal = move ? move->m_vecVelocity : Vector();
    UpdatePortalApproachVelocitySample("client", velocityAfterOriginal);
    bool bridgeApplied = TrySyncPortalCommittedMovement("client", heartbeat, ecx);
    if (!bridgeApplied)
        bridgeApplied = TryApplyPortalWalkMoveNudge("client", heartbeat, ecx, velocityBefore, log);
    if (!bridgeApplied)
        bridgeApplied = TryPreservePortalExitVelocity("client", heartbeat, ecx, velocityBefore);
    LogPortalWalkMoveFrame("client", heartbeat, ecx, originBefore, velocityBefore, originAfterOriginal, velocityAfterOriginal, bridgeApplied);
    LogMovementStageSnapshot("client", "WalkMove", "Exit", heartbeat, ecx, log);
}

void __fastcall CCSGameMovement::ClientFullWalkMove::Detour(void* ecx, void* edx)
{
	MovementStageScope stageScope("client", MovementStage::FullWalkMove);
	const bool log = ShouldLogServerMovement();
	static uint32_t heartbeat = 0u;
	++heartbeat;
	LogMovementStageSnapshot("client", "FullWalkMove", "Enter", heartbeat, ecx, log);
	ClientTable.Original<FN>(Index)(ecx, edx);
	LogMovementStageSnapshot("client", "FullWalkMove", "Exit", heartbeat, ecx, log);
}

void __fastcall CCSGameMovement::ClientCategorizePosition::Detour(void* ecx, void* edx)
{
	const bool log = ShouldLogServerMovement();
	static uint32_t heartbeat = 0u;
	++heartbeat;
	LogCategorizeSnapshot("client", "Enter", heartbeat, ecx, log);
	ClientTable.Original<FN>(Index)(ecx, edx);
	LogCategorizeSnapshot("client", "Exit", heartbeat, ecx, log);
}

unsigned long __fastcall CCSGameMovement::ClientTestPlayerPosition::Detour(void* ecx, void* edx, const Vector& pos, int collisionGroup, trace_t* pm)
{
	const bool log = ShouldLogServerMovement();
	static uint32_t heartbeat = 0u;
	++heartbeat;
	const unsigned long result = ClientTable.Original<FN>(Index)(ecx, edx, pos, collisionGroup, pm);
	LogTestPlayerPositionExit("client", heartbeat, ecx, pos, collisionGroup, pm, result, log);
	return result;
}

void CCSGameMovement::Init()
{
	const TracePlayerBBox::FN ctracePlayerBBox = reinterpret_cast<TracePlayerBBox::FN>(U::Offsets.m_dwTracePlayerBBox);
	U::LogInfo("[PortalBridge] server TracePlayerBBox signature target=%p.\n", ctracePlayerBBox);
	XASSERT(ctracePlayerBBox == nullptr);

	bool hookedSignatureTracePlayerBBox = false;
	if (ctracePlayerBBox)
		hookedSignatureTracePlayerBBox = TracePlayerBBox::Func.Init(ctracePlayerBBox, &TracePlayerBBox::Detour);
	U::LogInfo("[PortalBridge] server TracePlayerBBox signature hook=%s.\n", BoolText(hookedSignatureTracePlayerBBox));

	const CategorizePosition::FN categorizePosition = reinterpret_cast<CategorizePosition::FN>(U::Offsets.m_dwCategorizePosition);
	U::LogInfo("[PortalBridge] server CategorizePosition(void) signature target=%p.\n", categorizePosition);

	bool hookedSignatureCategorizePosition = false;
	if (categorizePosition)
		hookedSignatureCategorizePosition = CategorizePosition::Func.Init(categorizePosition, &CategorizePosition::Detour);
	U::LogInfo("[PortalBridge] server CategorizePosition(void) signature hook=%s.\n", BoolText(hookedSignatureCategorizePosition));

	if (!I::ServerGameMovement)
	{
		U::LogWarning("[PortalBridge] server GameMovement001 is null; skipping StepMove/TryPlayerMove diagnostics.\n");
		return;
	}

	if (ServerTable.Init(I::ServerGameMovement) == false)
	{
		U::LogError("[PortalBridge] failed to initialize server GameMovement vtable diagnostics table.\n");
		return;
	}


	void** serverVTable = *reinterpret_cast<void***>(I::ServerGameMovement);
	void* serverCategorizeVTableEntry = serverVTable ? serverVTable[CategorizePosition::Index] : nullptr;
	void* serverTryPlayerMoveVTableEntry = serverVTable ? serverVTable[TryPlayerMove::Index] : nullptr;
	void* serverTryPlayerMoveRejected40Entry = serverVTable ? serverVTable[40u] : nullptr;
	const HMODULE serverModule = GetModuleHandleA("server.dll");
	if (serverModule)
	{
		g_ServerStayOnGroundCandidateA = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(serverModule) + 0x000F661Bu);
	}
	void* serverStayOnGroundCandidateStart = FindNearbyFunctionPrologue(g_ServerStayOnGroundCandidateA, 0x700u);
	const bool hookedStayOnGroundCandidate = serverStayOnGroundCandidateStart
		? StayOnGround::Func.Init(serverStayOnGroundCandidateStart, &StayOnGround::Detour)
		: false;
	U::LogInfo("[PortalBridge] server StayOnGround candidate target=%p off=0x%08X hook=%s.\n",
		serverStayOnGroundCandidateStart,
		static_cast<unsigned int>(ModuleRelativeAddress("server.dll", serverStayOnGroundCandidateStart)),
		BoolText(hookedStayOnGroundCandidate));
	const bool serverCategorizeMatchesVTable = categorizePosition && serverCategorizeVTableEntry == reinterpret_cast<void*>(categorizePosition);

	const bool hookedPlayerMove = ServerTable.Hook(&PlayerMove::Detour, PlayerMove::Index);
	const bool hookedWalkMove = ServerTable.Hook(&WalkMove::Detour, WalkMove::Index);
	const bool hookedFullWalkMove = ServerTable.Hook(&FullWalkMove::Detour, FullWalkMove::Index);
	const bool hookedCategorizePosition = hookedSignatureCategorizePosition;
	const bool hookedTestPlayerPosition = false;
	const bool hookedTryPlayerMove = ServerTable.Hook(&TryPlayerMove::Detour, TryPlayerMove::Index);
	const bool hookedStepMove = ServerTable.Hook(&StepMove::Detour, StepMove::Index);
	U::LogInfo("[PortalBridge] server GameMovement diagnostics gm=%p TracePlayerBBox=signature-hook(%s) PlayerMove[%u]=%s WalkMove[%u]=%s FullWalkMove[%u]=%s CategorizePosition(void)=%p vtable[%u]=%p match=%s hook=%s TestPlayerPosition[%u]=%s(skipped: signature unverified) TryPlayerMove[%u]=%p hook=%s TryPlayerMoveRejected[40]=%p(skipped: crashed with TryPlayerMove signature) StepMove[%u]=%s(validating server index 64).\n",
		I::ServerGameMovement,
		BoolText(hookedSignatureTracePlayerBBox),
		PlayerMove::Index,
		BoolText(hookedPlayerMove),
		WalkMove::Index,
		BoolText(hookedWalkMove),
		FullWalkMove::Index,
		BoolText(hookedFullWalkMove),
		reinterpret_cast<void*>(categorizePosition),
		CategorizePosition::Index,
		serverCategorizeVTableEntry,
		BoolText(serverCategorizeMatchesVTable),
		BoolText(hookedCategorizePosition),
		TestPlayerPosition::Index,
		BoolText(hookedTestPlayerPosition),
		TryPlayerMove::Index,
		serverTryPlayerMoveVTableEntry,
		BoolText(hookedTryPlayerMove),
		serverTryPlayerMoveRejected40Entry,
		StepMove::Index,
		BoolText(hookedStepMove));

	if (!I::GameMovement)
	{
		U::LogWarning("[PortalBridge] client GameMovement001 is null; skipping client StepMove/TryPlayerMove diagnostics.\n");
		return;
	}

	if (ClientTable.Init(I::GameMovement) == false)
	{
		U::LogError("[PortalBridge] failed to initialize client GameMovement vtable diagnostics table.\n");
		return;
	}



	const bool hookedClientTracePlayerBBox = ClientTable.Hook(&ClientTracePlayerBBox::Detour, ClientTracePlayerBBox::Index);
	const bool hookedClientPlayerMove = ClientTable.Hook(&ClientPlayerMove::Detour, ClientPlayerMove::Index);
	const bool hookedClientWalkMove = ClientTable.Hook(&ClientWalkMove::Detour, ClientWalkMove::Index);
	const bool hookedClientFullWalkMove = ClientTable.Hook(&ClientFullWalkMove::Detour, ClientFullWalkMove::Index);
	const bool hookedClientCategorizePosition = false;
	const bool hookedClientTestPlayerPosition = false;
	const bool hookedClientTryPlayerMove = false;
	const bool hookedClientStepMove = false;
	U::LogInfo("[PortalBridge] client GameMovement diagnostics gm=%p TracePlayerBBox[%u]=%s PlayerMove[%u]=%s WalkMove[%u]=%s FullWalkMove[%u]=%s CategorizePosition[%u]=%s(skipped: not yet hooked) TestPlayerPosition[%u]=%s(skipped: signature unverified) TryPlayerMove[%u]=%s(skipped: index/signature unverified) StepMove[%u]=%s(skipped: index/signature unverified).\n",
		I::GameMovement,
		ClientTracePlayerBBox::Index,
		BoolText(hookedClientTracePlayerBBox),
		ClientPlayerMove::Index,
		BoolText(hookedClientPlayerMove),
		ClientWalkMove::Index,
		BoolText(hookedClientWalkMove),
		ClientFullWalkMove::Index,
		BoolText(hookedClientFullWalkMove),
		ClientCategorizePosition::Index,
		BoolText(hookedClientCategorizePosition),
		ClientTestPlayerPosition::Index,
		BoolText(hookedClientTestPlayerPosition),
		ClientTryPlayerMove::Index,
		BoolText(hookedClientTryPlayerMove),
		ClientStepMove::Index,
		BoolText(hookedClientStepMove));
}
