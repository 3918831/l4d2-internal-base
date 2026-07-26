# BSP Portal Collision Carving Implementation Plan

**Goal:** In single-player/local-server L4D2, temporarily remove the loaded BSP collision contents of the brushes carrying the active portal pair so the local player's original movement can cross the wall and the existing portal traversal system can commit teleport.

**Acceptance Criteria:** Windows BSP data is validated; placement resolves carrying brushes; writes are reversible and transactional; Phase 1 operates without proximity or blockers; legacy bypass can be disabled to prove causality; every portal/map/DLL lifecycle restores collision; later phases add proximity gating and optional blockers without replacing the core.

**Architecture:** Add a validated BSP data reader and a portal-owned collision carver under `src/Portal`. Reuse existing portal placement evidence, `PortalTransform`, `PortalTransitionSimulator`, `PortalPlayerTeleport`, logging, pattern scanning, and local-server lifecycle. Default development to the visual-only baseline; retain legacy collision bypass only as an explicit comparison mode.

**Tech Stack:** C++17, Windows x86 L4D2 internals, engine pattern scanning, BSP collision structures, existing MSBuild x86 solution, existing standalone C++ tests.

---

## 0. Evidence and Windows layout spike

- [x] 0.0 Add and unit-test `VisualOnlyBaseline`: preserve portal visuals while blocking the legacy state machine, controlled noclip, collision-trace clearing, movement nudges, teleport commits, and temporary MoveType diagnostic writes, with an explicit mode log.
- [x] 0.1 Record the target L4D2 build/module versions and add a focused `PortalBsp` log category to `src/Util/Logger/PortalFileLog.h`.
- [x] 0.2 Locate Windows x86 references to `g_BSPData` in `engine.dll`; document the candidate instruction, decoded address, surrounding function, and why it is stable enough for a signature.
- [x] 0.2a Add a read-only runtime oracle for map name, `numleafs`, exact `numbrushes` inferred from the `GetBrushInfo` validity boundary, brush contents samples, and portal-surface leaf/contents samples; unit-test the boundary search.
- [x] 0.2b Capture runtime oracle evidence on at least two maps and compare it field-by-field with the three EngineTrace anchors in IDA.
- [x] 0.2c Correct the one-byte Windows `GetBrushInfo` boolean return ABI; add read-only instruction decoding plus candidate BSP base, canonical/mirrored count, and array-pointer cross-check logs.
- [x] 0.3 Store the read-only candidate decoded from `GetBrushInfo` semantic operands in dedicated `src/Util/Offsets/PortalBspOffset.h/.cpp`, including source logging; do not enable writes or rewrite the existing non-UTF-8 offset files.
- [x] 0.4 Define minimal fixed-width BSP layout types in new `src/Portal/PortalBspTypes.h` and classify their evidence; round-three IDA plus two-map runtime evidence confirms the plane/node strides and the node array's fixed `+6` box-hull allocation relation.
- [x] 0.5 Add read-only `CPortalBspData` scaffolding in `src/Portal/PortalBspData.h/.cpp` and wire the new files into `src/l4d2_base.vcxproj` and `.filters`.
- [x] 0.6 Implement checked reads for every required array pointer, logical/allocation count relation, and complete allocated span; ordinary tables require equality while nodes require allocation count = logical count + 6; reject null pointers, non-positive counts, unreasonable bounds, invalid relations, overflowed address arithmetic, and unreadable ranges.
- [x] 0.7 Add a development status probe that prints the base, offsets, array addresses, counts, full spans, map name/generation, root/leaf/cmodel invariants, and `destructiveWrites=false`; invalidate the cache on map shutdown.
- [x] 0.8 Build Debug x86 with `MSBuild.exe src/l4d2_base.sln /p:Configuration=Debug /p:Platform=x86 /m`; result: zero warnings and zero compilation/link errors.
- [x] 0.9 Run the game probe on at least two maps and save evidence that counts/pointers change coherently across map transitions; verified `ssv1` and `c1m2_streets` in one game process, including cache invalidation, generation changes, and coherent address/count changes.
- [x] 0.10 Gate: do not continue to mutation work until the Windows base address and every consumed field layout are proven. Round four produced 19/19 ready snapshots across `ssv1` and `c1m2_streets` in one process, so the Stage A layout gate is passed; the normative retrospective is `stage-a-ccollisionbspdata-retrospective.zh-CN.md`.

## 1. Pure BSP query implementation

- [x] 1.1 Create `tests/PortalBspQueryTests.cpp` with synthetic BSP fixtures for front-child traversal, back-child traversal, negative child-to-leaf decoding, and invalid leaf rejection; the RED baseline is confirmed by MSVC `C1083` because `PortalBspQuery.h` does not exist yet.
- [x] 1.2 Extract memory-independent query helpers into `src/Portal/PortalBspQuery.h/.cpp` so unit tests do not depend on live engine addresses.
- [x] 1.3 Implement `PointLeafNum` equivalent using checked node and plane access; run tests and require all node/leaf tests to pass.
- [x] 1.4 Add failing tests for convex-brush point inclusion: inside, outside one plane, null plane, zero sides, and side-range overflow.
- [x] 1.5 Implement convex brush inclusion and require the tests to pass.
- [x] 1.6 Add failing tests for box-brush inclusion at center, faces, outside bounds, and invalid box index.
- [x] 1.7 Implement box-brush inclusion and require the tests to pass.
- [x] 1.8 Add failing tests for leafbrush enumeration, `MASK_PLAYERSOLID` filtering, duplicate candidate handling, and behind-surface sample order `{2, 4, 8, 1, 16, 0.5}`.
- [x] 1.9 Implement `FindBrushForSurfacePoint` and return diagnostic metadata containing leaf index, brush index, selected sample offset, and original contents.
- [x] 1.10 Add `tests/run_portal_bsp_query_tests.cmd` following the existing standalone test-runner pattern; all BSP query tests pass.
- [x] 1.11 Integrate the pure query helpers into `CPortalBspData` checked live-memory access.
- [x] 1.12 Run Debug x86 build plus all existing `tests/run_*.cmd` suites; Debug x86 completes with zero errors and all nine standalone test runners pass.

## 2. Portal placement binding, read-only

- [x] 2.1 Extend the portal placement result path in `src/Portal/client/weapon_portalgun.cpp` to retain/reuse the successful world hit position and plane normal when the portal becomes valid.
- [x] 2.2 Add `PortalBrushBinding` state owned by a new `CPortalBspCollisionCarver` in `src/Portal/PortalBspCollisionCarver.h/.cpp`; do not add brush fields directly to rendering responsibilities unless needed for lifecycle identity.
- [x] 2.3 Bind blue/orange portal ownership to resolved brush indices at successful placement and log the full resolution result.
- [x] 2.4 On portal replacement, resolve the new brush before releasing the previous binding; if resolution fails, retain the safe existing traversal behavior and do not mutate either brush.
- [x] 2.5 Clear read-only bindings on portal close, portal shutdown, and map invalidation.
- [x] 2.6 Add a diagnostic command/status output showing each portal's active state, resolved flag, brush index, original contents, and map generation.
- [x] 2.7 In game, compare the resolved brush against the SourcePawn algorithm on simple wall, floor, ceiling, and angled test placements; record unsupported cases without adding workarounds.
- [x] 2.8 Gate: require repeatable correct brush resolution on the chosen wall test case before permitting any write code to be enabled.

## 3. Reversible mutation core

- [x] 3.1 Add failing unit tests for one-owner activation, distinct-brush pair activation, same-brush dual ownership, reactivation idempotence, and original-contents preservation.
- [x] 3.2 Add failing tests for stale expected contents, out-of-range indices, map-generation mismatch, failed second write rollback, repeated restore, and owner release order.
- [x] 3.3 Implement an injectable brush-access interface for the carver so tests use fake storage and production uses `CPortalBspData`.
- [x] 3.4 Implement compare-before-write mutation to `CONTENTS_EMPTY`; never replace the saved original value with a value read after carving.
- [x] 3.5 Implement unique-brush transactions and owner masks for blue/orange portals.
- [x] 3.6 Implement one idempotent `RestoreAll(reason)` that validates generation and expected current value before restoring.
- [x] 3.7 Implement rollback of already-written brushes if any target validation or write fails.
- [x] 3.8 Run mutation unit tests; expected result: every success, rollback, shared-owner, and idempotence case passes.
- [x] 3.9 Add production writes behind an explicit development toggle that defaults off until the first controlled in-game run.
- [x] 3.10 Log every attempted, successful, rejected, rolled-back, and restored mutation with address-independent identifiers and values.

## 4. Phase 1 — unrestricted whole-brush experiment

- [x] 4.1 Add a Phase 1 policy: both portals active + both bindings valid + development toggle enabled implies pair carving active, with no local-player distance or proximity test.
- [x] 4.2 Integrate activation after a valid pair is established; preserve transactional ordering so a pair is never half-carved.
- [x] 4.3 Integrate restoration before portal replacement release, portal close completion/invalidation, `PortalShutdown`, level shutdown, and DLL unload.
- [x] 4.4 Ensure map shutdown calls restore while current BSP pointers remain valid, then invalidates/increments map generation.
- [ ] 4.5 Add diagnostic modes for baseline, BSP+legacy, BSP-only causal test, and forced-BSP-failure fallback.
- [ ] 4.6 Add before/after/restored client and local-server hull traces through the target portal point and log fraction, `startsolid`, `allsolid`, end position, and plane normal.
- [x] 4.7 Build Debug x86 and run all unit tests; expected result: zero errors and all tests pass.
- [ ] 4.8 Test distinct-brush blue/orange portals with BSP+legacy enabled; verify mutation and restoration values exactly match.
- [x] 4.9 Disable legacy collision-result clearing and controlled noclip for the causal run while retaining transition, transform, teleport, and prediction synchronization.
- [x] 4.9a Keep the transition simulator idle until BSP carving is actually active; when movement mutation is disabled, always preserve original client/server `PlayerMove` and legacy ground handling, with regression tests for both gates.
- [x] 4.9b Decouple one-shot committed Teleport prediction synchronization from continuous movement mutation; allow BSP traversal to atomically synchronize origin, velocity, and view angles during `ExitingPortal`/`Cooldown` without enabling controlled noclip or altering hull clearance.
- [x] 4.9c Add file-only focused visual diagnostics and a runtime `FullHull`/`PlaneEpsilon` exit-clearance A/B control; keep console output limited to commands, status, and errors, and leave height-difference behavior, portal placement constraints, and blockers unchanged.
- [x] 4.9d Add a runtime A/B portal-aware main-view near-clip reduction for the confirmed pre-Teleport portal-mask clipping interval; log it file-only and leave physical exit push, height-difference behavior, placement constraints, and blockers unchanged.
- [x] 4.9e Add an `ExactTransform` zero-push diagnostic mode, file-only camera-handoff continuity evidence, and explicit spatial-plus-temporal exit rearm guards; leave crossing trigger timing, height-difference behavior, placement constraints, brush policy, and blockers unchanged.
- [x] 4.9f Preserve the exact physical exit point while guarding the post-Teleport main-view handoff: append a validated safe exit visibility origin and extend portal-aware near clipping through bounded `ExitingPortal`/`Cooldown` intervals; keep recursive portal views, non-exact A/B modes, velocity, rearm policy, height-difference behavior, placement constraints, brush policy, and blockers unchanged.
- [x] 4.9g Correct crossing half-space and high-speed commit semantics: projected anchors land just behind the entry plane, the raw transformed exit must remain on or in front of the exit plane, a first valid behind-plane observation commits in the same update, and a consecutive-command segment-plane aperture test provides the high-speed fallback. Keep the existing `Teleport` API; defer official-style camera and interpolation-history handoff to isolated follow-up work.
- [x] 4.9h Add an official-style pre-Teleport entry camera handoff as a single-factor visual fix: while the local main camera is behind the tracked entry plane and inside its aperture during `IntersectingPortal`, transform only the rendered eye origin and angles through the existing entry-to-exit matrix. Keep physical/predicted Teleport, near clip, exit guard, movement, velocity, rearm, height differences, placement, brush policy, and interpolation-history behavior unchanged.
- [x] 4.9i Implement and test the near-plane portal-model mask experiment. Round-thirteen evidence showed `[PortalMaskRepair]` applied continuously down to a 0.24-unit positive eye depth while the complete wall view remained reproducible, falsifying this path; remove its render-state override, runtime command, status field, and diagnostics under 4.9j rather than retaining a disproven workaround.
- [x] 4.9j Align the remote RTT camera and exit clip plane with Portal SDK as the next single-factor fix: keep the exact transformed camera origin with zero normal push, use `dot(normal, exitOrigin - normal * 0.5)` for every remote clip plane, preserve the existing safe PVS origins and portal-entity/border placement, add rate-limited file-only `[PortalOfficialRemoteView]` evidence, and leave Teleport, BSP, near clip, entry handoff, exit guards, height differences, and placement constraints unchanged.
- [x] 4.9k Port the Portal SDK primary-view near-plane render-fix mesh as the next single-factor fix: generate a `zNear + 0.05` camera-plane quad, clip it against twelve expanded aperture planes and the portal front plane, project it to NDC depth `0.00001`, and draw it through the existing `DrawModelExecute` stencil-replace stage. Reuse `IMatRenderContext::GetDynamicMesh` through a local Windows x86 ABI adapter; add no hook or offset, leave depth clearing/fog repair deferred, and record rate-limited file-only `[PortalRenderFix]` evidence.
- [x] 4.9l Correct the render-fix dynamic-mesh index ABI after round-fifteen evidence: treat `m_nIndexSize` as the Source 0/1 element increment rather than a byte stride, add `m_nFirstVertex` to every 16-bit index, reject inactive, unexpected, undersized, negative, and overflowing descriptors before writing, and add pure regression tests plus rate-limited index diagnostics. Keep proxy geometry, stencil state, depth/fog, RTT, Teleport, BSP, and portal placement unchanged.
- [x] 4.10 Verify the local player can approach, cross, teleport, and exit using BSP carving as the wall-clearance mechanism. Round sixteen recorded 23 successful bidirectional slow/high-speed Teleports with legacy collision bypass, controlled noclip, and movement mutation disabled; no black sky or wall-mask artifact was observed.
- [ ] 4.11 Intentionally walk through other exposed parts of the carved brush and document the unrestricted behavior requested for Phase 1.
- [ ] 4.12 Test both portals on the same brush and verify one write/one final restore.
- [ ] 4.13 Re-place blue, re-place orange, close one portal, reload/chapter transition, map change, and normal DLL shutdown while carved; verify no original contents remain lost.
- [ ] 4.14 Force address/binding/write failures and verify the legacy traversal fallback remains operational and no partial mutation survives.
- [ ] 4.15 Phase 1 gate: record a decision report containing causal traversal evidence, restoration evidence, observed unrestricted anomalies, and whether engine cache invalidation is required.

## 5. Phase 2 — configurable proximity lifecycle

- [ ] 5.1 Add failing tests to `tests/PortalBspCarveActivationTests.cpp` for portal-local lateral, vertical, front-depth, and back-depth boundaries using the existing `PortalTransform` basis conventions.
- [ ] 5.2 Add failing tests for enter/leave hysteresis, either-portal activation, both-portals-invalid rejection, and repeated boundary oscillation.
- [ ] 5.3 Add failing tests proving `ApproachingPortal`, `IntersectingPortal`, `CommittingTeleport`, and `ExitingPortal` retain carving even when the proximity sample is false.
- [ ] 5.4 Implement `PortalCarveActivationConfig` with initial tunables `lateralMargin=24`, `verticalMargin=24`, `enterDepth=96`, and `leaveDepth=144`; enforce `leaveDepth > enterDepth`.
- [ ] 5.5 Implement `IsPlayerNearPortalForCarving` using the existing local-player anchor and portal-local transform rather than a world-space sphere.
- [ ] 5.6 Implement the pure hysteretic activation policy separately from brush writes.
- [ ] 5.7 Wire the policy into the existing per-command portal update after Phase 1 remains available as a diagnostic mode.
- [ ] 5.8 Refuse restoration while a crossing/exit phase is active; add a rate-limited log explaining every delayed restore.
- [ ] 5.9 Add a conservative recovery path for lost portal probes: keep carving until the transition returns to a safe phase or an explicit safe restore condition is met.
- [ ] 5.10 Run all pure tests and Debug x86 build; expected result: all pass.
- [ ] 5.11 In game, tune thresholds using logs for player anchor, local coordinates, phase, activation decision, and restoration decision.
- [ ] 5.12 Verify ordinary gameplay far from portals always uses original BSP contents, approach activates once, threshold jitter does not oscillate, and exit restores once.
- [ ] 5.13 Phase 2 gate: compare anomaly frequency and traversal reliability with Phase 1; retain Phase 1 mode for regression diagnosis.

## 6. Phase 3 — optional aperture blocker confinement

- [ ] 6.1 Review Phase 1/2 evidence and explicitly decide whether whole-brush exposure still warrants blocker confinement; skip Phase 3 if proximity gating is sufficient.
- [ ] 6.2 If needed, run a read/write diagnostic spike using existing `IServerTools` infrastructure to create one `env_physics_blocker`, set bounds/keyvalues, dispatch spawn, move/rotate it, and remove it safely.
- [ ] 6.3 Record whether the target build supports rotated blocking volume, which player collision masks it affects, and the correct entity removal lifecycle.
- [ ] 6.4 Add pure geometry tests for decomposing a supported planar wall region into left/right/top/bottom blocker volumes around the configured rectangular portal aperture.
- [ ] 6.5 Define supported Phase 3 surface constraints; reject rather than approximate brush geometry outside those constraints.
- [ ] 6.6 Implement `CPortalApertureBlockerSet` with transactional creation: all blockers succeed or all created blockers are removed and the brush is restored.
- [ ] 6.7 Bind blocker lifecycle to the same portal owner, map generation, activation, restoration, replacement, and shutdown paths as brush carving.
- [ ] 6.8 Verify the local player crosses only through the aperture while nearby tested regions remain solid.
- [ ] 6.9 Force partial blocker creation and portal replacement failures; verify no orphan blocker or carved brush survives.
- [ ] 6.10 Phase 3 gate: enable confinement only for proven surface classes; retain proximity-only fallback for all other cases.

## 7. Hardening and handoff

- [ ] 7.1 Run the full standalone test suite and Debug x86 solution build.
- [ ] 7.2 Repeat the chosen final-mode manual matrix on at least two maps and preserve focused `portal_l4d2_traversal.log` evidence.
- [ ] 7.3 Audit every portal/map/DLL exit path for `RestoreAll()` coverage and verify it is idempotent.
- [ ] 7.4 Audit that unsupported units and remote-server behavior are neither intercepted nor claimed by this feature.
- [ ] 7.5 Document development toggles, status output, supported scope, known brush-granularity limitations, recovery steps, and how to return to the legacy traversal path.
- [ ] 7.6 Perform a final spec-compliance review against every scenario in `specs/portal-bsp-collision-carving/spec.md`.
- [ ] 7.7 Do not remove legacy collision infrastructure in this change; propose cleanup separately after sustained BSP-only validation.
