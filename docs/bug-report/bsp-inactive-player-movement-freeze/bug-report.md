# BSP 未激活时传送门附近冻结问题报告

## 1. 问题与复现

在 `BspTraversal` 模式下保持 `portal_bsp_phase1` 关闭，放置成对传送门后走近入口墙面。玩家会在进入传送门孔径附近后完全停止响应；反向移动、横移、跳跃均无效，游戏自带 `noclip` 也无法脱离。

## 2. 日志证据

- 物理模式为 `BspTraversal`，且 `movementMutation=false`、`legacyCollisionBypass=false`、`teleport=true`。
- BSP 绑定成功，但最后一次修改状态为 `Phase1Disabled`，`phase1Enabled=false`、`carvingActive=false`、`destructiveWrites=false`，说明墙体 brush 从未被清空。
- 玩家接近蓝门后，状态从 `Idle` 进入 `IntersectingPortal`；随后多帧的原点和速度完全不变。
- 开启游戏自带 `noclip` 后，客户端与服务端 MoveType 均为 8，但位置仍然不变；日志中没有受控 noclip 成功接管记录，也没有传送提交记录。

## 3. 根因

旧穿越路径在 `IntersectingPortal`、`CommittingTeleport` 和 `ExitingPortal` 阶段会跳过引擎原始 `PlayerMove`，改由受控 noclip 移动接管。但当前 BSP 因果模式明确设置了 `movementMutation=false`，因此受控移动函数立即拒绝执行；调用方又没有回退到原始 `PlayerMove`。结果是一帧内既没有插件移动，也没有引擎移动。

与此同时，BSP 挖空未激活时状态机仍会根据玩家与视觉传送门的距离进入 `IntersectingPortal`，形成闭环：墙体仍为实体，玩家抵墙后进入相交阶段，原始移动被跳过，位置与速度冻结，状态机继续读取旧状态而无法退出。游戏自带 `noclip` 同样依赖原始移动求解，所以也被一起冻结。

## 4. 修复

- 增加运行时碰撞通道门禁：`BspTraversal` 只有在 `IsCarvingActive()` 为真时才允许穿越状态机推进；否则状态保持或恢复为 `Idle`。
- 客户端和服务端 `PlayerMove` 仅在“当前阶段需要受控移动”且“物理模式允许移动修改”时尝试替换原始函数；只要接管未成功，就无条件调用原始 `PlayerMove`。
- `WalkMove` 位置推进和 `StayOnGround` 跳过逻辑也受 `ShouldMutatePlayerMovement()` 约束，避免 BSP-only 模式被旧移动路径干预。
- 保留 BSP 激活后的状态机、坐标变换、Teleport 和提交后的预测同步，不重新启用 trace 清除或受控 noclip。

## 5. 验证要求

- 自动测试证明：BSP carving 未激活时，BSP 状态机运行门禁为假；激活后为真；Legacy 模式不依赖该状态。
- 自动测试证明：`movementMutation=false` 时，任何阶段都不得替换原始 `PlayerMove`；Legacy 模式在相交阶段仍可按原设计接管。
- 实机回归：Phase 1 关闭时玩家撞墙应正常被阻挡但绝不冻结，游戏自带 `noclip` 可正常进出；Phase 1 开启且两个 brush 成功挖空后，再执行完整穿越测试。
