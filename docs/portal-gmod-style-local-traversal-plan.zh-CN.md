# GMod 风格本地玩家传送门穿越复刻方案

日期：2026-07-07

## 背景

当前 L4D2 传送门穿越方案已经可以让 localserver 本地玩家完成基础穿越，但实测仍存在连续性问题：

- 玩家与门洞中心错开一定位置或角度进入时，可能出现视觉不连续。
- 快速或慢速进入门洞时，仍可能出现卡墙、被墙体 movement solver 拉回、速度突变等问题。
- 当前主干仍带有“普通 movement 撞墙后再补偿”的痕迹，例如 trace bypass、`WalkMoveNudge`、assisted embedding 等。

本方案目标是以 `Reference Code/PortalGun_Gmod_gmpublisher` 为参考，优先复刻其单人玩家穿越主干，而不是继续用近似等效的后处理方案妥协。

## 范围

本阶段只处理：

- localserver。
- 本地单人玩家。
- 蓝门与橙门均已存在且已链接。
- 玩家从入口门进入、跨过门平面、从出口门释放的主体验。

本阶段暂不处理：

- 玩家死亡。
- 多名玩家。
- bot 或 AI survivor。
- 普通物理实体穿越。
- 子弹/投射物穿越。
- 完整玩家 clone 和半身裁剪视觉。
- 完整 Portal SDK 级别的 `CPortalSimulator`、touching entity、portal environment。

## 参考方案核心理解

GMod 方案不是“最后一刻瞬移玩家”，而是在玩家进入门洞后建立一个受限的 portal movement environment：

```text
Normal WALK
  -> 玩家进入有效门洞范围
InPortal / controlled NOCLIP
  -> Move hook 接管玩家移动
  -> 在入口门 local space 内 clamp 玩家位置
Crossing
  -> 跨过入口 plane
  -> 位置、视角、速度一起变换到出口
ExitingPortal / controlled NOCLIP
  -> 继续在出口门 local space 内释放玩家
Normal WALK
```

关键点：

- `MOVETYPE_NOCLIP` 不是给玩家自由穿墙，而是半穿越期间绕开普通 player collision solver 的手段。
- GMod 的 `Move` hook 会每帧接管玩家速度和位置，并把玩家限制在传送门局部空间范围内。
- 传送后不会立刻恢复 `WALK`，而是继续进入出口门的 `InPortal` 状态，直到玩家真正离开出口门附近。
- 位置、视角、速度必须在同一 movement 时序内提交，避免“入口位置 + 出口视角”或“速度被下一帧清掉”的错配。

## 当前工程已有资产

### 已有或基本可用

- `PortalTransform`：已有入口到出口矩阵、点/向量/角度变换。
- server local player `Teleport(vtable[118])`：当前验证可用于实际传送。
- `CMoveData`：已有主要字段定义，包括 `m_vecVelocity`、`m_vecAngles`、`m_vecAbsOrigin`、forward/side move 等。
- server/client `CGameMovement` hooks：
  - `PlayerMove[18]`
  - `WalkMove[28]`
  - `FullWalkMove[30]`
  - server `TracePlayerBBox` 签名 hook
  - server `CategorizePosition`
  - server `StayOnGround`
- client prediction hooks：
  - `SetupMove`
  - `FinishMove`
- `C_BaseEntity::m_MoveType()` client 偏移访问。
- `C_BasePlayer` / `C_BaseEntity` 中已有 eye、center、mins/maxs、velocity、ground entity 等 netvar 或虚函数入口。

### 仍需确认或逆向

- server-side local player 的 `MoveType` 字段或 `SetMoveType` 等价函数。
- server-side `SetGroundEntity` 或 ground 状态更新方式。
- 最适合承载 GMod `Move` hook 的 L4D2 movement 层。
- `CBasePlayer::SetLocalVelocity` / `CBaseEntity::SetAbsVelocity` 的稳定调用方式。
- server-side view angle setter，例如 `SnapEyeAngles` 或等价接口。
- `TestPlayerPosition` 的正确签名和调用约定。
- `CMoveData` 在 server/client 两侧的完整布局和写入可靠性。

## 目标状态机

建议把单人玩家穿越收敛成明确状态：

```text
Normal
  玩家未处于传送门穿越状态，使用普通 WALK movement。

InPortal
  玩家已进入入口门洞，MoveType 为 NOCLIP，但 movement 被 portal solver 接管。

Crossing
  controlled movement 检测到玩家从入口侧跨过入口 plane。

ExitingPortal
  玩家已经被变换到出口门，仍保持 controlled NOCLIP，从出口门释放。

Cooldown
  短暂防抖，避免刚出门立刻反向触发。
```

## 总体架构

```text
Portal bounds detector
  -> 判断玩家是否有意进入门洞

Portal traversal session
  -> 保存 entry/exit side、saved move type、entry velocity、previous local forward

Portal controlled movement
  -> 在 movement hook 中接管 CMoveData
  -> 计算 velocity
  -> 应用 gravity
  -> 计算 next origin
  -> 转换到 portal local
  -> clamp local right/up
  -> 写回 origin/velocity

Portal crossing detector
  -> local forward 从入口侧跨到背侧时触发

Portal teleport commit
  -> eye/head position transform
  -> view angles transform
  -> velocity transform
  -> server Teleport[118]
  -> 同步 client view/velocity/move

Portal exit release
  -> 出口门 controlled movement
  -> 离开出口门范围后恢复 WALK
```

## 实施计划

### Task 1: 确认 server-side MoveType 控制

涉及文件：

- `src/SDK/L4D2/Entities/C_BaseEntity.h`
- `src/Portal/PortalTransition.cpp`
- `src/Util/Logger/PortalFileLog.h`

目标：

- 找到或确认 localserver server-side local player 的 `MoveType` 控制方式。
- 实现可靠的 `EnterControlledNoclip` 和 `RestoreWalkMove`。

步骤：

1. 在进入/退出 traversal 时记录 client player pointer、server player pointer、client `MoveType`、server `MoveType`。
2. 验证直接写 server-side `MOVETYPE_NOCLIP` 是否参与 server movement。
3. 验证恢复 `MOVETYPE_WALK` 后是否残留预测、碰撞或速度异常。
4. 如果 client/server 偏移不同，建立独立的 server player movement state resolver。

验收：

- 进入门洞后普通 walk solver 不再把玩家当作撞墙处理。
- 离开出口门后玩家恢复普通行走。

### Task 2: 确定 controlled movement hook 点

涉及文件：

- `src/Hooks/GameMovement/CCSGameMovement.cpp`
- `src/Hooks/ClientPrediction/ClientPrediction.cpp`
- `src/Portal/PortalTransition.cpp`

候选点：

- server `PlayerMove[18]`
- server `FullWalkMove[30]`
- server `WalkMove[28]`
- client `SetupMove`
- client `FinishMove`

目标：

- 找到最接近 GMod `Move` hook 的 L4D2 hook 点。
- 能在该点稳定读写 `CMoveData::m_vecAbsOrigin` 和 `m_vecVelocity`。

步骤：

1. 在候选 hook 中记录 `origin`、`velocity`、`buttons`、`forwardmove`、`sidemove`、`movetype`。
2. 进入 `InPortal` 后观察哪个 hook 能最早且稳定覆盖 movement 结果。
3. 选定一个 server 权威 hook。
4. 选定一个 client prediction mirror hook，减少视觉错位。

验收：

- controlled movement 写入后，后续 engine movement 不会再把 origin/velocity 覆盖回墙前。

### Task 3: 实现 traversal session 骨架

涉及文件：

- `src/Portal/PortalTransition.h`
- `src/Portal/PortalTransition.cpp`

目标：

- 明确 `Normal`、`InPortal`、`Crossing`、`ExitingPortal`、`Cooldown` 状态。
- 所有状态进入和退出都有统一日志。

需要保存的数据：

- entry side
- exit side
- saved move type
- entry velocity
- previous local forward distance
- enter time
- last controlled move time
- cooldown end time

验收：

- 日志可以清楚看到玩家从 `Normal` 进入 `InPortal`，再进入 `ExitingPortal`，最后恢复 `Normal`。

### Task 4: 复刻 GMod `PlayerWithinBounds`

涉及文件：

- `src/Portal/PortalTransition.cpp`
- `src/Portal/PortalTransform.h`
- `src/Portal/PortalTransform.cpp`

目标：

- 不再只用 center/eye 的单点 aperture 判断。
- 按 GMod 思路综合判断脚、头、center、门平面距离、输入方向。

判断条件建议：

- portal linked and active。
- player not already in another traversal session。
- eye/head 在门局部 `right/up` 范围内。
- feet/origin 不明显越出门洞。
- eye/center 到门平面距离小于进入阈值。
- `cmd` 或 velocity 表示玩家正在朝门内移动。

验收场景：

- 正对门慢速进入。
- 快速冲门。
- 斜向进入。
- 偏门左/右边缘进入。
- 擦边经过不误触发。

### Task 5: 实现 controlled NOCLIP movement

涉及文件：

- `src/Portal/PortalTransition.cpp`
- `src/Hooks/GameMovement/CCSGameMovement.cpp`
- `src/Hooks/ClientPrediction/ClientPrediction.cpp`

目标：

- 复刻 GMod `ipMove` 的核心行为。
- `InPortal` 期间不再靠 `WalkMoveNudge` 硬推。

伪流程：

```text
if player in InPortal or ExitingPortal:
    read move origin / velocity / forwardmove / sidemove / move angles
    compute acceleration
    newVelocity = oldVelocity + acceleration * dt
    newVelocity += gravity * dt
    nextWorld = origin + newVelocity * dt
    local = portal.WorldToLocal(nextWorld)
    clamp local.right to aperture halfWidth
    clamp local.up to aperture halfHeight
    move.origin = portal.LocalToWorld(local)
    move.velocity = newVelocity
    return handled
```

验收：

- 玩家在门洞里不会被普通墙体卡住。
- 玩家也不能借 `NOCLIP` 离开门洞范围穿其他墙。
- 快速和慢速进入都保持速度连续。

### Task 6: Crossing 判定内聚到 controlled movement

涉及文件：

- `src/Portal/PortalTransition.cpp`

目标：

- crossing 不再依赖外部 assisted embedding 的补偿结果。
- 由 controlled movement 直接检测玩家是否跨过入口 plane。

实现要点：

- 每帧记录 local forward distance。
- 使用 previous/current segment 判断跨 plane。
- 快速移动时不能穿过太深才传送。
- 慢速边界抖动时不能反复触发。

验收：

- 玩家只要在门内连续推进，跨 plane 当帧触发 teleport。
- 不出现“已经进墙很深才传送”的帧。

### Task 7: Teleport commit 同步位置、视角、速度

涉及文件：

- `src/Portal/PortalTransition.cpp`
- `src/Portal/PortalPlayerTeleport.cpp`
- `src/Portal/PortalTransform.cpp`

目标：

- 跨门时一次性计算并提交新 origin、view angles、velocity。
- 传送后立即切入出口门 `ExitingPortal` 状态。

实现要点：

- 使用 eye/head anchor 做位置 transform，再换算 player origin。
- 使用 `PortalTransform::TransformAngles` 转换 view angles。
- 使用 `PortalTransform::TransformVector` 转换 velocity。
- 使用 server `Teleport[118]` 提交。
- 同步 client `SetViewAngles`、`m_vecVelocity`、当前 `CMoveData`。

验收：

- 不再出现“位置还在入口、视角已经到出口”的错配帧。
- 不再出现传送后速度被下一帧清零或大幅突降。

### Task 8: 出口侧释放逻辑

涉及文件：

- `src/Portal/PortalTransition.cpp`

目标：

- 传送后继续 controlled NOCLIP，而不是立刻恢复 `WALK`。

逻辑：

```text
Teleport to exit
  -> mode = ExitingPortal
  -> active portal = exit
  -> controlled movement continues
  -> player eye/center clear of exit plane and aperture
  -> restore saved WALK move type
```

验收：

- 玩家从出口门自然走出。
- 出口门附近不会因过早恢复 `WALK` 被墙体或地面卡住。
- 刚出门不会被反向触发传回入口。

### Task 9: 降级旧 `WalkMoveNudge`

涉及文件：

- `src/Hooks/GameMovement/CCSGameMovement.cpp`
- `src/Portal/PortalCollisionBridge.cpp`

目标：

- 新主路径启用时，旧 `WalkMoveNudge` 不再同时改写 movement。
- trace bypass 可保留为门洞碰撞辅助。

处理方式：

- `InPortal` 或 `ExitingPortal` 状态下禁用 `WalkMoveNudge`。
- `WalkMoveNudge` 只作为 fallback 或诊断，通过开关控制。
- 日志中明确标识当前使用的是 controlled movement 还是 fallback nudge。

验收：

- 不出现 controlled movement 和 nudge 双重改写导致的位置抖动。

### Task 10: 日志和诊断重做

涉及文件：

- `src/Util/Logger/PortalFileLog.h`
- `src/Portal/PortalTransition.cpp`

目标：

- 日志围绕新状态机，而不是旧 bridge/nudge 主线。

建议日志标签：

- `PortalEnterState`
- `PortalControlledMove`
- `PortalLocalClamp`
- `PortalCrossing`
- `PortalTeleportCommit`
- `PortalExitState`
- `PortalRestoreWalk`

每条日志建议包含：

- mode
- entry side / exit side
- local forward/right/up
- origin
- eye
- velocity
- move type
- command forward/side
- plane distance

验收：

- 出现问题时可以从日志判断是 bounds、movement、crossing、teleport、exit release 哪一段出错。

## 验证矩阵

### 必测场景

- 正对墙门慢速走入。
- 正对墙门快速冲入。
- 斜向进入墙门。
- 偏门左侧进入。
- 偏门右侧进入。
- 倒退进入。
- 蓝门到橙门。
- 橙门到蓝门。
- 出口门贴近墙面时观察是否卡住。
- 出口门贴近地面时观察是否被弹出或掉速。

### 成功标准

- 玩家能稳定进入门洞。
- 玩家在门洞内不被墙体挡住。
- 玩家在门洞内不能自由穿其他墙。
- 跨门时视角、位置、速度连续。
- 出口释放自然。
- 恢复 `WALK` 后普通移动正常。

## 风险

- server-side `MoveType` 偏移或 setter 未确认，可能导致 client 看起来 noclip 但 server 仍按 walk 处理。
- `CMoveData::m_vecAbsOrigin` 在某些 hook 点写入可能被后续 movement 覆盖。
- 直接写 velocity 可能与 L4D2 的 stamina、受伤减速、地面摩擦等逻辑冲突。
- `StayOnGround` / `CategorizePosition` 仍可能在慢速贴地进门时把玩家拉回地面路径。
- 出口侧过早恢复 `WALK` 会重新引入卡墙。

## 建议执行顺序

优先级 1：

1. server-side `MoveType` 控制验证。
2. controlled movement hook 点确认。
3. `InPortal` 状态机骨架。
4. controlled NOCLIP movement 写 `CMoveData`。
5. crossing 与 teleport commit。

优先级 2：

6. GMod 风格 bounds 判断。
7. 出口侧 `ExitingPortal` 释放。
8. 旧 `WalkMoveNudge` 降级。
9. 新日志体系。

优先级 3：

10. 玩家 clone。
11. render clip plane。
12. 更完整的半身视觉连续性。

## 下一次会话建议入口

如果下一次会话要开始实现，建议从以下问题开始：

```text
请根据 docs/portal-gmod-style-local-traversal-plan.zh-CN.md，
先执行 Task 1 和 Task 2：
确认 server-side MoveType 控制方式，并找出最适合承载 controlled portal movement 的 hook 点。
先不要改完整穿越逻辑，只加诊断和最小验证。
```

这样可以先验证最关键的地基，再决定 controlled movement 应该落在 server/client 哪一层。
