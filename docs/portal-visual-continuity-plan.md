# Portal Visual Continuity Implementation Plan

**Feature:** Portal traversal visual continuity after local physics traversal baseline

**Goal:** Keep the existing stable local portal traversal physics, then reduce the visible camera discontinuity during the teleport frame without reintroducing wall clipping or stuck states.

**Acceptance Criteria:**
- Player traversal remains crash-free on localserver.
- Player can enter a portal, cross at the center point, teleport through the server-side entity path, and leave the exit portal without getting stuck.
- The entry-wall flash frame remains eliminated.
- The first post-teleport view no longer feels like the player has already moved too far forward from the exit portal.
- Any new visual continuity mechanism can be toggled or disabled for debugging.
- Console logs clearly identify traversal phase, visual compensation phase, and fallback behavior.

**Architecture:** Keep the current hybrid traversal implementation: GMod-style `InPortal` state and portal-plane crossing semantics, adapted to this L4D2 project through trace bypass, server-side vtable teleport, and assisted embedding. The next stage adds a visual continuity layer around the teleport frame instead of pushing the physical player closer to the exit wall.

**Tech Stack:** C++17, existing portal transform math, Source/L4D2 hooks already present in the project, current logging utilities, server-side local player teleport through `IServerTools`/server entity vtable.

---

## Current Baseline

The current branch has a stable physical traversal baseline:

- `InPortal` state starts when the player intentionally moves into a ready portal aperture.
- Player movement is assisted through the entry portal when normal hull collision blocks the center from reaching the portal plane.
- Teleport triggers when the player center crosses the portal plane, not merely when the hull touches the wall.
- If assisted embedding would cross the plane, the code teleports in the same update to avoid rendering an entry-wall frame.
- Server-side vtable teleport is used because client entity vtable teleport caused calling-convention failures.
- Exit clearance has been restored to the stable value: `finalExitEyeD ~= 32`.

This is a hybrid implementation. The high-level state machine follows `PortalGun_Gmod_gmpublisher`; the concrete implementation uses project-specific L4D2 hooks and server entity access because the GMod Lua APIs and reliable `CMoveData` path are not available here.

## Problem To Solve

Lowering the exit clearance improves apparent visual continuity but risks the player getting stuck. Keeping the stable clearance avoids stuck states but makes the first post-teleport view feel slightly too far forward from the exit portal.

Therefore, the next stage should not use player physical position as the only visual tuning knob. We need a short-lived camera/view compensation layer around the teleport frame.

## Non-Goals

- Do not implement entity cloning yet.
- Do not implement full recursive visual portal traversal for the player body.
- Do not depend on the currently invalid `CMoveData::m_vecAbsOrigin`.
- Do not replace the verified server-side teleport path unless a safer engine API is confirmed.
- Do not lower exit clearance below the stable value as the primary solution.

## Proposed Direction

### Stage 3A: Teleport Frame Diagnostics

**Files:**
- Modify: `src/Portal/PortalTransition.h`
- Modify: `src/Portal/PortalTransition.cpp`
- Optional modify: existing view/render hook files after locating the best hook point

**Implementation:**
- Add a small `VisualTransitionState` to record:
  - source portal side
  - exit portal side
  - pre-teleport eye position
  - predicted crossing eye position
  - physical post-teleport eye position
  - view angles before and after transform
  - start time and expiry time
- Log the physical exit offset and the desired visual offset on each teleport.

**Verification:**
- Build Debug x86.
- In game, confirm logs show one visual transition state per successful teleport.
- Confirm no behavior change yet beyond logs.

### Stage 3B: One-Frame View Position Compensation

**Files:**
- Modify: `src/Portal/PortalTransition.h`
- Modify: `src/Portal/PortalTransition.cpp`
- Modify: the selected view setup hook, likely under `src/Hooks/RenderView/` or `src/Hooks/ClientMode/`

**Implementation:**
- Keep physical teleport destination at stable clearance.
- For the first rendered frame after teleport, offset the camera backward along exit normal by the difference between stable physical clearance and desired visual clearance.
- Suggested initial values:
  - physical exit clearance: `32`
  - visual exit clearance: `10` to `16`
  - compensation duration: one rendered frame, or up to `0.03s`
- Clamp compensation so it never moves the camera behind the exit portal plane.
- Add a debug cvar or local constant to disable compensation.

**Verification:**
- Build Debug x86.
- In game, cross blue to orange and orange to blue repeatedly.
- Expected: no stuck state, no entry-wall flash, first post-teleport view feels closer to the portal surface.
- Logs should show compensation applied and then expired.

### Stage 3C: Short Blend Instead Of Single Frame

**Files:**
- Same files as Stage 3B

**Implementation:**
- If one-frame compensation is too abrupt, blend visual offset from desired visual clearance back to physical clearance over `0.05s` to `0.10s`.
- Use linear interpolation first; only add easing if linear looks visibly wrong.
- Keep physical player position unchanged.

**Verification:**
- Compare one-frame and short blend behavior.
- Acceptance is based on in-game observation:
  - no visible flash
  - no apparent extra forward jump
  - no camera clipping into the exit wall
  - no movement/control delay felt by the player

### Stage 3D: Exit Portal Grace Collision Review

**Files:**
- Modify: `src/Portal/PortalTransition.cpp`

**Implementation:**
- Review whether `ExitingPortal` trace bypass should remain active for the exit aperture.
- Keep it if it prevents edge-case sticking.
- Narrow it if it allows unintended wall penetration around the exit portal.

**Verification:**
- Place portals on flat walls and near corners.
- Confirm player cannot walk through adjacent non-portal wall areas.

## Risks And Open Questions

- We need to confirm the best hook to alter only the rendered view without changing server/player physical origin.
- If the view hook runs before teleport state updates, we may need to store state earlier or apply compensation in a different hook.
- Weapon/viewmodel alignment may need separate handling if camera compensation affects world view but not the weapon.
- If L4D2 prediction overwrites view origin after our hook, we may need to move compensation later in the render pipeline.

## Review Checklist Before Coding

- Confirm which view hook owns final camera origin for local player rendering.
- Confirm whether weapon viewmodel should share the visual offset.
- Choose initial desired visual clearance: `16` is safer than `10`; `10` is closer to the previous visual test.
- Decide whether Stage 3B starts as one-frame compensation or immediately uses a short blend.

## Suggested First Coding Pass

1. Add `VisualTransitionState` and logs only.
2. Build and verify no behavior changes.
3. Add one-frame view compensation behind a local constant.
4. Build and copy DLL for in-game test.
5. If one-frame compensation still feels abrupt, extend to a short blend.

