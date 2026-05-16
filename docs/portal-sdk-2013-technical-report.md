# Portal SDK 2013 Reference Technical Report

Date: 2026-05-17

## Purpose

This report summarizes the current `l4d2-internal-base` portal implementation and the deeply relevant parts of `Reference Code/portal-sdk-2013`. It is intended as a working technical reference for future implementation work, especially when porting Portal 1 mechanics into the current L4D2 internal mod architecture.

## Current Project Architecture

The current project is an injected L4D2 DLL plus launcher. It does not own the game DLL source, so it reconstructs Source Engine behavior through interfaces, pattern offsets, VMT hooks, and a small set of custom wrapper classes.

Key current files:

- `src/Portal/L4D2_Portal.h`
- `src/Portal/L4D2_Portal.cpp`
- `src/Portal/client/weapon_portalgun.cpp`
- `src/Portal/server/prop_portal.cpp`
- `src/Hooks/BaseClient/BaseClient.cpp`
- `src/Hooks/ModelRender/ModelRender.cpp`
- `src/Hooks/RenderView/RenderView.cpp`
- `src/SDK/L4D2/Interfaces/*`

The portal feature is currently built around `G::G_L4D2Portal`, which owns blue/orange `PortalInfo_t` state, render targets, dynamic materials, animation state, and per-frame render recursion state. The actual in-game portal entities are currently `prop_dynamic` instances with portal models:

- Blue: `models/blackops/portal.mdl`
- Orange: `models/blackops/portal_og.mdl`

`CProp_Portal::FindPortal()` in the current project is intentionally simplified. It stores one entity pointer per color in `PortalInfo_t::pPortalEntity`, creates a `prop_dynamic` through `I::CServerTools->CreateEntityByName("prop_dynamic")`, then reuses it by calling `Teleport()`.

This differs sharply from Portal SDK, where `prop_portal` is a real gameplay entity with networking, activation state, link groups, collision simulation, teleport ownership, traces through linked spaces, and placement validation.

## Current Portal Flow

### Lifecycle

`BaseClient` hooks map lifecycle:

- `LevelInitPreEntity`: precaches the portal models.
- `LevelInitPostEntity`: calls `G::G_L4D2Portal.PortalInit()`.
- `LevelShutdown`: calls `G::G_L4D2Portal.PortalShutdown()`.

There is also a late-injection fallback in `BaseClient::Init()` that calls `PortalInit()` immediately if `I::EngineClient->IsInGame()` is true. The docs note that the preferred and most reliable workflow is still injecting from the main menu, because lifecycle hooks can be missed when injected mid-map.

### Firing And Placement

Current `CWeaponPortalgun::FirePortal()`:

1. Checks core interfaces: `EngineClient`, `ClientEntityList`, `MaterialSystem`, `CServerTools`.
2. Uses local player eye origin and view angles.
3. Builds a tracer origin offset from the player eye.
4. Traces with `MASK_SHOT`.
5. Only treats `worldspawn` hits as successful placement.
6. Uses `tr.plane.normal` to build portal angles.
7. Offsets final position by `normal * 0.5f` to reduce z-fighting.
8. Finds or creates the corresponding portal entity.
9. Sets model on first creation, calls `DispatchSpawn`, then always calls `Teleport`.
10. Starts open animation and writes `PortalInfo_t::angles` / `normal`.

Current placement is therefore mostly "hit wall, place portal there". It does not yet implement Portal's fit/bump/no-portal surface system.

### Animation

`PortalInfo_t` now contains a unified animation state machine:

- `PORTAL_ANIM_IDLE`
- `PORTAL_ANIM_OPENING`
- `PORTAL_ANIM_OPEN`
- `PORTAL_ANIM_CLOSING`
- `PORTAL_ANIM_CLOSED`

`StartPortalOpenAnimation()` and `StartPortalCloseAnimation()` own state transitions. `UpdatePortalScaleAnimation()` is called from `ModelRender::DrawModelExecute::Detour()` and writes `m_flModelScale` at offset `C_BaseAnimating + 0x728`.

The current animation system is a good local abstraction. It should stay owned by `L4D2_Portal` rather than leaking state mutations into render hooks.

### Rendering

The strongest current implementation is render mode 1:

- `BaseClient::RenderView::Detour()` resets `m_nPortalRenderDepth`, clears `m_vViewStack`, pushes the main `CViewSetup`, and calls original `RenderView`.
- `ModelRender::DrawModelExecute::Detour()` detects portal models by exact model path.
- If both portals are active and the view stack is valid, it calls `RenderPortalViewRecursive()`.
- `RenderPortalViewRecursive()` computes the exit view, pushes a render target, sets custom visibility, pushes a custom clip plane, calls `Push3DView`, then calls `DrawWorldAndEntities`, which naturally re-enters `DrawModelExecute`.
- After recursion returns, `DrawModelExecute` writes a stencil mask with the portal model, binds the rendered texture to `dev/portal_content`, draws `DrawScreenSpaceQuad`, then draws a colored border material.

This mirrors Portal SDK's stencil/back-buffer recursive style more closely than the pre-rendered texture mode. The current docs correctly call this "Depth-First Natural Recursion".

Important current render lessons:

- `ViewCustomVisibility_t` is required to prevent PVS culling when the virtual camera is effectively behind the wall.
- `Push3DView(..., I::CustomView->GetFrustum(), ...)` is critical. Passing `nullptr` caused fixed missing-model cases.
- The portal surface should behave like a screen-space window, not a UV-mapped model surface.

## Portal SDK 2013 Architecture

The reference source is a full Portal game implementation. It is not just a rendering sample. The useful code is distributed across server, client, and shared directories:

- `src/game/server/portal/weapon_portalgun.cpp`
- `src/game/server/portal/prop_portal.cpp`
- `src/game/server/portal/portal_placement.cpp`
- `src/game/shared/portal/prop_portal_shared.cpp`
- `src/game/shared/portal/portal_util_shared.cpp`
- `src/game/shared/portal/PortalSimulation.cpp`
- `src/game/shared/portal/portal_gamemovement.cpp`
- `src/game/shared/portal/portal_player_shared.cpp`
- `src/game/client/portal/PortalRender.cpp`
- `src/game/client/portal/portalrenderable_flatbasic.cpp`
- `src/game/client/portal/c_prop_portal.cpp`

### Official Entity Model

Portal SDK uses a real `prop_portal` entity, not `prop_dynamic`.

Server `CProp_Portal`:

- Inherits from `CBaseAnimating` and `CPortalSimulatorEventCallbacks`.
- Registers in `CProp_Portal_Shared::AllPortals`.
- Belongs to a linkage group.
- Has `m_bIsPortal2` to distinguish blue/orange.
- Has `m_bActivated`.
- Holds `m_hLinkedPortal`.
- Maintains `m_matrixThisToLinked`.
- Owns a `CPortalSimulator` for collision/teleport simulation.

Official `FindPortal(unsigned char iLinkageGroupID, bool bPortal2, bool bCreateIfNothingFound)`:

- Searches the linkage group.
- Prefer active matching portal.
- Falls back to inactive matching portal.
- If requested, creates `prop_portal`, sets linkage group and color flag, then `DispatchSpawn`.

This is the correct conceptual model for us, but in L4D2 we likely cannot directly create `prop_portal` unless the class exists in the game DLL. The current `prop_dynamic` approach is a pragmatic visual proxy.

### Transform Matrix

The shared transform is central:

```cpp
MatrixInverseTR(localToWorld, matPortal1ToWorldInv);
matRotation.Identity();
matRotation.m[0][0] = -1.0f;
matRotation.m[1][1] = -1.0f;
*pMatrix = remoteToWorld * matRotation * matPortal1ToWorldInv;
```

This is the same 180-degree turn-through-portal transform currently approximated in `L4D2_Portal::CalculatePortalView()`. It should become our canonical transform utility and be reused for:

- Camera view transform.
- Player/entity origin transform.
- Angle transform.
- Velocity transform.
- Ray transform through portals.
- Future use/interact traces through portals.

### Official Placement

Portal gun firing is much more than a simple wall trace.

Official `TraceFirePortal()`:

- Uses a portal-aware trace filter: `UTIL_Portal_Trace_Filter`.
- Uses `CTraceFilterTranslateClones`.
- Detects near portals in front of/behind the player to avoid invalid shooting through a portal.
- Traces with `MASK_SHOT_PORTAL`.
- Treats sky/pass-through/no-portal/cleanser/door cases differently.
- Uses `VectorAngles(tr.plane.normal, vUp, qFinalAngles)`, with special handling for floors/ceilings.
- Calls `VerifyPortalPlacement(...)`.

Official placement validation lives in `portal_placement.cpp` and includes:

- `IsNoPortalMaterial()`: rejects `SURF_NOPORTAL`, glass, studio models.
- `IsPassThroughMaterial()`: rejects sky and pass-through materials.
- `FitPortalOnSurface(...)`: tries to bump the portal around corners so it fits.
- `FitPortalAroundOtherPortals(...)`: prevents overlap with same-plane portals.
- `IsPortalIntersectingNoPortalVolume(...)`: checks `func_noportal_volume`.
- `IsPortalOverlappingOtherPortals(...)`: rejects or fizzles overlapping portals.
- `TraceBumpingEntities(...)`: handles bumpers, cleansers, and dynamic obstructions.

The current project should not try to port all of this at once. The first high-value step is a smaller placement validator:

1. Reject non-world hits unless explicitly allowed.
2. Reject sky/noportal-like surfaces if flags are available.
3. Compute stable `qFinalAngles` with an up vector, especially for floor/ceiling.
4. Check portal OBB dimensions against world geometry.
5. Prevent same-plane portal overlap.
6. Add controlled bumping later.

### Official Portal Activation And Linkage

Official `PlacePortal()`:

- Handles placement failure by setting delayed fizzle state.
- On success, stores delayed placement or calls `NewLocation()`.

Official `NewLocation()`:

- Releases portal simulator ownership.
- Teleports the portal entity.
- Moves microphone/speaker helpers.
- Creates ambient sound and particles.
- Sets `m_bActivated = true`.
- Calls `UpdatePortalLinkage()`.
- Calls `UpdatePortalTeleportMatrix()`.
- Updates portal corners.
- Wakes nearby entities.
- Punches penetrating players when needed.
- Emits blue/orange open sound.

The current project only visually moves an entity and stores angle/normal. For future real teleportation, the missing concepts are:

- Linkage update.
- Teleport matrix update.
- Corners/portal OBB.
- Nearby/touching entity ownership.
- Activation state separate from animation state.

### Official Teleportation

Official teleporting is owned by `CProp_Portal`.

`ShouldTeleportTouchingEntity()` requires:

- The portal simulator owns the entity.
- Entity is teleportable.
- Linked portal exists.
- Entity center has crossed the portal plane.
- Entity is inside the portal hole.

`TeleportTouchingEntity()` then:

- Reads origin, center, angles, eye angles, and velocity.
- Transforms origin through `m_matrixThisToLinked`.
- Transforms angles with `TransformAnglesToWorldSpace`.
- Transforms velocity with `m_matrixThisToLinked.ApplyRotation`.
- Applies special player handling: eye angles, crouch/duck correction for floor/ceiling portals, pitch reorientation.
- Applies velocity clamps and minimum floor-exit velocity.
- Untouches local portal and transfers ownership to linked portal.
- Calls `Teleport()` / `UpdateVPhysicsPosition()`.

This is the biggest gap in current L4D2 implementation. Current code renders portals but does not implement robust crossing semantics.

Recommended staged approach for L4D2:

1. Player-only teleport prototype.
2. Detect plane crossing using previous/current player center relative to entry portal plane.
3. Check a simple portal rectangle aperture using right/up projections.
4. Transform player origin, view angles, and velocity via the canonical portal matrix.
5. Add a short cooldown or "recently portalled" guard to avoid ping-pong.
6. Later extend to projectiles and simple physics props if stable.

### Official Trace-Through-Portal Utilities

`portal_util_shared.cpp` is a high-value reference.

Important functions:

- `UTIL_Portal_FirstAlongRay(...)`: finds the first active linked portal intersected by a ray.
- `UTIL_Portal_TraceRay_Bullets(...)`: traces normally, checks portal intersection, transforms the remaining ray through `MatrixThisToLinked`, traces again, and remaps trace fraction/start position.
- `UTIL_PortalLinked_TraceRay(...)`: transforms rays into the linked portal's world and traces against the remote simulation.
- Beam tracing loops up to 16 portal crossings.

For the current project, these are useful for:

- Shooting portals through portals.
- Bullets/projectiles through portals.
- Player interaction/use through portals.
- Debug rays for placement and render validation.

### Official Rendering

Portal SDK has two rendering modes:

- Stencil/back-buffer recursive rendering via `CPortalRender::DrawPortalsUsingStencils`.
- Texture rendering via `CPortalRender::DrawPortalsToTextures`.

The official stencil path:

1. Filters active/visible portals.
2. Determines max stencil recursion depth from `r_portal_stencil_depth`, `MAX_PORTAL_RECURSIVE_VIEWS`, and available stencil bits.
3. Initializes stencil state.
4. Builds complex frustums per recursion level.
5. For each portal:
   - Draws pre-stencil effects.
   - Writes/increments stencil mask.
   - Uses occlusion query/pixel visibility to skip low-value renders.
   - Clears depth in stencil region.
   - Calls `RenderPortalViewToBackBuffer()`.
   - Restores fog, sky overlays, frustum, and view setup.

`portalrenderable_flatbasic.cpp` is directly relevant to our current render mode:

- `RenderPortalViewToBackBuffer()` transforms camera origin/angles via `m_matrixThisToLinked`.
- Pushes a custom clip plane at the linked portal plane with a `0.5f` backward offset.
- Builds `ViewCustomVisibility_t` by calling `m_pLinkedPortal->AddToVisAsExitPortal`.
- Pushes 3D view with `pViewRender->GetFrustum()`.
- Overrides the frustum when using a calculated see-through frustum.
- Calls `ViewDrawScene_PortalStencil()`.
- Restores frustum and view state.

`AddToVisAsExitPortal()` is more complete than our current safe-point approach:

- Adds the four portal corners as visibility origins if they are in valid leaves.
- Forces visibility override data.
- Forces the view leaf.

The current project already independently discovered the most important part: custom visibility plus a real frustum pointer are required to avoid missing indoor models.

### Official Portal Client Entity

Client `C_Prop_Portal`:

- Adds itself to `CProp_Portal_Shared::AllPortals`.
- Removes itself from portal render on destruction.
- Thinks every frame.
- `DrawModel()` returns 0 if not activated or while building cubemaps.
- Delegates actual portal drawing to `g_pPortalRender`.

This confirms a useful separation:

- Entity owns activation and transform state.
- Render system owns drawing policy and recursion.
- Shared utilities own transform/traces.

The current project has these responsibilities mixed inside `L4D2_Portal` and render hooks. That is acceptable for a mod, but future work should still split conceptual modules internally:

- `PortalState`
- `PortalTransform`
- `PortalPlacement`
- `PortalRender`
- `PortalTeleport`
- `PortalTrace`

## Gap Analysis

### Already Strong In Current Project

- Render target/material initialization.
- Natural recursive portal rendering through `DrawModelExecute`.
- Stencil plus screen-space quad compositing.
- Custom visibility workaround.
- Frustum pointer workaround.
- Portal open/close animation state machine.
- Entity reuse per portal color.
- Lifecycle cleanup across map changes.

### Missing Or Simplified

- Real `prop_portal` entity class.
- Full placement validation and bumping.
- Portal link groups.
- Portal-to-linked transform as a shared canonical utility.
- Player/entity crossing detection.
- Velocity and eye-angle transform.
- Portal-aware traces.
- Portal-aware game movement.
- Physics clone/simulator.
- Audio/particle parity.
- Occlusion/pixel visibility render skipping.
- Complex frustum clipped to portal polygon.

### High-Risk Areas

- VMT indexes and offsets in L4D2 differ from Portal SDK.
- Current `m_flModelScale = +0x728` is version-sensitive.
- `CServerTools->CreateEntityByName("prop_dynamic")` cannot provide Portal's touch/trigger behavior.
- Full `CPortalSimulator` is likely too large to port directly into an injected mod.
- Directly porting Source SDK server code into L4D2 may fail because entity class layouts and virtual methods differ.
- Recursion inside `DrawModelExecute` is powerful but sensitive to render-state leaks.
- Current `PortalShutdown()` decrements both global material pointers and class members that may alias the same material. This should be reviewed before future heavy resource work.

## Recommended Implementation Roadmap

### Phase 1: Canonical Transform Layer

Create a small, well-tested internal transform utility based on `CProp_Portal_Shared::UpdatePortalTransformationMatrix`.

Target behavior:

- Build `entryToExit` matrix from entry origin/angles and exit origin/angles.
- Transform point.
- Transform vector.
- Transform angles.
- Transform ray.

Use it to replace duplicated transform logic in `CalculatePortalView()` first, then later reuse for teleport and traces.

### Phase 2: Placement Validator

Port a simplified subset of `portal_placement.cpp`.

Start with:

- Surface rejection.
- Floor/ceiling angle correctness.
- Portal OBB fit against world.
- Same-plane portal overlap prevention.
- No dynamic bumping initially.

Do not attempt full `FitPortalOnSurface()` recursion until the simpler validator is stable.

### Phase 3: Player Teleport Prototype

Implement player-only crossing:

- Track previous player center and current player center.
- Check signed distance to portal plane.
- Require crossing front-to-back through active linked portal.
- Check local right/up bounds against portal half width/height.
- Transform origin, view angles, and velocity.
- Apply cooldown to avoid immediate re-entry.

This is the most visible gameplay milestone after rendering.

### Phase 4: Portal-Aware Rays

Implement a local equivalent of:

- `UTIL_Portal_FirstAlongRay`
- `UTIL_Portal_TraceRay_Bullets`
- `UTIL_Portal_RayTransform`

Use it for:

- Shooting portals through portals.
- Weapon traces.
- Debug overlays.

### Phase 5: Render Polish

Borrow selective optimizations from `CPortalRender`:

- Add portal corner visibility origins rather than only center-line samples.
- Add screen-area or simple visibility skip.
- Add a complex frustum clipped to the portal polygon if missing-model or overdraw issues remain.
- Review render-state restoration around stencil, depth override, fog, and material override.

### Phase 6: Physics And Props

Only after player teleport is stable:

- Support simple projectiles.
- Support selected physics props.
- Avoid direct full `CPortalSimulator` port unless absolutely necessary.

## Practical Mapping Table

| Portal SDK Concept | Reference File | Current Equivalent | Recommendation |
|---|---|---|---|
| `CProp_Portal::FindPortal` | `server/portal/prop_portal.cpp` | `src/Portal/server/prop_portal.cpp` | Keep current entity reuse, add linkage-like state internally |
| `UpdatePortalTransformationMatrix` | `shared/portal/prop_portal_shared.cpp` | `CalculatePortalView()` custom math | Extract canonical transform utility |
| `TraceFirePortal` | `server/portal/weapon_portalgun.cpp` | `src/Portal/client/weapon_portalgun.cpp` | Port validation gradually |
| `portal_placement.cpp` | `server/portal/portal_placement.cpp` | none | Use as staged validator reference |
| `TeleportTouchingEntity` | `server/portal/prop_portal.cpp` | none | Implement player-only simplified version first |
| `UTIL_Portal_TraceRay_Bullets` | `shared/portal/portal_util_shared.cpp` | none | Port ray-through-portal later |
| `CPortalRender` | `client/portal/PortalRender.cpp` | `ModelRender` + `BaseClient::RenderView` hooks | Keep current mode 1, borrow visibility/frustum optimizations |
| `CPortalRenderable_FlatBasic` | `client/portal/portalrenderable_flatbasic.cpp` | `L4D2_Portal::RenderPortalViewRecursive` | Use as closest render reference |
| `C_Prop_Portal::DrawModel` | `client/portal/c_prop_portal.cpp` | `DrawModelExecute` model filter | Current hook is necessary in injected mod |

## Bottom Line

The current project has already solved the hardest rendering integration problem for an injected L4D2 mod: recursive portal views through Source's render pipeline. Portal SDK should now be treated less as code to copy wholesale and more as a precise behavioral specification.

The highest-value next move is not more rendering. It is to formalize shared portal transforms and then build player teleportation on top of that. Once transform correctness is locked, placement validation and portal-aware traces can be added with much lower risk.
