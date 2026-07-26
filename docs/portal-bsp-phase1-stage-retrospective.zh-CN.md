# L4D2 传送门 BSP 物理穿越阶段性技术复盘

日期：2026-07-26

分支：`codex/bsp-portal-traversal`
对应 OpenSpec：`add-bsp-portal-collision-carving`

## 1. 阶段结论

本阶段已经证明：在单人、本地服务器范围内，运行时修改 L4D2 已加载的 BSP brush `contents`，可以让原始玩家移动碰撞观察到真实开口，并继续复用本工程已有的穿越状态机、坐标变换、速度变换与 server-side `Teleport` 完成传送门穿越。

第十六轮实测确认当前基线已经达到以下状态：

- 蓝门、橙门所在世界 brush 的碰撞可由插件事务性清空。
- `movementMutation=false`、`legacyCollisionBypass=false`、`controlledNoclip=false` 时，玩家仍可双向穿越。
- `ExactTransform` 下不再依赖出口法线 push，穿越速度变换有效。
- 没有黑天空。
- 没有条状墙体。
- 没有门后墙体画面。
- 没有第十五轮出现的巨型不规则墙面多边形。
- 高速穿越也未再观察到墙体闪烁。
- 关停时 brush `contents` 能恢复为原始值，恢复后的 hull trace 再次命中墙体。

仍存在轻微的主观视觉不连续或顿挫。本阶段明确不继续修复该问题；后续工作将独立研究 Portal 官方的客户端相机、插值历史和 viewmodel 交接。

这是一项阶段性成果，不等于整个 OpenSpec 已完成。Phase 1 的其他异常矩阵、同 brush、强制失败和更完整生命周期测试仍保留为后续任务，Phase 2 的附近检测及 Phase 3 的 blocker 也尚未开始。

## 2. 已限定的支持范围

本阶段只声明支持：

- 单人游戏或 local listen server。
- 本地幸存者玩家。
- 世界 BSP brush 上的传送门。
- 玩家本体的移动、跨面判定、坐标/速度/视角变换和传送提交。

本阶段不声明支持：

- 远程服务器或多人所有权。
- 普通感染者、特感、NPC 和 AI 导航。
- 子弹、近战 trace、投掷物、物理道具。
- 任意模型、实体或非 BSP 承载面。
- 完整 Portal 游戏的实体克隆、身体切割或 portal environment。

## 3. 最终运行链路

```text
地图加载
→ 从 EngineTrace / GetBrushInfo 语义锚点定位 CCollisionBSPData
→ 校验 plane/node/leaf/leafbrush/brush/brushside/boxbrush
→ 传送门放置 trace 成功
→ 用 hit position + normal 在表面后方采样
→ BSP node → leaf → leafbrush → brush
→ 保存蓝/橙门的 brush 绑定及原始 contents
→ 两扇门有效且 Phase 1 开启
→ 事务性把唯一 brush contents 写为 CONTENTS_EMPTY
→ 原始 client/server movement 通过墙体
→ PortalTransitionSimulator 判断跨越入口平面
→ PortalTransform 变换位置、速度、角度
→ PortalPlayerTeleport 提交 local-server Teleport
→ 主视图执行入口相机交接和近裁剪代理
→ 空间出口锁 + 时间冷却防止乒乓传送
→ 传送门关闭、换图或 DLL 关闭前恢复原始 contents
```

## 4. Windows `CCollisionBSPData` 探明过程

### 4.1 为什么不能直接使用 SourcePawn 的 Linux offset

`Reference Code/sourcemod-wallhack` 已经证明算法方向可行，但其 gamedata 只适用于 Linux。Windows x86 的全局变量位置、类布局、调用约定和编译器生成代码都可能不同，因此只复用算法，不复制 offset。

### 4.2 EngineTrace 运行时锚点

首先在目标 L4D2 版本中确认 EngineTrace vtable：

- `GetPointContents`
- `GetPointContents_WorldOnly`
- `TraceRay`
- `GetBrushesInAABB`
- `GetBrushInfo`
- `PointOutsideWorld`
- `GetLeafContainingPoint`

IDA 以 `GetBrushInfo` 为主入口，结合 `GetBrushesInAABB` 与 `GetLeafContainingPoint` 的交叉引用，确认 BSP 全局数据和各数组访问关系。

### 4.3 结构与计数关系

结合 Portal/CSGO Source 参考、IDA 伪代码和两张不同地图的运行时数据，确认目标 Windows x86 版本所需的最小布局：

- `CNode = 12`
- `CPlane = 20`
- `CLeaf = 16`
- `CBrush = 8`
- `CBrushSide = 8`
- `CBoxBrush = 48`

普通数组的逻辑数量与分配数量相等。node 数组存在固定的 `allocated = logical + 6`，额外六个 node 来自引擎 box hull。完整过程见：

- `openspec/changes/add-bsp-portal-collision-carving/stage-a-ccollisionbspdata-retrospective.zh-CN.md`
- `docs/bug-report/portal-bsp-node-count-plus-six/bug-report.md`

### 4.4 ABI 经验

逆向时不能只确认“地址看起来合理”，必须同时验证：

- x86 返回值宽度，例如 `GetBrushInfo` 的一字节 boolean。
- 每个数组的地址、逻辑数量、分配数量与元素尺寸。
- 完整内存范围可读，而不只是首元素可读。
- 地图切换后地址和数量是否按同一语义成组变化。
- 运行时读数是否能被公开 Source 结构和 IDA 访问模式共同解释。

## 5. BSP 查询与 brush 绑定

### 5.1 查询算法

实现沿用了 SourcePawn 参考的核心思想：

1. 从 BSP root node 开始，用 plane 判断点位于前子树或后子树。
2. 负 child 索引解码为 leaf。
3. 枚举 leaf 引用的 leafbrush。
4. 过滤不包含 `MASK_PLAYERSOLID` 的 brush。
5. 对普通凸 brush 检查所有 brush side plane。
6. 对 box brush 检查轴对齐 bounds。
7. 对传送门命中平面向墙内按 `{2, 4, 8, 1, 16, 0.5}` 依次采样。
8. 返回 brush index、leaf、采样偏移和原始 contents。

纯查询逻辑与实时引擎地址分离，使 node、leaf、convex brush、box brush、越界和损坏数据都能用合成数据测试。

### 5.2 为什么在放置时绑定

传送门放置 trace 的 hit position 和 plane normal 是最强的承载面证据。在放置成功时解析一次，比每帧重新查询更可靠，也避免从已经前移的渲染门实体反推原始墙面。

## 6. 可恢复事务模型

`CPortalBspCollisionCarver` 把蓝门和橙门的 brush 修改作为一个事务：

- 第一次写入前保存原始 contents。
- 写入前比较当前值与预期原始值。
- 两扇门位于不同 brush 时，全部目标验证后再依次写入。
- 第二个目标失败时回滚已写入的第一个目标。
- 两扇门共用同一 brush 时只写一次，用 owner mask 保存双重所有权。
- 恢复时再次比较当前值，避免覆盖外部未知修改。
- 地图 generation 不匹配时拒绝解引用旧地址。
- `RestoreAll(reason)` 幂等，可由多个生命周期出口安全调用。

恢复顺序的关键原则是：地图关闭时必须先恢复仍有效的 BSP 内存，再使地址和 generation 失效。

## 7. 干净物理基线

为了证明穿越来自 BSP 挖空，而不是旧方案，本阶段建立了明确的模式隔离：

```text
movementMutation=false
legacyCollisionBypass=false
controlledNoclip=false
teleport=true
teleportPredictionSync=true
```

旧 collision bridge 和 controlled noclip 代码没有删除，但当前 BSP 因果测试不会自动启用它们。BSP 不可用时默认回到纯视觉且不可穿越的安全状态，而不是偷偷回退旧绕过。

当前实测配置：

```text
portal_bsp_phase1 1
portal_bsp_clearance exact
portal_visual_nearclip 1
portal_visual_exitguard 0
portal_visual_entryhandoff 1
portal_bsp_status
```

## 8. 主要 BUG、尝试与结论

### 8.1 Phase 1 未开启时玩家被冻结在门附近

**现象：** BSP 尚未挖空，状态机仍进入穿越阶段并影响原始移动；玩家被墙体和穿越状态共同约束，noclip 也难以脱身。

**根因：** 穿越 simulator 的启动条件没有与 BSP carving active 绑定。

**修复：** BSP 挖空没有真正激活时，状态机保持 idle；movement mutation 关闭时始终执行原始 client/server `PlayerMove` 和地面处理。

归档：`docs/bug-report/bsp-inactive-player-movement-freeze/bug-report.md`

### 8.2 Teleport 后预测不同步

**现象：** 物理 server player 已传送，但客户端预测位置、速度或视角短暂不一致。

**根因：** 一次性 Teleport 同步曾被错误地和持续 movement mutation 绑定。

**修复：** 把 Teleport commit 的 origin/velocity/angles 预测同步从 noclip/movement mutation 中解耦。

归档：`docs/bug-report/bsp-teleport-prediction-desync/bug-report.md`

### 8.3 出口 push 造成画面坐标不连续

**现象：** 入口门内最后一帧使用精确映射的出口嵌入视角，但 Teleport 后第一帧被 push 到出口前方，两个观察坐标不一致。

**尝试：** `FullHull` 和 `PlaneEpsilon` 都保留不同程度的出口位移。

**结论：** `ExactTransform` 视觉最好，物理位置不再为 clearance 强制外推。防乒乓改为“出口空间锁 + 空间解锁后开始时间冷却”，而不是依靠 push 把玩家推出门。

### 8.4 黑天空

**现象：** Teleport 后一帧天空变黑，类似相机位于不可见 leaf 或墙内。

**根因方向：** 物理点可以正确，但主视图 PVS/可见性采样仍可能使用错误 leaf。

**修复：** 保持精确物理点不动，在有界出口交接期追加经过验证的出口安全 PVS 原点；相机原点和 PVS 原点分离。

### 8.5 高速穿越看到入口门背后

**现象：** 高速时画面像 Teleport 晚了一帧，能看到入口墙后的世界。

**修复过程：**

- 修正入口后半空间与变换后出口前半空间语义。
- 首次有效采样已经在入口后方时，同一 update 提交。
- 使用连续命令的 anchor segment 与门平面/孔径相交作为高速兜底。
- 保留官方同样使用的 entity `Teleport`，不把 API 本身当作根因。

### 8.6 Teleport 前眼位已经越过入口

**现象：** 玩家中心尚未到达提交条件，但相机眼位已经进入墙后。

**官方参考：** `C_Portal_Player::CalcPortalView` 会在物理玩家传送前，把门后眼位通过 linked portal 变换。

**修复：** 只在 `IntersectingPortal`、眼位位于入口后方且仍在孔径内时，变换主视图 eye origin 和 angles，不移动玩家。

归档：`docs/bug-report/portal-teleport-camera-handoff-discontinuity/bug-report.md`

### 8.7 条状墙体、门后墙体与 viewmodel 裁剪

**现象：** 极慢接近时可以稳定停在一帧条状墙体画面，viewmodel 同时被裁掉一部分；高速时表现为闪帧。

**排除：**

- 与 `FullHull`/`PlaneEpsilon` 无关，因为异常发生在 Teleport 前。
- 只修 Teleport 时机不能消除。
- 普通门模型极近投影/无深度拒绝的 `PortalMaskRepair` 实验被第十三轮证伪并彻底删除。

**官方对齐过程：**

1. 远端 RTT 相机保持精确入口到出口变换，不再额外沿出口法线推 1 单位。
2. 远端 clip plane 改为官方 `dot(normal, exitOrigin - normal * 0.5)`。
3. 移植 Portal SDK 主视图 near-plane render-fix proxy：
   - 在 `zNear + 0.05` 生成相机平面 quad。
   - 用十二个扩展孔径平面和门正面平面裁剪。
   - CPU 投影到 NDC `z=0.00001`。
   - 在现有 stencil `ALWAYS/REPLACE` 阶段用 dynamic mesh 绘制。

### 8.8 第十五轮巨型不规则多边形

**现象：** 条带变成大面积三角形、梯形和不规则墙体贴图，视角不动时仍会局部闪烁。

**根因：** 本地 `IndexDesc_t` ABI 适配错误：

- 把 `m_nIndexSize=1` 当作一字节步长。
- 在重叠字节地址上连续写入 16 位索引。
- 忽略 `m_nFirstVertex`。

这会把预期的 `0,1,2` 写成包含 `0x0100` 等越界值的损坏索引，GPU 随后读取动态缓冲区中的陈旧顶点并生成横跨屏幕的巨型多边形。

**官方参考：** Portal SDK `CIndexBuilder` 把 `m_nIndexSize` 作为 0/1 元素增量，并写入 `m_nFirstVertex + localIndex`。

**修复：**

- 仅接受 `indexSize=1`。
- 按 `unsigned short` 元素写入。
- 加上 `firstVertex`。
- 写入前验证容量、非负值和 16 位上限。
- 用非零 firstVertex、哨兵内存、异常步进和溢出用例回归。

归档：`docs/bug-report/portal-near-plane-mask-clipping/bug-report.md`

## 9. 第十六轮实机验收证据

日志位置（不纳入 Git）：

`TestLog/CCollisionBSPData成员确定/IDA和游戏内实测验证-第十六轮/portal_l4d2_traversal.log`

统计结果：

- 日志 4042 行，约 1.63 MB。
- 成功 `PortalTeleportCommit`：23 次。
  - 蓝→橙：12 次。
  - 橙→蓝：11 次。
- `[PortalRenderFix] applied=true`：20 次。
  - `zNear=1.0` 的近门样本：9 次。
  - `zNear=7.0` 的普通样本：11 次。
- 20/20 索引记录均满足：
  - `indexSize=1`
  - `firstIndexValue=firstVertex`
  - `lastIndexValue=firstVertex + vertices - 1`
- 没有真实 error、fatal、crash、事务回滚或穿越拒绝。
- `MissingData` 只出现在第二扇门尚未放置的预期中间态。
- 退出时两个 brush 都从 `0x00000000` 恢复为原始 `0x00000001`。
- 恢复前两个 hull trace 都是 `fraction=1.0`，恢复后都是 `fraction=0.375`，证明碰撞重新生效。

结合人工观察，本轮关闭“近门墙体遮罩闪烁”问题：

- 无黑天空。
- 无条状墙体。
- 无门后墙体。
- 无高速墙体闪帧。
- 无巨型错误多边形。

## 10. 测试与构建资产

当前共有 13 个独立测试 runner，覆盖：

- EngineTrace BSP 诊断。
- BSP 类型、查询、数据访问。
- portal brush 绑定。
- mutation 事务和恢复。
- 物理模式门禁。
- transition decision、跨面、精确出口和防重入。
- 文件日志策略。
- near-plane proxy 纯几何。
- dynamic mesh 索引 ABI。
- PortalTransform。

本阶段提交前要求：

- OpenSpec strict validation 通过。
- 13/13 runner 通过。
- Debug x86 solution 构建 0 error。
- `git diff --check` 通过。
- 不提交 `TestLog`、本地设置或编译产物。

## 11. 当前技术经验

1. **先证明因果，再加限制。** 不做附近检测的 Phase 1 暴露了 whole-brush 的真实行为，也证明原始移动确实能消费修改后的 BSP 数据。
2. **一次只改一个变量。** Teleport、出口 push、PVS、RTT clip、stencil proxy 和 mesh index 分轮验证，才能从失败中获得信息。
3. **失败实现必须删除。** 已被证伪的 `PortalMaskRepair` 没有作为隐藏开关保留，避免后续混淆有效链路。
4. **官方源码用于寻找不变量。** 不照搬 Portal 全部类体系，而是提取可验证的不变量：半空间、精确变换、相机先行、clip plane、near-plane proxy、index builder 语义。
5. **逆向结论必须多源互证。** IDA、运行时日志、公开 Source 结构和跨地图变化必须能互相解释。
6. **物理位置和视觉状态要分离。** 精确 Teleport 点、PVS origin、入口相机交接和未来插值修复属于不同责任。
7. **GPU 异常形状先检查拓扑。** 大型三角形、梯形和视角相关闪烁更像索引/顶点错误，而不是普通 Z-fighting。
8. **恢复是功能的一部分。** “能挖空”不算完成；必须证明换图/关闭前恢复原值，且恢复后的 trace 重新阻挡。

## 12. 尚未完成的工作

### 本次明确暂停

- 残余轻微视觉不连续或顿挫。
- 官方式客户端插值历史修复。
- viewmodel facing、eye-angle latch、demo interpolation 等交接。

### OpenSpec 后续

- Phase 1 更完整异常矩阵：
  - 从孔径外穿过同一被移除 brush。
  - 两门同 brush 实机验证。
  - 重放门、关门、换图、卸载的组合矩阵。
  - 强制地址/绑定/写入失败。
- Phase 2：可配置 portal-local 附近检测和迟滞恢复。
- Phase 3：如仍有需要，再评估 `env_physics_blocker` 孔径约束。

下一会话的视觉连续性工作必须以当前已验证基线为起点，不应重新启用旧 noclip、movement mutation、出口 push 或被证伪的遮罩方案。
