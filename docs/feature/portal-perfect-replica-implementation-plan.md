# Portal 完整复刻第一阶段实施计划

**创建日期:** 2026-06-05
**对应规格:** `docs/feature/portal-perfect-replica-spec.md`
**范围:** L4D2 单人 / localserver / 本地玩家
**目标:** 从旧 `CPortalTransition` 实验补丁迁移到完整 `CPortalTransitionSimulator` 路线。

---

## 1. 实施原则

本计划按以下原则执行：

1. 先建状态机，再改行为。
2. 先证明观测正确，再允许碰撞桥接。
3. 先证明出口安全，再提交 teleport。
4. 物理穿越稳定后，再做视觉连续性。
5. 每阶段都能编译和游戏内验证。
6. 不继续修补“真实玩家半嵌入墙体 + MoveData 补传送”旧路线。

核心迁移方向：

```text
旧 CPortalTransition
  - 分散状态
  - CreateMove / FinishMove / TracePlayerBBox / RenderView 共同修改行为
  - assisted embedding 推动真实 origin
  - teleport 和视觉补偿耦合

新路线
  - CPortalTransitionSimulator 管理生命周期
  - PortalCollisionBridge 只处理碰撞
  - PortalSafeExitPlacement 只处理出口安全
  - PortalViewBridge 只处理视觉
  - PortalTransform 保持纯数学
```

---

## 2. 当前代码基线

已存在的关键文件：

- `src/Portal/PortalTransform.h`
- `src/Portal/PortalTransform.cpp`
- `src/Portal/PortalTransition.h`
- `src/Portal/PortalTransition.cpp`
- `src/Portal/L4D2_Portal.h`
- `src/Portal/L4D2_Portal.cpp`
- `src/Hooks/ClientMode/ClientMode.cpp`
- `src/Hooks/ClientPrediction/ClientPrediction.cpp`
- `src/Hooks/GameMovement/CCSGameMovement.cpp`
- `src/Hooks/BaseClient/BaseClient.cpp`
- `src/Hooks/RenderView/RenderView.cpp`
- `src/SDK/L4D2/Interfaces/EngineTrace.h`

现有入口点：

- `ClientMode::CreateMove::Detour`
  - 当前调用 `G::G_L4D2Portal.m_PortalTransition.Update(cmd)`。

- `ClientPrediction::FinishMove::Detour`
  - 当前调用 `G::G_L4D2Portal.m_PortalTransition.OnFinishMove(player, ucmd, move)`。

- `CCSGameMovement::TracePlayerBBox::Detour`
  - 当前调用 `ShouldBypassPlayerBBoxTrace(...)`。

- `BaseClient::RenderView::Detour`
  - 当前调用 `ApplyVisualTransition(setup)`。

这些 hook 仍然可作为接入点，但职责需要重新划清。

---

## 3. 新模块目标结构

建议新增或重构为：

```text
src/Portal/
├── PortalTransform.h/cpp
├── PortalTransitionSimulator.h/cpp
├── PortalCollisionBridge.h/cpp
├── PortalSafeExitPlacement.h/cpp
├── PortalViewBridge.h/cpp
└── PortalDebugOverlay.h/cpp
```

第一阶段不一定一次性创建全部文件。如果为了降低风险，可以按阶段先在 `PortalTransitionSimulator` 中内联小结构，等行为稳定后再拆文件。但最终职责边界应保持上述形态。

---

## 4. Phase 0: 文档与范围锁定

### 目标

完成规格与实施计划，作为后续编码验收依据。

### 产物

- `docs/feature/portal-perfect-replica-spec.md`
- `docs/feature/portal-perfect-replica-implementation-plan.md`

### 验收

- 明确只做 local player / localserver。
- 明确禁止真实 origin 进入非法 world。
- 明确 MoveData 不是主控制器。
- 明确 `PortalTransitionSimulator` 是主线。

---

## 5. Phase 1: 建立 `CPortalTransitionSimulator` 骨架

### 目标

新增 simulator，只做状态机、玩家采样和日志，不改变玩家位置，不提交 teleport。

### 预计文件

新增：

- `src/Portal/PortalTransitionSimulator.h`
- `src/Portal/PortalTransitionSimulator.cpp`

修改：

- `src/Portal/L4D2_Portal.h`
- `src/Portal/L4D2_Portal.cpp`
- `src/Hooks/ClientMode/ClientMode.cpp`
- `src/l4d2_base.vcxproj`
- `src/l4d2_base.vcxproj.filters`

可选修改：

- `src/Portal/PortalTransition.h`
- `src/Portal/PortalTransition.cpp`

### 设计任务

1. 定义状态枚举：

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

2. 定义玩家采样：

   ```cpp
   struct PortalPlayerAnchor
   {
       Vector origin;
       Vector eye;
       Vector center;
       Vector feet;
       Vector velocity;
       QAngle viewAngles;
   };
   ```

3. 定义上下文：

   ```cpp
   struct PortalTransitionContext
   {
       PortalTransitionPhase phase;
       PortalSide entrySide;
       PortalSide exitSide;
       float enterTime;
       float lastUpdateTime;
       float signedDepth;
       bool insideAperture;
       bool movingIntoPortal;
       bool hasValidExitPlacement;
   };
   ```

4. 提供只读查询：

   ```cpp
   PortalTransitionPhase GetPhase() const;
   const PortalTransitionContext& GetContext() const;
   bool IsLocalPlayerTransitioning() const;
   bool IsInCollisionBridgePhase() const;
   ```

5. 接入 `CreateMove` 或等价每帧入口：

   - 读取 local player。
   - 读取蓝/橙 portal 状态。
   - 计算 anchor。
   - 对蓝/橙各自计算距离和 aperture。
   - 决定 `Idle -> ApproachingPortal -> IntersectingPortal -> Idle`。

### 禁止

- 不调用 teleport。
- 不调用 `SetViewAngles`。
- 不修改 `CMoveData`。
- 不修改 trace result。
- 不把真实玩家 origin 推向门内。

### 日志

新增低频日志：

```text
[PortalSim] readiness ...
[PortalSim] phase Idle -> ApproachingPortal ...
[PortalSim] phase ApproachingPortal -> IntersectingPortal ...
[PortalSim] probe entry=Blue eyeD=... originD=... inside=... cmdDot=... velDot=...
```

### 验证

命令：

```powershell
& 'D:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' 'src\l4d2_base.sln' /p:Configuration=Debug /p:Platform=x86 /m
```

游戏内：

- 放置两扇门。
- 靠近蓝门，日志进入 `ApproachingPortal`。
- 继续向门内移动，日志进入 `IntersectingPortal`。
- 后退，日志回到 `Idle`。
- 玩家位置不应改变。

---

## 6. Phase 2: 迁移碰撞桥接

### 目标

把当前 `TracePlayerBBox` 的门洞豁免逻辑从旧 `PortalTransition` 剥离，改为受 simulator phase 控制的 `PortalCollisionBridge`。

### 预计文件

新增：

- `src/Portal/PortalCollisionBridge.h`
- `src/Portal/PortalCollisionBridge.cpp`

修改：

- `src/Hooks/GameMovement/CCSGameMovement.cpp`
- `src/Portal/L4D2_Portal.h`
- `src/l4d2_base.vcxproj`
- `src/l4d2_base.vcxproj.filters`

可能修改：

- `src/Portal/PortalTransition.cpp`
  - 删除或禁用旧 `ShouldBypassPlayerBBoxTrace` 中的长期逻辑。

### 设计任务

1. 定义 bridge 输入：

   ```cpp
   struct PortalTraceRequest
   {
       Vector start;
       Vector end;
       unsigned int mask;
       int collisionGroup;
       trace_t* trace;
   };
   ```

2. bridge 查询 simulator：

   - 只有 `ApproachingPortal` / `IntersectingPortal` / `ExitingPortal` 可考虑豁免。
   - `Idle` 和 `Cooldown` 默认不豁免。

3. 几何条件：

   - trace start/end 或 intersection 必须穿过入口/出口 aperture。
   - trace 方向必须与当前 phase 对应。
   - 豁免只作用于门洞区域附近。
   - 门洞外碰撞保持原 trace。

4. 豁免结果：

   - 只在确定 trace 被入口门背后的墙挡住且穿过 aperture 时修改 trace。
   - 不无条件 `fraction = 1`。
   - 日志必须说明为什么豁免。

### 禁止

- 不直接 teleport。
- 不修改 view angles。
- 不同步真实 player origin 到墙内。

### 验证

编译通过后游戏内测试：

- 玩家向门洞推进时不再稳定停在旧实验中的约 `15.5` 单位。
- 玩家从门洞旁边撞墙仍被正常挡住。
- 玩家站在非门洞区域不能穿墙。
- 日志能区分 bypass accepted / rejected。

---

## 7. Phase 3: 安全出口候选点系统

### 目标

实现 `FindSafeExitPlacement`，在任何真实 teleport 前验证出口落点。

### 预计文件

新增：

- `src/Portal/PortalSafeExitPlacement.h`
- `src/Portal/PortalSafeExitPlacement.cpp`

修改：

- `src/Portal/PortalTransitionSimulator.h`
- `src/Portal/PortalTransitionSimulator.cpp`
- `src/l4d2_base.vcxproj`
- `src/l4d2_base.vcxproj.filters`

### 设计任务

1. 定义输出：

   ```cpp
   enum class PortalExitPlacementFailure
   {
       None,
       MissingInterfaces,
       InvalidTransform,
       OriginOutsideWorld,
       EyeOutsideWorld,
       HullBlocked,
       ExitPortalInvalid,
       NoCandidateAccepted,
   };

   struct PortalExitPlacementResult
   {
       bool valid;
       Vector origin;
       Vector eye;
       QAngle angles;
       Vector velocity;
       PortalExitPlacementFailure failure;
   };
   ```

2. 候选点策略：

   - 基础候选：entry-to-exit transform 后的位置。
   - 出口 normal 正方向安全推离。
   - 出口 normal 反方向小范围检查，仅用于判断，不得把玩家放到门背后非法位置。
   - 地板门/斜门可加最小 forward clearance，但必须通过 hull/world 检查。

3. 必须检查：

   - `I::EngineTrace` 是否可用。
   - `PointOutsideWorld(origin)`。
   - `PointOutsideWorld(eye)`。
   - player hull trace 或等价 hull fit。
   - 出口 eye/origin 到 portal plane 的距离。

4. 日志：

   ```text
   [PortalSafeExit] candidate=0 origin=(...) eye=(...) outsideOrigin=false outsideEye=false hullBlocked=false accepted=true
   [PortalSafeExit] rejected failure=OriginOutsideWorld ...
   ```

### 禁止

- 不允许 `outsideOrigin=true` 的候选点进入 teleport。
- 不允许只因为 `outsideEye=false` 就接受候选点。
- 不允许在此阶段修改玩家状态。

### 验证

游戏内：

- 平面墙门：候选点 accepted。
- 靠近角落门：如果 hull blocked，应拒绝或选择安全候选。
- 旧实验中导致黑天的位置：必须被日志拒绝。

---

## 8. Phase 4: 原子 teleport 提交

### 目标

只在 simulator 的 `CommittingTeleport` 阶段提交真实 teleport，并一次性处理 origin、view angles、velocity、exit state、cooldown。

### 预计文件

修改：

- `src/Portal/PortalTransitionSimulator.h`
- `src/Portal/PortalTransitionSimulator.cpp`
- `src/Portal/PortalTransition.cpp`
- `src/Portal/L4D2_Portal.h`
- `src/Portal/L4D2_Portal.cpp`

可能保留/迁移：

- 现有 server-side local player resolver。
- 现有 server vtable teleport 或 SetAbs 封装。

### 设计任务

1. 迁移或封装 teleport backend：

   ```cpp
   class PortalTeleportBackend
   {
   public:
       bool TeleportLocalPlayer(const PortalExitPlacementResult& placement);
   };
   ```

   如果不单独建文件，也应在 simulator 内保持单一提交函数。

2. 提交前检查：

   - phase 是 `CommittingTeleport`。
   - placement valid。
   - local player still valid。
   - entry/exit portal still active。
   - cooldown not active。

3. 提交顺序建议：

   ```text
   resolve server-side local player
   apply origin / angles / velocity
   set client view angles
   arm visual transition state
   phase -> ExitingPortal
   set cooldown token
   ```

4. teleport 后进入 `ExitingPortal`：

   - 记录 exit portal。
   - 记录 exit start time。
   - 记录 exit aperture/depth。

### 禁止

- 不在 `FinishMove` 前后散落提交位置/视角/速度。
- 不使用 client entity vtable[118]。
- 不使用 unsafe `CMoveData::m_vecAbsOrigin`。
- 不接受 unsafe placement。

### 验证

游戏内：

- 蓝到橙、橙到蓝都能穿越。
- 穿越后方向正确。
- 速度方向正确，至少不反向或归零。
- 不黑天。
- 不左右画面分裂。
- 不出现 view 先转、origin 后变的明显错帧。
- 穿越后不会立即 ping-pong。

---

## 9. Phase 5: 视觉连续性桥接

### 目标

在物理穿越稳定后，实现由 simulator 管理的短时视觉连续性，不依赖非法 player origin。

### 预计文件

新增：

- `src/Portal/PortalViewBridge.h`
- `src/Portal/PortalViewBridge.cpp`

修改：

- `src/Hooks/BaseClient/BaseClient.cpp`
- `src/Hooks/RenderView/RenderView.cpp`
- `src/Portal/L4D2_Portal.h`
- `src/l4d2_base.vcxproj`
- `src/l4d2_base.vcxproj.filters`

### 设计任务

1. 先实现纯诊断状态：

   ```cpp
   struct PortalVisualTransitionState
   {
       bool active;
       PortalSide exitSide;
       Vector physicalEye;
       Vector visualEye;
       float startTime;
       float expireTime;
   };
   ```

2. 单帧或短时 view compensation：

   - 物理位置保持安全 clearance。
   - 视觉 camera 短时向出口门面靠近。
   - offset 必须 clamp，不能越过出口门平面进入墙体。
   - 可通过常量或 debug switch 禁用。

3. 后续候选：

   - portal plane clip。
   - stencil aperture。
   - player render proxy/clone。

### 禁止

- 不通过降低物理 clearance 来硬凑视觉。
- 不让 camera 被拉到 `PointOutsideWorld`。
- 不让视觉层失败影响基础 teleport。

### 验证

- 视觉补偿禁用时，基础穿越仍稳定。
- 启用时，穿越当帧不明显“多走一截”。
- 不黑屏、不闪 skybox、不左右分裂。

---

## 10. Phase 6: 清理旧实验路径

### 目标

在新路线通过 Phase 4 或 Phase 5 后，清理旧 `PortalTransition` 中不再作为正式路线的实验逻辑。

### 候选清理项

- assisted embedding 推真实 origin 的逻辑。
- `kSyncMoveDataEmbeddingToServer` 相关路径。
- 依赖 `outsideEye=false` 忽略 origin 的路径。
- 延迟 `SetViewAngles` 作为非原子 teleport 补救的路径。
- 分散在 `FinishMove` 中的主控 teleport 判断。

### 保留项

- 有价值日志名字可保留或迁移：
  - `Distance probe`
  - `Trace probe`
  - `MoveData layout`
  - `outsideOrigin/outsideEye`

- 有价值工具函数可保留或迁移：
  - local player resolver。
  - server-side teleport backend。
  - portal readiness。
  - anchor construction。

### 验证

- 清理后编译通过。
- 基础穿越行为不退化。
- 日志不再出现旧路线误导性状态。

---

## 11. 测试矩阵

### 11.1 编译测试

每个编码阶段至少运行：

```powershell
& 'D:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' 'src\l4d2_base.sln' /p:Configuration=Debug /p:Platform=x86 /m
```

### 11.2 启动测试

使用已有 launcher：

```text
L4D2_Portal.exe
```

确认：

- 游戏能启动。
- DLL 能加载。
- 控制台日志正常。

### 11.3 游戏内场景

至少测试：

- 蓝门入口 -> 橙门出口。
- 橙门入口 -> 蓝门出口。
- 墙面门到墙面门。
- 墙面门到地板门。
- 地板门到墙面门。
- 靠近角落的出口门。
- 玩家从门洞旁边贴墙移动。
- 玩家进入门洞后后退。
- 玩家高速进入门。

### 11.4 失败场景

必须验证：

- 门未 linked 时不进入 transition。
- 门 closing 时不进入 transition。
- 出口 blocked 时不 teleport。
- `outsideOrigin=true` 时不 teleport。
- 玩家离开门洞后 trace bypass 停止。

---

## 12. 预期风险与缓解

### 12.1 Hook 时序风险

风险：

- `CreateMove`、`FinishMove`、`TracePlayerBBox`、`RenderView` 看到的状态不同。

缓解：

- simulator 是唯一状态源。
- 各 hook 只调用对应 bridge。
- teleport 只在 `CommittingTeleport` 提交。

### 12.2 出口安全误判

风险：

- hull trace 不完整导致出口仍然卡墙或非法。

缓解：

- origin/eye/hull/world 多重检查。
- 保留 failure reason 日志。
- 拒绝不确定候选点。

### 12.3 碰撞豁免过宽

风险：

- 玩家从门洞旁边穿墙。

缓解：

- bridge 只在 simulator phase 下启用。
- 必须通过 aperture intersection。
- 门洞外保留原 trace。

### 12.4 视觉补偿穿帮

风险：

- 相机补偿进入墙体或造成 viewmodel/world 不一致。

缓解：

- 物理稳定优先。
- 视觉补偿可禁用。
- offset clamp 到合法出口门前。

---

## 13. 第一轮编码建议

建议第一轮只做到 Phase 1，不改变行为：

1. 新增 `PortalTransitionSimulator.h/cpp`。
2. 在 `L4D2_Portal` 中新增 simulator 成员。
3. 在 `CreateMove` 中调用 simulator `Update(cmd)`。
4. 读取 local player 和 portal pair。
5. 生成 player anchor。
6. 计算蓝/橙门距离和 aperture。
7. 输出 phase transition 日志。
8. 编译并游戏内验证。

第一轮完成后，再进入 Phase 2。这样可以防止我们一上来又把碰撞、teleport 和视觉绑在一起。

---

## 14. 完成定义

当以下条件满足时，第一阶段可视为完成：

- `CPortalTransitionSimulator` 成为本地玩家穿门唯一主状态源。
- 碰撞桥接只通过 simulator 状态生效。
- 出口落点经过 `FindSafeExitPlacement`。
- teleport 只在 `CommittingTeleport` 阶段提交。
- 玩家能稳定穿越一对传送门。
- 不再出现非法 origin 导致的黑天/PVS/左右画面分裂。
- 视觉连续性可作为独立 bridge 开关控制。
