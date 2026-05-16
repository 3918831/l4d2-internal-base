# Reference Code/crave6948/src 技术报告

生成时间：2026-05-11

分析对象：`Reference Code/crave6948/src`

## 1. 核心结论

`Reference Code/crave6948/src` 并不是一套完整的《Portal 1》Source Engine 游戏源码树。它不包含典型 Source SDK / Portal 1 游戏 DLL 工程会出现的 `game/client`、`game/server`、`public`、`tier0`、`tier1`、`mathlib`、`vstdlib`、实体注册宏、游戏规则、武器脚本/实体脚本等完整结构。

实际内容是一套面向 Left 4 Dead 2 的内部 DLL 框架，带有 L4D2 SDK 接口封装、模式扫描、MinHook Hook 系统、模块化功能系统、2D 绘制、配置保存、Web UI 服务器，以及 Aimbot、NoSpread、BunnyHop、ESP、FastMelee、ThirdPerson 等功能模块。

对当前工程的参考价值主要在以下方面：

- 可以作为 L4D2 内部 DLL 框架参考：入口生命周期、接口获取、Hook 注册、模块分发、配置系统、实体缓存。
- 可以参考其 `CreateMove`、`FrameStageNotify`、`EngineVGui::Paint` 三条主回调链路。
- 可以参考其 aimbot/rotation/prediction/nospread 的时序组织方式。
- 不适合作为 Portal 1 门实体、传送、递归渲染、视锥裁剪、物理穿门等核心逻辑的直接参考，因为源码中几乎没有 Portal 专用实现。

## 2. 目录与规模

顶层目录：

- `Client/`：模块系统、配置系统、菜单/Web UI、旋转管理、引擎预测包装。
- `Entry/`：DLL 注入后的主初始化和卸载流程。
- `Hooks/`：Source Engine 关键接口/函数 Hook。
- `SDK/`：L4D2 entity/interface/include 封装、绘制工具、KeyValues、GameUtil。
- `Util/`：Hook、模式扫描、接口获取、偏移、数学、NetVar、JSON、字符串混淆。
- `DllMain.cpp`：DLL 主入口。
- `l4d2_base.sln` / `l4d2_base.vcxproj`：Visual Studio 工程。

文件规模：约 117 个 `.h`、49 个 `.cpp`、5 个 `.c`、1 个 `json.hpp`。这是一套注入 DLL 工程规模，而不是完整游戏源码规模。

## 3. 构建状态

解决方案配置：

- `l4d2_base.sln` 定义平台为 `Debug|x86`、`Release|x86`、`Debug|x64`、`Release|x64`。
- `l4d2_base.vcxproj` 内部项目平台仍使用 `Win32`/`x64`，解决方案将 `x86` 映射到项目的 `Win32`。
- 工具集为 `v143`，语言标准为 C++17。

我运行了：

```powershell
& 'D:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' 'Reference Code\crave6948\src\l4d2_base.sln' /p:Configuration=Debug /p:Platform=x86 /m
```

结果：当前源码不能直接完整编译。主要错误：

- `Client\ModuleManager\Module\Modules\Combat\Aimbot.cpp` 被工程引用，但实际文件在 `Client\ModuleManager\Module\Modules\Combat\Aimbot\Aimbot.cpp`。
- `Client\ModuleManager\Module\Utils\FindTarget.cpp` / `.h` 被工程引用，但实际缺失。
- `Client\None.cpp` 引用 `crow.h` 和 `crow/middlewares/cors.h`，但参考目录中没有 Crow 依赖。

另外还出现大量数值转换警告，例如 `float` 到 `int`、`double` 到 `float`，集中在 `Arraylist.h`、`ESPHelper.cpp`、`ModuleManager.cpp` 等 UI/绘制相关代码。

结论：该参考目录更像一个未整理干净的源码快照；如果要编译，需要先修工程路径、补齐 Crow，或移除 Web UI 相关代码。

## 4. DLL 生命周期

入口链路：

```text
DllMain.cpp
  -> DLL_PROCESS_ATTACH
  -> CreateThread(MainThread)
  -> G::ModuleEntry.Load()
  -> 循环等待 VK_END
  -> G::ModuleEntry.Unload()
  -> FreeLibraryAndExitThread()
```

`Entry/Entry.cpp` 中 `CGlobal_ModuleEntry::Load()` 的关键步骤：

1. 等待 `serverbrowser.dll` 加载，作为游戏初始化完成的粗略信号。
2. 执行 `U::Offsets.Init()`，通过模式扫描获取函数地址和全局指针地址。
3. 通过 `CreateInterface` 获取 L4D2/Source 接口：
   - `VClient016`
   - `VClientEntityList003`
   - `VClientPrediction001`
   - `GameMovement001`
   - `VEngineClient013`
   - `EngineTraceClient003`
   - `VEngineVGui001`
   - `VEngineRenderView013`
   - `VModelInfoClient004`
   - `VEngineModel016`
   - `VGUI_Panel009`
   - `VGUI_Surface031`
   - `VMaterialSystem080`
   - `VEngineCvar007`
4. 从模式扫描地址解引用得到：
   - `I::ClientMode`
   - `I::GlobalVars`
   - `I::MoveHelper`
   - `I::IInput`
5. 初始化 Client 子系统：模块、配置、菜单。
6. 初始化绘制系统。
7. 初始化 Hook。
8. 启动本地 Web 配置服务器。

卸载链路：

- `Client::client.shutdown()`：停止 Web 服务器并保存配置。
- `MH_Uninitialize()`：卸载 MinHook。
- `Hooks::WndProc::UnInitialize()`：恢复窗口过程。

注意：当前代码只在退出时调用 `MH_Uninitialize()`，没有显式逐个恢复 VMT/函数 Hook 状态，依赖 MinHook 全局卸载；如果后续工程复杂化，建议补更细的 Hook 释放流程。

## 5. Hook 系统

Hook 基础设施在 `Util/Hook/Hook.h`：

- `Hook::CFunction`：MinHook 函数 Hook，适用于模式扫描得到的裸函数地址。
- `Hook::CTable`：对接口 VTable 中某个 index 创建 MinHook。
- `Hook::CVMTable`：复制 VTable 并替换函数指针，属于 VMT shadow/copy 方式。

全局 Hook 初始化在 `Hooks/Hooks.cpp`：

```text
MH_Initialize()
  BaseClient::Init()
  BasePlayer::Init()
  CL_Main::Init()
  ClientMode::Init()
  ClientPrediction::Init()
  EngineVGui::Init()
  ModelRender::Init()
  ModelRenderSystem::Init()
  SequenceTransitioner::Init()
  TerrorGameRules::Init()
  TerrorPlayer::Init()
  WndProc::Init()
MH_EnableHook(MH_ALL_HOOKS)
```

### 5.1 ClientMode Hook

`Hooks/ClientMode/ClientMode.cpp` 是输入链路核心：

- Hook `CreateMove`。
- 拦截 `CUserCmd`。
- 获取本地玩家 `C_TerrorPlayer`。
- 调用 `Client::client.moduleManager.onCreateMove(cmd, pLocal)`。
- 调用 `I::Prediction->SetLocalViewAngles(cmd->viewangles)`。
- 返回 `false`，表示自行处理部分视角/输入结果。

这条链路承载：

- Aimbot 目标选择和视角调整。
- NoSpread 在预测阶段修正视角。
- BunnyHop 修改 `IN_JUMP`。
- FastMelee/AutoShoot 修改攻击键和 weaponselect。
- Movement fix。

### 5.2 BaseClient Hook

`Hooks/BaseClient/BaseClient.cpp` Hook 了：

- `LevelInitPreEntity`
- `LevelInitPostEntity`
- `LevelShutdown`
- `FrameStageNotify`

真正有行为的是 `FrameStageNotify`：

- 在 `FRAME_NET_UPDATE_END` 更新 `Utils::g_EntityCache`。
- 每帧调用模块系统的 `onFrameStageNotify(curStage)`。
- 再调用原始函数。

这个位置适合做实体缓存、第三人称状态同步、网络数据后处理等。

### 5.3 EngineVGui Hook

`Hooks/EngineVGui/EngineVGui.cpp` Hook `Paint`：

- 只在 `PAINT_UIPANELS` 阶段绘制。
- 每帧处理模块按键。
- 运行配置自动保存。
- 初始化屏幕宽高。
- 调用 `I::MatSystemSurface->StartDrawing()` / `FinishDrawing()`。
- 调用模块 `onRender2D()`。
- 绘制调试水印和菜单。

这是当前框架所有 2D Overlay / 菜单 / ESP 文本的主入口。

### 5.4 渲染相关 Hook

`ModelRender::DrawModelExecute`、`ModelRender::ForcedMaterialOverride`、`ModelRenderSystem::DrawModels` 当前都只是 passthrough，没有实际修改渲染。

这说明参考源码没有实现 chams、portal mask、递归渲染、render target 切换等逻辑。但 SDK 接口中已经声明了 `IMaterialSystem`、`IMatRenderContext`、`IVRenderView` 的 render target 与 3D view 能力，当前工程若做 Portal 渲染可以继续沿这些接口扩展。

### 5.5 其他 Hook

- `BasePlayer::CalcPlayerView`：临时清空 punch angle，调用原函数后恢复，实现视觉层面的 no recoil。
- `TerrorPlayer::AvoidPlayers`：直接 return，禁用玩家避让。
- `CL_Main::CL_Move`：目前只 passthrough，注释中可用于 tick manipulation。
- `ClientPrediction::{RunCommand,SetupMove,FinishMove}`：目前 passthrough。
- `SequenceTransitioner::CheckForSequenceChange`：passthrough。
- `TerrorGameRules::GetSurvivorSet`：passthrough。
- `WndProc`：替换 `Valve001` 窗口过程，但当前只转发原始过程。

## 6. SDK 与接口层

`SDK/L4D2/Interfaces` 提供了 Source/L4D2 接口声明，包括：

- `BaseClientDLL`
- `ClientEntityList`
- `EngineClient`
- `EngineTrace`
- `EngineVGui`
- `GameMovement`
- `MaterialSystem`
- `MatRenderContext`
- `MatSystemSurface`
- `ModelInfo`
- `ModelRender`
- `MoveHelper`
- `Prediction`
- `RenderView`
- `VGuiPanel`
- `VGuiSurface`
- `CVar`

`SDK/L4D2/Entities` 提供实体类封装：

- `C_BaseEntity`
- `C_BaseAnimating`
- `C_BasePlayer`
- `C_CSPlayer`
- `C_TerrorPlayer`
- `C_TerrorWeapon`
- `C_WeaponCSBase`
- `C_Infected`
- `C_DynamicProp`
- `IClientEntity` 系列接口

`SDK/L4D2/Includes` 提供基础结构：

- `client_class.h`
- `dt_recv.h`
- `usercmd.h`
- `view_shared.h`
- `globalvars_base.h`
- `const.h`
- `basehandle.h`
- `ehandle.h`
- `color.h`

整体风格是“够用式 SDK”：只声明当前功能需要的 vfunc、netvar、枚举和结构体，不追求完整 Source SDK 还原。

## 7. 模式扫描与偏移

`Util/Offsets/Offsets.cpp` 通过 byte pattern 定位：

- `SharedRandomFloat`
- `CheckForSequenceChange`
- `CalcPlayerView`
- `UpdateSpread`
- `DrawModels`
- `AvoidPlayers`
- `PhysicsRunThink`
- `SetPredictionRandomSeed`
- `GetSurvivorSet`
- `CL_Move`
- `ClientMode`
- `GlobalVars`
- `MoveHelper`
- `StartDrawing`
- `FinishDrawing`
- `IInput`
- `sv_cheats` 地址

`Util/Pattern/Pattern.cpp` 实现了十六进制字符串模式扫描，支持 `?` 通配符。

值得警惕的点：

- `FindPattern(dwAddress, dwLen, pattern)` 中循环写成 `dwCur < dwLen`，而调用传入的是 `BaseOfCode` 地址和 `SizeOfCode` 长度。按常规实现应为 `dwCur < dwAddress + dwLen`。如果该代码实际可用，可能依赖某些偶然条件或源码快照未同步。
- `m_dwSVCheat` 使用 `engine.dll + 0x6729A0` 硬编码，游戏更新后风险很高。
- 多数偏移对 L4D2 版本高度敏感，作为参考只能借鉴定位方式，不能盲目复用 pattern。

## 8. Client 模块系统

核心对象为 `Client::client`，定义在 `Client/None.h`：

```text
Client::None
  moduleManager
  fileManager
  menu
  initialize()
  shutdown()
  setupRoutes()
  startServer()
```

`Client::None::initialize()`：

- `moduleManager.Init()`
- `fileManager.init()`
- `menu.init()`

`ModuleManager` 持有所有模块实例：

Combat：

- `Aimbot`
- `NoSpread`
- `AutoShoot`
- `FastMelee`

Player：

- `BunnyHop`

Visuals：

- `Arraylist`
- `ESPHelper`
- `ClickGui`
- `ThirdPerson`
- `Rotations`

Misc：

- `FontManager`

模块基类 `ModuleHeader.h` 提供统一回调：

- `onPreCreateMove`
- `onPostCreateMove`
- `onPrePrediction`
- `onPrediction`
- `onPostPrediction`
- `onRender2D`
- `onFrameStageNotify`
- `onEnabled`
- `onDisabled`

这套事件分发很适合当前工程借鉴：Portal gun/ESP/aim/prediction/visual debug 可以都挂在统一生命周期里，而不是散落在 Hook detour 内。

## 9. CreateMove 与 Prediction 时序

`ModuleManager::onCreateMove` 的组织方式：

1. 获取武器。
2. 保存旧视角 `oldViewangles`。
3. 所有启用模块执行 `onPreCreateMove`。
4. `rotationManager.onUpdate()`。
5. 如果旋转管理器有 server rotation，则写入 `cmd->viewangles`。
6. 所有启用模块执行 `onPostCreateMove`。
7. 对武器存在的情况：
   - `onPrePrediction`
   - `F::EnginePrediction.Start(pLocal, cmd)`
   - `onPrediction`
   - `F::EnginePrediction.Finish(pLocal, cmd)`
8. `onPostPrediction`
9. `G::Util.FixMovement(oldViewangles, cmd)`

这一点非常有价值：它把“输入修改”、“目标/旋转计算”、“预测环境”、“运动修正”拆成了可维护的阶段。当前 Portal 工程中如果要继续扩展 portal placement assist、trajectory preview、movement compensation，也可以采用类似阶段化结构。

## 10. 功能模块分析

### 10.1 Aimbot

`Aimbot` 在 `onPreCreateMove` 执行主逻辑：

- 左键按下且满足 `ShouldRun` 才运行。
- 用 `switchDelay` 控制目标切换。
- 目标无效/死亡/超距/出 FOV/不可见时重新选择。
- 通过 hitbox 计算目标角度。
- 调用 `rotationManager.moveTo()` 平滑/拟人化移动到目标角。
- 如果开火时未对准，则取消 `IN_ATTACK`。
- 对近战武器使用单独 FOV 触发限制。

目标来源是 `Utils::g_EntityCache`，目标类型覆盖普通感染者、特感、Witch、Tank。

可借鉴点：

- 目标缓存与过滤拆分明确。
- 可见性验证使用 `EngineTrace`。
- 目标评分支持 FOV、Distance、组合模式。
- aim rotation 不直接等同 client view angle，而由 RotationManager 控制 server-side command angle。

风险点：

- 部分拼写错误如 `isInvaildOrDead` 不影响功能但影响维护。
- `foundTarget == nullptr` 时直接先赋值再返回，没有立刻计算 score，可能让第一个目标天然占优，后续才比较。
- 对 `pLocal`、`pWeapon` 的空指针保护不完全一致。

### 10.2 RotationManager

`RotationManager` 把当前 server rotation、目标 rotation、保持 tick、重置逻辑集中管理。

支持三种模式：

- Linear：水平/垂直速度区间随机。
- Conditional：按距离、角度差、是否已在准星中计算速度。
- Sigmoid：用 sigmoid 将角度差映射到速度。

可借鉴点：

- 把“算目标角”和“如何转过去”分开。
- `ForceBack()` 让模块停止接管后平滑回到玩家视角。
- `DisabledRotation` 表示当前没有服务端静默角接管。

### 10.3 NoSpread

`NoSpread` 在 prediction 阶段执行：

- 只对指定枪械运行，不处理霰弹枪/近战等。
- 调用 `pWeapon->UpdateSpread()` 得到当前 spread。
- 使用 `SharedRandomFloat` 的同名随机种子还原 L4D2 `CTerrorGun::FireBullet` 的横向/纵向扩散。
- 从 `cmd->viewangles` 中抵消扩散。
- 可选减去 punch angle 实现 recoil 修正。
- 恢复原始 `GetCurrentSpread()`。

这对当前工程的 NoSpread/Portal gun 准星预测有参考意义：如果 Portal gun fire hook 需要与游戏随机数或 weapon spread 兼容，必须在 prediction 时序里处理。

### 10.4 AutoShoot

`AutoShoot` 在 `onPostCreateMove` 中控制半自动武器点击：

- 过滤使用键、挂边、舌头控制、换弹、无武器等状态。
- 对 pistol、deagle、sniper、shotgun 类武器生效。
- 根据 `CanPrimaryAttack(-0.2)` 判断是否允许开火。
- 支持维持若干 tick 的点击状态。
- 可自动右键 shove/punch，且可限制 sniper/shotgun。

### 10.5 FastMelee

`FastMelee` 通过 weaponselect 切换绕过近战间隔：

- 检查本地玩家状态、近战武器、攻击可用性。
- 阶段 1：切到非近战 slot。
- 阶段 2：切回 slot 1。
- 通过 `waitingTicks` 控制间隔。

这类逻辑高度依赖 L4D2 武器槽位和服务器规则，不适合直接混入 Portal gun，但其状态机写法可借鉴。

### 10.6 BunnyHop

`BunnyHop` 当前只有 Normal 模式：

- `onPrePrediction` 保存旧按钮并清掉 `IN_JUMP`。
- `onPrediction` 读取预测后的地面状态。
- `onPostPrediction` 根据真实/预测落地状态决定是否恢复或压制跳跃。
- 随机延迟 10 到 14 tick，降低机械感。

### 10.7 ESPHelper

`ESPHelper`：

- 从 `EntityCache` 获取不同感染者类型列表。
- 取目标 hitbox world position。
- `WorldToScreen` 后绘制小角标。
- 用颜色标出 aimbot 目标和 aimbot 范围内目标。
- 对 Witch/Tank/特感绘制文字。

可借鉴点：

- ESP 与 Aimbot 共享 EntityCache。
- 视觉层可以复用 combat 模块状态，例如 targetInfo。

### 10.8 ThirdPerson

`ThirdPerson`：

- 在 `FrameStageNotify` 中监听 `V` 键切换第三人称。
- 修改 `IInput->m_fCameraInThirdPerson()`。
- 设置 `cam_idealdist`、`cam_collision`、`c_thirdpersonshoulder*` 等 cvar。
- 使用 `I::Prediction->SetLocalViewAngles(rotation)` 同步第三人称角度。

当前 Portal 工程如果需要观察 portal placement、传送调试、玩家相机行为，这部分有直接参考价值。

## 11. EntityCache

`Utils::EntityCache::update()` 在 `FRAME_NET_UPDATE_END` 执行：

- 清空不同 class id 的缓存。
- 遍历 `ClientEntityList`。
- 排除本地玩家。
- 按 `ClientClass::m_ClassID` 分类。
- 对 Witch/Infected 使用 `IsInfectedAlive(m_usSolidFlags, m_nSequence)` 判断存活。
- 对 Tank/特感使用 `deadflag()` 判断存活。

缓存类型：

- `Infected`
- `Boomer`
- `Jockey`
- `Smoker`
- `Hunter`
- `Spitter`
- `Charger`
- `Witch`
- `Tank`

这是当前参考源码最值得直接迁移/对照的结构之一。当前工程已有 ESP/Aimbot/MeleeAimbot 时，统一 EntityCache 可以减少每个功能自己遍历实体的重复成本。

## 12. 配置与 Web UI

配置系统由 `ValueManager`、各种 `Value` 类型、`FileManager` 组成。

支持值类型：

- boolean
- list
- number
- float
- floatRange
- color
- string

`FileManager`：

- 从当前工作目录的 `settings.json` 加载。
- 按模块分类保存。
- 支持 `/getjson` 与 `/loadjson` HTTP API。
- 每隔 `auto_save_interval` 自动保存。

`Client/None.cpp` 使用 Crow 启动本地 HTTP 服务：

- 端口：`18080`
- `/` 返回 `index.html`
- `/getjson` 返回配置 JSON
- `/loadjson` 接收配置 JSON
- `/assets/<path>` 提供静态资源

风险点：

- Crow 依赖没有随源码提供。
- 服务线程 `detach()`，卸载时只 `app.stop()`，没有 join；DLL 卸载时可能存在生命周期风险。
- 静态资源路径只检查 `..`，但未做更严格的 canonical path 校验。
- 注入到游戏进程内开 HTTP server，会引入额外网络/线程/异常面，对稳定性不利。

当前工程如果不需要外部 Web 配置界面，建议只借鉴 JSON 配置模型，不直接引入 Crow server。

## 13. 与当前 L4D2 Portal 工程的关系

当前主工程 `src/` 已经有：

- `Portal/`
- `Portal/client/weapon_portalgun.*`
- `Portal/server/prop_portal.*`
- `Portal/CustomRender.h`
- `Hooks/C_Weapon/Weapon_Pistol.*`
- `Hooks/RenderView/`
- `Hooks/EngineTrace/`
- `Launcher/`
- `Features/ESP/`
- `Features/Aimbot/`
- `Features/MeleeAimbot/`
- `Features/EnginePrediction/`
- `Features/NoSpread/`
- `Features/BunnyHop/`
- `Features/FastMelee/`

参考源码没有这些 Portal 专用目录，说明当前工程比参考源码在 Portal 方向上已经走得更远。参考源码更适合作为“旧框架/功能模块体系”的比较对象，而不是 Portal 机制蓝本。

对当前工程的具体参考建议：

1. 对照 `ModuleManager::onCreateMove` 的阶段化分发，检查当前工程输入、预测、运动修正是否也有清晰阶段。
2. 对照 `EntityCache`，确认当前 ESP/Aimbot/MeleeAimbot 是否可以共享实体缓存，避免多次遍历 `ClientEntityList`。
3. 对照 `RotationManager`，评估当前 Aimbot/MeleeAimbot 是否需要统一 server rotation 管理，尤其是 silent aim 和第三人称同步。
4. 对照 `FileManager`，如果当前工程配置项变多，可以引入统一 Value/JSON 模型；但不建议直接引入 Crow。
5. 对照 `NoSpread`，检查当前 `SharedRandomFloat`、`UpdateSpread`、`PunchAngle` 的恢复逻辑是否完整。
6. 对照 `BasePlayer::CalcPlayerView`，当前工程如果有视觉 no recoil，应保留“调用原函数前清空，调用后恢复”的最小侵入模式。
7. 对照 `EngineVGui::Paint`，确认所有 overlay 绘制都包在 `StartDrawing/FinishDrawing` 内，并限制到合适 paint mode。

## 14. 主要问题清单

### P1：源码身份与预期不一致

该目录不是完整 Portal 1 游戏 DLL 源码。报告对象中没有 Portal 1 核心机制实现，例如：

- `CProp_Portal`
- `CWeaponPortalgun`
- portal placement rules
- linked portal teleport
- recursive portal rendering
- stencil portal mask
- portal plane / clip plane
- physics object through-portal transform
- server/client entity networking

### P1：当前快照不能直接编译

至少存在三类构建阻塞：

- 工程中 Aimbot 源文件路径错误。
- `FindTarget.cpp/.h` 缺失。
- Crow 依赖缺失。

### P2：Pattern 扫描实现疑似边界错误

`FindPattern` 循环上界疑似把长度当成绝对地址。建议修为 `dwAddress + dwLen` 风格，并加单元/运行时验证。

### P2：Hook 初始化断言语义可读性差

代码中多处：

```cpp
XASSERT(Table.Init(...) == false);
```

如果 `XASSERT` 是“条件为真则报警/断言”，这种写法是在检查失败；如果 `XASSERT` 是“条件为假则报警”，那就反了。需要确认 `XASSERT` 定义。当前写法可读性不佳，容易误判初始化状态。

### P2：Web server 生命周期风险

注入 DLL 内部启动 detach 线程，卸载时没有 join，有潜在野线程/静态对象析构顺序风险。

### P2：路径/依赖管理不完整

工程引用了缺失文件和外部头文件，但没有 dependency 说明、include path 或 vendor 目录。

### P3：代码质量问题

- 大量数值转换警告。
- 命名拼写错误。
- 部分空指针检查不一致。
- 部分 hook 只是 passthrough，可能是占位代码。
- `WndProc` 使用 `SetWindowLongW` 而不是 `SetWindowLongPtrW`，在 32 位下可用，但 x64 配置中风险更高。

## 15. 可迁移资产优先级

高价值：

- `EntityCache`
- `ModuleManager` 生命周期分发
- `RotationManager`
- `EnginePrediction` 调用时序
- `NoSpread` 的 spread/punch 修正思路
- `EngineVGui::Paint` overlay 绘制结构
- `FileManager` 的 JSON schema 思路

中价值：

- `ThirdPerson`
- `AutoShoot`
- `FastMelee`
- `BunnyHop`
- `DrawManager`
- `GameUtil`
- `NetVarManager`

低价值或不建议直接迁移：

- Crow Web server
- 旧工程文件
- 硬编码 `sv_cheats` 地址
- 当前 `Pattern::FindPattern` 实现
- 只 passthrough 的 hook 文件

## 16. 建议的后续行动

如果目标是继续提升当前 L4D2 Portal 工程，建议按下面顺序参考：

1. 建立“当前工程 vs crave6948”差异表，列出当前已有、参考有、双方不同的模块。
2. 优先抽象统一 EntityCache，让 ESP/Aimbot/MeleeAimbot/Portal placement debug 共用实体遍历。
3. 检查当前 `CreateMove` 时序是否和“pre create move -> rotation -> post create move -> prediction -> movement fix”一致。
4. 检查当前 Pattern/Offsets 是否已修复参考源码里的扫描边界和硬编码问题。
5. 若要引入配置系统，迁移 Value/JSON 思路，不迁移 Crow server。
6. Portal 专用功能仍应以当前 `src/Portal` 和真实 Portal/Source SDK 资料为主，不能依赖该参考目录。

## 17. 一句话判断

这份 `Reference Code/crave6948/src` 的真正身份是“L4D2 内部 DLL 功能框架源码快照”，而不是“Portal 1 完整游戏核心 DLL 源码”。它对当前工程最有价值的是 Hook/模块/输入预测/实体缓存这些工程组织经验；对 Portal 核心技术，如门实体、传送数学、递归渲染和跨门物理，参考价值很有限。
