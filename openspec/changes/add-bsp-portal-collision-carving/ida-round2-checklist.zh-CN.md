# 第二轮 Windows `CCollisionBSPData` 联合取证清单

## 本轮目的

本轮仍然只做诊断，不修改 BSP 碰撞数据。目标是在两张完全不同的地图上验证候选基址和字段布局，并从 IDA 补齐 `map_rootnode`、`map_leafbrushes` 等尚未闭环的证据。

当前高可信候选为：

- `g_BSPData`：RVA `0x6883B0`
- `GetBrushInfo`：RVA `0x17AF30`
- `GetBrushesInAABB`：RVA `0x17AD90`
- `GetLeafContainingPoint`：RVA `0x177760`

IDA image base 继续设为 `0` 即可。下列地址均是 RVA，不需要加运行时 ASLR 基址。

## 一、游戏内测试

### 操作步骤

1. 测试前删除或改名旧的 `portal_l4d2_traversal.log`。
2. 进入第一张地图，在普通静态墙面分别放置蓝门和橙门；再把其中一扇门移动到另一面普通墙上一次。
3. 退出或切换到一张完全不同的第二张地图，重复上一步。
4. 保持当前 `VisualOnlyBaseline`：门应正常显示，但玩家不能穿越。
5. 返回完整日志，并说明两张地图的名称。

建议选择 BSP 规模差异明显的地图，避免两图的 leaf/brush 数量碰巧接近。

### 需要保留的日志

```text
[PortalPhysicsMode]
[PortalBsp][EngineTrace]
[PortalBsp][RuntimeOracle]
[PortalBsp][LayoutCandidate]
[PortalBsp][LayoutTable]
[PortalBsp][LayoutCrossCheck]
[PortalBsp][RuntimeOraclePoint]
[PortalBsp][RuntimeOracleBrush]
```

### 预期判据

- 两张地图均不崩溃、不可穿越。
- `LayoutCandidate valid=true`，且两张地图的 `bspRva` 都是 `0x006883B0`。
- `brushCountExact=true`，不再触及 `1,048,576` 安全上限。
- `brushes` 的 `canonical`、`validated` 和 `apiCount` 三者相等。
- 碰撞 BSP 的 `leafs canonical` 等于 `levelLeafCount + 1`；额外项是 collision empty leaf，且 `emptyLeaf == levelLeafCount`、`solidLeaf == 0`。
- 本轮旧版日志中的普通表应为 `countsMatch=true`、非空数组 `arrayReadable=true`。后续代码已将字段名升级为 `countRelationMatches`，并显式允许 nodes 的合法分配数量为逻辑数量 `+6`；最终规则以 Stage A 回溯规范为准。
- 有效 brush 样本 `directReadable=true` 且 `matches=true`；边界样本 `index=brushCount` 必须 `valid=false`，并且不会直接读取越界元素。
- 同一地图移动传送门后，布局地址与各表计数保持稳定；换图后数组地址和计数允许变化。

## 二、IDA 需要补齐的信息

请对下面四个被调函数分别提供：函数起止 RVA、完整反汇编、完整伪代码、直接调用者，以及函数内出现的全部绝对全局地址。不要只截取局部伪代码。

### P0：`sub_17A980`

这是 `GetBrushesInAABB` 将 `g_BSPData`（`offset dword_6883B0`）传入的核心函数。需要确认：

1. 第二个或相关参数如何保存、传递和访问 `g_BSPData`。
2. 对 leaf 索引的处理，以及 `cleaf_t` 步长。
3. `firstleafbrush`、`numleafbrushes` 的字段偏移。
4. `map_leafbrushes` 的数组指针、全局计数和元素宽度（预期 `uint16`）。
5. leafbrush 得到 brush index 后如何访问 `map_brushes[index]`。
6. contents mask 判断和所有边界检查。

重点验证这组三元组：

```text
0x688458 = g_BSPData + 0xA8  候选规范 numleafbrushes
0x68845C = g_BSPData + 0xAC  候选 map_leafbrushes
0x688460 = g_BSPData + 0xB0  候选镜像计数
```

### P0：`sub_14B130`

`GetLeafContainingPoint`（`0x177760`）只是跳板，真正逻辑在这里。需要确认：

1. BSP 根节点从全局还是参数取得。
2. 是否直接使用 `0x6883B0`，从而证明 `map_rootnode` 位于 `g_BSPData + 0x00`。
3. `cnode_t` 的步长、plane 指针、两个 child 字段偏移。
4. 根据点与平面选择 child 的分支。
5. 负 child 转 leaf 的公式（`-1-child` 或 `~child`）。
6. node/leaf/plane 的边界检查及相关全局地址。

### P1：`sub_A4720`

`GetBrushInfo` 的 box-brush 分支以 `ecx = 0x688424` 调用它。需要确认它是否为 `CRangeValidatedArray::operator[]` 或等价逻辑：

1. `[ecx+0]` 是否为数组指针。
2. `[ecx+4]` 是否为镜像计数。
3. 索引越界时的行为。
4. 返回的是元素地址、元素内容还是其他值。

这将验证：

```text
0x688420 = g_BSPData + 0x70  候选规范 numboxbrushes
0x688424 = g_BSPData + 0x74  map_boxbrushes
0x688428 = g_BSPData + 0x78  候选镜像计数
```

### P1：`sub_14C3F0`

它由 `GetBrushesInAABB` 调用并生成 leaf 列表。需要确认：

1. 它是否读取 `map_leafs` 或 BSP 根节点。
2. leaf 记录步长是否为 `16`。
3. leaf 数量的边界检查。
4. 是否引用以下三元组：

```text
0x688444 = g_BSPData + 0x94  规范 numleafs
0x688448 = g_BSPData + 0x98  map_leafs
0x68844C = g_BSPData + 0x9C  镜像计数
```

## 三、全局数据交叉引用

请在 IDA 对以下地址逐个执行“列出所有交叉引用”，复制完整 Xref 列表；若某地址没有 Xref，也请明确写“无 Xref”。

```text
0x6883B0
0x688414  0x688418  0x68841C
0x688420  0x688424  0x688428
0x688444  0x688448  0x68844C
0x688458  0x68845C  0x688460
0x688470  0x688474  0x688478
```

如果 Xref 指向地图加载/释放函数，请额外提供这些函数中对相邻字段连续写入或清零的完整代码。加载函数通常能一次性确认“规范计数 → 指针 → 镜像计数”的真实布局。

## 四、回传格式

```text
函数名/RVA：
函数范围：
完整反汇编：
完整伪代码：
直接调用者：
涉及的全局地址及语义：
相关全局地址的全部 Xref：
你的备注（可选）：
```

游戏日志与 IDA 信息可以分文件提供；关键是保留完整上下文和原始地址，不要先手工重命名后只留下推测结果。
