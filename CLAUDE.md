# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a DLL injection-based game mod for Left 4 Dead 2 that implements a Portal gun feature along with various game modifications (ESP, EnginePrediction, NoSpread). The DLL injects into the L4D2 process and uses function hooking to modify game behavior.

## Build Commands

### Prerequisites
- Visual Studio 2022 with v142 platform toolset
- Windows 10 SDK (10.0.26100.0)
- C++17 support

### Building from Command Line

Find MSBuild location (VS2022 typically at `D:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe`):

```powershell
# 32-bit Debug (main development config)
powershell.exe -Command "& 'D:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' 'src\l4d2_base.sln' /p:Configuration=Debug /p:Platform=x86"

# 32-bit Release
powershell.exe -Command "& 'D:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' 'src\l4d2_base.sln' /p:Configuration=Release /p:Platform=x86"
```

**Important**: Use `/p:Platform=x86` NOT `/p:Platform=Win32` - the solution file defines platforms as `x86` not `Win32`.

Output: `src/Debug/Lak3_l4d2_hack.dll` or `src/Release/Lak3_l4d2_hack.dll`

### Alternative Build Methods
If MSBuild 12.0 is the only available version, modify `l4d2_base.vcxproj`:
- Change `PlatformToolset` from `v142` to `v120`
- Remove `<LanguageStandard>stdcpp17</LanguageStandard>`
- Remove `<ConformanceMode>true</ConformanceMode>`

## Architecture

### Entry Point Flow

`DllMain.cpp` → `Entry/Entry.cpp::CGlobal_ModuleEntry`

The DLL waits for `serverbrowser.dll` to load (ensuring game is fully initialized), then:
1. Initializes SDK interfaces via pattern scanning
2. Sets up all hooks
3. Initializes features
4. Allocates debug console

### Hooking System

Three types of hooks are used (defined in `Util/Hook/Hook.h`):

| Hook Type | Usage | Implementation |
|-----------|-------|----------------|
| `CFunction` | Inline function hooks | MinHook library |
| `CVMTable` | Virtual method table hooks | VMT copy + redirection |
| `CTable` | Index-based VMT hooks | Direct index manipulation |

**Key Hook Locations:**
- `Hooks/BaseClient/` - Level init/shutdown, frame rendering
- `Hooks/ClientMode/` - Input and rendering hooks
- `Hooks/ModelRender/` - Model rendering for visual effects (chams, ESP)
- `Hooks/TerrorPlayer/` - Player-specific function overrides
- `Hooks/C_Weapon/Weapon_Pistol.cpp` - Portal gun fire logic

### SDK Interface System

Game interfaces are accessed via global `I::` namespace (defined in `SDK/SDK.h`). Interfaces are retrieved at runtime using:
- Pattern scanning (`Util/Pattern/Pattern.cpp`)
- Interface name lookup (`Util/Interface/Interface.cpp`)

Key interface locations:
- `SDK/L4D2/Interfaces/` - All game engine interfaces
- `SDK/L4D2/Entities/` - Client-side entity definitions

### Portal System Architecture

The Portal feature spans multiple directories:

```
Portal/
├── L4D2_Portal.h/cpp      - Main portal manager (singleton)
├── client/weapon_portalgun - Portal gun weapon implementation
├── server/prop_portal      - Portal entity
└── CustomRender.h          - Portal rendering utilities
```

**Portal Mechanics:**
- Uses recursive rendering with stencil buffer for portal masking
- Render targets store portal views (max 5 depth)
- Ray tracing via `I::EngineTrace` for placement
- Custom materials for portal effects
- Placement logic in `Hooks/C_Weapon/Weapon_Pistol.cpp`

### Namespace Conventions

| Namespace | Purpose | Examples |
|-----------|---------|----------|
| `G::` | Global singletons | `G::ModuleEntry`, `G::RenderView` |
| `I::` | Game interfaces | `I::EngineClient`, `I::ModelRender` |
| `U::` | Utilities | `U::Interface`, `U::Pattern` |
| `F::` | Features | `F::ESP`, `F::EnginePrediction` |

### Critical Patterns

**Pattern Scanning**: Memory addresses are found via byte pattern scanning (`Util/Pattern/Pattern.cpp`). Patterns must be updated when game updates change binary layouts.

**VMT Hooking**: Used extensively for class method interception. Original VMT is preserved for restoration.

**Render Target Management**: Portals require careful render target management to avoid texture corruption. Portal rendering happens in `ModelRender` hooks.

**Client-Side Only**: The mod operates entirely client-side. Server communication happens through existing game protocols (no custom network code).

## Development Notes

### Adding New Features

1. Create feature class in `Features/` directory
2. Add initialization in `Entry/Entry.cpp::CGlobal_ModuleEntry::Initialize()`
3. Add necessary hooks in `Hooks/Hooks.cpp::Initialize()`
4. Register any required interfaces in `SDK/SDK.cpp::Initialize()`

### Adding New Hooks

1. Create hook directory under `Hooks/`
2. Implement hook class inheriting from appropriate base (`CFunction`, `CVMTable`, `CTable`)
3. Register in `Hooks/Hooks.cpp`
4. Handle cleanup in `Hooks/Hooks.cpp::Unload()`

### Portal Development

The Portal system integrates tightly with the rendering pipeline. When modifying:
- `CustomRender.h` contains stencil buffer operations
- Recursive depth is controlled by `MAX_PORTAL_RECURSION` constant
- Portal pair linking happens through `CPortalPair` class

### Debugging

Debug console is allocated at `Entry/Entry.cpp:92`. Use `ConsolePrint()` or standard `printf` for output.

Key test functions for development:
- `Func_TraceRay_Test` - Ray tracing validation
- `Func_Pistol_Fire_Test` - Portal gun firing logic
- `Func_IPhysicsEnvironment_Test` - Physics environment checks
