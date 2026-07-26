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

### Decision 8: Isolate physical exit push with an exact-transform diagnostic

`ExactTransform` applies no exit-normal clearance push after the entry-to-exit transform. It is an explicit Phase 1 causal diagnostic, not yet the final clearance policy. During this mode the existing `ExitingPortal` phase remains a spatial rearm lock until the player reaches 16 units in front of the exit plane or leaves the aperture. The 0.20-second temporal cooldown begins only after that spatial lock releases, so slow exits cannot consume the cooldown while still overlapping the exit portal. Commit/exit phases also reject traversal entry at the policy layer. Focused logs compare the exact mapped eye, the portal render eye approximation, and the physical post-Teleport eye.

### Decision 9: Keep exact physics and repair the post-Teleport main-view handoff separately

Round-nine evidence shows zero error between the exact mapped eye and the physical post-Teleport eye, while the first main-render frames can still use a behind-wall visibility leaf. Moving the player would reintroduce the discontinuity that `ExactTransform` removed. Therefore the bounded exit handoff keeps the physical position unchanged, appends a validated visibility origin in front of the exit to the main view's existing PVS origins, and keeps the reduced world near clip active through `ExitingPortal` and the immediate `Cooldown` handoff. The guard is restricted to `ExactTransform`, the portal aperture, a bounded plane distance, and the non-recursive main view. It has a runtime A/B switch and file-only diagnostics.

### Decision 10: Match official crossing half-spaces before adopting the official camera handoff

Portal SDK evidence confirms that the server also uses player `Teleport`; the API itself is therefore not the primary suspect. The authoritative crossing is committed only after the player center enters the back half-space of the entry portal, whose 180-degree transform places it in the front half-space of the exit. The immediate path commits during `Touch`, while a swept/high-speed path is retained as fallback. Phase 1 therefore corrects the predicted target to a small negative entry depth, rejects any transaction whose raw transformed exit depth is negative, commits the first valid behind-plane observation in the same update, and tests the consecutive-command anchor segment against the zero plane and aperture.

The official client additionally transforms the main camera before the physical player center crosses and repairs interpolation histories, eye interpolation, viewmodel facing, and visibility history when the entity portals. Those mechanisms remain the preferred follow-up if black-sky/backside flashes are gone but residual visual stutter remains. They are intentionally not mixed into this half-space fix so the current in-game test can identify whether crossing timing and location were the primary defects.

### Decision 11: Hand off the entry camera before Teleport without moving the player

Round-eleven evidence separates the remaining backside flash from the already corrected Teleport half-spaces. During slow crossings the rendered eye can remain behind the entry plane for roughly 27–53 ms before the movement command commits Teleport; high-speed crossings show the same interval and can expose geometry behind the removed carrying brush. Portal SDK's `C_Portal_Player::CalcPortalView` establishes the relevant client behavior: once the eye is behind the portal plane and the player is in the portal hole, the client transforms the eye origin and angles through the linked portal before the authoritative player transfer completes.

Phase 1 therefore applies the existing entry-to-exit matrix after local `CalcPlayerView`, only during `IntersectingPortal`, only after the raw eye depth becomes negative, only inside the tracked entry aperture, and only while BSP carving and a valid portal pair are active. This changes the rendered camera only. Physical and predicted Teleport, movement, velocity, exact exit position, near clip, exit visibility guard, rearm, and all BSP behavior remain unchanged. A runtime `portal_visual_entryhandoff` switch and file-only `[PortalEntryViewHandoff]` evidence preserve a controlled A/B path. Official interpolation-history repair and residual-stutter work remain explicitly deferred.

### Decision 12: The ordinary portal-model mask workaround was a falsified experiment

Round-twelve testing can still freeze a wall-texture strip before Teleport, with part of the viewmodel clipped at the same time. The logs confirm the camera-handoff controls were active and black sky did not return. The remaining fault is therefore narrowed to the portal model used to write the main-view stencil being clipped by the near plane, rather than Teleport commit timing, BSP collision, or exit placement. Portal SDK 2013 likewise retains the ordinary main-view near clip and uses dedicated render-fix geometry plus controlled stencil/depth state for the close portal surface.

Round thirteen falsified the proposed ordinary-model workaround. `[PortalMaskRepair]` remained active while the positive entry-eye depth fell from roughly 1.99 to 0.24 units, yet the tester could freeze a complete carrying-wall view inside the visible blue portal border. Reducing the same model's near projection and bypassing depth therefore did not reproduce Portal SDK's generated render-fix mesh. Its render-state override, runtime command, status field, tests, and diagnostics are removed rather than retained as a dormant workaround.

### Decision 13: Align the remote RTT camera and clip plane before porting the official render-fix mesh

The side-view screenshot preserves the blue border while the portal interior is filled by the wall, and the failure is stable before Teleport. The active remote-view code differs from Portal SDK in two coupled coordinates: it pushes the transformed RTT camera one unit along the exit normal, and places the custom clip plane at `dot(normal, exitOrigin) + 1`. Portal SDK keeps the exact transformed camera and uses `dot(normal, exitOrigin - normal * 0.5)`. At observed eye depths of 0.2–2.0 units, the current 1.0-unit camera error and 1.5-unit clip-plane displacement are first-order, not epsilon-sized.

The next single-factor implementation therefore removes the remote camera push and applies the official half-unit-behind clip plane in every render mode. Existing valid PVS origins at the exit remain unchanged and separate from the camera, preserving the earlier black-sky fix without changing perspective. Portal entity placement and border offsets remain unchanged in this round; the user's note that their offsets are negotiable is recorded, but they are not required to test this root-cause hypothesis. A rate-limited file-only `[PortalOfficialRemoteView]` record captures source depth, transformed exit depth, clip distance, and zero camera push. If the full-wall view persists, the next architectural step is the official stencil-hole/depth-clear/generated-render-fix/depth-restore chain, not another near-plane threshold.

### Decision 14: Port the official near-plane proxy before depth and fog repair

Round fourteen reduced but did not eliminate the stable wall view. The decisive sample held `entryEyeDepth=0.6339` while the active main-view `zNear=1.0`: the ordinary portal model plane is clipped, but its carrying wall is approximately 0.5 units farther away and remains renderable. This explains both the stable slow case and the high-speed strip without requiring a Teleport or remote-camera error.

The isolated fix therefore ports Portal SDK's generated primary-view proxy: a quad at camera `zNear + 0.05`, clipped by twelve 1.1-expanded aperture planes and the portal front plane, CPU-projected to NDC `z=0.00001`, and drawn while the existing stencil state is `ALWAYS/REPLACE`. The current `DrawModelExecute` hook remains the only entry point. `IMatRenderContext::GetDynamicMesh` is already present, so a local minimal Windows x86 mesh ABI adapter is sufficient; no new engine hook, interface locator, signature, or offset is introduced. Depth clearing inside the stencil and fog/post-stencil repair are deliberately deferred until the wall-mask result is measured in game.

### Decision 15: Correct the dynamic-mesh index ABI before changing render architecture

Round fifteen changed the failure from a regular wall strip into large irregular triangles and trapezoids that changed with view angle and could flicker while the camera was stationary. The focused log showed the proxy was being generated continuously with plausible three-to-five-vertex polygons and no engine error. That evidence is inconsistent with ordinary Z-fighting and instead points to malformed proxy triangles.

The local ABI adapter wrote a 16-bit index at `reinterpret_cast<byte*>(indices) + stripIndex * indexSize` and stored only `stripIndex`. Portal SDK defines `IndexDesc_t::m_nIndexSize` as the active 0/1 element increment, while `CIndexBuilder` advances an `unsigned short*` by that increment and writes `m_nFirstVertex + localIndex`. With the observed active value `1`, the old byte arithmetic overlapped adjacent 16-bit writes and could turn intended indices `0,1,2` into values such as `256`, causing the GPU to consume stale dynamic-buffer vertices and form screen-scale polygons.

This round therefore changes only index emission: accept the official active increment `1`, write complete 16-bit elements at `indices[i]`, add `firstVertex`, validate capacity and 16-bit range before the first write, and fail closed for every other descriptor. Pure tests use nonzero base vertices and sentinel storage to prove values, boundaries, and no partial writes. Rate-limited `[PortalRenderFix]` evidence records `firstVertex`, `firstIndex`, `indexSize`, and the first/last emitted values. Proxy geometry, stencil/depth state, fog, RTT, Teleport, BSP, and portal placement are unchanged; no new hook, interface, signature, offset, or IDA work is required.

### Decision 16: Accept the wall-mask result and defer residual continuity

Round-sixteen testing completed 23 successful bidirectional Teleports, including slow and high-speed passes, without black sky, wall strips, carrying-wall views, or the malformed screen-scale polygons from round fifteen. All 20 rate-limited render-fix records used `indexSize=1`, `firstIndexValue=firstVertex`, and the expected contiguous last index. Shutdown restored both modified brushes from `CONTENTS_EMPTY` to their original `0x00000001`; corresponding client hull traces changed from open `fraction=1.0` before restoration to blocking `fraction=0.375` afterward.

The primary-view wall-mask defect is therefore accepted as resolved for the current single-player/local-server Phase 1 baseline. A smaller subjective visual discontinuity remains, but this change does not mix another camera or interpolation experiment into the accepted collision/render result. Follow-up work starts from Portal SDK's client `PlayerPortalled`, eye interpolation, eye-angle latch, viewmodel-facing, and visibility-history behavior, with transaction-level diagnostics before any mutation. Depth/fog repair remains deferred because round sixteen provides no black-sky or mask evidence that requires it.

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
- A teleport cannot rearm while `CommittingTeleport` or `ExitingPortal` is active; temporal cooldown starts when spatial exit clearance releases.
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
