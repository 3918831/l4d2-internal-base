# L4D2 Portal Traversal Runtime Exploration

Date: 2026-06-09

## 阶段性目标

本文记录当前 `l4d2-internal-base` 中复刻 Portal 玩家穿越逻辑的运行时探索成果。它不是 Portal SDK 2013 官方源码研究的替代品，而是面向当前 L4D2 注入式 DLL 工程的实测技术资产。

当前阶段目标是：

- 在 L4D2 单人线下 localserver 中，让玩家可以像 Portal 官方实现一样在门洞处部分嵌入、停留、继续向前移动。
- 不使用 `MOVETYPE_NOCLIP` 或临时 noclip 类方案作为主干体验。
- 保持 Portal 官方主思路：在传送门洞区域改写玩家 hull 碰撞/移动结果，而不是把玩家简单地卡点瞬移。
- 优先只处理本地玩家穿越；其他实体、物理物件、感染者后续扩展。
- 当前视觉部分已有基础，本文重点记录穿越运动、碰撞豁免、运行时 hook 资产。

## 背景

Portal 官方不是在玩家碰到门面后简单 `Teleport`。官方逻辑大致由三部分配合：

1. `prop_portal` 管理门的激活、链接、矩阵、touch、最终传送。
2. `PortalSimulation` 在门洞附近生成特殊碰撞环境，使实体可以进入 portal hole。
3. `portal_gamemovement` 改写玩家 movement trace、ground check、position test，使玩家 hull 可以嵌入门洞并在穿过 portal plane 后提交传送。

当前 L4D2 工程是注入式 DLL，没有 Portal 的完整游戏 DLL 源码所有权，只能通过 interface、pattern scan、VMT hook、函数签名、调试器单步来反推运行时路径。因此，本阶段的关键任务不是直接写完整传送，而是先定位：

- 玩家为什么地面走向门会像普通墙一样停住。
- 需要 hook 哪些 L4D2 movement 函数。
- 哪些函数已被证实可用，哪些还缺签名或类布局。
- 哪些现象来自地面态，哪些来自空中态。

相关上游研究文档：

- `docs/portal-traversal-logic-study.md`
- `docs/portal-sdk-2013-technical-report.md`
- `docs/feature/portal-ground-step-movement-next-plan.md`
- `docs/feature/portal-perfect-replica-implementation-plan.md`

## 当前实现涉及的主要文件

- `src/Hooks/GameMovement/CCSGameMovement.cpp`
- `src/Hooks/GameMovement/CCSGameMovement.h`
- `src/Hooks/ClientPrediction/ClientPrediction.cpp`
- `src/Hooks/ClientMode/ClientMode.cpp`
- `src/Portal/PortalCollisionBridge.cpp`
- `src/Portal/PortalCollisionBridge.h`
- `src/Portal/PortalStage1Probe.cpp`
- `src/Portal/PortalStage1Probe.h`
- `src/Portal/PortalTransitionSimulator.cpp`
- `src/Portal/PortalTransitionSimulator.h`
- `src/Portal/PortalTransition.cpp`
- `src/Portal/L4D2_Portal.h`
- `src/Util/Offsets/Offsets.cpp`
- `src/Util/Offsets/Offsets.h`
- `src/Util/Logger/PortalFileLog.h`

## 已做工作总览

### 1. 取消旧的 noclip / MoveType 方向

早期测试表明，通过修改玩家 MoveType 或临时 noclip 让玩家穿过墙面，会造成体验退化：

- 玩家不是顺滑进入门洞，而是按住移动一段时间后突然进入。
- 地面移动和空中移动表现不一致。
- 视觉连续性和 Portal 官方体验偏离较大。
- 与 Portal 官方源码主思路不一致。

因此当前主线明确放弃 MoveType/noclip 作为主干方案。传送瞬间仍然使用 `Teleport(vtable[118])`，但嵌入门洞阶段必须由 movement trace / movement result 处理。

### 2. 建立 PortalStage1Probe 诊断

新增或扩展了 `PortalStage1Probe`，用于收集每帧关键状态：

- 接口是否可用。
- 蓝门/橙门是否 active、open、ready。
- 玩家 origin、eye、center、feet、velocity。
- 玩家 hull mins/maxs。
- 与蓝门/橙门平面的 signed distance。
- 是否在门洞 aperture 范围内。
- `PortalTransitionSimulator` 当前 phase、entry side、exit side、depth。
- `PortalCollisionBridge` 当前 trace 统计和最后一次决策。

这一步确认：

- 传送门放置和 pair ready 正常。
- 玩家靠近蓝门后，状态能进入 `ApproachingPortal` 和 `IntersectingPortal`。
- 视觉实体和传送门位置/normal 信息可用于运动逻辑。

### 3. 建立本地文件日志

由于控制台日志过多，新增本地文件日志收集机制，当前重点日志会写到：

```text
D:\portal_l4d2_trace.log
```

后续分析统一使用过滤读取，例如 `Select-String -Pattern "WalkMoveNudge|TracePost|StageProbe"`，不要整文件硬读。

### 4. 实现 PortalCollisionBridge

新增 `PortalCollisionBridge`，负责在 `TracePlayerBBox` hook 中判断玩家 hull trace 是否可以在门洞区域被豁免。

当前 trace 分类：

- `HorizontalMove`
- `ZeroLengthPositionTest`
- `VerticalGroundProbe`
- `StepUpDownProbe`
- `Other`

已实现的统计资产：

- 总请求数、eligible 数、accepted 数。
- rejected by phase / pair / aperture。
- horizontal accepted。
- zero-length accepted。
- startsolid accepted。
- vertical / ground-like rejected。
- 每帧最后一次 horizontal trace。
- 每帧最后一次 step trace。
- 每帧最后一次 other trace。

核心结论：

- 单纯让 `TracePlayerBBox` 在门洞区域 `fraction=1` 不足以让地面玩家顺滑进入。
- 但它是必要条件。没有它，玩家 hull 根本不会被允许进入门洞附近。

### 5. 对比地面走入和跳跃进入

用户实测发现：

- 地面持续走向门：被挡住，像普通墙。
- 跳跃进入门：可以嵌入甚至越过门面，卡顿较少。

这成为关键线索。说明问题不是 portal aperture 几何完全错误，也不是视觉门/实体位置完全错误，而是地面 movement 路径中有额外逻辑把玩家夹回墙面。

从日志看：

- 空中进入时 ground 相关逻辑弱化或不参与。
- 地面进入时 `WalkMove` / ground adjustment / step movement 会重新处理落点和速度。

### 6. 探索 CGameMovement hook

通过用户提供的 server 端 CGameMovement 布局、Portal SDK 对照、函数字节、VMT index、签名扫描、VS 单步调试，逐步确认了多个关键入口。

已确认：

- `server TracePlayerBBox` 签名 hook 有效。
- `client TracePlayerBBox[16]` 有效。
- `server CategorizePosition(void)` 签名和 `vtable[52]` 匹配。
- `server PlayerMove[18]` 有效。
- `server WalkMove[28]` 有效。
- `server FullWalkMove[30]` 有效。
- `server TryPlayerMove[38]` 可 hook，但当前不是地面卡门的主要阻塞点。
- `server StepMove[64]` 可 hook，但当前不是主阻塞点。
- `server StayOnGround` 可通过 return address 附近 prologue 找到并 hook。
- `client PlayerMove[18]` 有效。
- `client WalkMove[28]` 有效。
- `client FullWalkMove[30]` 有效。

被排除或暂不使用：

- `server TracePlayerBBox` 的 vtable `[14]` 方案曾导致 ESP/参数错乱，回退到签名 hook。
- `client TryPlayerMove[38]` 未验证，不作为当前依赖。
- `TryPlayerMove[40]` 使用 `TryPlayerMove` 签名会崩溃，判定不是该函数或签名不匹配。
- `StepMove[66]` 尝试方向无效或不稳定。
- `EngineTrace::TraceRay` hook 未观察到目标 movement 路径命中，不是当前主路线。

### 7. 验证 StayOnGround

通过 ground probe return address：

- `server.dll + 0x000F661B`
- `server.dll + 0x000F665C`

反向扫描附近 prologue，找到：

```text
server.dll + 0x000F6590
```

该函数 hook 后出现大量：

```text
[PortalBridge][StayOnGround][Enter]
[PortalBridge][StayOnGround][Exit]
```

随后测试在 portal bridge phase 中跳过 `StayOnGround()`：

```text
[PortalBridge][StayOnGround] skipped during portal bridge ...
```

结果：

- skip 生效。
- 但地面走向门仍然被阻挡。

结论：

- `StayOnGround` 参与地面态修正，但不是唯一或最终阻塞点。
- 仅跳过 `StayOnGround` 不足以让玩家进入门洞。

### 8. 验证 WalkMove 出口修正

在 `WalkMove` 原函数返回后，新增实验性 `TryApplyPortalWalkMoveNudge`：

触发条件：

- 当前处于 `PortalTransitionPhase::IntersectingPortal`。
- `context.insideAperture == true`。
- `context.movingIntoPortal == true`。
- `context.entrySide != None`。
- 本帧有 accepted horizontal trace。
- 当前 origin 到入口门平面的距离在合理范围内。

实验动作：

- 将 `CMoveData::m_vecAbsOrigin` 沿入口门 normal 的反方向小幅推进。
- 恢复门法线方向速度，避免 `WalkMove` 把该方向速度归零。
- server/client `WalkMove` 都做同类诊断修正。

实测结果：

- 地面持续走向蓝门时，玩家确实可以进入门洞。
- 日志出现 8 次 `[PortalBridge][WalkMoveNudge]`。
- 玩家最终卡在墙体模型里。

关键日志摘录：

```text
cmd=495 origin=(-304.4 -1505.0 -120.0)->(-304.4 -1511.0 -120.0)
cmd=496 origin=(-304.4 -1511.0 -120.0)->(-304.4 -1517.0 -120.0)
cmd=512 origin=(-304.4 -1517.0 -120.0)->(-304.4 -1520.0 -120.0)
cmd=513 origin=(-304.4 -1520.0 -120.0)->(-304.4 -1523.0 -120.0)
```

这一组日志是当前最重要的阶段性证据。

## 推理分析过程

### 假设 1：仅 TracePlayerBBox 豁免即可进入门洞

验证方式：

- hook `TracePlayerBBox`。
- 在门洞 aperture 范围内将 horizontal trace 改为未命中。
- 扩展 zero-length position test / step probe 的处理。

结果：

- 日志显示 horizontal trace 和 step probe 可以被 accepted。
- 玩家地面走入仍然停在门面附近。

结论：

- 该假设不完整。
- `TracePlayerBBox` 豁免是必要但不充分条件。

### 假设 2：StayOnGround 把玩家吸回地面或墙面

验证方式：

- 找到并 hook `StayOnGround`。
- 在 portal bridge phase 中跳过原函数。

结果：

- skip 日志出现。
- 地面走入仍然失败。

结论：

- `StayOnGround` 是地面路径的一部分，但不是主阻塞点。

### 假设 3：WalkMove 原函数返回后，最终 origin/velocity 被夹回墙面

验证方式：

- hook `WalkMove`。
- 原函数返回后读取 `CMoveData`。
- 在 `IntersectingPortal` 且 horizontal trace accepted 时，主动修正 origin/velocity。

结果：

- 玩家行为出现明确差异：地面可以进入门洞。
- 日志显示 origin 连续跨过 portal plane。
- 速度从被归零恢复为沿门 normal 方向进入。

结论：

- 该假设成立。
- 地面走不进门的当前主阻塞点在 `WalkMove` / `FullWalkMove` movement result 合成阶段。

## 现阶段进展

### 已经做到

- 门洞处的玩家 hull trace 可以被 portal-aware 逻辑豁免。
- 地面走向门时，可以通过 `WalkMove` 出口修正确认玩家能够进入门洞。
- server/client movement hook 的关键入口已经大量确认。
- 已建立可复用日志收集路径。
- 已确认不能继续走 noclip/MoveType 旧路线。
- 已确认下一阶段不应继续泛化 TraceRay，而应做穿越提交。

### 当前还没做到

- 玩家穿过入口门平面后，还没有触发正式传送提交。
- 日志中没有出现：

```text
Teleport: 0
CommittingTeleport: 0
```

- 玩家进入门洞后会卡进门后的原始墙体/model。
- `WalkMoveNudge` 是行为验证代码，不是最终设计。
- 还没有实现入口到出口的 origin / angles / velocity 变换。
- 还没有处理出口门附近的 collision bridge / cooldown。
- 还没有完整处理 client prediction 与 server authoritative 之间的视觉连续性。

## 下一步计划

### 目标 1：正式实现穿越提交

当玩家从入口门正面进入，并且 center/origin 已跨过 portal plane 时，触发一次正式 teleport。

建议触发条件：

- `PortalTransitionPhase::IntersectingPortal`。
- `entrySide != None`。
- `exitSide != None`。
- `insideAperture == true`。
- `signedDepth` 从正值变为 `<= 0`，或当前 depth 已低于一个小阈值。
- 本帧或最近一帧 horizontal trace accepted。

执行内容：

- 根据 entry/exit portal 构建 transform。
- 计算目标 origin。
- 计算目标 angles。
- 计算目标 velocity。
- 调用玩家实体 `Teleport(vtable[118])`。
- 切换 phase 到 `CommittingTeleport` / `ExitingPortal` / `Cooldown`。
- 设置短暂防重复触发窗口。

### 目标 2：用正式 movement result 合成替换 WalkMoveNudge

当前 `WalkMoveNudge` 证明方向正确，但它是硬推进：

- 固定推进距离。
- 固定恢复 normal velocity。
- 不根据真实 intended movement delta 计算。

下一步应改为：

- 读取 `TracePlayerBBox` accepted horizontal trace 的 intended end。
- 按 official portal-aware movement 思路合成最终 movement result。
- 只移除入口门洞墙面的阻挡，不无条件推玩家。
- 保留 side movement、垂直速度、重力。
- 保证在未 crossing 时可以自然停留在门洞内。

### 目标 3：处理出口侧连续性

Teleport 后需要：

- 将玩家放在出口门前合适位置。
- 避免立即被出口门后的墙体或模型卡住。
- 在短时间内对出口侧做 collision bridge 或 phase cooldown。
- 保证 camera/viewangles 与 velocity 连续。

### 目标 4：清理诊断日志

目前日志量很大。进入下一阶段前建议：

- 保留 `WalkMoveNudge` / teleport commit / crossing 相关日志。
- 降低 `StageProbe` 和 `TracePost` 常规输出频率。
- 将 function probe / vtable scan 诊断改为只在 debug cvar 或显式命令下打印。
- 保留本地文件日志能力。

## 关键信息资产

### 已确认接口和全局对象

#### GameMovement interface

当前工程中：

```cpp
namespace I
{
    inline IGameMovement* GameMovement = nullptr;       // client side
    inline IGameMovement* ServerGameMovement = nullptr; // server side
}
```

`CGameMovement` 实例对象目前可从 interface 获取。server/client 两侧对象都已用于 VMT hook。

#### CMoveData 位置

当前使用的运行时推断：

```cpp
// x86: vptr, player, mv
CMoveData* mv = *reinterpret_cast<CMoveData**>(
    reinterpret_cast<uintptr_t>(gameMovement) + sizeof(void*) * 2u);
```

即 `CGameMovement` 对象布局中，`mv` 位于对象起始地址 `+8`。

这个布局已在大量 movement 日志中验证可用。

#### CMoveData 当前可用字段

来自 `src/SDK/L4D2/Interfaces/GameMovement.h`：

```cpp
class CMoveData
{
public:
    bool            m_bFirstRunOfFunctions : 1;
    bool            m_bGameCodeMovedPlayer : 1;
    unsigned long   m_nPlayerHandle;
    int             m_nImpulseCommand;
    QAngle          m_vecViewAngles;
    QAngle          m_vecAbsViewAngles;
    int             m_nButtons;
    int             m_nOldButtons;
    float           m_flForwardMove;
    float           m_flSideMove;
    float           m_flUpMove;
    float           m_flMaxSpeed;
    float           m_flClientMaxSpeed;
    Vector          m_vecVelocity;
    QAngle          m_vecAngles;
    QAngle          m_vecOldAngles;
    float           m_outStepHeight;
    Vector          m_outWishVel;
    Vector          m_outJumpVel;
    Vector          m_vecConstraintCenter;
    float           m_flConstraintRadius;
    float           m_flConstraintWidth;
    float           m_flConstraintSpeedFactor;
    bool            m_bConstraintPastRadius;
    Vector          m_vecAbsOrigin;

    void SetAbsOrigin(const Vector& vec) { m_vecAbsOrigin = vec; }
    const Vector& GetAbsOrigin() const { return m_vecAbsOrigin; }
};
```

重要字段：

- `m_vecAbsOrigin`
- `m_vecVelocity`
- `m_nButtons`
- `m_flForwardMove`
- `m_flSideMove`
- `m_outStepHeight`
- `m_bGameCodeMovedPlayer`

### Portal runtime state

#### PortalInfo_t

来自 `src/Portal/L4D2_Portal.h`：

```cpp
struct PortalInfo_t
{
    bool bIsActive;
    Vector origin;
    QAngle angles;
    Vector normal;
    CProp_Portal* pPortalEntity;
    Vector lastOrigin;
    float currentScale;
    bool isAnimating;
    EPortalAnimState animState;
    EScaleAnimationType animType;
    float animDuration;
    float animStartTime;
    bool bIsClosing;
    float closeAnimDuration;
    float closeAnimStartTime;
    float lastTime;
};
```

关键字段：

- `origin`
- `angles`
- `normal`
- `bIsActive`
- `animState`
- `pPortalEntity`

当前全局入口：

```cpp
G::G_L4D2Portal.g_BluePortal
G::G_L4D2Portal.g_OrangePortal
```

#### PortalTransitionContext

来自 `src/Portal/PortalTransitionSimulator.h`：

```cpp
struct PortalTransitionContext
{
    PortalTransitionPhase phase;
    PortalTransitionSide entrySide;
    PortalTransitionSide exitSide;
    float enterTime;
    float lastUpdateTime;
    float signedDepth;
    bool insideAperture;
    bool movingIntoPortal;
    bool hasValidExitPlacement;
};
```

已使用字段：

- `phase`
- `entrySide`
- `exitSide`
- `signedDepth`
- `insideAperture`
- `movingIntoPortal`

关键 phase：

- `Idle`
- `ApproachingPortal`
- `IntersectingPortal`
- `CommittingTeleport`
- `ExitingPortal`
- `Cooldown`

### 已确认函数 hook 资产

#### server TracePlayerBBox

当前推荐方式：签名 hook。

函数签名：

```cpp
void __fastcall Detour(
    void* ecx,
    void* edx,
    const Vector& start,
    const Vector& end,
    unsigned int fMask,
    int collisionGroup,
    trace_t* pm);
```

pattern：

```text
53 8B DC 83 EC 08 83 E4 F0 83 C4 04 55 8B 6B ? 89 6C 24 ? 8B EC 83 EC 6C 56 8B F1
```

结论：

- 签名 hook 稳定。
- 通过 vtable `[14]` 调 server TracePlayerBBox 曾导致 ESP/参数错乱，不再使用。

#### client TracePlayerBBox

当前方式：client `CGameMovement` VMT hook。

index：

```text
client TracePlayerBBox[16]
```

结论：

- `[16]` 已被实测确认有效。
- 这与用户提供的 server Linux/接口布局中的 `[14]` 存在 `+2` 偏移现象。

#### server CategorizePosition

当前推荐方式：签名 hook，同时确认 VMT `[52]` 匹配。

函数签名：

```cpp
void __fastcall Detour(void* ecx, void* edx);
```

Windows pattern：

```text
55 8B EC 51 56 57 8B F9 8B B7 E0 07 00 00 85 F6 0F 84 ? ? ? ? F3 0F 10 86 54 28 00 00 0F 57 D2 0F 2F C2 0F 86 ? ? ? ? A1 ? ? ? ? F3 0F 10 58 10 F3 0F 59 1D ? ? ? ? 0F 28 C8 F3
```

确认日志：

```text
server CategorizePosition(void) signature target=...
server.CategorizePosition.vtable[52] address=...
match=true hook=true
```

结论：

- Windows L4D2 中当前确认的是 `CategorizePosition(void)`。
- Linux `.so` 中得到的 `CategorizePosition(bool)` 签名不能直接用于 Windows。
- 早期 bool 签名扫描失败是合理的，不能据此判断函数不存在。

#### server PlayerMove

方式：server `CGameMovement` VMT hook。

index：

```text
server PlayerMove[18]
```

用途：

- movement stage 总入口诊断。
- 捕捉进入/退出 PlayerMove 时的 `CMoveData` 状态。

#### server WalkMove

方式：server `CGameMovement` VMT hook。

index：

```text
server WalkMove[28]
```

用途：

- 当前阶段最关键的地面走入门洞阻塞点。
- `WalkMove` 原函数返回后，origin/velocity 已经被墙面逻辑修正或归零。
- `WalkMoveNudge` 验证了在该出口修正 `CMoveData` 可以让玩家进入门洞。

#### server FullWalkMove

方式：server `CGameMovement` VMT hook。

index：

```text
server FullWalkMove[30]
```

用途：

- 观察 `WalkMove` 外层流程。
- 发现 crossing 后仍有 `FullWalkMove` 内部 horizontal trace/position restore 路径。

#### server TryPlayerMove

方式：server `CGameMovement` VMT hook。

index：

```text
server TryPlayerMove[38]
```

用户提供原型：

```cpp
CGameMovement::TryPlayerMove(Vector* pFirstDest, CGameTrace* pFirstTrace)
```

当前 hook 原型：

```cpp
int __fastcall Detour(
    void* ecx,
    void* edx,
    Vector* pFirstDest,
    trace_t* pFirstTrace);
```

结论：

- `[38]` 可用。
- 但在当前地面卡门问题中，它不是最关键的阻塞点。
- 可作为后续正式 movement result 合成或调试 TryPlayerMove 内部 sliding 的资产保留。

#### server StepMove

方式：server `CGameMovement` VMT hook。

index：

```text
server StepMove[64]
```

用户提供原型：

```cpp
CGameMovement::StepMove(Vector& vecDestination, CGameTrace& trace)
```

当前 hook 原型：

```cpp
void __fastcall Detour(
    void* ecx,
    void* edx,
    Vector& vecDestination,
    trace_t& trace);
```

结论：

- `[64]` 可 hook。
- 当前不是主阻塞点。
- 可用于后续处理台阶、坡面、门洞边缘时保留。

#### server StayOnGround

方式：由 return address 附近 prologue 定位，再用函数 hook。

已知 return points：

```text
server.dll + 0x000F661B
server.dll + 0x000F665C
```

candidate start：

```text
server.dll + 0x000F6590
```

当前 hook 原型：

```cpp
void __fastcall Detour(void* ecx, void* edx);
```

结论：

- 已验证可 hook。
- skip 原函数不能单独解决地面卡门。
- 后续仍有价值：用于控制 portal bridge phase 中的 ground snap、地面吸附和出口落地状态。

#### client PlayerMove

方式：client `CGameMovement` VMT hook。

index：

```text
client PlayerMove[18]
```

用途：

- client prediction 侧 movement stage 诊断。

#### client WalkMove

方式：client `CGameMovement` VMT hook。

index：

```text
client WalkMove[28]
```

用途：

- 当前 `WalkMoveNudge` server/client 双侧都已打入。
- 对视觉预测连续性有价值。

#### client FullWalkMove

方式：client `CGameMovement` VMT hook。

index：

```text
client FullWalkMove[30]
```

用途：

- client prediction 外层 movement 诊断。

### 已尝试但不应作为资产依赖的入口

#### server TracePlayerBBox[14]

问题：

- 按用户提供布局中的 `[14]` 直接 hook server 端，会导致参数错乱、ESP 错误或崩溃。

结论：

- server TracePlayerBBox 使用签名 hook。
- client TracePlayerBBox 使用 `[16]`。

#### TryPlayerMove[40]

问题：

- 使用 `TryPlayerMove` 签名 hook `[40]` 后靠近蓝门崩溃。
- 参数表现不像 `TryPlayerMove(Vector*, trace_t*)`。

结论：

- `[40]` 不是当前需要的 TryPlayerMove，或签名完全不同。
- 暂时不要使用。

#### StepMove[66]

问题：

- 曾按 `+2` 思路尝试，出现崩溃或无效。

结论：

- server StepMove 当前保留 `[64]`。

#### EngineTrace::TraceRay

问题：

- 目标地面移动路径没有稳定走到我们需要的 EngineTrace hook。
- 没有出现预期 movement bypass 日志。

结论：

- 当前不走 EngineTrace 主线。

### 待获取或待验证的重要函数

#### TestPlayerPosition

用户确认签名：

```cpp
CGameMovement::TestPlayerPosition(Vector const&, int, CGameTrace&)
```

当前状态：

- index `[63]` 曾被列入诊断，但签名未验证。
- 尚未稳定 hook。

价值：

- 处理 startsolid。
- 处理玩家 hull 已在门洞中时的 position test。
- 对“卡在墙体模型里”和出口侧放置安全性非常重要。

#### client CategorizePosition

当前状态：

- server 已确认。
- client 尚未 hook。

价值：

- 预测侧 ground state。
- 如果后续出现客户端视觉抖动、预测回弹，需要补齐。

#### client TryPlayerMove / StepMove

当前状态：

- 未验证。

价值：

- 可能用于更完整的 client prediction 侧连续性。
- 目前优先级低于正式 teleport commit。

#### CBaseEntity / CBasePlayer Teleport

当前已知：

- `Teleport(vtable[118])` 可用。
- `SetAbsOrigin` / `SetAbsAngles` / `SetAbsVelocity` 签名此前已知有问题，不作为依赖。

下一步需要：

- 固化 `Teleport` wrapper。
- 明确参数为 origin、angles、velocity 指针。
- 确认 player entity 上调用与 prop portal 上调用都稳定。

### 关键日志模式资产

#### 进入 portal phase

```text
[PortalSim] phase Idle/None -> ApproachingPortal/Blue reason=approach ...
[PortalSim] phase ApproachingPortal/Blue -> IntersectingPortal/Blue reason=aperture-intersection ...
```

#### horizontal trace 被豁免

```text
[PortalBridge][TracePost] ... class=HorizontalMove bypassed=true phase=IntersectingPortal entry=Blue ...
```

#### StayOnGround 被跳过

```text
[PortalBridge][StayOnGround] skipped during portal bridge ...
```

#### WalkMove 出口修正生效

```text
[PortalBridge][WalkMoveNudge][server] ... origin=(... -1505.0 ...)->(... -1511.0 ...) vel=(...)->(...)
[PortalBridge][WalkMoveNudge][client] ... origin=(... -1505.0 ...)->(... -1511.0 ...) vel=(...)->(...)
```

#### 当前还没有正式传送

```text
Teleport: 0
CommittingTeleport: 0
```

## 当前风险和注意事项

- `WalkMoveNudge` 是验证代码，不应长期作为最终实现。
- 固定推进距离和固定 normal velocity 会导致不自然移动，且可能把玩家推入墙后模型。
- 当前 trace 豁免可能过宽，尤其 startsolid 后的 trace 需要更精细的 portal hole 判定。
- `StayOnGround` 当前通过 return address 附近 prologue 找到，虽然实测有效，但仍需保留函数字节/模块 offset 校验。
- client/server 双侧 movement 修正可能在后续正式 teleport 后产生预测差异，需要逐步收敛。
- 不应再回到 MoveType/noclip 方案。

## 建议的下一次开发切入点

1. 在 `PortalTransitionSimulator` 或新的 traversal commit 模块中记录上一帧 signedDepth。
2. 当 signedDepth 从正到负跨越入口门 plane 时，触发 `CommittingTeleport`。
3. 使用 entry/exit portal transform 计算：
   - target origin
   - target angles
   - target velocity
4. 调用玩家 `Teleport(vtable[118])`。
5. 进入 `ExitingPortal` / `Cooldown`，避免同一帧或后续几帧重复传送。
6. 在出口侧临时启用 collision bridge 或最小安全推出，避免玩家刚出门就被出口墙体夹住。
7. 将 `WalkMoveNudge` 改为正式的 movement result 合成，不再固定硬推。

## 阶段性结论

当前阶段最重要的成果是：已经证明 L4D2 地面玩家无法走入 portal 门洞的主阻塞点并不是传送门放置、视觉、PortalTransition phase，也不是单纯 `TracePlayerBBox` 是否被调用，而是地面 movement 的 `WalkMove / FullWalkMove` 结果合成会把玩家最终 origin/velocity 重新夹回墙面。

`WalkMoveNudge` 实测让玩家地面走入门洞，说明这条路是正确的。下一阶段应把“能够嵌入门洞”的验证代码升级为“跨过入口 plane 后正式 teleport 到出口”的主干实现。
