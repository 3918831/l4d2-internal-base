# GMod PortalGun 参考实现理解记录

**创建日期:** 2026-06-05
**参考路径:** `Reference Code/PortalGun_Gmod_gmpublisher`
**目标:** 在开始 L4D2 完整复刻《Portal》物理穿越逻辑前，确认参考实现的关键机制、可移植资产和需要改写的地方。

---

## 1. 我对参考方案的总体理解

`PortalGun_Gmod_gmpublisher` 不是官方 Portal SDK 的 C++ 实现，而是一个 Garry's Mod Lua 版本的 Portal Gun。它的价值不在于 API 可以直接复制，而在于它把 Source 引擎里最难的穿门问题拆成了几层：

1. 用 `prop_portal` trigger 识别玩家或实体进入门洞。
2. 用入口门到出口门的局部坐标变换重映射位置、角度和速度。
3. 对玩家临时启用 `MOVETYPE_NOCLIP`，再用 `Move` hook 把 noclip 严格限制在门洞范围内。
4. 对物理物体和玩家创建出口侧克隆体，制造半穿越视觉。
5. 在客户端对真实实体和克隆体启用 render clip plane，只显示应该露出的半边。
6. 用 render target、stencil、custom clip plane 渲染门另一侧画面。
7. 通过清理逻辑避免残留 `InPortal`、残留 noclip、残留克隆体。

这套方案的核心不是“触碰后瞬移”，而是：

```text
进入门洞
  -> 进入 InPortal 状态
  -> 短时间由传送门系统接管移动/碰撞/视觉
  -> 跨过门平面时提交一次真实位置/视角/速度变换
  -> 继续处于出口门 InPortal 状态
  -> 离开出口门范围后恢复普通移动
```

这和我们下一阶段要做的 `CPortalTransitionSimulator` 是同一个方向：把穿门作为持续生命周期建模，而不是把 teleport 当成一个孤立函数。

---

## 2. 参考实现的关键文件

### 2.1 技术文档

- `PORTAL_PHYSICS_TRAVERSAL_TECHNICAL_DESIGN.md`
  - 总结了完整穿越方案：trigger、临时 noclip、自定义移动、坐标变换、克隆体、裁剪平面、portal view。

- `PORTAL_TRAVERSAL_CORE_ONLY.md`
  - 抽出了最小核心路径：玩家 noclip + 自定义移动，物理实体直接重映射。

### 2.2 传送门实体

- `lua/entities/prop_portal/shared.lua`
  - 定义门类型、链接状态、坐标/角度/速度变换、门洞 bounds 检查、地板门 offset、门朝向判断。

- `lua/entities/prop_portal/init.lua`
  - 服务端核心逻辑：创建门、链接门、`StartTouch`、`Touch`、`EndTouch`、`DoPort`、玩家进入门、克隆体创建、子弹穿门、清理。

- `lua/entities/prop_portal/cl_init.lua`
  - 客户端渲染逻辑：portal render target、stencil 绘制、custom clip plane、进入门后的 render clip plane。

### 2.3 玩家移动

- `lua/autorun/portalmove.lua`
  - `CreateMove` 改写玩家输入。
  - `Move` hook 中的 `ipMove` 是玩家半穿越移动控制核心。
  - 扩展了 `Vector:PlaneDistance` 和 `util.ClosestPointInOBB`。

### 2.4 玩家克隆体

- `lua/entities/portalplayerclone.lua`
  - 出口侧玩家视觉克隆。
  - 每帧用入口到出口的 portal transform 同步位置、角度、骨骼。
  - 绘制时启用 clip plane，避免完整双份玩家穿帮。

### 2.5 武器与规则边界

- `lua/weapons/weapon_portalgun/shared.lua`
  - 负责发射传送门、验证门面位置、创建或移动门、portal projectile、拾取物体等。

- `lua/entities/projectile_portal_ball/shared.lua`
  - 传送门弹丸实体，主要是视觉/物理飞行载体。

- `lua/entities/trigger_portal_cleanser/*`
  - 清除门、销毁弹丸和物体。

- `lua/entities/func_noportal/*`
  - no-portal 区域规则。

---

## 3. 坐标变换是核心资产

参考实现里最值得直接保留思想的是三类 transform。

### 3.1 位置变换

`GetPortalPosOffsets(portal, ent)` 的逻辑是：

```text
world position
  -> entry.WorldToLocal
  -> local.x/local.y mirror
  -> exit.LocalToWorld
```

玩家使用 `EyePos`/`GetHeadPos` 作为映射点，再通过 `SetHeadPos` 换算回脚底 origin。普通物体直接映射实体 origin。

对 L4D2 的启发：

- `PortalTransform` 应保持纯数学模块。
- 玩家不能只用 origin 判断，至少需要 eye、origin、hull leading edge 三套参考点。
- 出口位置不能只看 eye 合法，必须同时验证 origin/eye/hull。

### 3.2 角度变换

参考实现先按入口门平面反射 forward/up，再转入口局部角，修正 yaw/roll，最后转出口世界角。

对 L4D2 的启发：

- 欧拉角版本可作为参考，但正式实现应优先用矩阵或 basis 变换。
- 我们需要统一公式：`exit * mirror * inverse(entry)`。
- 视角变换必须和 origin、velocity 在同一个提交阶段完成，避免旧实验中“视角先变、位置后变”。

### 3.3 速度变换

`TransformOffset(v, a1, a2)` 把速度投影到入口门 right/up/forward，再用出口门 basis 重建，最后乘 `-1` 让实体从出口门向外飞出。

对 L4D2 的启发：

- 动量连续性依赖 basis 变换，不能简单复制速度。
- 地板门/斜门可能需要最低出门速度兜底，但这应是 simulator 的规则，而不是散落在 teleport helper 里。

---

## 4. 玩家穿门路径的关键实现

GMod 玩家路径可以概括为：

```text
StartTouch
  -> PlayerWithinBounds
  -> PlayerEnterPortal
  -> SetMoveType(MOVETYPE_NOCLIP)
  -> Move hook: ipMove
  -> Touch/EndTouch: DoPort
  -> SetHeadPos + SetEyeAngles + SetLocalVelocity
  -> portal:PlayerEnterPortal(player)
  -> 出口门继续 NOCLIP + clamp
  -> 离开门洞后恢复 MOVETYPE_WALK
```

最关键的点有三个。

### 4.1 NOCLIP 只是碰撞桥，不是最终方案本体

GMod 方案不是让玩家自由 noclip，而是只在 `ply.InPortal` 有效时启用。`ipMove` 每帧把玩家下一位置转到门局部坐标，然后 clamp：

```text
local.y in [-20, 20]
local.z in [-44, 44] 或地板门特化范围
frontDist < 16 时允许继续
frontDist 超过阈值则恢复 WALK
```

对 L4D2 的启发：

- 我们不能简单照搬 `MOVETYPE_NOCLIP`，因为 L4D2 内部 movement、prediction、PVS 更敏感。
- 但这个角色可以由 `PortalCollisionBridge` 承担：只在门洞 aperture 内选择性豁免 hull trace/墙体阻挡。
- simulator 应明确区分：
  - `IntersectingPortal`: 碰撞层允许继续向门洞推进。
  - `ExitingPortal`: 已经从出口出来，但还没完全离开出口门范围。

### 4.2 玩家传送后不立即退出门状态

`DoPort` 中玩家跨过入口平面后，设置新位置、速度、视角，然后调用：

```lua
portal:PlayerEnterPortal(ent)
```

也就是让玩家进入出口门的 `InPortal` 状态，而不是立刻恢复 walk。

对 L4D2 的启发：

- 我们的状态机需要 `ExitingPortal`，不能 teleport 后直接回 `Idle`。
- 冷却也不应只是时间布尔值，而应和“是否已离开出口 aperture / plane 附近”绑定。
- 这可以避免刚出门又立刻被出口门反向触发造成抖动。

### 4.3 玩家范围判断使用脚、头、近似 OBB

`PlayerWithinBounds` 不只看一个点：

- 墙面门看玩家脚底 local z/y 和 head local z/y。
- 地板/天花板门使用 `ClosestPointInOBB` 来估计 hull 前缘到门平面的距离。
- `frontDist > 17` 时拒绝进入。

对 L4D2 的启发：

- 我们之前中心点跨平面失败是预期结果。
- 新实现的 `PortalTransitionSimulator` 应保存多个采样点：
  - player origin
  - eye
  - hull leading point
  - projected aperture footprint
- `signedDepth` 应来自 hull/aperture 几何，而不是只来自 origin 到 plane 的距离。

---

## 5. 半穿越视觉的关键实现

参考实现的视觉连续性靠两件事叠加：

1. 入口侧真实实体启用 clip plane，只显示还在入口侧的半边。
2. 出口侧克隆体同步 transform，也启用 clip plane，只显示已经穿到出口侧的半边。

玩家克隆体 `PortalPlayerClone` 额外复制骨骼：

```text
for each bone:
  bone position entry.WorldToLocal
  mirror local x/y
  exit.LocalToWorld
  bone angle entry.WorldToLocalAngles
  mirror yaw/roll
  exit.LocalToWorldAngles
```

对 L4D2 的启发：

- 第一阶段不必立即做完整玩家克隆体，但视觉连续性的正解不是把真实 origin 塞进墙里。
- `PortalViewBridge` 后续应显式处理半边世界/半边身体：
  - portal plane clip
  - stencil aperture
  - 必要时 draw clone 或 render-only proxy
- 当前文档中的“真实玩家实体尽量保持合法世界位置”是正确红线。

---

## 6. Portal view 渲染关键点

`prop_portal/cl_init.lua` 的 portal view 流程是：

```text
RenderScene
  -> for each portal in front of viewer
  -> RenderPortal(origin, angles)
  -> 将当前相机通过入口门映射到出口门
  -> render.RenderView 到 render target
  -> DrawPortal 用 stencil 只在门模型区域画 render target
  -> PushCustomClipPlane 裁掉出口门背后的错误几何
```

对 L4D2 的启发：

- 我们已有传送门视觉渲染基础，这个参考主要确认：portal view 和实体穿越视觉应共享同一套 `PortalTransform`。
- portal view 不能依赖玩家真实 origin 非法移动来触发 PVS。
- 如果穿越期间需要双空间视觉，应由 `PortalViewBridge` 显式构造 view/clip/stencil。

---

## 7. 物理实体路径可作为后续扩展，不是本阶段主线

参考实现对物理物体做了：

- `CanPort` 过滤不可穿越对象。
- 进入门时 `ent.InPortal = self`。
- 添加 ballsocket 约束稳定物体。
- 创建无碰撞、无重力、无拖拽克隆体。
- 每帧同步克隆体到出口侧。
- 跨平面时把本体移动到 clone 位置，转换角度和速度。

本次 L4D2 范围是单人/localserver/本地玩家，所以物理实体不进入第一阶段。但它给了我们后续扩展的模型：

```text
real entity = authority
clone/proxy = visual continuity
teleport commit = one-time authoritative transform
```

---

## 8. 和当前 L4D2 技术路线的对齐

### 8.1 可直接吸收的思想

- 穿门是持续状态，不是瞬时触发。
- 入口/出口 transform 必须成为纯数学公共模块。
- 玩家进入门洞后要有专门的移动/碰撞桥接层。
- teleport 后要进入出口门退出阶段，而不是立即结束。
- 视觉连续性依赖显式 clip/stencil/clone/proxy，而不是非法 origin。
- 所有异常路径都必须恢复状态：门删除、门失效、玩家死亡、退出门洞、出口不安全。

### 8.2 不能直接照搬的地方

- GMod 的 `MOVETYPE_NOCLIP` 方案不能原样移植到 L4D2 正式路线。
  - 我们已经验证过真实玩家 origin 进入非法 leaf 会破坏 PVS/天空盒/渲染。
  - L4D2 中应改成 `PortalCollisionBridge` 的门洞选择性 hull trace 豁免。

- GMod 的 `SetHeadPos` 直接改玩家位置不能原样照搬。
  - L4D2 必须先经过 `FindSafeExitPlacement`。
  - 候选点必须同时满足 origin、eye、hull、`PointOutsideWorld`。

- GMod 的 Lua usermessage/network 状态不适合我们。
  - 本阶段只做 local player，可以先用本地 singleton/只读状态暴露。
  - 后续如扩多人，再单独设计网络状态。

- GMod 的物理实体和克隆体不是第一阶段任务。
  - 但玩家视觉 clone/proxy 可以作为 Phase 5 的候选路线。

---

## 9. 我理解的 L4D2 下一步关键实现

结合参考实现和当前实验结论，我认为下一阶段应这样落地。

### 9.1 Phase 0: 先写 SDD 和 implementation plan

需要明确：

- 第一阶段只做本地玩家。
- 不把真实玩家 origin 推进非法 world。
- 基础穿越稳定优先于半嵌入视觉。
- MoveData 作为观测点，不作为主控制器。
- 所有穿门状态由 `CPortalTransitionSimulator` 管理。

### 9.2 Phase 1: `CPortalTransitionSimulator`

职责：

- 读取当前蓝/橙门状态。
- 判断 local player 是否进入门洞有效区。
- 维护 transition phase。
- 计算 `signedDepth`、entry/exit side、exit candidate。
- 暴露只读状态给 collision/render/debug。
- 决定是否进入 `CommittingTeleport`。

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

### 9.3 Phase 2: `PortalCollisionBridge`

职责：

- 接管现有 `TracePlayerBBox` 中分散的门洞豁免。
- 只在 simulator 认为玩家处于 `ApproachingPortal` 或 `IntersectingPortal` 时生效。
- 基于 hull 与 aperture 的几何关系豁免，而不是简单 `inside == true` 就 `fraction = 1`。
- 不提交 teleport，不改 view angles，不改真实 origin。

### 9.4 Phase 3: `FindSafeExitPlacement`

这是吸收 GMod `GetFloorOffset` 思想后的 L4D2 安全版。

必须检查：

- transformed eye 是否 world 内。
- transformed origin 是否 world 内。
- player hull 是否能放下。
- 出口门前/后候选偏移是否更安全。
- `PointOutsideWorld(origin)` 为 true 时直接拒绝。
- 需要日志明确打印 `outsideOrigin/outsideEye/hullBlocked`。

### 9.5 Phase 4: 原子 teleport

只允许在 simulator 的 `CommittingTeleport` 执行一次：

```text
safe origin
safe eye/view angles
transformed velocity
transition -> ExitingPortal
cooldown token
```

提交点必须尽量集中，避免旧实验中的 prediction/render 分阶段看到不同状态。

### 9.6 Phase 5: `PortalViewBridge`

基础穿越稳定后，再做视觉连续性：

- 基于 simulator phase 做 view compensation。
- 使用 stencil/clip plane 显式表现门洞穿越。
- 需要时添加玩家 render proxy，而不是让真实 origin 进入墙体。

---

## 10. 最重要的技术判断

GMod 参考实现给我的最大确认是：

> 真正的传送门穿越不是把世界物理连通，而是在短时间内由传送门系统接管实体的移动、碰撞和视觉，再在安全时机提交一次权威 transform。

因此，在 L4D2 中我们不应该继续补当前 `PortalTransition` 的瞬移/MoveData 方案，而应该从 `CPortalTransitionSimulator` 开始，把 GMod 方案中的 `InPortal`、`Move hook clamp`、`DoPort`、`PortalPlayerClone`、`clip plane` 分别映射为：

```text
GMod InPortal
  -> L4D2 PortalTransitionContext

GMod MOVETYPE_NOCLIP + ipMove clamp
  -> L4D2 PortalCollisionBridge + simulator aperture state

GMod DoPort
  -> L4D2 CommittingTeleport atomic submit

GMod portal:PlayerEnterPortal(exit)
  -> L4D2 ExitingPortal phase

GMod PortalPlayerClone + render clip plane
  -> L4D2 PortalViewBridge / render proxy / stencil clip
```

这就是我理解的当前技术路线：保留 transform 和生命周期思想，放弃“真实 origin 半嵌入非法区域”，用 simulator 把穿门过程显式建模，再逐层接碰撞、落点、安全提交和视觉连续性。
