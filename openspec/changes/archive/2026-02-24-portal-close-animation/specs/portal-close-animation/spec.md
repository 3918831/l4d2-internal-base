## ADDED Requirements

### Requirement: Portal close animation can be triggered by Reload key

The system SHALL provide a mechanism to trigger portal close animation when the player presses the Reload key while holding a pistol.

#### Scenario: Player presses Reload with active portals
- **WHEN** player presses Reload key AND both blue and orange portals are active
- **THEN** system SHALL start close animation for both portals simultaneously

#### Scenario: Player presses Reload with only one active portal
- **WHEN** player presses Reload key AND only one portal is active
- **THEN** system SHALL start close animation for the active portal only

#### Scenario: Player presses Reload with no active portals
- **WHEN** player presses Reload key AND no portals are active
- **THEN** system SHALL do nothing (no animation triggered)

---

### Requirement: Close animation uses linear scale interpolation

The system SHALL animate portal scale from 1.0 to 0.0 using linear interpolation during close animation.

#### Scenario: Close animation scale progression
- **WHEN** close animation is in progress
- **THEN** portal scale SHALL decrease linearly from 1.0 to 0.0 over the configured duration

#### Scenario: Close animation completion
- **WHEN** portal scale reaches 0.0
- **THEN** system SHALL set `bIsActive = false` for that portal

---

### Requirement: Close animation duration is independently configurable

The system SHALL allow close animation duration to be configured separately from open animation duration.

#### Scenario: Default close animation duration
- **WHEN** portal is created with default settings
- **THEN** `closeAnimDuration` SHALL default to 0.5 seconds

#### Scenario: Custom close animation duration
- **WHEN** `closeAnimDuration` is set to a custom value
- **THEN** close animation SHALL complete in that many seconds

---

### Requirement: Portal content continues rendering during close animation

The system SHALL continue rendering the portal content (scene behind portal) while close animation is in progress.

#### Scenario: Content rendering during close animation
- **WHEN** close animation is in progress AND scale > 0.0
- **THEN** system SHALL continue rendering portal content via RTT texture

#### Scenario: Content rendering stops at scale zero
- **WHEN** close animation completes AND scale == 0.0
- **THEN** system SHALL stop rendering portal content

---

### Requirement: Portal creation is blocked during close animation

The system SHALL ignore portal creation requests while close animation is in progress.

#### Scenario: Player fires new portal during close animation
- **WHEN** player fires new portal AND portal is currently closing (`bIsClosing == true`)
- **THEN** system SHALL ignore the creation request

#### Scenario: Portal creation allowed after close animation
- **WHEN** player fires new portal AND close animation has completed (`bIsClosing == false`)
- **THEN** system SHALL process the creation request normally

---

### Requirement: Portal entity is not destroyed during close animation

The system SHALL keep the portal entity in memory after close animation completes, only setting it to inactive state.

#### Scenario: Entity persistence after close
- **WHEN** close animation completes
- **THEN** portal entity SHALL remain in memory with `bIsActive = false`

#### Scenario: Entity pointer preservation
- **WHEN** close animation completes
- **THEN** `pPortalEntity` pointer SHALL NOT be cleared
