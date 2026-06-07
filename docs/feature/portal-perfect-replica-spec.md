# Portal 完整复刻第一阶段规格

**创建日期:** 2026-06-05
**目标分支:** `codex/portal-perfect-replica`
**范围:** L4D2 单人 / localserver / 本地玩家
**参考资料:**
- `docs/feature/portal-perfect-replica-transition-plan.md`
- `docs/feature/portal-gmod-reference-understanding.md`
- `docs/server-side-portal-teleport-plan.md`
- `docs/portal-visual-continuity-plan.zh-CN.md`
- `docs/portal-sdk-2013-technical-report.md`
- `Reference Code/PortalGun_Gmod_gmpublisher`
- `Reference Code/portal-sdk-2013`

---

## 1. 目标

本规格定义 L4D2 中完整复刻《Portal》物理穿越逻辑的第一阶段。第一阶段只处理本地玩家在单人 localserver 中穿越一对已经放置且激活的蓝/橙传送门。

本阶段的目标不是一次性复刻官方 Portal 的所有实体模拟、多人同步、完整物理对象穿越和玩家身体分割渲染，而是建立正确的架构路线：

```text
PortalTransform
  -> PortalTransitionSimulator
  -> PortalCollisionBridge
  -> SafeExitPlacement
  -> AtomicTeleport
  -> PortalViewBridge
```

核心体验目标：

- 本地玩家可以从入口门穿到出口门。
- 玩家穿越后保留合理的视角方向和速度方向。
- 玩家靠近门洞时不会被普通墙体碰撞永久挡在门外。
- 玩家穿越过程由显式状态机管理，而不是由多个 hook 中的临时布尔变量拼接。
- 玩家真实实体不再被强行推进墙体或非法 world leaf。
- 出口落点必须安全，不能再出现 `outsideOrigin=true outsideEye=false` 仍然提交 teleport 的情况。
- 基础穿越稳定优先，半嵌入视觉连续性作为后续由 simulator 管理的能力。

---

## 2. 非目标

以下内容不属于第一阶段必须完成的范围：

- 多人同步。
- 远程玩家、感染者、Witch、Tank、物理 props、ragdoll、projectile 的完整穿越。
- 完整官方 `CPortalSimulator` 移植。
- 完整玩家身体分割、出口侧玩家克隆体、骨骼同步。
- 完整 portal placement bumping/noportal volume/cleanser 规则。
- 门洞内交互 trace、子弹多次穿门、use trace 穿门。
- 完整递归 stencil 视觉重构。
- 为所有极端地图几何提供最终解。

这些内容可以作为后续阶段扩展，但第一阶段不得为了它们破坏本地玩家基础穿越稳定性。

---

## 3. 技术红线

### 3.1 禁止真实玩家 origin 进入非法 world

正式路线不得再依赖把真实玩家实体 origin 同步推进墙体、solid brush 或非法 leaf。

任何 teleport 候选点如果满足以下任一条件，必须拒绝提交：

- `PointOutsideWorld(origin) == true`
- `PointOutsideWorld(eye) == true`
- player hull trace 显示出口位置不可容纳玩家
- 出口候选点会导致 PVS、skybox 或 viewmodel/world render 明显不同步

历史实验中 `outsideOrigin=true outsideEye=false` 是明确失败信号，不允许作为可接受落点。

### 3.2 禁止 MoveData 成为主控制器

`CMoveData` 可以用于观察 prediction 状态、诊断 movement 时序、辅助推断玩家意图，但不得作为完整穿门状态的主控制器。

不得依赖 `CMoveData::m_vecAbsOrigin` 作为权威 teleport anchor。

### 3.3 禁止分散提交 teleport 状态

位置、视角、速度、cooldown、exit state 必须由 `PortalTransitionSimulator` 在一个明确提交阶段集中处理。

不得再次出现：

- 本帧 view angles 已变，origin 未变。
- 本帧 movement 已变，render view 未变。
- camera 在出口，player origin 还在入口或墙体内。
- 一部分系统用 old PVS，另一部分系统用 new view。

### 3.4 禁止无条件 trace 全豁免

`TracePlayerBBox` 或 hull trace 的门洞豁免必须有严格几何条件。

不得长期使用“只要靠近门或命中门洞就 `fraction=1`”的方案。门洞外碰撞必须保持原始行为。

---

## 4. 核心模型

### 4.1 传送门对

系统只在两扇门都满足以下条件时启用穿越：

- 蓝门 active。
- 橙门 active。
- 两扇门不处于 closing 状态。
- 两扇门具有合法 origin、normal、angles。
- 两扇门可以构造 entry-to-exit transform。

### 4.2 传送门局部空间

传送门必须提供局部坐标系：

```text
portal.origin
portal.normal / forward
portal.right
portal.up
aperture half width
aperture half height
```

任何接近、进入、穿越、退出判断都应优先转到 portal local space 处理。

### 4.3 玩家采样点

本地玩家不能只使用中心点判断穿越。系统至少需要维护：

- `origin`: 玩家真实实体 origin。
- `eye`: 玩家视点。
- `center`: 玩家 hull 近似中心。
- `feet`: 玩家脚底或下缘参考点。
- `leadingHullPoint`: 沿移动方向或入口 normal 的 hull 前缘参考点。
- `velocity`: 当前速度。
- `viewAngles`: 当前视角。

第一阶段允许 hull 几何是近似的，但必须比单点 origin 更丰富。

---

## 5. 状态机规格

必须新增或等价实现一个集中状态机，暂定名：

```cpp
CPortalTransitionSimulator
```

建议状态：

```cpp
enum class PortalTransitionPhase
{
    Idle,
    ApproachingPortal,
    IntersectingPortal,
    CommittingTeleport,
    ExitingPortal,
    Cooldown,
};
```

### 5.1 Idle

含义：本地玩家当前没有参与穿门。

进入条件：

- 地图初始化。
- 传送门不可用。
- 玩家离开门洞范围。
- cooldown 结束并离开出口影响区。
- 状态异常被 reset。

行为：

- 不修改玩家位置。
- 不修改玩家视角。
- 不修改 trace 结果。
- 只允许输出低频 readiness/distance debug。

### 5.2 ApproachingPortal

含义：玩家在入口门有效范围附近，并具有进入意图，但尚未进入门洞交互状态。

进入条件：

- 门对 ready。
- 玩家 eye/origin/leading hull 与门平面距离小于阈值。
- 玩家投影在 aperture 附近。
- 玩家输入方向或速度方向朝向入口门内。

行为：

- 记录 entry side 和 exit side。
- 记录 player anchor。
- 计算 signed distance / signed depth。
- 可请求 collision bridge 做非常保守的门洞边缘辅助。
- 不允许 teleport。

退出条件：

- 玩家远离门。
- 玩家不再朝向门内移动。
- 门失效。
- 进入 `IntersectingPortal`。

### 5.3 IntersectingPortal

含义：玩家 hull 已经与门洞有效区域发生穿越交互，需要碰撞桥接辅助，避免被入口墙体卡住。

进入条件：

- `ApproachingPortal` 中满足更严格 aperture/hull overlap。
- 玩家继续朝入口门内移动。
- 门洞区域允许局部 trace 豁免。

行为：

- `PortalCollisionBridge` 可以在门洞 aperture 内选择性豁免玩家 hull trace。
- simulator 持续更新 signed depth。
- simulator 持续计算出口候选点安全性。
- 如果达到 crossing 条件且出口安全，进入 `CommittingTeleport`。
- 如果玩家退出门洞或出口不安全，回退或保持，不允许强行提交。

禁止行为：

- 禁止把真实 origin 同步推进非法 leaf。
- 禁止直接在碰撞桥里 teleport。
- 禁止在此阶段直接改 view angles。

### 5.4 CommittingTeleport

含义：simulator 已确认玩家完成有效穿越，并且出口候选点安全，可以提交真实 teleport。

进入条件：

- entry/exit transform 有效。
- 玩家 crossing 条件成立。
- `FindSafeExitPlacement` 返回合法候选点。
- cooldown 不阻止本次提交。

提交内容：

- origin。
- view angles。
- velocity。
- exit side。
- cooldown token。
- visual transition state。

行为要求：

- 提交必须尽量原子化。
- server-side local player movement path 优先用于权威移动。
- `EngineClient->SetViewAngles` 仅作为视角同步的一部分，不能单独作为 teleport。
- 提交后进入 `ExitingPortal`，而不是直接 `Idle`。

### 5.5 ExitingPortal

含义：玩家已经从出口门出来，但仍处于出口门附近，需要短时间防止反向触发和边缘卡墙。

进入条件：

- `CommittingTeleport` 成功。

行为：

- 记录 exit portal。
- collision bridge 可对出口门洞附近做严格受限的防卡辅助。
- view bridge 可使用短时视觉补偿。
- 不允许同一 portal pair 立即反向 teleport。

退出条件：

- 玩家离开出口 aperture/plane 影响区。
- cooldown 到期且玩家不再 intersecting。
- 门失效或玩家死亡。

### 5.6 Cooldown

含义：防止立即 ping-pong 或重复提交。

行为：

- 阻止同一对门的重复 teleport。
- 不阻止 debug probe。
- 冷却必须与 exit state 协同，不应只靠固定时间。

---

## 6. 模块职责

### 6.1 `PortalTransform`

职责：

- 构造 entry-to-exit matrix。
- 转换 point。
- 转换 vector/velocity。
- 转换 angles。
- 转换 ray。
- 执行 portal local projection。
- 判断 point/hull 是否在 aperture 内。

约束：

- 不访问游戏实体。
- 不做 teleport。
- 不管理状态机。
- 尽量可测试。

### 6.2 `PortalTransitionSimulator`

职责：

- 管理本地玩家穿门生命周期。
- 读取 portal pair 状态。
- 生成 `PortalTransitionContext`。
- 判断 phase transition。
- 管理 cooldown / exiting。
- 调用 safe exit 查询。
- 只在提交阶段调用 teleport backend。
- 向 collision/view/debug 暴露只读状态。

### 6.3 `PortalCollisionBridge`

职责：

- 封装 `TracePlayerBBox` / hull trace 的门洞豁免。
- 根据 simulator phase 和 aperture 几何做选择性 bypass。
- 保持门洞外原始碰撞。

禁止：

- 不直接 teleport。
- 不直接改 view angles。
- 不直接改真实 player origin。

### 6.4 `PortalSafeExitPlacement`

职责：

- 输入 entry/exit transform 和 player anchor。
- 生成出口候选点。
- 检查 origin/eye world 合法性。
- 检查 hull 可容纳性。
- 检查出口门前/后偏移。
- 返回明确 failure reason。

### 6.5 `PortalViewBridge`

职责：

- 根据 simulator phase 处理视觉连续性。
- teleport 后短时间 view compensation。
- 后续扩展 stencil/clip/render proxy。

约束：

- 不依赖非法 player origin。
- 不直接提交物理移动。

### 6.6 `PortalDebugOverlay`

职责：

- 输出 debug log。
- 后续可绘制 portal plane、aperture、hull、candidate、phase。
- 提供验收用的稳定诊断信息。

---

## 7. 行为需求

### 7.1 接近门

当玩家靠近 active linked portal 并朝门内移动时：

- simulator 应进入 `ApproachingPortal`。
- 日志应包含 entry side、exit side、eye distance、origin distance、inside aperture、cmdDot、velDot。
- 玩家不应被立刻 teleport。

### 7.2 进入门洞交互

当玩家 hull 与门洞 aperture 发生有效交互时：

- simulator 应进入 `IntersectingPortal`。
- collision bridge 应允许玩家在门洞区域继续向内推进。
- 门洞外墙体碰撞不应被豁免。
- 玩家真实 origin 不应进入非法 world。

### 7.3 完成穿越

当 crossing 条件成立且出口点安全时：

- simulator 应进入 `CommittingTeleport`。
- 一次性提交 origin、angles、velocity。
- teleport 后进入 `ExitingPortal`。
- 日志应包含 old/new origin、old/new eye、old/new velocity、old/new angles、exit safety result。

### 7.4 出口退出

当玩家离开出口门影响区时：

- simulator 应回到 `Idle` 或 `Cooldown` 结束态。
- collision bridge 不再豁免 trace。
- view compensation 过期。
- 玩家不应被出口门反向瞬间传回。

### 7.5 出口不安全

当出口候选点不安全时：

- 不提交 teleport。
- 日志应明确 failure reason。
- 玩家状态可以保持 `IntersectingPortal` 或退出，但不得被塞入墙体。

---

## 8. 验收标准

### 8.1 文档验收

- 存在 `docs/feature/portal-perfect-replica-spec.md`。
- 存在 `docs/feature/portal-perfect-replica-implementation-plan.md`。
- 文档明确禁止真实 origin 进入非法 world。
- 文档明确 MoveData 不是主控制器。
- 文档明确基础穿越稳定优先于半嵌入视觉。

### 8.2 Phase 1 验收

- 编译通过。
- simulator 可进入 `ApproachingPortal`。
- simulator 可进入 `IntersectingPortal`。
- simulator 可退出回 `Idle`。
- 不改变玩家位置。
- 不提交 teleport。

### 8.3 Phase 2 验收

- 门洞区域允许玩家继续向门内推进。
- 门洞外碰撞不受影响。
- trace bypass 日志可说明 side、phase、aperture hit、fraction。
- 不直接修改真实 player origin。

### 8.4 Phase 3 验收

- `FindSafeExitPlacement` 拒绝 `outsideOrigin=true`。
- origin、eye、hull 检查结果都有日志。
- 出口不安全时不提交 teleport。

### 8.5 Phase 4 验收

- 玩家可稳定从蓝门穿到橙门，从橙门穿到蓝门。
- 速度方向转换合理。
- 视角方向转换合理。
- 不出现视角先变、位置后变。
- 不出现出口黑天。
- 不出现左右画面可见性分裂。
- 不出现立即 ping-pong。

### 8.6 Phase 5 验收

- teleport 当帧视觉补偿可开关。
- 补偿不会把相机拉入墙体。
- 穿越瞬间不黑屏、不闪 skybox、不左右分裂。
- 视觉层失败时可以禁用，不影响基础物理穿越。

---

## 9. 调试日志要求

至少保留以下日志类别：

- `PortalReadiness`
- `DistanceProbe`
- `TransitionPhase`
- `ApertureProbe`
- `CollisionBridge`
- `SafeExitPlacement`
- `TeleportCommit`
- `VisualTransition`
- `FailureReason`

日志必须能区分：

- 入口门和出口门。
- 当前 phase。
- 玩家 origin/eye/center。
- signed depth。
- inside aperture。
- origin/eye 是否 outside world。
- hull trace 是否 blocked。
- teleport 是否被提交或拒绝。

---

## 10. 成功定义

本阶段成功不是“立刻做到官方 Portal 的完整半身穿越视觉”，而是：

1. 建立一个集中、可验证、可扩展的穿门状态机。
2. 证明本地玩家基础穿越可以在不破坏 Source 可见性假设的情况下稳定完成。
3. 彻底停止依赖真实 origin 非法嵌入墙体的路线。
4. 为后续视觉连续性、玩家 clone/proxy、物理实体穿越留下正确接口。
