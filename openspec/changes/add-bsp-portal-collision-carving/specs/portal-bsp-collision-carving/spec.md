## ADDED Requirements

### Requirement: Windows BSP collision data is resolved and validated

The system SHALL resolve the current map's loaded BSP collision data in the Windows x86 L4D2 process and SHALL reject the feature unless every required pointer, count, offset, and structure assumption passes validation.

#### Scenario: Valid BSP collision data is available
- **WHEN** a local-server map finishes loading
- **THEN** the system SHALL resolve `g_BSPData`
- **AND** validate the required plane, node, leaf, leafbrush, brush, brush-side, and box-brush arrays
- **AND** mark BSP collision queries ready for the current map generation

#### Scenario: BSP collision data cannot be proven valid
- **WHEN** address resolution or structural validation fails
- **THEN** the system SHALL perform no BSP memory write
- **AND** remain in the visual-only baseline with the original wall collision blocking the player
- **AND** log the failed validation with enough data to diagnose it

### Requirement: Portal placement resolves a carrying world brush

The system SHALL reuse successful portal-placement surface data to resolve the BSP world brush carrying each portal.

#### Scenario: Portal is placed on a supported world brush
- **WHEN** portal placement succeeds against world geometry
- **THEN** the system SHALL use the placement hit position and plane normal to locate the containing BSP leaf and brush
- **AND** store the brush index, original contents, portal owner, and map generation

#### Scenario: No valid carrying brush is found
- **WHEN** all behind-surface sample offsets fail to identify a valid `MASK_PLAYERSOLID` brush
- **THEN** the system SHALL leave BSP collision unchanged
- **AND** mark that portal binding unresolved
- **AND** remain in the visual-only baseline without automatically enabling legacy traversal

### Requirement: Brush collision mutation is reversible and transactional

The system SHALL treat modification of all unique portal brushes as one reversible transaction.

#### Scenario: Two portals use distinct brushes
- **WHEN** pair carving activates with valid blue and orange bindings
- **THEN** the system SHALL preserve each brush's original contents exactly once
- **AND** set both brush contents to `CONTENTS_EMPTY`

#### Scenario: Two portals share one brush
- **WHEN** blue and orange portals resolve to the same brush index
- **THEN** the system SHALL write that brush once
- **AND** retain ownership for both portals
- **AND** restore it only after both owners release it

#### Scenario: A pair activation partially fails
- **WHEN** validation or writing fails after another target brush has already been modified
- **THEN** the system SHALL immediately roll back every write made by that activation
- **AND** report activation failure

#### Scenario: Restoration is requested repeatedly
- **WHEN** restoration is called more than once
- **THEN** subsequent calls SHALL be safe no-ops
- **AND** SHALL NOT overwrite unrelated memory values

### Requirement: Phase 1 exposes unrestricted whole-brush carving

Phase 1 SHALL remove collision from the complete carrying brushes without player-proximity gating and without blocker entities so that raw behavior can be tested.

#### Scenario: Both portals are active with valid bindings
- **WHEN** Phase 1 carving is enabled
- **THEN** the system SHALL activate pair carving regardless of player distance
- **AND** keep carving active while the pair remains valid

#### Scenario: Player uses the portal with legacy bypass disabled
- **WHEN** pair carving is active and the local player crosses the entry aperture
- **THEN** original client and local-server movement traces SHALL permit progress through the carrying wall
- **AND** the existing transition simulator SHALL commit teleport to the linked exit

#### Scenario: Unrestricted side effects are tested
- **WHEN** Phase 1 carving is active
- **THEN** the system SHALL allow testers to observe the complete consequences of whole-brush removal
- **AND** SHALL NOT silently add proximity or blocker constraints

### Requirement: Collision is restored on every destructive lifecycle transition

The system SHALL restore modified brush contents before discarding valid map addresses or portal ownership.

#### Scenario: A portal is replaced
- **WHEN** an active portal is successfully rebound to another brush
- **THEN** the old brush owner SHALL be released
- **AND** a brush with no remaining owner SHALL be restored
- **AND** the new valid pair SHALL be activated transactionally

#### Scenario: A portal closes or becomes invalid
- **WHEN** either portal no longer forms a valid pair
- **THEN** all pair carving SHALL be restored

#### Scenario: Map or DLL shuts down
- **WHEN** level shutdown, portal shutdown, or DLL unload begins
- **THEN** the system SHALL invoke the same idempotent restoration path before invalidating the BSP map generation

### Requirement: Existing portal traversal infrastructure remains authoritative

The BSP collision system SHALL only control carrying-brush collision and SHALL NOT duplicate crossing, transformation, teleport, prediction, or visual responsibilities.

#### Scenario: Player crosses an opened carrying brush
- **WHEN** the existing simulator detects portal-plane crossing
- **THEN** `PortalTransform` SHALL determine transformed position, velocity, and angles
- **AND** `PortalPlayerTeleport` SHALL perform the local-server teleport
- **AND** the existing transition and rendering systems SHALL continue their normal lifecycle

#### Scenario: BSP carving is unavailable
- **WHEN** BSP initialization or portal-brush binding fails
- **THEN** the system SHALL remain visual-only and non-traversable
- **AND** the existing collision bridge and controlled traversal path SHALL only run in an explicitly selected legacy-comparison mode

### Requirement: Development defaults to a visual-only baseline

The system SHALL render and place portals by default, but SHALL NOT advance the legacy traversal state machine, change player move type (including temporary diagnostic writes), clear player collision traces, nudge player position, or commit teleport.

#### Scenario: BSP traversal is not enabled

- **WHEN** the player walks into either active portal aperture
- **THEN** the original map wall collision SHALL block the player
- **AND** the view through the portal SHALL continue rendering
- **AND** diagnostics SHALL identify the active mode as `VisualOnlyBaseline`

### Requirement: Diagnostic modes prove causality and restoration

The system SHALL expose diagnostic controls and logs that distinguish BSP effects from the existing collision bypass.

#### Scenario: Causal BSP test is selected
- **WHEN** BSP carving is enabled and legacy trace clearing/noclip bypass is disabled for testing
- **THEN** logs SHALL identify that mode
- **AND** player traversal success SHALL be attributable to BSP carving

#### Scenario: A brush is modified and restored
- **WHEN** a mutation lifecycle completes
- **THEN** logs SHALL record map generation, portal owner, brush index, original contents, replacement contents, current contents, and reason
- **AND** diagnostic traces SHALL show collision before mutation, after mutation, and after restoration

### Requirement: Phase 2 limits carving to the local player's traversal vicinity

After Phase 1 acceptance, Phase 2 SHALL optionally activate carving through a configurable portal-local proximity policy with hysteresis.

#### Scenario: Player enters a portal activation volume
- **WHEN** the local player's anchor enters the expanded portal-local width, height, and enter-depth limits of either active portal
- **THEN** pair carving SHALL activate

#### Scenario: Player moves near a threshold boundary
- **WHEN** carving is active and the player leaves the enter threshold but remains inside the larger leave threshold
- **THEN** carving SHALL remain active without oscillating

#### Scenario: Traversal remains in progress
- **WHEN** the transition phase is `ApproachingPortal`, `IntersectingPortal`, `CommittingTeleport`, or `ExitingPortal`
- **THEN** carving SHALL remain active regardless of a transient proximity result

#### Scenario: Player safely leaves the portal vicinity
- **WHEN** the player is outside both leave volumes and traversal is no longer active
- **THEN** the system SHALL restore original brush collision

### Requirement: Phase 3 may confine the open area with server blockers

After Phase 2 evidence demonstrates a need for spatial confinement, Phase 3 MAY create local-server blocker entities around the portal aperture, but SHALL do so only after target-build behavior is validated.

#### Scenario: Blocker capability is not validated
- **WHEN** rotated bounds, collision behavior, or lifecycle cleanup has not been proven
- **THEN** the system SHALL NOT enable blocker confinement by default

#### Scenario: Blocker confinement is enabled
- **WHEN** validated blocker geometry is created successfully around a carved brush
- **THEN** the aperture SHALL remain traversable by the local player
- **AND** surrounding tested wall regions SHALL remain blocking
- **AND** any partial creation failure SHALL remove created blockers and restore the brush

## Scope Constraints

- The capability SHALL target single-player/local-server play only.
- Acceptance SHALL concern only the local survivor player's movement and portal teleport.
- The capability SHALL NOT claim support for NPCs, common infected, special infected, AI navigation, bullets, melee traces, throwables, physics props, or remote servers.
