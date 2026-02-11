# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a DLL injection-based game mod for Left 4 Dead 2 that implements a Portal gun feature along with various game modifications (ESP, EnginePrediction, NoSpread). The DLL injects into the L4D2 process and uses function hooking to modify game behavior.

**NEW**: The project now includes an auto-loading launcher program (`L4D2_Portal.exe`) that automatically injects the DLL, eliminating the need for manual injection tools.

## Build Commands

### Prerequisites
- Visual Studio 2022 with v142 platform toolset
- Windows 10 SDK (10.0.26100.0)
- C++17 support

### Building from Command Line

**Step 1: Find MSBuild location** (if not in PATH)
```powershell
# Using vswhere.exe (recommended)
powershell.exe -Command "& 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -latest -products * -requires Microsoft.Component.MSBuild -property installationPath"

# Or search directly
powershell.exe -Command "Get-ChildItem 'C:\Program Files*' -Recurse -Filter MSBuild.exe -ErrorAction SilentlyContinue | Select-Object -First 3 FullName"
```

**Step 2: Build**
```powershell
# 32-bit Debug (main development config)
powershell.exe -Command "& 'D:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' 'g:\Code_Project\Visual Studio 2022 Project\3918831\Github\l4d2-internal-base\src\l4d2_base.sln' /p:Configuration=Debug /p:Platform=x86"

# 32-bit Release
powershell.exe -Command "& 'D:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' 'g:\Code_Project\Visual Studio 2022 Project\3918831\Github\l4d2-internal-base\src\l4d2_base.sln' /p:Configuration=Release /p:Platform=x86"
```

**Important**:
- Use `/p:Platform=x86` NOT `/p:Platform=Win32` - the solution file defines platforms as `x86`
- Adjust the path to match your actual workspace location

**Output**: `src/Debug/` or `src/Release/`
- `L4D2_Portal.dll` - Mod DLL (390 KB)
- `L4D2_Portal.exe` - Launcher program (58 KB)

### Solution Structure

```
l4d2_base.sln
├── l4d2_base.vcxproj    # Mod DLL project (outputs L4D2_Portal.dll)
└── Launcher.vcxproj      # Launcher project (outputs L4D2_Portal.exe)
```

### Common MSBuild Locations
| Visual Studio Version | MSBuild Path |
|----------------------|--------------|
| VS2022 Community | `D:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe` |
| VS2022 Professional | `C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe` |
| VS2022 Enterprise | `C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe` |

### Alternative Build Methods
If MSBuild 12.0 is the only available version, modify `l4d2_base.vcxproj`:
- Change `PlatformToolset` from `v142` to `v120`
- Remove `<LanguageStandard>stdcpp17</LanguageStandard>`
- Remove `<ConformanceMode>true</ConformanceMode>`

## Auto-Loading System

### Overview

The project now includes an auto-loading launcher that:
1. Detects if L4D2 is running
2. Launches the game with custom parameters if not running
3. Automatically injects the mod DLL
4. Supports injecting into already-running game instances

### Launcher Implementation

**Key File**: `Launcher/Launcher.cpp`

**Process Flow**:
```
L4D2_Portal.exe
    │
    ├── Check if L4D2 is running
    │   ├─ Yes → Inject DLL directly
    │   └─ No → Launch L4D2 with args → Wait 10s → Inject DLL
    │
    └── Exit
```

**Game Launch Parameters**:
- `-insecure` - Disable VAC anti-cheat (for testing)
- `-steam` - Enable Steam authentication
- `-novid` - Skip intro video

**DLL Injection Method**:
- Uses `CreateRemoteThread` and `LoadLibrary` technique
- Allocates memory in target process
- Writes DLL path to target memory
- Creates remote thread that calls `LoadLibraryA`

### Modifying Launch Parameters

Edit `Launcher/Launcher.cpp:189`:
```cpp
const char* cmdArgs = "-insecure -steam -novid -console -windowed";
```

### Console Behavior

**Debug Build**:
- Allocates console window for debugging
- Automatically minimizes console to background
- Use `ShowWindow(GetConsoleWindow(), SW_MINIMIZE)` in `Entry/Entry.cpp:291`

**Release Build**:
- No console allocation (cleaner experience)

## Architecture

### Entry Point Flow

`DllMain.cpp` → `Entry/Entry.cpp::CGlobal_ModuleEntry`

The DLL waits for `serverbrowser.dll` to load (ensuring game is fully initialized), then:
1. Initializes SDK interfaces via pattern scanning
2. Sets up all hooks
3. Initializes features
4. Allocates debug console (and minimizes it in Debug builds)

### Hooking System

Three types of hooks are used (defined in `Util/Hook/Hook.h`):

| Hook Type | Usage | Implementation |
|-----------|-------|----------------|
| `CFunction` | Inline function hooks | MinHook library |
| `CVMTable` | Virtual method table hooks | VMT copy + redirection |
| `CTable` | Index-based VMT hooks | Direct index manipulation |

**Key Hook Locations:**

*Client-Side Hooks:*
- `Hooks/BaseClient/` - Level init/shutdown, frame rendering
- `Hooks/ClientMode/` - Input and rendering hooks
- `Hooks/ModelRender/` - Model rendering for visual effects (chams, ESP)
- `Hooks/TerrorPlayer/` - Player-specific function overrides
- `Hooks/C_Weapon/Weapon_Pistol.cpp` - Portal gun fire logic
- `Hooks/CL_Main/` - Main client logic hooks
- `Hooks/ClientPrediction/` - Client prediction system hooks
- `Hooks/GameMovement/` - Movement and player physics hooks
- `Hooks/BasePlayer/` - Base player class hooks
- `Hooks/WndProc/` - Window procedure hooks for input handling

*Rendering and Visual Hooks:*
- `Hooks/RenderView/` - View rendering setup hooks
- `Hooks/ModelRenderSystem/` - Model rendering system hooks
- `Hooks/EngineVGui/` - VGUI system hooks

*Engine Hooks:*
- `Hooks/EngineTrace/` - Ray tracing hooks for portal placement
- `Hooks/EngineServer/` - Server communication hooks
- `Hooks/TerrorGameRules/` - Game rule system hooks
- `Hooks/SequenceTransitioner/` - Animation sequence transition hooks

### SDK Interface System

Game interfaces are accessed via global `I::` namespace (defined in `SDK/SDK.h`). Interfaces are retrieved at runtime using:
- Pattern scanning (`Util/Pattern/Pattern.cpp`)
- Interface name lookup (`Util/Interface/Interface.cpp`)

Key interface locations:
- `SDK/L4D2/Interfaces/` - All game engine interfaces
- `SDK/L4D2/Entities/` - Client-side entity definitions

**Entity System:**
- `C_TerrorPlayer` - Player entity with health, position, team
- `C_TerrorWeapon` - Weapon entity with ammo, fire state
- `C_BaseEntity` - Base entity class (position, model, angles)
- `C_WeaponCSBase` - Base weapon class
- Entity iteration via `I::EntityList->GetHighestEntityIndex()`

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
- `CWeaponPortalgun` class in `Portal/client/` handles blue/orange portal creation
- `prop_portal` entity in `Portal/server/` handles server-side portal logic and network sync

### Namespace Conventions

| Namespace | Purpose | Examples |
|-----------|---------|----------|
| `G::` | Global singletons | `G::ModuleEntry`, `G::RenderView` |
| `I::` | Game interfaces | `I::EngineClient`, `I::ModelRender` |
| `U::` | Utilities | `U::Interface`, `U::Pattern` |
| `F::` | Features | `F::ESP`, `F::EnginePrediction` |
| `Hooks::` | Hook implementations | `Hooks::CL_Main`, `Hooks::RenderView` |
| `Vars::` | Configuration variables | Feature toggles and settings |

### Core Utility Systems

**DrawManager** (`SDK/DrawManager/`)
- Comprehensive 2D rendering system for all UI elements
- Multiple font types: DEBUG, ESP, ESP_NAME, ESP_WEAPON, MENU variants
- Drawing primitives: lines, rectangles, circles, triangles, gradients
- Text alignment with flags: LEFT, CENTERX, CENTERY, RIGHT, etc.
- Used extensively by ESP, menu, and debug visualizations

**GameUtil** (`SDK/GameUtil/`)
- World-to-screen coordinate conversion
- Movement fixing/prediction algorithms
- Team validation utilities
- Health-based color calculation
- Material creation helpers
- Ray tracing wrapper functions

**NetVarManager** (`Util/NetVarManager/`)
- Network variable management system
- Simplifies accessing networked entity properties
- Critical for ESP and features requiring entity data

**Math Library** (`SDK/Math/`)
- Complete math utilities for vectors (2D, 3D, 4D)
- Matrix operations and transformations
- Essential for aimbot calculations and portal physics

### Critical Patterns

**Pattern Scanning**: Memory addresses are found via byte pattern scanning (`Util/Pattern/Pattern.cpp`). Patterns must be updated when game updates change binary layouts.

**VMT Hooking**: Used extensively for class method interception. Original VMT is preserved for restoration.

**Render Target Management**: Portals require careful render target management to avoid texture corruption. Portal rendering happens in `ModelRender` hooks.

**Client-Side Only**: The mod operates entirely client-side. Server communication happens through existing game protocols (no custom network code).

### Feature Modules

| Feature | Description | Location |
|---------|-------------|----------|
| **Portal Gun** | Creates linked portals for teleportation | `Portal/` |
| **ESP** | Extra Sensory Perception - shows players through walls | `Features/ESP/` |
| **Aimbot** | Automated aiming with target selection | `Features/Aimbot/` |
| **Melee Aimbot** | Melee-specific aiming with FOV and weapon type checks | `Features/MeleeAimbot/` |
| **EnginePrediction** | Client-side prediction for smooth gameplay | `Features/EnginePrediction/` |
| **NoSpread** | Removes weapon spread randomness | `Features/NoSpread/` |
| **Bunny Hop** | Automatic jumping for faster movement | `Features/BunnyHop/` |
| **Fast Melee** | Speeds up melee attack animations | `Features/FastMelee/` |

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

Debug console is allocated at `Entry/Entry.cpp:286`. It automatically minimizes to keep the view clean. Use `ConsolePrint()` or standard `printf` for output.

**Test Functions** (currently commented out in `Entry/Entry.cpp`):
- `Func_TraceRay_Test()` - Ray tracing validation for portal placement
- `Func_Pistol_Fire_Test()` - Portal gun firing logic testing
- `Func_IPhysicsEnvironment_Test()` - Physics environment checks
- `Func_CServerTools_Test()` - Server tools testing

These can be uncommented for debugging specific systems during development.

## Recent Changes

### 2026-02-11: Aimbot and Melee Improvements
- Added melee aimbot with FOV filtering
- Added weapon type condition checks for aimbot
- General code refactoring

### 2026-01-26: Auto-Loading Implementation

**Problem**: Manual DLL injection using tools like Extreme Injector was not user-friendly.

**Solution**: Implemented an auto-loading launcher program.

**Attempts**:
1. **DLL Proxy Approach (tier0_s.dll)** - Failed
   - L4D2 uses `tier0.dll` not `tier0_s.dll`
   - Replacing `tier0.dll` caused game to not start
   - Too many exported functions to forward correctly
   - High risk of breaking game updates

2. **Launcher Program** - Success
   - Created standalone launcher application
   - Uses `CreateRemoteThread` for DLL injection
   - Automatically detects and launches game
   - Supports injecting into already-running instances

### File Name Changes

| Old Name | New Name | Reason |
|----------|----------|--------|
| `Lak3_l4d2_hack.dll` | `L4D2_Portal.dll` | Better naming consistency |
| `Launcher.exe` | `L4D2_Portal.exe` | Unified naming |

### Console Behavior

**Debug Build**:
- Console allocated but auto-minimized
- Use `ShowWindow(GetConsoleWindow(), SW_MINIMIZE)` at `Entry/Entry.cpp:291`
- Can be restored from taskbar for debugging

### Project Structure Changes

**New Files**:
- `Launcher/Launcher.cpp` - Launcher program source
- `Launcher/Launcher.vcxproj` - Launcher project file
- `docs/dll-auto-loading-implementation.md` - Technical documentation

**Modified Files**:
- `DllMain.cpp` - Simplified (removed proxy logic)
- `Entry/Entry.cpp` - Added console minimization
- `l4d2_base.vcxproj` - Changed output name to `L4D2_Portal`
- `l4d2_base.sln` - Added Launcher project

## Usage Instructions

### For End Users

1. Copy `L4D2_Portal.exe` and `L4D2_Portal.dll` to L4D2 game directory
2. Run `L4D2_Portal.exe`
3. Game will launch automatically
4. Enter any map to use portal features

### For Developers

1. Build the solution using MSBuild
2. Copy outputs from `src/Debug/` to game directory
3. Test by running launcher
4. Debug console is minimized but accessible from taskbar

## Known Issues

### Injection Timing

**Issue**: Injecting during gameplay may cause portal features to not initialize properly.

**Workaround**: Always inject at main menu. The launcher handles this automatically by waiting for full initialization.

### Game Updates

**Issue**: Game updates may break pattern scanning or offset values.

**Solution**: Update patterns in `Util/Pattern/Pattern.cpp` and offsets in `Util/Offsets/Offsets.cpp`.

## Technical Documentation

For detailed technical information about the auto-loading implementation, see:
- `docs/dll-auto-loading-implementation.md` - Complete technical documentation
- `docs/portal-development-summary.md` - Portal feature development
- `docs/portal-injection-timing-fix.md` - Injection timing fixes
