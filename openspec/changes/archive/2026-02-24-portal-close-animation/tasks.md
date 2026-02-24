## 1. Data Structure Changes

- [x] 1.1 Add `bIsClosing` boolean field to `PortalInfo_t` struct in `L4D2_Portal.h`
- [x] 1.2 Add `closeAnimDuration` float field to `PortalInfo_t` struct (default 0.5f)
- [x] 1.3 Add `closeAnimStartTime` float field to `PortalInfo_t` struct

## 2. Close Animation Function

- [x] 2.1 Declare `StartPortalCloseAnimation(PortalInfo_t* pPortal)` in `L4D2_Portal.h`
- [x] 2.2 Implement `StartPortalCloseAnimation()` in `L4D2_Portal.cpp`:
  - Check if portal is active before starting
  - Set `bIsClosing = true`
  - Set `isAnimating = true`
  - Set `closeAnimStartTime` to current time

## 3. Update Animation Logic

- [x] 3.1 Modify `UpdatePortalScaleAnimation()` in `L4D2_Portal.cpp` to handle close animation:
  - Check `bIsClosing` flag at start of function
  - Calculate scale using linear interpolation: `scale = 1.0f - t`
  - Set `bIsActive = false` when scale reaches 0.0
  - Set `bIsClosing = false` when animation completes
  - Apply scale to entity via offset 0x728

## 4. Reload Hook Integration

- [x] 4.1 Modify `Pistol::Reload::Detour` in `Weapon_Pistol.cpp`:
  - Call `StartPortalCloseAnimation(&g_BluePortal)`
  - Call `StartPortalCloseAnimation(&g_OrangePortal)`
  - Keep original function call

## 5. Creation Blocking

- [x] 5.1 Locate portal creation code in `weapon_portalgun.cpp` (FirePortal1/FirePortal2)
- [x] 5.2 Add check at start of creation: if portal `isAnimating && bIsClosing`, return early to ignore request

## 6. Testing

- [x] 6.1 Build project and verify compilation
- [ ] 6.2 Test: Press Reload with no portals → no crash, no action
- [ ] 6.3 Test: Press Reload with one portal → single portal closes smoothly
- [ ] 6.4 Test: Press Reload with two portals → both close simultaneously
- [ ] 6.5 Test: Fire new portal during close animation → request is ignored
- [ ] 6.6 Test: Fire new portal after close completes → portal creates normally
- [ ] 6.7 Verify portal content renders throughout close animation
