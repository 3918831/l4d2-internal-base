# L4D2 Portal HDR / Tonemapping Visual Continuity

## Background

Portal traversal was already geometrically continuous: the camera direction, portal pairing, render target view, and traversal transform were correct. However, once traversal became playable, a new visual discontinuity became obvious:

- The frame before crossing and the frame after crossing showed a large brightness, fog, and sky exposure shift.
- The world orientation did not jump much, so the discontinuity looked like lighting or environment state changed suddenly.
- If the player stood very close to the portal for 2-3 seconds before crossing, the portal view gradually recovered to normal exposure and the crossing became much smoother.
- If the player walked directly through the portal, the abrupt exposure shift remained obvious.

This made the problem look like a portal environment mismatch at first, but the final cause was HDR dynamic tonemapping / eye adaptation.

## Investigation Summary

### Environment State Was Ruled Out

Diagnostic logging was added around traversal and portal rendering:

- `[PortalEnvironment]` sampled leaf, area, fog, skybox, tonemap-controller related state, ambient light, and point contents around player and transformed destination positions.
- `[PortalRenderState]` sampled render-time values around portal render target passes.
- Later logging was throttled because render hooks are extremely high frequency.

The useful result was negative evidence: fog, skybox, leaf, and area state did not explain the visible difference. They were stable enough across the problematic crossing.

### Render-Time Tone Values Matched The Symptom

The important pattern appeared in portal render state logs:

- Direct crossing had portal render tone scale around a higher value, roughly `1.6x` in the captured session.
- Waiting near the portal before crossing allowed the tone scale to drift down and stabilize, roughly around `1.2x`.
- The visual report matched this exactly: waiting near the portal let the bright sky / portal view gradually normalize, and crossing after that felt much less abrupt.

This strongly indicated temporal exposure adaptation rather than a static lighting, fog, or camera transform bug.

### Manual Tone Clamp Attempt Was Ineffective

An attempted fix sampled and applied tone scale during traversal / visual transition timing. Logs showed it often read `1.000` at those moments, while the real portal render tone difference appeared later during the render pass.

This means the attempted fix was operating at the wrong point in the Source render pipeline. The value that mattered was controlled by the engine HDR dynamic tonemapping path, not by the traversal transform itself.

## Root Cause

The root cause is Source engine HDR dynamic tonemapping, also known as eye adaptation.

Portal rendering creates a special visual situation:

- Before crossing, the destination view is visible inside the portal render target, but it occupies only part of the screen.
- As the player approaches the portal, the portal content occupies more screen area, so HDR eye adaptation gradually responds to that content.
- If the player waits near the portal, exposure has time to converge toward the destination view.
- If the player crosses immediately, the main camera suddenly becomes the destination view before exposure has converged.

The result is a sudden exposure / fog-like brightness jump across adjacent traversal frames.

The issue can be mistaken for fog, light environment, skybox, or area state because HDR exposure changes affect the whole image and can visually look like fog density or ambient lighting changed.

## Confirmed Fix

The official cvar `mat_dynamic_tonemapping` can disable dynamic tonemapping.

For the portal feature, the practical fix is:

1. Disable dynamic tonemapping while the player is in a portal-sensitive state.
2. Restore the previous cvar value when leaving that state.

Example console validation:

```text
mat_dynamic_tonemapping 0
```

Restore when testing outside portal traversal:

```text
mat_dynamic_tonemapping 1
```

In gameplay testing, disabling this during portal state removes the abrupt traversal exposure jump, and the visual impact is small enough to be acceptable.

## Related Diagnostic Cvars

`mat_hdr_manual_tonemap_rate` affects how quickly exposure adapts:

```text
mat_hdr_manual_tonemap_rate 0.25
```

This is useful for diagnosis because it changes how quickly the portal view exposure converges. It is not the preferred final fix because it only changes adaptation speed. It does not remove the mismatch between direct crossing and waited crossing.

`mat_dynamic_tonemapping` is the better portal-state control because it removes the temporal adaptation discontinuity instead of trying to tune around it.

## Implementation Recommendation

Use a small portal tonemapping guard rather than permanently changing the cvar globally.

Recommended behavior:

- On portal state enter, find `mat_dynamic_tonemapping`, store its previous value, and set it to `0`.
- Keep it disabled while any of these are true:
  - The player is inside traversal handling.
  - The player is very near / intersecting a portal plane.
  - A short post-traversal visual continuity cooldown is active.
- On portal state exit, restore the stored previous value.
- On map shutdown, DLL unload, or portal system shutdown, always restore the stored value.
- Write the cvar only on state transitions, not every frame.
- Log only state transitions, for example:

```text
[PortalTone] dynamic tonemapping disabled previous=1 reason=near-portal
[PortalTone] dynamic tonemapping restored value=1 reason=portal-state-exit
```

Avoid high-frequency per-render-pass logging here. The previous investigation showed portal render hooks can generate huge logs from a single traversal.

## Test Checklist

Use these checks when validating future changes:

- Direct walk-through traversal no longer has a large exposure jump.
- Waiting near the portal for 2-3 seconds before traversal still looks smooth.
- The image does not become noticeably too dark after crossing.
- Normal non-portal gameplay restores the original `mat_dynamic_tonemapping` value.
- Map change / shutdown / reinjection does not leave the cvar forced off accidentally.
- Logs contain only portal tonemapping state transitions, not per-frame spam.

## Knowledge Takeaways

- A fog-like or lighting-like jump can be caused by post-process HDR exposure rather than environment state.
- The "wait near the portal" observation is a strong signal for temporal adaptation.
- For portal rendering, visual continuity must consider render pipeline state, not only camera transform and destination environment state.
- Cvar-level controls are sometimes more reliable than trying to manually override tone scale at traversal timing, because the Source renderer may update the real exposure value later in the frame.
