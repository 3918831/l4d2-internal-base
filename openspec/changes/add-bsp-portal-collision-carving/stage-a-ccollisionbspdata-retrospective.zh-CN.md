# Stage A 技术回溯：Windows x86 `CCollisionBSPData` 定位与布局验证

## 1. 文档状态

| 项目 | 内容 |
|---|---|
| OpenSpec 变更 | `add-bsp-portal-collision-carving` |
| 阶段 | Stage A：Windows BSP 碰撞数据定位与只读布局验证 |
| 状态 | **通过** |
| 适用范围 | L4D2 单人/本地服务器、Windows x86、本地幸存者 |
| 验收地图 | `ssv1`、`c1m2_streets`，同一游戏进程内换图 |
| 安全模式 | `VisualOnlyBaseline`，无穿越、无 BSP 写入 |
| 后续入口 | Stage B：纯 BSP 查询；Stage C：传送门到承载 brush 的只读绑定 |

本文是 Stage A 的规范性结论和技术回溯。早期各轮 IDA/实测清单保留原始调查过程；发生冲突时，以本文、当前 `design.zh-CN.md` 和 `tasks.zh-CN.md` 为准。

## 2. 本阶段究竟证明了什么

Stage A 已经证明：对于指定的 `engine.dll`，插件可以在运行时稳定定位当前地图的 `CCollisionBSPData`，读取算法所需的 plane、node、leaf、leafbrush、brush、brushside、boxbrush 和 cmodel 元数据，验证完整数组跨度，并在换图时使旧快照失效。

Stage A **没有**证明以下事情：

- 任意传送门命中点都能解析到正确的 world brush；
- 找到的 brush 一定是承载该传送门表面的唯一 brush；
- 修改 brush contents 后客户端预测和本地服务端移动会立即观察到变化；
- 修改是否需要刷新引擎碰撞缓存；
- 写入失败、两门共用 brush、重放置、换图和 DLL 卸载时一定能够完整恢复。

因此，“`CCollisionBSPData` 已探明”表示数据库入口和所需布局已经可信，不等于可以立即安全修改其中任意 brush。

## 3. 下一阶段的主要目的

下一阶段要建立从“传送门表面证据”到“承载 world brush”的确定性只读查询链：

```text
放置命中点 + 表面法线
        ↓
向表面内部进行多深度采样
        ↓
从 map_rootnode 遍历 plane/node，得到 leaf
        ↓
枚举 leafbrush → brush
        ↓
按 MASK_PLAYERSOLID 过滤
        ↓
验证采样点确实位于凸 brush 或 box brush 内部
        ↓
返回 leaf、brush、contents、采样深度、地图代际和失败原因
```

该阶段的核心产物不是穿越效果，而是一个可测试、可拒绝、可解释的 `FindBrushForSurfacePoint`。对于 displacement、模型、实体表面、过薄几何或无法证明承载 brush 的位置，查询必须返回“未解析”，继续保持纯视觉且不可穿越。

## 4. 当前不能写 BSP 的原因

这里的“不能写”是阶段门禁，不是技术上永远不能写。

1. **尚未建立 brush 身份。** 当前只知道数组在哪里，不知道某扇传送门对应哪个 brush。此时写入只能依赖猜测索引。
2. **最小修改粒度是整块 brush。** 错一个索引不是只破坏孔径，而是可能移除地图另一整块墙、地板或边界的碰撞。
3. **还没有点在 brush 内的严格验证。** 只凭 leaf、最近表面或一次 `WorldOnly` contents 采样不足以证明承载关系；一个 leaf 可以引用多个 brush。
4. **写入事务尚未实现。** 原始 contents 保存、预期值比较、两门共享所有权、部分失败回滚和幂等恢复都属于后续阶段。
5. **破坏性生命周期尚未验收。** 必须证明重放置、关闭传送门、地图切换和 DLL 卸载都会在地址失效前恢复。
6. **还不能证明因果关系。** 必须在旧碰撞绕过关闭时，对比修改前、修改后和恢复后的客户端/本地服务端 hull trace。
7. **必须保持失败闭合。** 在任何查询或验证失败时，安全结果应是原墙仍存在，而不是自动启用旧 noclip/trace 清除方案。

生产 BSP 写入只有在只读查询和传送门绑定门禁通过、可恢复事务测试完成，并由默认关闭的显式开发开关启用后，才能进入受控实机实验。

## 5. 目标二进制与稳定锚点

| 项目 | 已验证值 |
|---|---|
| 游戏版本标识 | L4D2 `2.2.4.3` |
| `engine.dll` 大小 | `4,817,088` bytes |
| PE 时间戳 | `0x67465ACA`（UTC `2024-11-26 23:33:30`） |
| SHA-256 | `677BBDC3430879A3C2BF25BD3A181791F2286E540260C946425746456CD3CE2F` |
| IDA image base | `0` |
| `g_BSPData` RVA | `0x006883B0` |
| 运行时定位来源 | `IEngineTrace::GetBrushInfo` 语义操作数解码 |
| `GetLeafContainingPoint` RVA | `0x00177760` |
| `GetBrushesInAABB` RVA | `0x0017AD90` |
| `GetBrushInfo` RVA | `0x0017AF30` |

运行时模块基址受 ASLR 影响可以变化，RVA 必须保持一致。插件不把某次运行的绝对地址写死，而是从 `GetBrushInfo` 的已验证指令语义解出 `g_BSPData`。当 `engine.dll` 的大小、时间戳或 hash 变化时，本阶段结论自动降级为“待复验”。

## 6. 已验证的最小布局

数组三元组统一表示“逻辑数量、数组指针、实际分配数量”。完整内存跨度按实际分配数量计算；算法索引上限按逻辑数量计算。

| 字段/数组 | 相对基址 | 元素大小 | 数量关系 | 查询用途 |
|---|---:|---:|---|---|
| `map_rootnode` | `+0x00` | 指针 | `map_rootnode == map_nodes` | world BSP 遍历入口 |
| brushsides | `+0x64/+0x68/+0x6C` | `8` | 分配 = 逻辑 | 凸 brush 平面集合 |
| boxbrushes | `+0x70/+0x74/+0x78` | `48` | 分配 = 逻辑 | box brush 边界 |
| planes | `+0x7C/+0x80/+0x84` | `20` | 分配 = 逻辑 | node 分支和 brush 内点测试 |
| nodes | `+0x88/+0x8C/+0x90` | `12` | 分配 = 逻辑 `+6` | BSP 树遍历；额外六项为 box-hull nodes |
| leafs | `+0x94/+0x98/+0x9C` | `16` | 分配 = 逻辑 | leafbrush 范围 |
| `emptyleaf/solidleaf` | `+0xA0/+0xA4` | `4/4` | empty = render leaf count；solid = 0 | collision leaf 不变量 |
| leafbrushes | `+0xA8/+0xAC/+0xB0` | `2` | 分配 = 逻辑 | leaf 到 brush 的 `uint16` 索引 |
| cmodels | `+0xB4/+0xB8/+0xBC` | 首模型至少 `40` bytes | 分配 = 逻辑 | world collision model 交叉验证 |
| brushes | `+0xC0/+0xC4/+0xC8` | `8` | 分配 = 逻辑 | contents、普通/box brush 分支 |

已确认的查询结构语义：

- `cnode_t`：大小 `12`，`plane* +0`，两个 child 位于 `+4/+8`；负 child 使用 `leaf = -1 - child` 解码。
- `cplane_t`：大小 `20`，法线占前三个 float，距离位于 `+12`，type 位于 `+16`。
- `cbrush_t`：大小 `8`，contents 位于 `+0`，`numsides` 位于 `+4`，`firstbrushside` 位于 `+6`；`numsides == 0xFFFF` 表示 box brush。
- `cbrushside_t`：大小 `8`。
- leafbrush 元素为 `uint16`，必须小于逻辑 brush 数量。

代码不得把完整引擎对象直接强转为未经证明的 C++ 大结构；只能通过定宽字段、受检地址运算和完整范围验证访问已证明字段。

## 7. 证据链与调查过程

### 7.1 建立纯视觉基线

首先屏蔽旧状态机、受控 noclip、碰撞 trace 清除、移动推进、传送提交和临时 MoveType 写入，只保留传送门视觉。该步骤把“是否能够穿越”与旧实现解耦，确保后续任何物理变化都能归因于 BSP 路径。

### 7.2 从 `IEngineTrace` 建立 Windows 锚点

运行时枚举并验证 `GetPointContents`、`TraceRay`、`GetBrushesInAABB`、`GetBrushInfo`、`PointOutsideWorld` 和 `GetLeafContainingPoint` 的 vtable slot、函数地址及 engine 模块内 RVA，再从 IDA 的同 RVA 入口追踪全局 BSP 数据。

### 7.3 用引擎公开行为作为真值

- 用 `GetBrushInfo(index)` 的指数查找和二分边界得到精确 brush 数量。
- 读取 brush `0`、中间、最后和边界外第一项，验证 ABI 与索引边界。
- 用 `LevelLeafCount()`、`GetLeafContainingPoint()`、`GetPointContents_WorldOnly()` 和 `PointOutsideWorld()` 与候选内存字段交叉验证。
- 对有效 brush 同时调用接口和直接读取候选数组，要求 contents 完全一致。

### 7.4 用三类证据收敛字段语义

每个字段至少通过以下证据中的两类，关键字段通过三类：

1. IDA 中真实读写指令和交叉引用；
2. 游戏内接口真值与跨地图稳定性；
3. SourcePawn wallhack 和 CSGO Source 源码提供的算法/结构不变量。

参考源码只能证明算法关系，不能直接提供 L4D2 Windows offset。CSGO、L4D2 Linux 和 L4D2 Windows 的成员排列存在差异，直接复制 offset 会得到错误结果。

### 7.5 第三轮失败与 node `+6` 根因

第三轮两张地图共 16 次 snapshot 均只有 nodes 失败：

- `ssv1`：逻辑 `549`、分配 `555`；
- `c1m2_streets`：逻辑 `4165`、分配 `4171`。

IDA 构建路径和参考源码共同证明引擎执行 `Attach(count + 6)`，额外六个 node 用于 box hull。原先“所有三元组两端数量都必须相等”的通用假设是错误的。修正方式不是放宽校验，而是把每张表的合法数量关系显式写进布局：普通表 delta 为 0，nodes delta 为 6；同时按分配数量验证完整跨度。

该问题形成了独立 bug capsule 和回归测试：`+6` 必须通过，`+5/+7` 必须失败，只覆盖逻辑 node 的截断内存范围必须失败。

### 7.6 第四轮最终验收

同一游戏进程内先运行 `ssv1`，再通过 `map` 切换到 `c1m2_streets`：

| 证据 | `ssv1` | `c1m2_streets` |
|---|---:|---:|
| snapshot 数量 | 10 | 9 |
| `ready=true` | 10 | 9 |
| nodes 逻辑/分配 | `549/555` | `4165/4171` |
| nodes 完整跨度 | `6660` | `50052` |
| collision/render leaf | `561/560` | `4468/4467` |
| brushes API/内存 | `148/148` | `2110/2110` |
| cmodels 逻辑/分配 | `10/10` | `301/301` |

总计 19 次 snapshot 中，以下失败计数全部为 0：

- `ready=false`
- `fieldsReadable=false`
- `countRelationMatches=false`
- `spanReadable=false`
- root、leaf、cmodel 或 brush API 交叉验证失败
- `destructiveWrites=true`
- 崩溃、访问异常或断言失败

57 个有效 brush 样本的接口 contents 与直接内存读取全部一致；19 个 `index == brushCount` 的边界样本全部按预期无效。同一地图内七张表的地址和数量保持稳定，换图后七张表全部切换到新的地址和数量。

## 8. 生命周期结论

- BSP snapshot 绑定地图 generation，不得跨 generation 使用。
- `LevelShutdown` 会清空 BSP base、数组地址和快照，日志明确记录 `cacheInvalidated=true`。
- 一次地图切换可能触发多次 `LevelShutdown`，因此 generation 不保证每张地图只递增 1；正确不变量是单调递增、旧快照失效和新地图重新捕获。
- 当前阶段没有 destructive writes，多次失效是安全的幂等操作。
- 后续存在写入后，生命周期顺序必须改为“恢复所有 brush → 使快照失效”，不能先丢弃地址再恢复。

## 9. 已知但不阻塞 Stage A 的观察

### 9.1 两处表面后向采样仍为空

第四轮 19 个传送门表面样本中，17 个在 `back-1/back-4` 得到 world solid；`ssv1` 有两处在两个深度都为 `worldContents=0`。这可能表示非 world-brush 表面、实体/displacement，或简单偏移没有进入承载 brush。

该现象不影响数据布局，但直接决定下一阶段必须：

- 使用多深度采样，而不是只测一个点；
- 枚举 leaf 引用的全部 brush；
- 对凸 brush/box brush 做严格内点验证；
- 无法证明时返回“未解析”，不得猜测或写入。

### 9.2 脚下垂直放门的 `(0,0,0)`

早期日志中的 `(0,0,0)` 来自向脚下放门失败，不是 BSP 数据失效。第四轮没有执行该动作。本问题属于传送门放置状态的独立缺陷，后续单独处理，不应污染 BSP 查询或布局门禁。

## 10. 后续阶段准入门禁

### Stage B：纯 BSP 查询

- 先用合成数据测试正/负 child、负 child 到 leaf 的解码和所有越界路径。
- 测试凸 brush、box brush、leafbrush 枚举、contents mask 和多深度采样。
- 所有查询都是只读；损坏数据必须返回结构化失败原因。

### Stage C：传送门只读绑定

- 必须复用成功放置 trace 的原始 hit position/normal，不能只从渲染用 portal origin 猜测。
- 简单 world brush 墙面必须能重复解析同一 brush。
- 不支持表面必须稳定拒绝，保持纯视觉基线。

### Stage D：可恢复写入核心

- 先在假存储中测试原始值保存、预期值比较、共享所有权、部分失败回滚和重复恢复。
- 生产写入默认关闭，并由显式开发开关启用。
- 在 Stage C 门禁通过前不得进行任何实机 BSP 写入。

### Stage E：受控整块 brush 实验

- 比较修改前、修改后、恢复后的客户端与本地服务端 hull trace。
- 关闭旧碰撞绕过进行因果测试。
- 验证重放置、关闭、换图和 DLL 卸载恢复。
- 只在此阶段观察整块 brush 被移除的无约束副作用。

## 11. 可复用的技术原则

1. **用语义锚点定位，不依赖单次绝对地址。** ASLR 下比较 RVA 和指令语义。
2. **公开接口是运行时真值，不是内存猜测的替代品。** 接口边界、直接内存和 IDA 必须闭环。
3. **布局差异应显式建模。** nodes 的 `+6` 是合法语义，不应通过关闭计数校验解决。
4. **验证完整跨度。** 只检查首元素可读不足以证明整个数组安全。
5. **逻辑数量与分配数量职责不同。** 前者限制算法索引，后者限制内存跨度。
6. **先建立因果基线。** 旧穿越路径必须可显式关闭，否则无法证明 BSP 修改是否生效。
7. **失败闭合优先。** 未知表面、过期 generation、边界异常和不可读内存都必须保持原碰撞。
8. **把实机发现变成确定性回归测试。** node `+6` 的失败先用测试复现，再修改实现。
9. **参考源码只作结构佐证。** 不同 Source 分支、平台和编译配置的 offset 不可直接搬运。
10. **诊断日志要能解释拒绝原因。** 不能只有最终布尔值；必须保留字段、关系、跨度和地图代际。

## 12. 版本变化后的复验流程

当 `engine.dll` hash、大小或时间戳变化时：

1. 保持 `VisualOnlyBaseline` 和所有 BSP 写入关闭；
2. 重新验证 EngineTrace vtable slot 与函数 RVA；
3. 重新从 `GetBrushInfo` 语义操作数定位 `g_BSPData`；
4. 在至少两张规模不同的地图运行完整 snapshot；
5. 验证所有表的 offset、步长、数量关系、跨度和不变量；
6. 重新执行 brush API/直接内存 contents 交叉验证；
7. 只有全部通过后，才能恢复只读查询；写入仍需通过后续绑定和事务门禁。

## 13. 证据与实现索引

- 变更设计：`design.zh-CN.md`
- 实施任务：`tasks.zh-CN.md`
- 能力规格：`specs/portal-bsp-collision-carving/spec.zh-CN.md`
- 初始联合取证：`ccollision-bspdata-offset-evidence-plan.zh-CN.md`
- 第二轮清单：`ida-round2-checklist.zh-CN.md`
- 第三轮清单：`ida-runtime-round3-checklist.zh-CN.md`
- node `+6` bug capsule：`docs/bug-report/portal-bsp-node-count-plus-six/bug-report.md`
- 定宽布局：`src/Portal/PortalBspTypes.h`
- 受检 snapshot：`src/Portal/PortalBspData.h/.cpp`
- 运行时偏移记录：`src/Util/Offsets/PortalBspOffset.h/.cpp`
- EngineTrace 诊断：`src/Hooks/EngineTrace/EngineTrace.cpp`
- 第四轮原始日志（本地证据，不建议作为源码提交）：`TestLog/CCollisionBSPData成员确定/IDA和游戏内实测验证-第四轮/portal_l4d2_traversal.log`
