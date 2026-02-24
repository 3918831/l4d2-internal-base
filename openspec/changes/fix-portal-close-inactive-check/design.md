## Context

The current `StartPortalCloseAnimation()` function checks `bIsActive` to determine if a portal should animate:

```cpp
void L4D2_Portal::StartPortalCloseAnimation(PortalInfo_t* pPortal)
{
    if (!pPortal || !pPortal->bIsActive) {
        return;
    }
    // ...start animation
}
```

However, `bIsActive` can be `true` even when the portal entity (`pPortalEntity`) is `nullptr`. This happens when:
1. A portal was previously created (setting `bIsActive = true`)
2. The portal entity was removed or never properly initialized
3. `bIsActive` was not reset to `false`

## Goals / Non-Goals

**Goals:**
- Fix the bug where inactive portals trigger close animation
- Ensure only portals with valid entities animate

**Non-Goals:**
- Refactoring the entire portal state management system
- Changing how `bIsActive` is managed elsewhere

## Decisions

### Decision 1: Add entity pointer check

**Choice:** Add `pPortalEntity != nullptr` check to `StartPortalCloseAnimation()`

**Rationale:**
- Simple and direct fix
- Ensures animation only runs for portals that actually exist
- Minimal code change

**Implementation:**
```cpp
void L4D2_Portal::StartPortalCloseAnimation(PortalInfo_t* pPortal)
{
    if (!pPortal || !pPortal->bIsActive || !pPortal->pPortalEntity) {
        return;
    }
    // ...start animation
}
```

## Risks / Trade-offs

| Risk | Mitigation |
|------|------------|
| Entity pointer becomes stale | Unlikely in this context; entity lifecycle is managed by engine |
| Check is too strict | Acceptable; we only want to animate truly existing portals |
