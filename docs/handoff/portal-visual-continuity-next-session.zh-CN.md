# 新会话交接：BSP 穿越后的残余视觉连续性

日期：2026-07-26

当前分支：`codex/bsp-portal-traversal`

工作目录：`l4d2-internal-base-bsp-query-stage1`
主要方法论：从 Portal SDK 2013 提取官方不变量，以日志和单因子实测逐项验证，不凭感觉叠加 workaround。

## 先读结论

物理穿越主线和近门墙体遮罩问题已经取得阶段性成功。第十六轮人工实测确认：

- BSP brush 挖空可以独立提供玩家穿墙通道。
- 旧 collision bypass、controlled noclip 和持续 movement mutation 全部关闭。
- 双向慢速、高速均可穿越。
- 没有黑天空、条状墙体、门后墙体或巨型错误多边形。
- 当前只剩轻微的视觉不连续/顿挫，暂未定位和修复。

新会话不得把旧文档 `docs/portal-l4d2-traversal-implementation-summary.md` 中依赖 movement trace bypass、assisted embedding、noclip 和大出口 clearance 的方案当成当前基线。当前真相源是：

1. `docs/portal-bsp-phase1-stage-retrospective.zh-CN.md`
2. `openspec/changes/add-bsp-portal-collision-carving/design.zh-CN.md`
3. `docs/bug-report/portal-teleport-camera-handoff-discontinuity/bug-report.md`
4. `docs/bug-report/portal-near-plane-mask-clipping/bug-report.md`
5. 本文件

## What：当前已经实现了什么

### 物理与碰撞

- Windows x86 `CCollisionBSPData` 定位和完整最小布局校验。
- 传送门放置时从 hit position/normal 解析承载 BSP brush。
- 蓝/橙门 brush 的事务性 `CONTENTS_EMPTY` 修改。
- shared-brush owner mask、回滚、幂等恢复、map generation 防护。
- BSP 不可用时回到纯视觉且不可穿越，不自动启用旧方案。

### 穿越与同步

- 原有 `PortalTransitionSimulator` 继续拥有跨越阶段。
- `PortalTransform` 继续拥有位置、速度和角度变换。
- `PortalPlayerTeleport` 继续用 local-server entity `Teleport` 提交。
- `ExactTransform` 不施加出口 normal push。
- 空间出口锁和解锁后冷却防止两门间乒乓。
- Teleport prediction sync 与持续 movement mutation 解耦。
- 高速使用连续 command anchor segment 与门平面/孔径相交兜底。

### 视觉与渲染

- 物理 Teleport 前，眼位已经越过入口时执行官方式 entry camera handoff。
- 远端 RTT 使用精确变换相机，不再额外外推。
- 远端 clip plane 对齐官方 `dot(normal, exitOrigin - normal * 0.5)`。
- 出口安全 PVS origin 与精确物理/相机 origin 分离。
- 主视图 near-plane render-fix proxy 已实现。
- dynamic mesh index ABI 已按 Portal SDK `CIndexBuilder` 修正。
- 第十六轮已验证墙体遮罩异常关闭。

### 当前运行配置

```text
portal_bsp_phase1 1
portal_bsp_clearance exact
portal_visual_nearclip 1
portal_visual_exitguard 0
portal_visual_entryhandoff 1
portal_bsp_status
```

状态必须保持：

```text
movementMutation=false
legacyCollisionBypass=false
controlledNoclip=false
clearanceMode=ExactTransform
remoteView=OfficialExactCameraClipMinus0.5
nearPlaneRenderFix=OfficialStencilProxyOnly
depthFogRepair=deferred
```

## Why：为什么下一步聚焦官方客户端交接

目前已经排除或解决：

- BSP 墙碰撞本身。
- 旧 noclip 和 trace bypass 干扰。
- 出口 push 导致的坐标跳变。
- 错误入口/出口半空间。
- 高速 crossing 漏采样。
- Teleport 前眼位穿墙。
- 黑天空/PVS leaf。
- RTT 相机和 clip plane。
- near-plane stencil mask。
- dynamic mesh 索引损坏。

剩余主观问题是“穿越仍有轻微不连续或顿挫”，而不是可定格的错误墙体帧。Portal SDK 表明，官方并非只调用一次 `Teleport`；客户端还会在网络状态变化时修复相机、眼位插值、角度插值、viewmodel 朝向和实体 latch/history。

最值得优先阅读的官方代码：

- `Reference Code/portal-sdk-2013/src/game/client/portal/c_portal_player.cpp`
  - `C_Portal_Player::PlayerPortalled`，约 1026 行。
  - 数据变化处理和 `m_PendingPortalMatrix`，约 1030～1170 行。
  - `UpdatePortalEyeInterpolation`，约 1299 行。
  - `CalcView` 中的 portal environment 处理，约 1404～1472 行。
  - `CalcPortalView`，约 1511 行。
- `Reference Code/portal-sdk-2013/src/game/client/portal/c_portal_player.h`
  - `PortalEyeInterpolation_t`，约 166～180 行。
  - `m_iv_angEyeAngles`、`m_hPortalEnvironment`。
- `Reference Code/portal-sdk-2013/src/game/client/portal/c_prop_portal.cpp`
  - 客户端检测实体 portalled 后调用 `PlayerPortalled`，约 114～143 行。
- `Reference Code/portal-sdk-2013/src/game/server/portal/prop_portal.cpp`
  - 官方 server 仍调用实体 `Teleport`，约 1196 行。

需要重点理解的官方动作：

- 保存并应用 pending portal matrix。
- 同时变换 interpolated 和 uninterpolated eye position。
- 重置 `m_iv_angEyeAngles`。
- 变换 viewmodel 的 `m_vecLastFacing`。
- `ResetLatched()`。
- `engine->ResetDemoInterpolation()`。
- portal eye interpolation 如何逐帧追赶真实 eye，而不是简单线性拖动相机。

## Tradeoff：已经放弃或暂缓什么

### 已放弃

- 重新启用 controlled noclip。
- 在 movement trace 中清除墙碰撞结果。
- 使用 assisted embedding 或连续位置小推。
- 用 `FullHull`/`PlaneEpsilon` 出口 push 换取不卡墙。
- 用普通门模型极近投影/无深度测试修 stencil；该 `PortalMaskRepair` 已被证伪并删除。
- 把 `m_nIndexSize` 当字节步长的本地 dynamic mesh 写法。

### 暂缓

- stencil 内局部清 depth。
- fog 和 post-stencil depth repair。
- Phase 2 附近检测。
- Phase 3 `env_physics_blocker`。

原因：第十六轮没有黑天空或墙体遮罩问题，当前没有证据要求继续修改 depth/fog。新会话必须先为“轻微不连续”建立可测量证据，再决定是否需要这些路径。

### 当前选择的代价

- Phase 1 会在两扇门有效期间持续清空整个承载 brush，孔径外区域可能也可穿越。
- `ExactTransform` 不主动把玩家推出出口，因此依赖空间锁防重入；玩家主动后退仍可能进入 whole-brush 开口的地图外空间。
- 入口 camera handoff 是本工程提取的官方不变量，不等于完整 Portal 客户端预测/插值实现。

## Open Questions：下一会话需要回答什么

1. 轻微不连续发生在 Teleport 前、提交帧、还是 Teleport 后的第一个或第二个渲染帧？
2. 不连续主要来自 eye origin、view angles、FOV、viewmodel facing，还是客户端网络 origin 插值？
3. 当前 `PortalEntryViewHandoff` 最后一帧与 Teleport 后 `CalcPlayerView` 第一帧的 origin/angles 差值是多少？
4. L4D2 客户端实体暴露了哪些可安全重置的 interpolation/latch 接口？
5. 能否只变换/重置本地玩家的相关历史，而不影响第三人称实体、demo 或其他系统？
6. 官方 `PlayerPortalled` 中哪些动作是必要不变量，哪些依赖 Portal 独有字段而不能直接移植？
7. viewmodel 朝向或动画插值是否构成用户感受到的“顿一下”？
8. 当前日志时间基准能否把 usercmd、server Teleport 和 render frame 对齐到同一事件序列？

## Next Action：建议的新会话步骤

### 第一步：只加诊断，不改画面

为每次成功穿越生成一个唯一 transaction id，把以下信息按顺序写入文件日志：

- 最后一帧入口 handoff 的 render eye origin/angles。
- server Teleport 的 old/new origin、velocity、angles。
- prediction sync 后客户端玩家 origin/velocity。
- Teleport 后前 3～5 个主渲染帧的 eye origin/angles。
- viewmodel 当前 facing/angles（如果可以安全读取）。
- transition phase、usercmd number、engine time、frame count。

验收：能明确指出哪一对连续帧产生最大 position/angle delta。

### 第二步：完整研读官方 `PlayerPortalled`

建立“官方动作 → L4D2 可用基础设施 → 风险 → 是否需要 hook/offset”的映射表。不要直接编码所有动作。

优先候选：

1. 变换本地 eye interpolation history。
2. 重置或变换 eye-angle interpolator。
3. 修复 viewmodel last-facing。
4. 最后才考虑更广泛的 entity latch reset。

### 第三步：单因子 A/B

一次只启用一个候选修复，并保持：

- BSP 配置不变。
- `ExactTransform` 不变。
- near-plane proxy 不变。
- Teleport API 不变。
- depth/fog 不变。

每轮都做慢速、普通速度、高速双向测试，主观反馈必须和 transaction 日志对齐。

### 第四步：失败即回退

如果候选修复：

- 引入黑天空；
- 恢复条带/门后墙体；
- 改变物理出口点或速度；
- 造成 viewmodel 大幅跳变；
- 需要启用旧 movement mutation；

则立即回退该单因子，不在其上继续叠加补丁。

## 新会话建议开场提示

可以直接把以下内容交给新会话：

> 当前 `codex/bsp-portal-traversal` 已完成单人 localserver 的 BSP whole-brush 物理穿越阶段性基线。第十六轮确认 23 次双向穿越，无黑天空、条状墙体、门后墙体和高速闪烁，brush 关停恢复成功。请先阅读 `docs/portal-bsp-phase1-stage-retrospective.zh-CN.md` 与 `docs/handoff/portal-visual-continuity-next-session.zh-CN.md`，不要重新启用旧 noclip、movement mutation、出口 push 或 `PortalMaskRepair`。下一任务只研究残余视觉连续性，方法论以 Portal SDK 2013 的 `C_Portal_Player::PlayerPortalled`、`UpdatePortalEyeInterpolation`、`CalcPortalView` 为主，先加 transaction 级诊断，再做单因子 A/B。

## 交接五件套自检

- [x] What：当前实现、运行配置和已验证结果。
- [x] Why：剩余问题为什么应转向官方客户端交接和插值历史。
- [x] Tradeoff：已放弃、暂缓方案及当前 Phase 1 代价。
- [x] Open Questions：下一会话必须用证据回答的问题。
- [x] Next Action：诊断、官方映射、单因子 A/B 和失败回退顺序。
