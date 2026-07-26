# BSP Teleport 客户端预测不同步

## 1. 现象与复现范围

在 `BspTraversal` 模式下，地图 brush 已成功清空，玩家能够依靠 BSP 挖空进入墙体并由既有 Teleport 逻辑完成传送。人工测试中，每次穿越附近仍可能出现约一至两帧的画面闪烁；部分穿越后还会表现出速度或落点不自然。

本轮只定位并修复 Teleport 提交后的客户端预测同步问题。出口高度差和旧方案遗留的 hull clearance push 属于另一条因果链，本轮不修改。

## 2. 证据

- 日志记录了 5 次成功的 `[PortalTeleportCommit]`，说明 BSP 挖空、平面跨越判定、坐标变换和服务端 Teleport 提交链路均已运行。
- 以 `cmd=1726` 为代表，提交后的服务端/Tools 位置已位于出口，视角也已变换；同一时刻客户端预测的 player/move 仍停留在入口位置并保留旧速度。下一次采样客户端才抵达出口，期间水平速度还可能归零。
- 整份日志没有 `[PortalPredictionSync]` 或 `CommittedMoveSync`，与上述入口/出口分裂一致。
- `[PortalPhysicsMode]` 中 `movementMutation=false`、`legacyCollisionBypass=false` 符合本阶段约束，因此问题不是旧 noclip 或射线绕过被重新启用。
- 日志中的 `clearancePush=19.50` / `[PortalGModOffset]` 保留，供后续单独验证；它不是本轮修复对象。

## 3. 根因

`CPortalTransitionSimulator::TryGetCommittedMovementForCommand` 原先使用 `PortalPhysicsMode::ShouldMutatePlayerMovement()` 作为入口门禁。`BspTraversal` 为避免旧方案接管 `PlayerMove`，该能力必须为 `false`，结果也连带关闭了 Teleport 已提交结果向客户端 `CMoveData` 的一次性同步。

同时，预测同步决策只允许 `RequiresControlledNoclip()` 对应的阶段。BSP 模式不进入受控 noclip，而 Teleport 提交后状态会进入 `ExitingPortal`，并可能在下一命令进入 `Cooldown`，因此同步窗口与旧移动接管概念错误耦合。

## 4. 修复

- 新增独立能力 `ShouldSynchronizeCommittedTeleportPrediction()`：`LegacyTraversal` 与 `BspTraversal` 启用，`VisualOnlyBaseline` 禁用。
- `TryGetCommittedMovementForCommand` 改用该独立能力，不再依赖 `movementMutation`。
- 一次性同步窗口明确限定为 `ExitingPortal` 或 `Cooldown`；`IntersectingPortal`、`CommittingTeleport` 和 `Idle` 均不消费旧提交结果。
- 保留既有命令号容差（当前命令或下一命令）和 48 单位位置差门槛，避免对已经同步的预测状态重复写入。
- 消费成功时原子同步预测 origin、velocity、move angles、user command viewangles 和引擎视角。
- `movementMutation=false`、原始客户端/服务端 `PlayerMove`、原始贴地处理、`MOVETYPE_WALK`、旧 trace bypass 禁用状态均保持不变。
- 未修改 `ComputeExitClearancePush`、`exitClearancePush` 或出口位置公式。

## 5. 验证要求

自动化验证应覆盖：

- BSP 和 Legacy 模式允许一次性 Teleport 预测同步，VisualOnly 模式拒绝同步；
- `ExitingPortal` 与 `Cooldown` 在位置差足够大时同步；
- `IntersectingPortal` 与 `Idle` 不消费旧提交；
- 已接近提交位置时不重复同步；
- 只有移动同步成功时才同步视角。

游戏内验证每次 `[PortalTeleportCommit]` 后应出现一次同命令或下一命令的 `[PortalPredictionSync]`。主观上重点比较平坦、等高的两面墙之间往返穿越时闪烁是否消失或明显减少；高度不匹配造成的落点与速度问题留到后续独立验证。
