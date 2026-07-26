#include "EngineTrace.h"
#include "EngineTraceBspProbe.h"
#include "EngineTraceDiagnostics.h"

#include "../../SDK/L4D2/Interfaces/EngineTrace.h"
#include "../../Portal/L4D2_Portal.h"
#include "../../Portal/PortalBspData.h"
#include "../../Portal/PortalPhysicsMode.h"
#include "../../Util/Logger/Logger.h"
#include "../../Util/Offsets/PortalBspOffset.h"

#include <climits>
#include <cstring>

using namespace Hooks;

namespace
{
	using GetBrushInfoOracleFn = EngineTraceBspProbe::GetBrushInfoResult(__thiscall*)(void*, int, void*, int*);

	struct EngineTraceSlot
	{
		const char* name;
		std::uint32_t index;
	};

	bool g_LoggingRuntimeOracle = false;
	bool g_BlueOracleLogged = false;
	bool g_OrangeOracleLogged = false;
	Vector g_LastBlueOracleOrigin;
	Vector g_LastOrangeOracleOrigin;

	EngineTraceDiagnostics::ModuleRange GetEngineModuleRange();

	bool IsReadableMemory(std::uintptr_t address, std::size_t size)
	{
		if (address == 0 || size == 0 || address > UINTPTR_MAX - size)
			return false;

		MEMORY_BASIC_INFORMATION region{};
		if (VirtualQuery(reinterpret_cast<const void*>(address), &region, sizeof(region)) != sizeof(region))
			return false;

		if (region.State != MEM_COMMIT || (region.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
			return false;

		const std::uintptr_t regionBase = reinterpret_cast<std::uintptr_t>(region.BaseAddress);
		return address >= regionBase && address + size <= regionBase + region.RegionSize;
	}

	template <typename T>
	bool TryReadMemory(std::uintptr_t address, T& value)
	{
		if (!IsReadableMemory(address, sizeof(T)))
			return false;

		std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(T));
		return true;
	}

	EngineTraceBspProbe::GetBrushInfoLayoutCandidate DecodeLiveGetBrushInfoLayout()
	{
		constexpr std::size_t kDecodeWindow = 96u;
		if (!I::EngineTrace)
			return {};

		void** vtable = *reinterpret_cast<void***>(I::EngineTrace);
		if (!vtable || !vtable[16])
			return {};

		const EngineTraceDiagnostics::ModuleRange module = GetEngineModuleRange();
		const std::uintptr_t functionAddress = reinterpret_cast<std::uintptr_t>(vtable[16]);
		if (!EngineTraceDiagnostics::DescribeAddress(module, functionAddress).insideModule
			|| functionAddress > UINTPTR_MAX - kDecodeWindow
			|| !EngineTraceDiagnostics::DescribeAddress(module, functionAddress + kDecodeWindow - 1u).insideModule
			|| !IsReadableMemory(functionAddress, kDecodeWindow))
		{
			return {};
		}

		return EngineTraceBspProbe::DecodeGetBrushInfoLayout(
			reinterpret_cast<const std::uint8_t*>(functionAddress),
			kDecodeWindow,
			module.base,
			module.size);
	}

	bool NearlySamePoint(const Vector& lhs, const Vector& rhs)
	{
		const Vector delta = lhs - rhs;
		return delta.LenghtSqr() <= 0.0001f;
	}

	bool TryGetBrushInfo(int brushIndex, int& contents)
	{
		if (!I::EngineTrace || brushIndex < 0)
			return false;

		void** vtable = *reinterpret_cast<void***>(I::EngineTrace);
		if (!vtable || !vtable[16])
			return false;

		const auto getBrushInfo = reinterpret_cast<GetBrushInfoOracleFn>(vtable[16]);
		contents = 0;
		return getBrushInfo(I::EngineTrace, brushIndex, nullptr, &contents);
	}

	void LogBrushSample(
		const char* portalName,
		std::size_t brushIndex,
		const EngineTraceBspProbe::GetBrushInfoLayoutCandidate& layout)
	{
		int contents = 0;
		const bool valid = brushIndex <= static_cast<std::size_t>(INT_MAX)
			&& TryGetBrushInfo(static_cast<int>(brushIndex), contents);

		int directContents = 0;
		std::uint32_t brushArray = 0;
		std::uint32_t directBrushCount = 0;
		const bool directReadable = layout.valid
			&& TryReadMemory(layout.numBrushes, directBrushCount)
			&& brushIndex < directBrushCount
			&& TryReadMemory(layout.mapBrushes, brushArray)
			&& brushIndex <= (UINTPTR_MAX - static_cast<std::uintptr_t>(brushArray)) / 8u
			&& TryReadMemory(static_cast<std::uintptr_t>(brushArray) + brushIndex * 8u, directContents);
		U::LogInfo("[PortalBsp][RuntimeOracleBrush] portal=%s index=%zu valid=%s contents=0x%08X directReadable=%s directContents=0x%08X matches=%s.\n",
			portalName,
			brushIndex,
			valid ? "true" : "false",
			static_cast<unsigned int>(contents),
			directReadable ? "true" : "false",
			static_cast<unsigned int>(directContents),
			(valid && directReadable && contents == directContents) ? "true" : "false");
	}

	void LogBspLayoutCandidate(
		const char* portalName,
		const EngineTraceBspProbe::ContiguousCountResult& apiBrushCount,
		const EngineTraceBspProbe::GetBrushInfoLayoutCandidate& layout)
	{
		const EngineTraceDiagnostics::ModuleRange module = GetEngineModuleRange();
		const char* mapName = I::EngineClient && I::EngineClient->GetLevelName()
			? I::EngineClient->GetLevelName()
			: "unknown";
		if (!layout.valid)
		{
			U::LogError("[PortalBsp][LayoutCandidate] map=%s portal=%s source=GetBrushInfo valid=false.\n",
				mapName, portalName);
			return;
		}

		U::LogInfo("[PortalBsp][LayoutCandidate] map=%s portal=%s source=GetBrushInfo valid=true bspBase=%p bspRva=0x%08X numBrushesField=%p mapBrushesField=%p validatedCountField=%p.\n",
			mapName,
			portalName,
			reinterpret_cast<void*>(layout.bspBase),
			static_cast<unsigned int>(layout.bspBase - module.base),
			reinterpret_cast<void*>(layout.numBrushes),
			reinterpret_cast<void*>(layout.mapBrushes),
			reinterpret_cast<void*>(layout.validatedBrushCount));

		const bool offsetStored = U::PortalBspOffset::RecordCandidate(
			layout.bspBase,
			module.base,
			module.size,
			"EngineTrace.GetBrushInfo.semantic-operands");
		const int renderLeafCount = I::EngineClient ? I::EngineClient->LevelLeafCount() : -1;
		G::PortalBspData.CaptureForMap(layout.bspBase, renderLeafCount, mapName);
		const PortalBsp::Snapshot& bsp = G::PortalBspData.GetSnapshot();
		const PortalBsp::QueryStorage& query = G::PortalBspData.GetQueryStorage();

		U::LogInfo("[PortalBsp][LayoutState] map=%s portal=%s generation=%u ready=%s offsetStored=%s destructiveWrites=false.\n",
			mapName,
			portalName,
			bsp.mapGeneration,
			bsp.ready ? "true" : "false",
			offsetStored ? "true" : "false");
		U::LogInfo("[PortalBsp][QueryCache] map=%s portal=%s generation=%u ready=%s planes=%zu nodes=%zu leafs=%zu leafbrushes=%zu brushes=%zu brushsides=%zu boxbrushes=%zu invalidNodePlanes=%zu invalidBrushSidePlanes=%zu failure=%s destructiveWrites=false.\n",
			mapName,
			portalName,
			bsp.mapGeneration,
			query.ready ? "true" : "false",
			query.planes.size(),
			query.nodes.size(),
			query.leaves.size(),
			query.leafBrushes.size(),
			query.brushes.size(),
			query.brushSides.size(),
			query.boxBrushes.size(),
			query.invalidNodePlanePointers,
			query.invalidBrushSidePlanePointers,
			query.failureReason.empty() ? "none" : query.failureReason.c_str());

		for (const PortalBsp::TableSnapshot& table : bsp.tables)
		{
			U::LogInfo("[PortalBsp][LayoutTable] map=%s portal=%s table=%s evidence=%s offsets=(0x%02X,0x%02X,0x%02X) stride=%zu expectedAllocatedDelta=%u fieldsReadable=%s logical=%u allocated=%u countPositive=%s countReasonable=%s countRelationMatches=%s array=%p rangeNoOverflow=%s spanBytes=%zu spanReadable=%s valid=%s.\n",
				mapName,
				portalName,
				table.layout.name,
				table.layout.evidence,
				table.layout.canonicalCountOffset,
				table.layout.pointerOffset,
				table.layout.validatedCountOffset,
				table.layout.elementSize,
				table.layout.validatedCountDelta,
				table.fieldsReadable ? "true" : "false",
				table.canonicalCount,
				table.validatedCount,
				table.metadata.countPositive ? "true" : "false",
				table.metadata.countReasonable ? "true" : "false",
				table.metadata.countRelationMatches ? "true" : "false",
				reinterpret_cast<void*>(table.arrayAddress),
				table.metadata.rangeDoesNotOverflow ? "true" : "false",
				table.metadata.byteSize,
				table.spanReadable ? "true" : "false",
				table.IsValid() ? "true" : "false");
		}

		const PortalBsp::TableSnapshot& nodes = bsp.tables[3];
		const PortalBsp::TableSnapshot& leafs = bsp.tables[4];
		const PortalBsp::TableSnapshot& brushes = bsp.tables[6];
		U::LogInfo("[PortalBsp][RootInvariant] map=%s portal=%s root=%p nodes=%p rootPresent=%s nodesPresent=%s rootMatchesNodes=%s valid=%s.\n",
			mapName,
			portalName,
			reinterpret_cast<void*>(bsp.rootNode),
			reinterpret_cast<void*>(nodes.arrayAddress),
			bsp.rootInvariant.rootPresent ? "true" : "false",
			bsp.rootInvariant.nodesPresent ? "true" : "false",
			bsp.rootInvariant.rootMatchesNodes ? "true" : "false",
			bsp.rootInvariant.valid ? "true" : "false");
		U::LogInfo("[PortalBsp][LeafInvariant] map=%s portal=%s renderLeafCount=%d collisionLeafCount=%u emptyLeaf=%u solidLeaf=%u collisionPlusOne=%s emptyMatchesRender=%s solidIsZero=%s valid=%s.\n",
			mapName,
			portalName,
			bsp.renderLeafCount,
			leafs.canonicalCount,
			bsp.emptyLeaf,
			bsp.solidLeaf,
			bsp.leafInvariant.collisionCountMatches ? "true" : "false",
			bsp.leafInvariant.emptyLeafMatches ? "true" : "false",
			bsp.leafInvariant.solidLeafMatches ? "true" : "false",
			bsp.leafInvariant.valid ? "true" : "false");
		U::LogInfo("[PortalBsp][CollisionModels] map=%s portal=%s canonical=%u validated=%u array=%p firstModelBytes=%zu valid=%s.\n",
			mapName,
			portalName,
			bsp.collisionModelCount,
			bsp.validatedCollisionModelCount,
			reinterpret_cast<void*>(bsp.collisionModels),
			PortalBsp::kCollisionModelRequiredBytes,
			bsp.collisionModelsValid ? "true" : "false");
		U::LogInfo("[PortalBsp][LayoutCrossCheck] map=%s portal=%s table=brushes apiCount=%zu apiExact=%s canonical=%u validated=%u apiMatchesCanonical=%s.\n",
			mapName,
			portalName,
			apiBrushCount.count,
			apiBrushCount.complete ? "true" : "false",
			brushes.canonicalCount,
			brushes.validatedCount,
			(apiBrushCount.complete && brushes.IsValid()
				&& apiBrushCount.count == brushes.canonicalCount) ? "true" : "false");
	}

	void LogPortalRuntimeOracle(const char* portalName, const PortalInfo_t& portal)
	{
		constexpr std::size_t kBrushSafetyCap = 1u << 20;
		const auto brushCount = EngineTraceBspProbe::FindContiguousCount(
			[](std::size_t index)
			{
				int contents = 0;
				return index <= static_cast<std::size_t>(INT_MAX)
					&& TryGetBrushInfo(static_cast<int>(index), contents);
			},
			kBrushSafetyCap);
		const auto layout = DecodeLiveGetBrushInfoLayout();

		// Portal placement stores trace.endpos + normal * 0.5f as the origin.
		const Vector estimatedHit = portal.origin - portal.normal * 0.5f;
		U::LogInfo("[PortalBsp][RuntimeOracle] map=%s portal=%s source=portalOriginMinusNormal0.5 hit=(%.3f,%.3f,%.3f) normal=(%.6f,%.6f,%.6f) levelLeafCount=%d brushCount=%zu brushCountExact=%s probeCalls=%zu.\n",
			I::EngineClient && I::EngineClient->GetLevelName() ? I::EngineClient->GetLevelName() : "unknown",
			portalName,
			estimatedHit.x, estimatedHit.y, estimatedHit.z,
			portal.normal.x, portal.normal.y, portal.normal.z,
			I::EngineClient ? I::EngineClient->LevelLeafCount() : -1,
			brushCount.count,
			brushCount.complete ? "true" : "false",
			brushCount.probeCalls);
		LogBspLayoutCandidate(portalName, brushCount, layout);

		constexpr float offsets[] = { 1.0f, -1.0f, -4.0f };
		constexpr const char* labels[] = { "front+1", "back-1", "back-4" };
		for (std::size_t i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i)
		{
			const Vector point = estimatedHit + portal.normal * offsets[i];
			U::LogInfo("[PortalBsp][RuntimeOraclePoint] portal=%s sample=%s point=(%.3f,%.3f,%.3f) leaf=%d worldContents=0x%08X outsideWorld=%s.\n",
				portalName,
				labels[i],
				point.x, point.y, point.z,
				I::EngineTrace->GetLeafContainingPoint(point),
				static_cast<unsigned int>(I::EngineTrace->GetPointContents_WorldOnly(point, MASK_ALL)),
				I::EngineTrace->PointOutsideWorld(point) ? "true" : "false");
		}

		if (!brushCount.complete || brushCount.count == 0)
			return;

		LogBrushSample(portalName, 0, layout);
		LogBrushSample(portalName, brushCount.count / 2, layout);
		LogBrushSample(portalName, brushCount.count - 1, layout);
		LogBrushSample(portalName, brushCount.count, layout);
	}

	void MaybeLogPortalRuntimeOracles()
	{
		if (g_LoggingRuntimeOracle || !I::EngineTrace)
			return;

		g_LoggingRuntimeOracle = true;
		const PortalInfo_t& blue = G::G_L4D2Portal.g_BluePortal;
		const PortalInfo_t& orange = G::G_L4D2Portal.g_OrangePortal;

		if (!blue.bIsActive)
			g_BlueOracleLogged = false;
		else if (!g_BlueOracleLogged || !NearlySamePoint(blue.origin, g_LastBlueOracleOrigin))
		{
			LogPortalRuntimeOracle("blue", blue);
			g_LastBlueOracleOrigin = blue.origin;
			g_BlueOracleLogged = true;
		}

		if (!orange.bIsActive)
			g_OrangeOracleLogged = false;
		else if (!g_OrangeOracleLogged || !NearlySamePoint(orange.origin, g_LastOrangeOracleOrigin))
		{
			LogPortalRuntimeOracle("orange", orange);
			g_LastOrangeOracleOrigin = orange.origin;
			g_OrangeOracleLogged = true;
		}

		g_LoggingRuntimeOracle = false;
	}

	EngineTraceDiagnostics::ModuleRange GetEngineModuleRange()
	{
		const HMODULE module = GetModuleHandleA("engine.dll");
		if (!module)
			return {};

		const auto base = reinterpret_cast<std::uintptr_t>(module);
		const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
		if (dos->e_magic != IMAGE_DOS_SIGNATURE)
			return {};

		const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
		if (nt->Signature != IMAGE_NT_SIGNATURE)
			return {};

		return { base, nt->OptionalHeader.SizeOfImage };
	}

	void LogEngineTraceAnalysisAnchors()
	{
		U::LogInfo("[PortalPhysicsMode] active=%s render=%s simulation=%s movementMutation=%s legacyCollisionBypass=%s teleport=%s teleportPredictionSync=%s destructiveDiagnostics=%s.\n",
			PortalPhysicsMode::CurrentName(),
			PortalPhysicsMode::ShouldRenderPortals() ? "true" : "false",
			PortalPhysicsMode::ShouldRunTraversalSimulation() ? "true" : "false",
			PortalPhysicsMode::ShouldMutatePlayerMovement() ? "true" : "false",
			PortalPhysicsMode::ShouldUseLegacyCollisionBypass() ? "true" : "false",
			PortalPhysicsMode::ShouldCommitTeleport() ? "true" : "false",
			PortalPhysicsMode::ShouldSynchronizeCommittedTeleportPrediction() ? "true" : "false",
			PortalPhysicsMode::ShouldRunDestructiveDiagnostics() ? "true" : "false");

		const EngineTraceDiagnostics::ModuleRange module = GetEngineModuleRange();
		void** vtable = I::EngineTrace ? *reinterpret_cast<void***>(I::EngineTrace) : nullptr;

		U::LogInfo("[PortalBsp][EngineTrace] moduleBase=%p moduleSize=0x%zX interface=%p vtable=%p.\n",
			reinterpret_cast<void*>(module.base), module.size, I::EngineTrace, vtable);

		if (!vtable || module.base == 0 || module.size == 0)
		{
			U::LogError("[PortalBsp][EngineTrace] cannot enumerate analysis anchors because module/interface validation failed.\n");
			return;
		}

		constexpr EngineTraceSlot slots[] = {
			{ "GetPointContents", 0u },
			{ "GetPointContents_WorldOnly", 1u },
			{ "TraceRay", 5u },
			{ "GetBrushesInAABB", 14u },
			{ "GetBrushInfo", 16u },
			{ "PointOutsideWorld", 17u },
			{ "GetLeafContainingPoint", 18u },
		};

		for (const EngineTraceSlot& slot : slots)
		{
			const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(vtable[slot.index]);
			const auto description = EngineTraceDiagnostics::DescribeAddress(module, address);
			U::LogInfo("[PortalBsp][EngineTrace] slot=%u name=%s address=%p rva=0x%08X insideEngine=%s.\n",
				slot.index,
				slot.name,
				reinterpret_cast<void*>(address),
				static_cast<unsigned int>(description.rva),
				description.insideModule ? "true" : "false");
		}
	}
}

// GetLeafContainingPoint Hook implementation
// This function is called by the engine to determine which leaf (BSP tree node) contains a point
// Leaves are used for visibility determination and portal rendering
int __fastcall EngineTrace::GetLeafContainingPoint::Detour(void* ecx, void* edx, const Vector& ptTest)
{
	// Call original function
	int nLeaf = Table.Original<FN>(Index)(ecx, edx, ptTest);
	MaybeLogPortalRuntimeOracles();
	return nLeaf;
}

bool EngineTrace::EnsurePortalBspDataReady(const char* reason)
{
	const char* mapName = I::EngineClient && I::EngineClient->GetLevelName()
		? I::EngineClient->GetLevelName()
		: "unknown";
	const PortalBsp::Snapshot& current = G::PortalBspData.GetSnapshot();
	if (G::PortalBspData.IsReady()
		&& G::PortalBspData.IsQueryReady()
		&& current.mapName == mapName)
	{
		return true;
	}

	const EngineTraceBspProbe::GetBrushInfoLayoutCandidate layout = DecodeLiveGetBrushInfoLayout();
	if (!layout.valid)
	{
		const bool hadCachedData = current.base != 0u
			|| G::PortalBspData.IsReady()
			|| G::PortalBspData.IsQueryReady();
		if (hadCachedData)
			G::PortalBspData.InvalidateForMapChange();
		U::PortalBspOffset::ClearCandidate();
		U::LogError("[PortalBsp][QueryInit] map=%s reason=%s ready=false failure=layout-decode-failed destructiveWrites=false.\n",
			mapName,
			reason ? reason : "unknown");
		return false;
	}

	const EngineTraceDiagnostics::ModuleRange module = GetEngineModuleRange();
	const bool offsetStored = U::PortalBspOffset::RecordCandidate(
		layout.bspBase,
		module.base,
		module.size,
		"EngineTrace.GetBrushInfo.semantic-operands");
	const int renderLeafCount = I::EngineClient ? I::EngineClient->LevelLeafCount() : -1;
	const bool layoutReady = G::PortalBspData.CaptureForMap(
		layout.bspBase,
		renderLeafCount,
		mapName);
	const PortalBsp::Snapshot& snapshot = G::PortalBspData.GetSnapshot();
	const PortalBsp::QueryStorage& query = G::PortalBspData.GetQueryStorage();
	const bool ready = offsetStored && layoutReady && query.ready;
	U::LogInfo("[PortalBsp][QueryInit] map=%s reason=%s generation=%u layoutReady=%s queryReady=%s ready=%s planes=%zu nodes=%zu leafs=%zu leafbrushes=%zu brushes=%zu brushsides=%zu boxbrushes=%zu invalidNodePlanes=%zu invalidBrushSidePlanes=%zu failure=%s destructiveWrites=false.\n",
		mapName,
		reason ? reason : "unknown",
		snapshot.mapGeneration,
		layoutReady ? "true" : "false",
		query.ready ? "true" : "false",
		ready ? "true" : "false",
		query.planes.size(),
		query.nodes.size(),
		query.leaves.size(),
		query.leafBrushes.size(),
		query.brushes.size(),
		query.brushSides.size(),
		query.boxBrushes.size(),
		query.invalidNodePlanePointers,
		query.invalidBrushSidePlanePointers,
		query.failureReason.empty() ? "none" : query.failureReason.c_str());
	return ready;
}

bool EngineTrace::TryGetBrushContentsForDiagnostics(int brushIndex, int& contents)
{
	return TryGetBrushInfo(brushIndex, contents);
}

void EngineTrace::Init()
{
	// Validate that I::EngineTrace interface has been initialized
	if (I::EngineTrace == nullptr)
	{
		U::LogError("I::EngineTrace interface is null! Make sure SDK initialization completes before Hook initialization.\n");
		return;
	}

	LogEngineTraceAnalysisAnchors();

	// Initialize the VMT table hook
	if (Table.Init(I::EngineTrace) == false)
	{
		U::LogError("Failed to initialize VMT table for IEngineTrace!\n");
		return;
	}

	// Hook the GetLeafContainingPoint function at index 18
	if (Table.Hook(&GetLeafContainingPoint::Detour, GetLeafContainingPoint::Index) == false)
	{
		U::LogError("Failed to hook GetLeafContainingPoint function!\n");
		return;
	}

	U::LogInfo("Successfully hooked IEngineTrace::GetLeafContainingPoint (Index 18)\n");
}
