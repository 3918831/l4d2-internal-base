# Portal Ground/Step Movement Next Implementation Plan

**Feature:** Portal traversal stage 1 continuation
**Goal:** Make grounded players enter a portal aperture smoothly without changing `MoveType` to `MOVETYPE_NOCLIP`.
**Acceptance Criteria:**
- Grounded local player can press forward into an active portal and naturally embed past the wall plane.
- Jumping and walking use the same portal-aware collision path; jumping must not be the only way to enter.
- Ground/floor traces remain valid outside the portal aperture.
- Pure vertical ground traces are not globally bypassed.
- Real teleport commit uses server `Teleport` through vtable index `118` only at plane-crossing time.
- No traversal path sets player `MoveType` to noclip.
**Architecture:** Continue the new route: `PortalTransitionSimulator` owns state, `PortalCollisionBridge` owns movement trace policy, and a new ground/step diagnostic layer identifies which GameMovement stage rejects grounded aperture entry. Do not revive the old `CPortalTransition` noclip session path.
**Tech Stack:** L4D2 injected DLL, Source SDK movement hooks, `TracePlayerBBox`, `CMoveData`, `ClientPrediction::SetupMove/FinishMove`, server-side `Teleport(vtable[118])`.
**Frontend Verification:** No.

---

## Current Findings

1. Portal placement and visual rendering are working.
2. `TracePlayerBBox` hook is active and portal-aware horizontal movement traces are being accepted.
3. Zero-length `TracePlayerBBox(pos, pos)` position tests are now accepted when the candidate hull is inside the portal aperture.
4. Walking into the portal still stops at about `centerD=15.50`, matching the standing hull front touching the wall plane.
5. Jumping into the portal can embed through the wall plane, proving the bridge is effective in the airborne path.
6. The grounded path likely fails in `StepMove`, `CategorizePosition`, ground snapping, or the final `CMoveData` origin comparison/writeback path.
7. The old `CPortalTransition` path changed `MoveType` to `MOVETYPE_NOCLIP`; this is confirmed unsuitable and has been removed from the active route.

---

## Non-Goals

- Do not reintroduce `MOVETYPE_NOCLIP`.
- Do not use repeated small teleports as the primary embedding mechanism.
- Do not globally bypass all player collision near a portal.
- Do not solve multi-entity traversal in this stage.
- Do not rewrite rendering or placement.

---

## Task 1: Add Grounded Movement Diagnostics

**Files:**
- Modify: `src/Hooks/ClientPrediction/ClientPrediction.cpp`
- Modify: `src/Portal/PortalStage1Probe.h`
- Modify: `src/Portal/PortalStage1Probe.cpp`

**Steps:**
1. Add a `PortalMoveFrameDiagnostics` struct with:
   - command number
   - `CMoveData::m_vecAbsOrigin` before/after `FinishMove`
   - `CMoveData::m_vecVelocity` before/after `FinishMove`
   - `m_outStepHeight`
   - `m_bGameCodeMovedPlayer`
   - local player origin before/after
   - local player velocity before/after
   - phase/entry side from `PortalTransitionSimulator`
2. Capture `FinishMove` before calling the original function and after it returns.
3. Log only while `PortalTransitionSimulator::IsInCollisionBridgePhase()` is true.
4. Compare walking failure vs jumping success.

**Verification:**
- Build Debug x86.
- Walk into a portal: log should show whether `CMoveData` moves past `centerD=15.50` and is later reverted, or never moves past it.
- Jump into a portal: log should show the airborne path that successfully moves past the plane.

---

## Task 2: Classify TracePlayerBBox Call Sites By Shape

**Files:**
- Modify: `src/Portal/PortalCollisionBridge.h`
- Modify: `src/Portal/PortalCollisionBridge.cpp`
- Modify: `src/Portal/PortalStage1Probe.cpp`

**Steps:**
1. Extend `PortalCollisionBridgeDiagnostics` with counters:
   - horizontal accepted
   - zero-length accepted
   - vertical rejected
   - ground-like rejected
   - startsolid accepted
2. Add a trace classifier:
   - `HorizontalMove`
   - `ZeroLengthPositionTest`
   - `VerticalGroundProbe`
   - `StepUpDownProbe`
   - `Other`
3. Log the last accepted and rejected classifier.
4. Keep the current restriction that pure vertical ground probes are not globally bypassed.

**Verification:**
- Walking failure should identify which trace class remains rejected before origin stalls.
- Jumping success should identify which trace class enables the air path.

---

## Task 3: Grounded Portal Aperture Policy

**Files:**
- Modify: `src/Portal/PortalCollisionBridge.cpp`

**Steps:**
1. If diagnostics show `StepMove` down/up probes are rejecting aperture entry, add a grounded portal policy:
   - Only active while simulator phase is `ApproachingPortal` or `IntersectingPortal`.
   - Only applies when player hull front is inside aperture.
   - Only applies when the attempted destination advances toward or through entry portal plane.
   - Does not bypass floor support traces outside the aperture.
2. For step-down traces inside aperture, return a non-wall result that does not restore the old grounded path.
3. Preserve valid ground detection for normal floor outside the portal.

**Verification:**
- Walking into portal begins moving past `centerD=15.50`.
- Jumping behavior does not regress.
- Walking beside portal still collides with the wall.

---

## Task 4: Plane Crossing Commit

**Files:**
- Modify: `src/Portal/PortalTransitionSimulator.h`
- Modify: `src/Portal/PortalTransitionSimulator.cpp`
- Modify: `src/Portal/L4D2_Portal.h`
- Modify: `src/Portal/L4D2_Portal.cpp`

**Steps:**
1. Add previous frame center/eye signed distances to simulator context.
2. Detect crossing when center or eye moves from front side to behind entry portal plane while inside aperture.
3. Build entry-to-exit transform using `PortalTransform`.
4. Resolve server local player through `CServerTools`/edict path already proven in old `EntityTeleport`.
5. Call server entity `Teleport` via vtable index `118`.
6. Transform velocity and view angles atomically.
7. Enter `Cooldown`/`ExitingPortal` state without changing `MoveType`.

**Verification:**
- Walking or jumping through a portal exits at the paired portal.
- No noclip session log appears.
- No repeated micro-teleport is needed before crossing.

---

## Task 5: Regression Checks

**Files:**
- Modify as needed after diagnostics.

**Steps:**
1. Build Debug x86.
2. Test walking into a portal on a flat wall.
3. Test jumping into a portal on the same wall.
4. Test walking beside the portal aperture.
5. Test closing/replacing portals resets simulator/bridge state.
6. Test with console command `portal_stage1_probe`.

**Expected Result:**
- Walking and jumping both enter the aperture smoothly.
- Wall collision remains intact outside aperture.
- Teleport occurs only at crossing.
- No `MOVETYPE_NOCLIP` is used.

