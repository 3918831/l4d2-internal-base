# Portal 完整复刻路线技术交接文档

**创建日期:** 2026-06-05
**当前分支:** `codex/portal-perfect-replica`
**上游实验分支:** `codex-local-portal-traversal`
**阶段结论:** 停止继续叠加当前“本地玩家半嵌入 + MoveData 补传送”方案，转向完整复刻式 `PortalTransitionSimulator` 设计。

---

## 1. 背景与目标重新对齐

当前工程已经在 L4D2 中实现了可用的传送门视觉渲染：传送门贴图、递归视角、基本遮罩效果都已经可以支撑“看到另一侧世界”的体验。下一阶段原本尝试做本地玩家穿门，目标包括：

- 本地玩家可以从一扇门进入另一扇门出来。
- 保留玩家速度方向和视角方向。
- 支持靠近墙面传送门时，玩家身体可以部分进入门洞，而不是一碰门就瞬移。
- 第一阶段不考虑多人、网络同步、极限边界、其他实体，只处理本地玩家。

参考代码是 `Reference Code/portal-sdk-2013`。官方 Portal 的核心思路不是“玩家碰到平面后瞬移”，而是用一套跨门模拟器维护实体和传送门之间的过渡状态。玩家可以在门洞中处于“跨两个空间”的状态：一部分身体在入口侧，一部分身体在出口侧，碰撞、视图、触发条件都围绕这个状态处理。

---

## 2. 当前实验路线做过什么

### 2.1 第一版：中心点跨平面触发

最初实现了 `PortalTransform` 和 `PortalTransition`：

- `PortalTransform` 负责入口门到出口门的矩阵变换。
- `PortalTransition` 在 `CreateMove` 中检测本地玩家是否跨过门平面。
- 传送时调用玩家 `Teleport`，并同步 `EngineClient->SetViewAngles` 和 `m_vecVelocity`。
- `TracePlayerBBox` 中尝试对门洞区域的玩家 hull trace 做豁免。

实际测试结果：

- 门状态、玩家距离、门洞投影都能正确打印。
- 玩家靠近门时，`inside=true`，说明门洞投影判断有效。
- 但玩家中心点会被墙体碰撞挡在门前，距离稳定停在大约 `15.5` 单位，无法跨到 `<= 0`。

结论：

中心点跨平面触发只适合“点实体”或无碰撞场景，不适合玩家 hull。真实玩家需要以碰撞盒前缘、眼点、移动方向和门洞区域共同判断。

### 2.2 第二版：触碰式穿门

为了绕过中心点无法进墙的问题，我们加入了“触碰式穿门”条件：

- 玩家中心或眼点在门洞投影内。
- 玩家距离门平面小于阈值。
- 当前输入方向或速度方向朝向门内。

效果：

- 可以在持续按住前进键后触发传送。
- 速度和视角大体可以保留。

问题：

- 玩家不能真正半嵌入墙面。
- 仍然会有闪烁帧。
- 初始触发需要持续按键一段时间，体验不像官方 Portal。

结论：

触碰式穿门可以作为“基础瞬移版”的兜底，但不是完整复刻路线。

### 2.3 第三版：MoveData 虚拟嵌入与预测传送

随后尝试在 `ClientPrediction::FinishMove` 中接入：

- 在原始 `FinishMove` 前调用 `OnPreFinishMove`。
- 使用 `CMoveData` 的 origin、velocity、view angles 来预测玩家在本帧的移动。
- 如果玩家持续向门内移动，则虚拟推进 `anchor.origin/eye/center`。
- 当虚拟中心点跨平面后，使用入口到出口矩阵计算出口位置。
- 写回 `move->SetAbsOrigin`、`move->m_vecVelocity`、`move->m_vecAngles` 等字段。

这一版让“持续前进后可以穿越”的成功率提高，也让日志显示出更接近真实穿越过程的数据：

- `MoveData Crossing probe`
- `Crossing confirmed`
- `Applied predicted portal crossing`

问题：

- 虚拟嵌入没有同步到真实实体时，视觉上仍然不能半嵌入。
- 同步到真实实体后，玩家 origin 会被推向墙体或非法 world leaf。
- 传送时有的系统看到新角度，有的系统看到旧位置，导致一两帧视觉错位。

结论：

MoveData 可用于读取预测状态和调试，但不应作为完整复刻的主架构。直接在 prediction 中分阶段改实体位置、视角、速度，时序很难稳定。

### 2.4 第四版：实体同步嵌入与延迟视角

为了修复“位置不嵌入”和“视角先变位置后变”的问题，又尝试了：

- 打开 `kSyncMoveDataEmbeddingToServer`，把虚拟嵌入同步到玩家实体。
- 穿越时 `EntityTeleport` 只传 origin 和 velocity，不传 angles。
- 把 `SetViewAngles` 延迟到原始 `FinishMove` 后执行。

效果：

- 玩家可以穿越。
- 半嵌入感有所增强。
- 原先那种“视角先转，位置后变”的问题有所缓解。

新问题：

- 出现左右画面不一致、天空盒黑掉、画面像被切成两套可见性结果。
- 日志显示：`outsideOrigin=true outsideEye=false`。

这个日志非常关键：出口落点中玩家 eye 还在合法世界内，但玩家实体 origin 已经被 `EngineTrace->PointOutsideWorld` 判定为非法或 world 外。Source 引擎的 PVS、天空盒、实体渲染、武器渲染、预测位置可能在不同系统中使用不同参考点，于是出现同一帧画面左右不一致。

结论：

直接把玩家真实实体塞进墙体或非法 leaf 附近，会破坏 Source 引擎的可见性和渲染假设。这不是简单延迟视角或补一次 Teleport 能解决的问题。

---

## 3. 为什么当前方案不可继续

当前方案的核心是：

> 让本地玩家实体真实进入墙体附近，再通过 MoveData/Teleport 把它推到出口。

这个方向有几个结构性问题。

### 3.1 玩家 origin 不能安全进入墙体或非法 leaf

官方 Portal 可以让玩家“看起来”半个身体进入门洞，但并不是简单让普通 Source 玩家 hull 穿进实体墙。它有专门的传送门模拟、碰撞特殊处理、空间转换和实体管理。

我们的实验中，一旦真实 origin 被推入墙附近，就会出现：

- `PointOutsideWorld(origin) == true`
- PVS 或可见性叶子异常
- 天空盒缺失
- 左右画面渲染结果不一致
- 武器/视图模型和世界画面不同步

这说明引擎内部已经不再把玩家当作正常处于一个稳定空间里的实体。

### 3.2 Prediction、movement、render view 的时序难以完全原子化

我们尝试过在多个时机改状态：

- `CreateMove`
- `TracePlayerBBox`
- `FinishMove` 前
- `FinishMove` 后
- `RenderView`

每个系统都有自己的输入状态和缓存结果。只要 origin、velocity、view angles 不是在同一个引擎认可的点原子提交，就可能出现：

- 本帧 movement 已变，render view 未变。
- 本帧 view angles 已变，origin 未变。
- 本帧 camera 在出口，玩家 origin 还在入口或墙体内。
- 某个 render 阶段用 old PVS，另一个阶段用 new view。

这类问题越补越细，不适合继续靠局部 patch 修。

### 3.3 当前方案缺少“跨门状态”的一等建模

官方 Portal 中，实体穿门不是一个瞬时函数调用，而是一个持续状态：

- 实体是否正在穿越。
- 入口门和出口门是哪一对。
- 实体当前哪些点在入口侧，哪些点在出口侧。
- 是否需要碰撞裁剪。
- 何时提交最终 Teleport。
- 是否要为渲染提供特殊视角或裁剪信息。

当前方案只是在多个 hook 里用布尔状态和距离阈值拼接行为，没有形成真正的 simulator。继续扩展会越来越像分散补丁，而不是可验证系统。

---

## 4. 哪些资产仍然值得保留

虽然当前方案不能继续作为正式路线，但实验中有价值的资产应该保留。

### 4.1 值得保留

- `PortalTransform`
  - 入口到出口矩阵。
  - 点、向量、角度转换。
  - 门洞局部投影。

- 传送门状态读取
  - `PortalInfo_t` 中的 origin、normal、angles、active、animState。
  - 蓝门/橙门配对逻辑。

- 已补齐的 SDK 接口
  - `CMoveData` 布局验证经验。
  - server entity / edict / server unknown 相关接口。
  - `EngineTrace->PointOutsideWorld` 作为安全诊断信号。

- 日志经验
  - `Distance probe`
  - `Trace probe`
  - `MoveData Crossing probe`
  - `outsideOrigin/outsideEye`

- 文档
  - `docs/server-side-portal-teleport-plan.md`
  - `docs/portal-visual-continuity-plan.md`
  - `docs/portal-visual-continuity-plan.zh-CN.md`

### 4.2 不建议继续保留为正式实现

- 将真实玩家实体同步推进墙体的逻辑。
- 在 `FinishMove` 前直接提交跨门 Teleport 的逻辑。
- 依赖 `outsideEye=false` 而忽略 `outsideOrigin=true` 的出口落点选择。
- 通过延迟 `SetViewAngles` 来补救非原子传送的路线。
- 把 `TracePlayerBBox` 简单改成命中门洞就 `fraction=1` 的长期方案。

这些可以作为失败经验保留在实验分支，但不应进入完整复刻主线。

---

## 5. 完整复刻路线的基本思路

下一阶段应该设计一个类似官方 `CPortalSimulator` 的系统。暂定名称：

`CPortalTransitionSimulator`

它不是单个 teleport helper，而是一个集中管理穿门生命周期的模块。

### 5.1 核心设计原则

1. **真实玩家实体尽量保持在合法世界位置**

   不再把玩家 origin 强行塞进墙内。半嵌入效果应通过 simulator 状态、碰撞豁免、渲染裁剪和安全提交点实现，而不是让 Source 普通玩家实体进入非法 leaf。

2. **跨门状态集中管理**

   所有穿门判断、入口/出口配对、进门深度、提交 Teleport、冷却、退出状态都由 simulator 统一维护。

3. **碰撞和渲染分离**

   碰撞层负责让玩家在门洞区域不会被墙挡住。

   渲染层负责在玩家接近或穿越门洞时，提供正确的可见性、视角补偿或裁剪。不能指望修改实体 origin 自动产生正确视觉。

4. **只在安全点提交真实 Teleport**

   当 simulator 判断玩家已经越过传送门有效分界，且出口落点合法时，再一次性提交真实 teleport。

5. **落点必须同时满足 origin 与 eye 合法**

   `outsideOrigin=true` 的候选点不能再进入正式路径。出口点必须通过 origin、eye、hull trace、PointOutsideWorld 等验证。

### 5.2 推荐模块划分

建议新增或重构为：

```text
Portal/
├── PortalTransform.h/cpp
├── PortalTransitionSimulator.h/cpp
├── PortalCollisionBridge.h/cpp
├── PortalViewBridge.h/cpp
└── PortalDebugOverlay.h/cpp
```

#### PortalTransform

职责：

- 提供纯数学变换。
- 不访问游戏实体。
- 不做 teleport。
- 可被单元测试或小型本地测试验证。

#### PortalTransitionSimulator

职责：

- 维护本地玩家穿门状态机。
- 管理当前 active transition。
- 计算入口门、出口门、进门深度、退出条件。
- 决定何时提交真实 teleport。
- 向 collision/render bridge 暴露只读状态。

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

建议 transition 数据：

```cpp
struct PortalTransitionContext
{
    PortalSide entrySide;
    PortalSide exitSide;
    Vector entryOrigin;
    Vector entryNormal;
    Vector exitOrigin;
    Vector exitNormal;
    Vector playerOriginAtStart;
    Vector playerEyeAtStart;
    Vector entryVelocity;
    float enterTime;
    float signedDepth;
    bool hasValidExitPlacement;
};
```

#### PortalCollisionBridge

职责：

- 只处理 `TracePlayerBBox`、hull trace、玩家门洞碰撞豁免。
- 不直接 teleport。
- 不直接修改 view angles。
- 从 simulator 查询当前玩家是否处于门洞 transition。
- 只在门洞区域对墙体 trace 做选择性豁免。

关键原则：

- 豁免应基于 hull 与 portal aperture 的几何关系，而不是单点。
- 门洞外仍然保持原始碰撞。
- 不要无限制把所有靠近门的 trace 都设成 `fraction=1`。

#### PortalViewBridge

职责：

- 处理穿门期间的视觉连续性。
- 在必要时对 `CViewSetup` 做受控调整。
- 后续可引入 portal stencil 裁剪或双视图拼接。

关键原则：

- 不依赖非法 player origin 产生视觉效果。
- 如果要显示“半边世界”，应显式渲染，而不是让引擎误判 PVS。

#### PortalDebugOverlay

职责：

- 打印和绘制调试信息。
- 显示 portal plane、aperture、player hull、transition phase、exit candidate。
- 为游戏内验收服务。

---

## 6. 下一阶段推荐实施计划

### Phase 0: 新规格与范围控制

目标：

写一份新的 SDD 规格，明确完整复刻路线第一阶段只做本地玩家，且不追求一次完成官方所有细节。

产物：

- `docs/feature/portal-perfect-replica-spec.md`
- `docs/feature/portal-perfect-replica-implementation-plan.md`

验收：

- 明确“不再把真实 origin 推入非法 world”。
- 明确“基础穿越稳定优先，半嵌入视觉作为 simulator 管理下的能力”。

### Phase 1: 建立 simulator 骨架

目标：

新增 `CPortalTransitionSimulator`，只做状态机和调试输出，不改变玩家位置。

验收：

- 能进入 `ApproachingPortal`。
- 能进入 `IntersectingPortal`。
- 能退出回 `Idle`。
- 日志能说明 entry/exit/depth/inside aperture。

### Phase 2: 重写碰撞桥接

目标：

将当前 `TracePlayerBBox` 的门洞豁免逻辑从 `PortalTransition` 中剥离，改为 `PortalCollisionBridge`。

验收：

- 门洞区域可以允许玩家继续向门内推进。
- 门洞外碰撞不受影响。
- 不直接修改真实 player origin。

### Phase 3: 安全出口候选点系统

目标：

建立 `FindSafeExitPlacement`：

- 转换 eye 和 origin。
- 检查 origin 和 eye 是否 world 内。
- 检查 hull trace。
- 检查出口门前后两个候选方向。
- 禁止 `outsideOrigin=true` 的候选点。

验收：

- 出口点不再触发黑天/PVS 异常。
- 如果出口点不安全，不提交 teleport，只保持 transition 或退出。

### Phase 4: 原子 teleport 提交

目标：

只在 simulator 的 `CommittingTeleport` 阶段一次性提交：

- origin
- angles
- velocity
- cooldown
- transition exit state

验收：

- 不出现视角先变位置后变。
- 不出现出口黑天。
- 不出现左右画面不一致。

### Phase 5: 视觉连续性

目标：

在基础穿越稳定后，再处理接近官方的半嵌入视觉。

候选路线：

- RenderView 中基于 simulator phase 做短时 view compensation。
- 门洞区域 stencil 裁剪。
- 必要时将入口侧/出口侧画面显式组合，而不是依赖非法 origin。

验收：

- 玩家贴近门时不穿帮。
- 穿越瞬间不黑屏、不闪 skybox、不左右分裂。

---

## 7. 下个会话启动建议

下个会话建议从这几个动作开始：

1. 确认当前分支：

   ```powershell
   git branch --show-current
   ```

   应为：

   ```text
   codex/portal-perfect-replica
   ```

2. 阅读本文档：

   ```text
   docs/feature/portal-perfect-replica-transition-plan.md
   ```

3. 阅读实验结论相关文档：

   ```text
   docs/server-side-portal-teleport-plan.md
   docs/portal-visual-continuity-plan.zh-CN.md
   docs/portal-sdk-2013-technical-report.md
   ```

4. 不要继续从最后一轮未提交实验补丁开始，因为那些补丁已经回退。

5. 新建 SDD 规格文档，再开始写 `CPortalTransitionSimulator`。

---

## 8. 最终判断

当前实验路线证明了几件事：

- 本地玩家基础穿越在技术上可行。
- Portal transform、速度/视角转换可行。
- Source/L4D2 中直接同步玩家实体进入墙体或非法 leaf，会导致可见性和天空盒异常。
- MoveData 是有用的观测点，但不适合作为完整复刻主控制器。
- 完整复刻需要从“触发 teleport”升级为“模拟穿门过程”。

所以，下一阶段应从完整 `PortalTransitionSimulator` 重新设计，而不是继续修补当前 `PortalTransition`。
