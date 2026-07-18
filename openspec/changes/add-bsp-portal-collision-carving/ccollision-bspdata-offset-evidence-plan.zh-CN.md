# `CCollisionBSPData` Windows offset 联合取证计划

## 本轮目标与边界

本轮只建立 Windows x86 L4D2 的只读证据，不修改 `g_BSPData` 或任何 brush。最终要确定以下成员相对 `CCollisionBSPData` 基址的 offset：

- `map_rootnode`
- `map_brushsides` / `numbrushsides`
- `map_boxbrushes` / `numboxbrushes`
- `map_leafs` / `numleafs`
- `map_leafbrushes` / `numleafbrushes`
- `map_brushes` / `numbrushes`

offset 只有同时满足“IDA 指令语义、游戏内接口真值、参考源码结构不变量”三类证据时才可标记为已验证。

## 1. 游戏内需要实测的数据

工程已增加只读 `RuntimeOracle` 探针。传送门首次激活或移动到新位置后，探针通过现有 EngineTrace 接口记录：

1. 当前地图名。
2. `IEngineClient::LevelLeafCount()` 返回的 `numleafs` 真值。
3. 通过 `GetBrushInfo(index)` 有效区间边界推导的 `numbrushes` 真值：
   - 采用指数查找加二分查找，调用次数是对数级；
   - `brushCountExact=true` 才能作为精确证据；
   - 安全上限为 `1,048,576`，触顶时拒绝声称精确。
4. brush `0`、中间项、最后一项和边界外第一项的 `valid/contents`：
   - `0`、`count/2`、`count-1` 应有效；
   - `count` 应无效；
   - 有效项的 `contents` 用于和 IDA 定位出的 `map_brushes[index].contents` 逐项比对。
5. 每扇门表面前方 `1` unit、墙内 `1` unit、墙内 `4` units 三个点的：
   - `GetLeafContainingPoint` 返回值；
   - `GetPointContents_WorldOnly(..., MASK_ALL)`；
   - `PointOutsideWorld`。

### 游戏实测步骤

1. 删除旧的 `portal_l4d2_traversal.log`。
2. 在第一张地图选择普通、无位移表面的墙，各放置蓝门和橙门。
3. 把蓝门移动到另一面墙，再把橙门移动到另一面墙，确保每次换位产生新样本。
4. 换到第二张地图，重复步骤 2。
5. 将完整日志交回，重点保留：

```text
[PortalBsp][RuntimeOracle]
[PortalBsp][RuntimeOraclePoint]
[PortalBsp][RuntimeOracleBrush]
```

### 游戏日志最低通过条件

- 两张地图均无崩溃。
- `levelLeafCount > 0`。
- `brushCountExact=true` 且 `brushCount > 0`。
- `index=brushCount-1 valid=true`。
- `index=brushCount valid=false`。
- 同一地图重复放门时 `levelLeafCount` 和 `brushCount` 稳定。
- 换图后允许数量不同，但同一张地图内必须自洽。

如果调用后崩溃、`brushCount=0` 或边界关系不成立，应停止 offset 推导，先重新确认 L4D2 的 slot 16 ABI。

## 2. IDA 中需要取得的数据

请以本轮运行日志确认的 RVA 为准，分析以下三个入口：

- `engine.dll+0x17AF30`：`GetBrushInfo`
- `engine.dll+0x17AD90`：`GetBrushesInAABB`
- `engine.dll+0x177760`：`GetLeafContainingPoint`

每个函数请复制“完整伪代码 + 完整反汇编”，并包含函数起止地址、IDA image base 和所有绝对全局操作数。只给局部伪代码不足以区分全局对象地址、字段地址和字段保存的堆指针。

### 2.1 `GetBrushInfo` 必找数据

请标注以下指令对应的绝对地址和反汇编：

1. `brushIndex < numbrushes` 的上界比较。
2. 加载 `map_brushes` 指针的指令。
3. `brushIndex * 8` 的 brush 地址计算。
4. 读取 `cbrush_t.contents`、`numsides`、`firstbrushside` 的指令。
5. `numsides == 0xFFFF` 的 box brush 分支。
6. 普通 brush 分支加载 `map_brushsides` 的指令。
7. box brush 分支加载 `map_boxbrushes` 的指令。
8. 上述绝对地址各自的交叉引用列表。

这一步确定：`map_brushes`、`numbrushes`、`map_brushsides`、`map_boxbrushes`，并证明 `cbrush_t` 步长为 8。

### 2.2 `GetLeafContainingPoint` 必找数据

从入口继续进入它调用的 `CM_PointLeafnum`/递归或循环遍历函数，复制至少两层调用。请标注：

1. BSP 根节点指针的全局读取。
2. `cnode_t` 地址计算，预期步长为 12。
3. plane 指针、两个 child 的读取。
4. 根据点和平面选择 child 的分支。
5. 负 child 转 leaf 的表达式，预期为 `leaf = -1 - child` 或 `~child`。
6. 任何 `numplanes`、node/leaf 边界检查及其绝对全局地址。

参考分支中 `map_rootnode` 是 offset `0`。如果 L4D2 指令直接读取全局对象首字段，该字段地址可作为 `g_BSPData` 基址的最强候选，但仍需和另外两个函数收敛。

### 2.3 `GetBrushesInAABB` 必找数据

主体可能通过 `TraceInfo_t::m_pBSPData` 间接访问，因此请继续跟进 `BeginTrace` 或等效初始化函数，标注：

1. 把 BSP 对象地址写入 trace context 的指令。
2. `numleafs` 和 `map_leafs` 的读取。
3. `cleaf_t.firstleafbrush`、`cleaf_t.numleafbrushes` 的读取。
4. `map_leafbrushes[first+i]` 的 `uint16` 读取。
5. `map_brushes[brushIndex]` 和 contents mask 判断。
6. `numleafbrushes`、`numbrushsides`、`numboxbrushes` 的边界校验引用；如果本函数没有，请提供加载 BSP lump 的写交叉引用。

这一步确定：`map_leafs`、`numleafs`、`map_leafbrushes`、`numleafbrushes`，并对 `map_brushes` 和 `g_BSPData` 基址做第二次验证。

### 2.4 请按此格式回传每个候选字段

```text
函数/RVA：
指令地址：
原始字节：
反汇编：
IDA 伪代码：
绝对全局地址：
该地址表示：对象 / 字段 / 字段保存的指针（请选择）
交叉引用：
你的备注：
```

## 3. CSGO 参考源码结论

`Reference Code/CSGO-Source-Code/cstrike15_src/engine/cmodel_private.h` 能证明成员语义和访问链，但不能直接提供 L4D2 Windows offset。

### CSGO x86 retail 可推导核心布局

| Offset | 成员 |
|---:|---|
| `0x00` | `map_rootnode` |
| `0x64` | `numbrushsides` |
| `0x68` | `map_brushsides` |
| `0x6C` | `numboxbrushes` |
| `0x70` | `map_boxbrushes` |
| `0x7C` | `numnodes` |
| `0x80` | `map_nodes` |
| `0x84` | `numleafs` |
| `0x88` | `map_leafs` |
| `0x94` | `numleafbrushes` |
| `0x98` | `map_leafbrushes` |
| `0xA4` | `numbrushes` |
| `0xA8` | `map_brushes` |

### 与 L4D2 Linux 已知布局的冲突

wallhack gamedata 给出的 L4D2 Linux 布局是：

| 成员 | Offset |
|---|---:|
| `map_rootnode` | `0x00` |
| `map_brushsides` / `numbrushsides` | `0x68` / `0x6C` |
| `map_boxbrushes` / `numboxbrushes` | `0x74` / `0x78` |
| `map_leafs` / `numleafs` | `0x98` / `0x9C` |
| `map_leafbrushes` / `numleafbrushes` | `0xAC` / `0xB0` |
| `map_brushes` / `numbrushes` | `0xC4` / `0xC8` |

CSGO 多数是“count→pointer”，L4D2 Linux 多数是“pointer→count”，且中间成员排列不同。这已经足以否定“直接复制 CSGO offset”的做法。

### 可复用的结构不变量

- `cbrush_t` 大小为 8：`contents +0`、`numsides +4`、`firstbrushside +6`。
- `numsides == 0xFFFF` 表示 box brush，否则 `firstbrushside + numsides <= numbrushsides`。
- `cbrushside_t` 大小为 8。
- leaf 中的 `firstleafbrush + numleafbrushes` 不得超过全局 `numleafbrushes`。
- `map_leafbrushes` 元素为 `uint16`，每项必须小于 `numbrushes`。
- node 的负 child 通过 `-1-child` 解码成 leaf。
- `map_rootnode` 通常与 `map_nodes` 基址一致。

## 4. offset 的最终判定规则

每个字段必须通过以下检查：

1. 从 IDA 的字段绝对地址减去同一个候选 `g_BSPData` 基址，得到稳定 offset。
2. 不同函数推导出的候选基址完全相同。
3. IDA 中碰撞 BSP 的 `numleafs` 等于游戏日志的 `levelLeafCount + 1`；额外项由 `emptyleaf == levelLeafCount`、`solidleaf == 0` 共同证明。
4. IDA 中的 `numbrushes` 等于游戏日志的精确 `brushCount`。
5. 用候选 `map_brushes + index*8` 读取的 contents，与三个 `RuntimeOracleBrush` 有效样本逐项一致。
6. 数组指针可读、计数合理、索引关系满足参考源码不变量。

任何一项不一致，相关 offset 保持“未验证”，不得进入 BSP 写入阶段。
