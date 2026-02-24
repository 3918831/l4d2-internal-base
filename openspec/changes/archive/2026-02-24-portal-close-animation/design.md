## Context

The portal system currently supports portal creation with a "pop-in" animation (scale 0→1) triggered when a portal entity is first rendered. The animation uses an elastic curve and updates the entity's `m_flModelScale` property (offset 0x728).

**Current Animation Flow:**
```
FirePortal() → Create Entity → DrawModelExecute() → UpdatePortalScaleAnimation()
                                                        ↓
                                              Position changed? → Start animation
                                                        ↓
                                              scale: 0.0 → 1.0 (ELASTIC)
```

**Key Infrastructure:**
- `PortalInfo_t` - Stores per-portal state including animation properties
- `UpdatePortalScaleAnimation()` - Core animation update function
- `ScaleCurves` namespace - Animation curve implementations (Linear, EaseIn, EaseOut, Elastic)
- Entity scale modification via `*(float*)((uintptr_t)pEntity + 0x728)`

## Goals / Non-Goals

**Goals:**
- Add portal removal animation triggered by Reload key
- Scale from 1.0 → 0.0 using linear interpolation
- Keep portal content rendering during animation (scale > 0)
- Block new portal creation during close animation
- Allow independent configuration of close animation duration

**Non-Goals:**
- Entity destruction (portals remain in memory, just inactive)
- Non-linear close animation curves (only linear is required)
- Per-portal sequential closing (both close simultaneously)

## Decisions

### 1. Animation Direction Flag

**Decision:** Add `bIsClosing` boolean to `PortalInfo_t` to distinguish opening vs closing animation.

**Rationale:**
- Simple state machine: opening (false) vs closing (true)
- Minimal changes to existing code
- Clear semantic meaning

**Alternative Considered:** Use negative `currentScale` direction. Rejected because it conflates two concepts (scale value and direction).

### 2. Separate Duration Configuration

**Decision:** Add `closeAnimDuration` and `closeAnimStartTime` fields separate from `animDuration`/`animStartTime`.

**Rationale:**
- User requirement: independent tuning of close speed
- Allows faster close vs slower open (or vice versa)
- Clean separation without complex conditionals

### 3. Linear Interpolation for Close Animation

**Decision:** Use `SCALE_LINEAR` curve for close animation, hardcoded.

**Rationale:**
- User requirement: simple linear fade
- No need for elastic bounce on disappear
- Linear is visually clean for "shrinking away" effect

### 4. Creation Blocking During Close

**Decision:** Check `bIsClosing` flag in portal creation code and ignore requests during close animation.

**Rationale:**
- Prevents visual glitches from interrupted animations
- Clear user feedback: portal is "busy" closing
- Simpler than animation interruption/restart logic

**Alternative Considered:** Interrupt close animation and start opening new portal. Rejected because:
- More complex state management
- Could cause jarring visual transitions

### 5. bIsActive Timing

**Decision:** Set `bIsActive = false` only when `currentScale` reaches exactly 0.0.

**Rationale:**
- Ensures portal content renders throughout entire animation
- `DrawModelExecute` already checks `bIsActive` for rendering decision
- Natural termination condition

## Risks / Trade-offs

| Risk | Mitigation |
|------|------------|
| Animation interrupted by map change | Engine handles entity cleanup; animation state resets naturally |
| Player rapidly presses Reload | Animation is already in progress; no-op behavior is correct |
| Scale precision (never exactly 0.0) | Use `<= 0.0f` check with clamping |
| Entity still exists but invisible | Intentional design; allows quick re-activation if needed |

## Data Structure Changes

```cpp
// PortalInfo_t additions
struct PortalInfo_t {
    // ... existing fields ...

    // Close animation state
    bool bIsClosing = false;           // true during close animation
    float closeAnimDuration = 0.5f;    // configurable close speed
    float closeAnimStartTime = 0.0f;   // when close animation started
};
```

## Function Changes

### UpdatePortalScaleAnimation()

```cpp
bool L4D2_Portal::UpdatePortalScaleAnimation(PortalInfo_t* pPortal, const Vector& currentPos, C_BaseAnimating* pEntity)
{
    // ... existing position change detection for opening ...

    // Handle close animation
    if (pPortal->bIsClosing) {
        float flCurrentTime = I::EngineClient->OBSOLETE_Time();
        float flElapsedTime = flCurrentTime - pPortal->closeAnimStartTime;
        float t = flElapsedTime / pPortal->closeAnimDuration;

        // Linear interpolation: 1.0 → 0.0
        float scale = 1.0f - t;

        // Clamp to valid range
        if (scale <= 0.0f) {
            scale = 0.0f;
            pPortal->bIsActive = false;
            pPortal->bIsClosing = false;
        }

        pPortal->currentScale = scale;

        // Apply to entity
        if (pEntity) {
            float* pScale = (float*)((uintptr_t)pEntity + 0x728);
            if (pScale) *pScale = scale;
        }

        return true;
    }

    // ... existing opening animation logic ...
}
```

### StartPortalCloseAnimation() [New]

```cpp
void L4D2_Portal::StartPortalCloseAnimation(PortalInfo_t* pPortal)
{
    if (!pPortal || !pPortal->bIsActive) return;

    pPortal->bIsClosing = true;
    pPortal->isAnimating = true;
    pPortal->closeAnimStartTime = I::EngineClient->OBSOLETE_Time();
}
```
