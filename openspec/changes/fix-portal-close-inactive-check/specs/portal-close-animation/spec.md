## MODIFIED Requirements

### Requirement: Portal close animation can be triggered by Reload key

The system SHALL provide a mechanism to trigger portal close animation when the player presses the Reload key while holding a pistol.

#### Scenario: Player presses Reload with active portals
- **WHEN** player presses Reload key AND both blue and orange portals are active
- **THEN** system SHALL start close animation for both portals simultaneously

#### Scenario: Player presses Reload with only one active portal
- **WHEN** player presses Reload key AND only one portal is active (has valid entity)
- **THEN** system SHALL start close animation for only the active portal
- **AND** system SHALL NOT trigger animation for inactive portals

#### Scenario: Player presses Reload with no active portals
- **WHEN** player presses Reload key AND no portals are active
- **THEN** system SHALL do nothing (no animation triggered)

#### Scenario: Inactive portal without entity is skipped
- **WHEN** player presses Reload key AND a portal has `bIsActive = true` but `pPortalEntity = nullptr`
- **THEN** system SHALL skip animation for that portal
