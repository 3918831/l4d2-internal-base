## Why

The current portal traversal implementation must bypass several client and server `CCSGameMovement` collision paths so that the local player's hull can cross the world wall carrying a portal. This is fragile because normal movement performs horizontal, step, ground, and position-validity traces through multiple call sites.

L4D2 loads compiled BSP world collision into an in-memory `CCollisionBSPData` structure. Temporarily removing the collision contents of the brush carrying a portal can make the original movement code observe a real opening, while the existing portal state machine, coordinate transform, teleport, prediction, and visual systems continue to handle traversal.

## What Changes

- Add a Windows x86 BSP collision-data resolver and validated structure view for the local L4D2 process.
- Add read-only BSP traversal that resolves a portal placement hit to its containing world brush.
- Add a reversible brush-content modification manager with duplicate-brush ownership handling and fail-safe restoration.
- Integrate brush resolution with the existing portal placement lifecycle and preserve the existing `PortalTransitionSimulator`, `PortalTransform`, and `PortalPlayerTeleport` responsibilities.
- Deliver the feature incrementally:
  - **Phase 1 — Unrestricted experiment:** when both portals are valid, remove the complete collision contents of their carrying brushes without proximity constraints or blocker entities. Restore on portal replacement, close, shutdown, or map transition. This phase intentionally exposes the raw side effects for manual testing.
  - **Phase 2 — Proximity lifecycle:** activate carving only while the local player is near either portal or traversal is in progress, using a configurable portal-local volume and hysteresis.
  - **Phase 3 — Aperture confinement:** optionally surround the portal aperture with server-side blocker entities when testing proves whole-brush removal needs spatial confinement.
- Add deterministic unit tests for BSP math, brush ownership, and activation policy, plus explicit in-game diagnostic and manual test procedures.

## Capabilities

### New Capabilities

- `portal-bsp-collision-carving`: Resolve and temporarily modify BSP world brushes to open a collision path for local-player portal traversal.

### Modified Capabilities

None. Existing portal traversal behavior remains authoritative for crossing detection, transforms, teleport commit, and visual transition.

## Impact

**Primary code areas:**
- `src/Portal/` — BSP collision manager, portal placement integration, lifecycle integration
- `src/Util/Offsets/` and `src/Util/Pattern/` — Windows `g_BSPData` resolution
- `src/Hooks/BaseClient/` or the existing per-command portal update path — manager update and map lifecycle
- `tests/` — pure BSP geometry and state-policy tests

**Operational scope:**
- Supported: single-player/local listen server and the local survivor player
- Not supported: remote authoritative servers, multiplayer ownership, NPC traversal, AI navigation, bullets, special infected, common infected, throwables, or physics props

**Compatibility:**
- Existing collision-bypass and controlled-noclip code remains available for explicit comparison, but development defaults to a visual-only baseline and never enables the legacy path automatically when BSP is unavailable.
- All memory writes are reversible and guarded by validated addresses, counts, indices, original values, and map generation.
