## Context

The portal visual path is already mature. Physical traversal currently uses `CPortalTransitionSimulator` to track approach/intersection/commit/exit phases, `PortalTransform` for portal-local geometry and entry-to-exit transforms, `PortalPlayerTeleport` for the authoritative local-server teleport, and `CPortalCollisionBridge` plus controlled noclip to bypass wall collisions in client and server movement.

The SourcePawn reference implementation under `Reference Code/sourcemod-wallhack/` demonstrates another collision path. It resolves `g_BSPData`, walks BSP nodes to locate a leaf, finds solid brushes referenced by that leaf, tests whether a point is inside a convex or box brush, records the brush's original `contents`, and writes `CONTENTS_EMPTY`. Its gamedata is Linux-only, so its symbol and offsets are evidence for the algorithm, not valid Windows addresses.

This change intentionally targets only the local survivor in single-player/local-server play. Effects on other entities are diagnostic observations, not acceptance requirements.

## Goals / Non-Goals

**Goals:**
- Make the original local-player movement traces observe an open path through the world brush carrying a portal.
- Reuse portal placement, transform, transition, teleport, logging, interface, pattern, and lifecycle infrastructure already in the project.
- Make every BSP write reversible, idempotent, map-scoped, and diagnosable.
- Establish Phase 1 without proximity gating so raw engine behavior can be measured.
- Add proximity gating and optional aperture confinement only in later phases, without replacing the Phase 1 core.
- Preserve a controlled comparison against the current collision bridge and noclip implementation.

**Non-Goals:**
- NPC or AI navigation support.
- Bullet, melee, throwable, physics-prop, infected, or special-infected portal behavior.
- Remote-server support or per-player multiplayer collision.
- Replacing portal visuals, portal transforms, teleport commit, prediction synchronization, or transition state handling.
- Editing BSP files on disk.
- Reconstructing arbitrary brush geometry in Phase 1 or Phase 2.

## Architecture

### Components

#### `PortalBspTypes`

Defines fixed-width Windows x86 views and constants for the validated fields used by the algorithm. The implementation must not reinterpret the entire engine type until its layout is proven. Access is through checked address arithmetic and typed loads.

Required logical views:

```cpp
struct PortalBspLayout {
    std::ptrdiff_t mapRootNode;
    std::ptrdiff_t mapBrushSides;
    std::ptrdiff_t numBrushSides;
    std::ptrdiff_t mapBoxBrushes;
    std::ptrdiff_t numBoxBrushes;
    std::ptrdiff_t mapLeafs;
    std::ptrdiff_t numLeafs;
    std::ptrdiff_t mapLeafBrushes;
    std::ptrdiff_t numLeafBrushes;
    std::ptrdiff_t mapBrushes;
    std::ptrdiff_t numBrushes;
};
```

IDA and two-map runtime evidence for the target Windows build confirms `CNode=12`, `CPlane=20`, `CLeaf=16`, `CBrush=8`, `CBrushSide=8`, and `CBoxBrush=48`. The first count in each array triplet is logical and the final count is allocated. Ordinary arrays use equal counts, while the node allocation is always logical count plus six because the engine appends its box-hull nodes. Full node-span validation therefore uses `(logical + 6) * 12`, while BSP traversal still bounds map-node indices by the logical count. This is an explicit validated relation, not a reason to weaken generic count validation.

#### `CPortalBspData`

Owns the resolved `g_BSPData` address, validated layout, and current map generation. It exposes read-only queries until validation succeeds:

```cpp
bool InitializeForMap();
void InvalidateForMapChange();
bool IsReady() const;
std::optional<int> FindBrushForSurfacePoint(
    const Vector& hitPosition,
    const Vector& hitNormal,
    unsigned int requiredMask) const;
std::optional<int> ReadBrushContents(int brushIndex) const;
bool WriteBrushContents(int brushIndex, int expectedCurrent, int replacement);
```

`WriteBrushContents` must compare the current value with `expectedCurrent`; it must refuse a stale or externally changed value.

#### `CPortalBspCollisionCarver`

Owns portal-to-brush bindings and brush mutation state:

```cpp
enum class PortalBrushOwner { Blue, Orange };

struct PortalBrushBinding {
    int brushIndex = -1;
    int originalContents = CONTENTS_EMPTY;
    uint32_t mapGeneration = 0;
    bool resolved = false;
};

struct ModifiedPortalBrush {
    int brushIndex = -1;
    int originalContents = CONTENTS_EMPTY;
    uint8_t ownerMask = 0;
    uint32_t mapGeneration = 0;
    bool modified = false;
};
```

The manager provides:

```cpp
bool BindPortalBrush(PortalBrushOwner owner,
                     const Vector& hitPosition,
                     const Vector& hitNormal);
void UnbindPortalBrush(PortalBrushOwner owner);
bool ActivatePairCarving();
void RestoreAll(const char* reason);
void UpdatePhase2(const C_TerrorPlayer* player,
                  const PortalTransitionContext& context);
```

An owner bitmask prevents premature restoration when both portals share one brush. Rebinding one portal first releases only that owner's claim.

#### Phase 2 activation policy

Phase 2 adds a pure decision function rather than embedding distance thresholds in mutation code:

```cpp
struct PortalCarveActivationConfig {
    float lateralMargin = 24.0f;
    float verticalMargin = 24.0f;
    float enterDepth = 96.0f;
    float leaveDepth = 144.0f;
};

bool IsPlayerNearPortalForCarving(...);
bool ShouldKeepCarvingActive(bool nearBlue,
                             bool nearOrange,
                             PortalTransitionPhase phase,
                             bool currentlyActive);
```

The local-player anchor and portal-local basis must come from existing `PortalTransform` facilities. `leaveDepth` must exceed `enterDepth` to provide hysteresis. Traversal phases from `ApproachingPortal` through `ExitingPortal` force carving to remain active even if the distance sample briefly leaves the activation volume.

#### Phase 3 blocker confinement

Phase 3 is conditional on Phase 1/2 evidence. A `CPortalApertureBlockerSet` may use existing `IServerTools::CreateEntityByName`, `SetKeyValue`, and `DispatchSpawn` infrastructure to create four or more `env_physics_blocker` entities around the aperture. Before implementation, a diagnostic spike must prove blocker bounds, rotation, collision mask, spawn/remove lifecycle, and local-player behavior in the target L4D2 build.

### Runtime flow

#### Map initialization

```text
Level initialization
→ resolve Windows g_BSPData
→ read counts and array pointers
→ perform structural sanity checks
→ increment map generation
→ enable read-only brush queries
```

Failure leaves the existing traversal implementation untouched.

#### Portal placement

```text
existing portal placement trace succeeds
→ reuse its world hit position and plane normal
→ sample points behind the plane using the reference offset sequence
→ find leaf and candidate leaf brushes
→ require candidate contents to intersect MASK_PLAYERSOLID
→ bind brush to blue/orange portal
→ log brush index, contents, leaf, sample offset, and map generation
```

The system must not infer a brush from the final rendered portal origin alone when the original placement trace data is available.

#### Phase 1 mutation lifecycle

```text
both portals active and both bindings valid
→ atomically validate both bindings
→ save original contents once per unique brush
→ write CONTENTS_EMPTY to each unique brush
→ keep carving active without player proximity checks

portal replaced/closed, map ends, DLL unloads, or validation fails
→ restore every modified brush whose map generation and current value are valid
→ clear bindings/mutation state as appropriate
```

Phase 1 intentionally does not use nearby detection and does not create blockers. The purpose is to expose and record unrestricted whole-brush behavior.

#### Phase 2 mutation lifecycle

```text
both portals valid AND player enters either portal-local activation volume
→ activate pair carving

player remains near OR transition phase is active
→ keep pair carving active

player leaves the larger hysteresis volume AND transition returns to Idle/Cooldown-safe state
→ restore pair collision
```

Before restoration, the manager must reject restoration while a crossing/exit phase is active. A conservative delayed restoration is preferable to restoring collision around the player hull.

## Decisions

### Decision 1: Modify loaded BSP collision data, never the `.bsp` file

**Rationale:** The engine already uses the loaded collision representation. In-memory modification is fast, reversible, and map-scoped.

### Decision 2: Phase 1 has no proximity constraint

**Rationale:** The first experiment must measure the unfiltered consequences of removing the complete carrying brush. Proximity gating could hide lifecycle and geometry defects and make it unclear whether carving itself works.

### Decision 3: Keep existing traversal state and teleport authority

**Rationale:** BSP carving only solves wall clearance. The existing simulator already owns portal aperture intersection, plane crossing, transform, velocity, exit clearance, cooldown, and prediction synchronization.

### Decision 4: Resolve a brush at portal placement time

**Rationale:** Portal placement already supplies the strongest surface evidence. Resolving once avoids per-frame BSP traversal and lets invalid placements fail before mutation.

### Decision 5: Treat brush writes as a transaction

Activation validates all unique targets before the first write. If any write fails, already-written targets are immediately restored. Restoration uses map generation, index range, address range, and expected-current-value checks.

### Decision 6: Retain old collision bypass during initial bring-up

The first in-game runs use a diagnostic mode matrix:

| Mode | BSP carving | Legacy bridge/noclip | Purpose |
|---|---:|---:|---|
| Visual-only baseline (default) | Off | Off | Confirm portal rendering while the original wall fully blocks the player |
| Write validation | On | On | Prove safe mutation/restoration |
| Causal test | On | Off | Prove BSP carving alone clears the wall |
| Explicit legacy comparison | Off or forced failure | On | User-selected regression comparison only |

No legacy code is deleted until the causal test is repeatable, but failure returns to the visual-only baseline and never enables legacy traversal automatically.

### Decision 7: Later proximity gating is portal-local and hysteretic

**Rationale:** A sphere does not match a portal aperture and will activate through nearby unrelated geometry. A portal-local expanded prism reuses existing transforms and provides separately tunable lateral, vertical, enter-depth, and leave-depth limits.

## Safety Invariants

- No write occurs unless `g_BSPData`, all required counts, and all required pointers pass sanity checks.
- No write occurs for an out-of-range brush index.
- Original contents are captured before the first write and never overwritten by a later zero value.
- A brush shared by both portals is written once and restored only after both owners release it.
- A map-generation mismatch forbids dereferencing old brush addresses.
- Map shutdown restores while the current generation is still valid, then invalidates addresses.
- DLL shutdown calls one idempotent `RestoreAll()` path.
- A failed second write rolls back the first write.
- Repeated activation and repeated restoration are no-ops.
- Diagnostic logging is rate-limited during per-frame updates but never suppresses mutation and restoration events.

## Validation Strategy

The Stage A Windows x86 locator, complete layout, two-map round-four acceptance, and binary-change revalidation procedure are closed out in `stage-a-ccollisionbspdata-retrospective.zh-CN.md`. Passing Stage A authorizes the next read-only BSP-query work only; production BSP writes remain prohibited until the read-only portal-to-brush binding gate and recoverable transaction tests pass.

### Deterministic tests

Pure tests use synthetic memory-independent node/leaf/brush fixtures to verify:
- positive/negative BSP child traversal and leaf decoding
- convex brush plane inclusion
- box-brush bounds inclusion
- rejection of corrupt counts, indices, pointers, and side ranges
- sample-offset selection behind a hit plane
- shared-brush owner/reference behavior
- transactional rollback and idempotent restoration
- Phase 2 portal-local volume and hysteresis decisions
- transition phases preventing premature restoration

### In-game diagnostics

Focused logs must include:
- resolved `g_BSPData` address and source signature
- every field offset, array address, and count
- portal color, hit point/normal, leaf, brush index, original contents
- every mutation with old/new contents and reason
- every restoration with expected/current/restored values and reason
- client and server hull trace results before mutation, after mutation, and after restoration

### Phase 1 manual test matrix

- Wall portal on a simple rectangular brush
- Blue/orange portals on different brushes
- Blue/orange portals on the same brush
- Re-place one portal while carving is active
- Close one portal while carving is active
- Restart chapter/map while carving is active
- Unload DLL while carving is active
- Walk through the removed brush away from the aperture to document unrestricted side effects
- Place on floor/ceiling/angled surfaces to document behavior without claiming support
- Run the causal mode with legacy collision bypass disabled

## Risks / Trade-offs

| Risk | Mitigation |
|---|---|
| Linux reference offsets are invalid on Windows | Resolve and validate the Windows layout independently; never copy raw offsets without proof |
| A carrying brush covers a much larger region than the visible wall | Phase 1 explicitly measures this; Phase 2 narrows the time window; Phase 3 optionally confines space |
| Existing bridge masks whether BSP carving works | Use the causal diagnostic mode with legacy bypass disabled |
| Restore occurs after map memory is invalid | Restore in level shutdown before invalidation and guard with map generation |
| Both portals share a brush | Unique-brush transaction and owner bitmask |
| Re-placement loses the true original contents | Capture original contents once and preserve them until the final owner releases |
| Engine caches collision state | Compare immediate client/server traces before and after write; if unchanged, stop before integrating movement |
| Whole-brush removal exposes unsupported effects | Record them during Phase 1; they are outside current acceptance scope |

## Open Questions Resolved by Spikes

These do not block the SDD, but implementation must answer them with evidence:

1. What Windows x86 signature reliably resolves `g_BSPData` in the target build?
2. Which SourcePawn structure sizes and offsets match the Windows build?
3. Do client-prediction and local-server player hull traces observe the same modified data immediately?
4. Is any cache invalidation required after changing brush contents?
5. For Phase 3, does L4D2 `env_physics_blocker` support rotated bounds and the required player collision behavior?

## Rollout and Fallback

- Gate the new system behind a development configuration/console toggle initially.
- Default to the existing traversal path when BSP data is unavailable or brush resolution fails.
- Never partially activate a pair: both active portals must have valid bindings.
- Keep a console command/log marker for `status`, `resolve`, `activate`, and `restore` diagnostics during development.
- Phase 2 and Phase 3 are enabled only after the preceding phase acceptance gate is recorded as passed.
