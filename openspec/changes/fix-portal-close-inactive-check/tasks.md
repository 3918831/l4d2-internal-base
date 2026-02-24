## 1. Fix Implementation

- [x] 1.1 Modify `StartPortalCloseAnimation()` in `L4D2_Portal.cpp` to add entity pointer check: `!pPortal->pPortalEntity`

## 2. Testing

- [x] 2.1 Build project and verify compilation
- [ ] 2.2 Test: Press Reload with no portals → no animation
- [ ] 2.3 Test: Press Reload with only blue portal → only blue portal animates
- [ ] 2.4 Test: Press Reload with only orange portal → only orange portal animates
- [ ] 2.5 Test: Press Reload with both portals → both animate
