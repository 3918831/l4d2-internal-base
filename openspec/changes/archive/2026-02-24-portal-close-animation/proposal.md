## Why

Currently, portals can only be created (via left/right click) but have no mechanism for removal. Players need a way to clear existing portals without restarting the game or changing maps. This feature adds a "portal removal" animation triggered by the Reload key, providing intuitive visual feedback when portals disappear.

## What Changes

- **New**: Portal close animation system that smoothly scales portal models from 1.0 to 0.0
- **New**: `StartPortalCloseAnimation()` function to trigger the close animation
- **New**: `bIsClosing` flag in `PortalInfo_t` to track animation direction
- **New**: Independent `closeAnimDuration` configuration for close animation speed
- **Modified**: `UpdatePortalScaleAnimation()` to support both opening (0→1) and closing (1→0) directions
- **Modified**: `Pistol::Reload::Detour` to trigger close animation for both portals
- **Modified**: Portal creation logic to ignore creation requests during close animation

## Capabilities

### New Capabilities

- `portal-close-animation`: Portal removal animation system with linear scale interpolation, simultaneous dual-portal closing, and creation blocking during animation

### Modified Capabilities

- None (this is a new feature, not modifying existing spec requirements)

## Impact

**Files Modified:**
- `src/Portal/L4D2_Portal.h` - Add new fields to `PortalInfo_t`, declare `StartPortalCloseAnimation()`
- `src/Portal/L4D2_Portal.cpp` - Implement close animation logic in `UpdatePortalScaleAnimation()`
- `src/Hooks/C_Weapon/Weapon_Pistol.cpp` - Trigger close animation in `Reload::Detour`
- `src/Portal/client/weapon_portalgun.cpp` - Add check to block creation during close animation

**Dependencies:**
- Existing portal animation infrastructure (`ScaleCurves` namespace)
- Entity scale system (`m_flModelScale` at offset 0x728)
- Portal rendering system in `ModelRender::DrawModelExecute`

**User Experience:**
- Pressing Reload key will smoothly animate both portals shrinking to nothing
- Portal content continues to render during animation until scale reaches 0
- New portal creation is blocked during close animation
