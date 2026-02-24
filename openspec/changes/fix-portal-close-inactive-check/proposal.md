## Why

The portal close animation feature has a bug: when pressing Reload with only one portal active, the inactive portal also triggers a close animation. This causes a non-existent portal to animate, which is visually incorrect and confusing.

## What Changes

- **Fix**: `StartPortalCloseAnimation()` should check not only `bIsActive` but also verify the portal entity exists before starting animation
- **Fix**: Ensure inactive portals are properly skipped during close animation trigger

## Capabilities

### New Capabilities

None

### Modified Capabilities

- `portal-close-animation`: Add additional check to skip inactive portals that don't have a valid entity

## Impact

**Files Modified:**
- `src/Portal/L4D2_Portal.cpp` - Add entity existence check in `StartPortalCloseAnimation()`

**User Experience:**
- Pressing Reload with only one portal active will only animate that portal
- Inactive portals (without entity) will be silently skipped
