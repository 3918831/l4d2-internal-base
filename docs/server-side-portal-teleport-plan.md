# Server-Side Portal Teleport Implementation Plan

## Goal

Replace the unsafe client-entity `vtable[118]` teleport attempt with a localserver-only server-side movement path for player portal traversal.

## Current Findings

- Portal crossing detection reaches the teleport request stage.
- The previous `C_TerrorPlayer` `vtable[118]` call is invalid because the local player comes from `client.dll`, while the referenced teleport vfunc applies to server-side entities.
- `CMoveData::m_vecAbsOrigin` is not reliable in the current SDK layout and must not be used as the teleport anchor.
- The project already has two plausible server entity bridges:
  - `IServerTools::GetIServerEntity(IClientEntity*)`
  - `IPlayerInfoManager::GetGlobalVars()->pEdicts[localIndex].GetUnknown()->GetBaseEntity()`

## Target Architecture

Client-side player data remains responsible for portal proximity, crossing detection, camera continuity, and transform calculation. Server-side entity data becomes responsible for authoritative localserver movement through `CBaseEntity::SetAbsOrigin`, `CBaseEntity::SetAbsAngles`, and `CBaseEntity::SetAbsVelocity`.

The final teleport path should:

1. Calculate `newOrigin`, `newAngles`, and `newVelocity` from the portal transform.
2. Resolve the local player to a server-side `CBaseEntity*`.
3. Apply the server-side SetAbs functions.
4. Set the client view angles with `I::EngineClient->SetViewAngles`.
5. Keep cooldown and exit-portal state handling in `CPortalTransition`.

## Implementation Steps

### 1. Pattern Scan Server SetAbs Functions

Add offsets or a portal-local resolver for:

- `CBaseEntity::SetAbsOrigin(const Vector& absOrigin)`
- `CBaseEntity::SetAbsAngles(const QAngle& absAngles)`
- `CBaseEntity::SetAbsVelocity(const Vector& vecAbsVelocity)`

Use `server.dll` as the scan module. The function pointer types should be:

```cpp
using FnSetAbsOrigin = void(__thiscall*)(void*, const Vector&);
using FnSetAbsAngles = void(__thiscall*)(void*, const QAngle&);
using FnSetAbsVelocity = void(__thiscall*)(void*, const Vector&);
```

Validation logs:

- scan result address for each signature
- whether each address is non-null
- whether each address is inside `server.dll`

### 2. Resolve Server-Side Local Player

Prefer `IServerTools` first:

```cpp
IClientEntity* clientLocal = I::ClientEntityList->GetClientEntity(localIndex);
IServerEntity* serverEntity = I::CServerTools->GetIServerEntity(clientLocal);
CBaseEntity* serverBase = serverEntity ? serverEntity->GetBaseEntity() : nullptr;
```

Keep `edict` as the fallback:

```cpp
CGlobalVars* globals = I::PlayerInfoManager->GetGlobalVars();
edict_t* edict = globals ? &globals->pEdicts[localIndex] : nullptr;
CBaseEntity* serverBase = edict && edict->GetUnknown() ? edict->GetUnknown()->GetBaseEntity() : nullptr;
```

Validation logs:

- local player index
- client local pointer
- `IServerTools` server entity pointer
- `IServerTools` server base pointer
- edict pointer
- edict unknown pointer
- edict base pointer
- whether both resolver paths agree

### 3. Dry Run Teleport

Before writing movement, keep the portal transform and print:

- target origin
- target angles
- target velocity
- chosen server base pointer
- SetAbs function pointers

This stage should not move the player. Its purpose is to prove that resolver and signatures are stable without risking a calling-convention crash.

### 4. Enable Server SetAbs Teleport

After dry-run logs are stable, replace the legacy `EntityTeleport` call with:

```cpp
setAbsVelocity(serverBase, Vector(0.0f, 0.0f, 0.0f));
setAbsOrigin(serverBase, newOrigin);
setAbsAngles(serverBase, newAngles);
setAbsVelocity(serverBase, newVelocity);
I::EngineClient->SetViewAngles(Vector(newAngles.x, newAngles.y, newAngles.z));
```

Keep the existing portal cooldown and exit state updates after successful application.

### 5. Safety Switches

Keep compile-time switches during integration:

```cpp
constexpr bool kDryRunServerTeleport = true;
constexpr bool kUseServerSetAbsTeleport = false;
```

Only one movement backend should be active at a time. The legacy client-entity vtable teleport path should remain disabled.

## Acceptance Criteria

- Creating portals still works.
- Approaching a portal no longer triggers the Debug CRT ESP failure.
- Dry run logs show a stable non-null server-side local player.
- Enabling SetAbs teleport moves the local player from entry to exit.
- View angles and velocity are transformed consistently with the portal pair.
- `CMoveData::m_vecAbsOrigin` is not required for the teleport anchor.

## Known Risks

- The supplied signatures may match function bodies rather than true function entries; this must be verified by runtime logging and, if needed, debugger disassembly.
- `IServerTools::GetIServerEntity` may be unavailable or may not map the local client player in all map states.
- The edict path depends on `gpGlobals->pEdicts` layout matching L4D2.
- `SetAbsAngles` may not fully synchronize player view; `EngineClient->SetViewAngles` should remain part of the client-side continuity path.
