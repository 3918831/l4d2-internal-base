# CCollisionBSPData 第三轮：完整只读布局验证清单

## 本轮目的与安全边界

本轮把前两轮已经确认的 `g_BSPData`、brush、leaf 等证据收进一个受检的只读数据层，并首次同时验证 plane、node、collision leaf 特殊项和 collision model。仍处于 Stage A：

- 只读取 `engine.dll` 和当前地图的 BSP hunk 内存；
- 不写 brush contents，不挖空墙体；
- 保持 `VisualOnlyBaseline`，玩家应当仍然无法穿越；
- 任一字段不可读、数量不一致、范围溢出或不变量失败时，整个 snapshot 的 `ready=false`；
- `LevelShutdown` 在引擎释放地图前清空 BSP base、数组指针和地图快照，禁止跨地图复用旧地址。

## 目标二进制锚点

| 项目 | 值 |
|---|---|
| 游戏目录标识 | L4D2 `2.2.4.3` |
| `engine.dll` 路径 | `J:\Games\Left 4 Dead 2 v2.2.4.3\bin\engine.dll` |
| 文件大小 | `4,817,088` bytes |
| PE 时间戳 | `0x67465ACA`（UTC `2024-11-26 23:33:30`） |
| SHA-256 | `677BBDC3430879A3C2BF25BD3A181791F2286E540260C946425746456CD3CE2F` |
| IDA image base | `0` |
| `g_BSPData` RVA | `0x006883B0` |
| 运行时定位来源 | `IEngineTrace::GetBrushInfo` 的语义操作数解码 |

以上锚点只适用于这一份 `engine.dll`。以后 DLL hash 变化时必须重新验证，不应把 RVA 当作跨版本常量。

## 本轮读取的最小布局

下表中的三个地址依次表示“规范数量、数组指针、构建后镜像数量”。数组跨度使用 `规范数量 × 步长` 验证为完整可读，而不是只检查首元素。

| 表/字段 | 相对 `g_BSPData` | IDA 绝对 RVA | 步长 | 当前证据状态 |
|---|---:|---:|---:|---|
| `map_rootnode` | `+0x00` | `0x6883B0` | 指针 | IDA + 结构不变量 |
| brushsides 三元组 | `+0x64/+0x68/+0x6C` | `0x688414/18/1C` | `8` | IDA + 两图运行时 |
| boxbrushes 三元组 | `+0x70/+0x74/+0x78` | `0x688420/24/28` | `48` | IDA + 两图运行时 |
| planes 三元组 | `+0x7C/+0x80/+0x84` | `0x68842C/30/34` | `20` | IDA + 双地图运行时已闭环 |
| nodes 三元组 | `+0x88/+0x8C/+0x90` | `0x688438/3C/40` | `12` | IDA + 双地图运行时已闭环；分配数量固定为逻辑数量 `+6` |
| leafs 三元组 | `+0x94/+0x98/+0x9C` | `0x688444/48/4C` | `16` | IDA + 两图运行时 |
| `emptyleaf/solidleaf` | `+0xA0/+0xA4` | `0x688450/54` | `uint32` | IDA 交叉引用 + 双地图运行时不变量已闭环 |
| leafbrushes 三元组 | `+0xA8/+0xAC/+0xB0` | `0x688458/5C/60` | `2` | IDA + 两图运行时 |
| cmodels 三元组 | `+0xB4/+0xB8/+0xBC` | `0x688464/68/6C` | 至少首模型 `40` bytes | IDA 调用链 + 双地图运行时已闭环；当前只消费数量、指针和首模型可读性 |
| brushes 三元组 | `+0xC0/+0xC4/+0xC8` | `0x688470/74/78` | `8` | IDA + `GetBrushInfo` 运行时真值 |

当前代码要求以下不变量同时成立才输出 `ready=true`：

1. 七个数组的逻辑数量和分配数量均为正且不超过 `1,048,576`；普通表要求二者相等，nodes 要求分配数量等于逻辑数量加 6；
2. 七个数组指针非空，按分配数量计算的完整数组跨度均可读且地址运算不溢出；
3. `map_rootnode == map_nodes`；
4. collision BSP 的 `numleafs == IEngineClient::LevelLeafCount() + 1`；
5. `emptyleaf == IEngineClient::LevelLeafCount()`，`solidleaf == 0`；
6. cmodel 数量为正、规范/镜像数量一致，数组非空，首模型前 `40` bytes 可读；
7. `GetBrushInfo` 推出的 brush 数量等于 BSP brushes 规范数量。

## 游戏内实测方法

### 测试准备

1. 使用本轮编译部署的 `L4D2_Portal.exe` 和 `L4D2_Portal.dll` 启动游戏。
2. 整轮测试尽量只启动一次游戏进程，先进入地图 A，再通过正常换图或控制台 `map` 命令进入地图 B。这样日志中的地图代际和生命周期失效才可直接比较。
3. 选择两张 BSP 规模明显不同的地图；不要在同一地图重复两次充当“两图验证”。
4. 优先把门放在普通 world brush 的平整墙面。至少额外尝试一次地板、天花板或斜面；记录哪些位置可能是 displacement、静态模型或实体表面。

### 每张地图的动作

1. 放置蓝门，等待日志产生一次 blue snapshot。
2. 放置橙门，等待日志产生一次 orange snapshot。
3. 走向任一传送门并持续向墙体移动，确认视觉正常但完全不能穿越。
4. 把其中一扇门移动到同一地图的另一块墙体，再等待一次 snapshot。
5. 如果方便，在普通墙、地板/天花板、斜面各放置一次；无需穷举，也不要为了本轮诊断修改游戏设置。
6. 换到第二张完全不同的地图，重复上述动作。

### 需要从日志检查的标记

```text
[PortalPhysicsMode]
[PortalBsp][Offset]
[PortalBsp][LayoutCandidate]
[PortalBsp][LayoutState]
[PortalBsp][LayoutTable]
[PortalBsp][RootInvariant]
[PortalBsp][LeafInvariant]
[PortalBsp][CollisionModels]
[PortalBsp][LayoutCrossCheck]
[PortalBsp][Lifecycle]
```

### 通过判据

- 启动时仍为 `VisualOnlyBaseline`，`movementMutation=false`、`legacyCollisionBypass=false`、`teleport=false`；
- 两张地图均无崩溃、卡死和穿越；
- 每次 snapshot 都有七条不同 `table=` 的 `LayoutTable`；
- 七张表均为 `fieldsReadable=true`、`countRelationMatches=true`、`spanReadable=true`、`valid=true`；nodes 还必须输出 `expectedAllocatedDelta=6`，其他表为 `0`；
- `RootInvariant` 中 `rootMatchesNodes=true`；
- `LeafInvariant` 的三个子判据与总 `valid` 均为 `true`；
- `CollisionModels valid=true`；
- `LayoutCrossCheck apiMatchesCanonical=true`；
- 同一地图移动传送门时，base、各表地址、计数和 generation 保持稳定；
- 换图时出现 `Lifecycle cacheInvalidated=true`，新地图重新生成 snapshot，generation 增加，数组地址/数量按新地图合理变化。

出现任一 `ready=false`、`valid=false`、异常数量、缺少七张表、游戏崩溃或换图后仍复用旧地址时，不要继续做碰撞写入实验，直接保留完整日志。

## 第三轮结果与本次修正

- `ssv1` 的 nodes 为 `logical=549`、`allocated=555`，`c1m2_streets` 为 `logical=4165`、`allocated=4171`；两图都严格相差 6。
- IDA 中的 nodes 构建路径对数组执行 `Attach(count + 6)`，同时把逻辑节点数保存为 `count`；额外六项用于引擎 box hull。
- `sub_14B0B0` 进一步确认 node 步长 `12`、plane 步长 `20`、node 的 plane/children 字段，以及负 child 的 `-1-child` leaf 解码。
- 旧版通用“两个数量必须相等”校验错误地把合法 nodes 判为无效，造成所有 snapshot 的唯一失败项。代码现已把各表的计数关系显式放入布局，并按分配数量验证完整跨度。
- 本轮不再需要补充 IDA 信息。下一次游戏内验证只需确认修正后的双地图 snapshot 全部 `ready=true`；在此之前 0.10 写入门禁仍保持关闭。
- 测试中临时出现的传送门 `(0,0,0)` 是垂直向脚下放置失败留下的状态，属于后续放置逻辑问题，不作为 BSP 布局失败依据。

## 已完成的 IDA 信息清单（留档）

### A. 最高优先级：`sub_14B0B0`

`sub_14B130` 已证明：当 `dword_68842C` 非零时，以 `&dword_6883B0`、点和 `0` 调用 `sub_14B0B0`。请提供 `sub_14B0B0` 的：

1. 完整函数起止 RVA、完整反汇编和完整伪代码；
2. 对 `0x68842C/0x688430/0x688434` 的全部访问；
3. 对 `0x688438/0x68843C/0x688440` 的全部访问；
4. plane 的元素步长、法线/距离字段偏移，以及 node 如何引用 plane；
5. node 的元素步长、两个 child 的字段偏移和正负 child 的解释；
6. 负 child 转 leaf 的精确表达式（`~child` 或 `-1-child`）；
7. 根节点入口究竟直接使用 `map_rootnode`，还是通过 cmodel 的 `headnode`；
8. 所有数量边界检查及错误分支。

这份函数用于最终证明 planes、nodes、root 和 leaf traversal，不要只复制局部伪代码。

### B. 高优先级：`sub_14B2E0`

`sub_14C3F0` 已知会把 `dword_688468 + 0x24` 作为最后一个参数传给 `sub_14B2E0`。请提供：

1. `sub_14B2E0` 的完整函数起止 RVA、完整反汇编和完整伪代码；
2. 最后一个参数的语义，是否为 `cmodel_t::headnode`；
3. 递归/迭代遍历 node、plane、leaf 的所有字段访问；
4. 所有 child、leaf、node、plane 的边界检查；
5. 该函数写入 `v12[0]` 的内容语义；
6. 若调用了子函数，请一并给出直接影响 BSP 字段解释的子函数。

### C. 交叉引用补齐

上轮汇总已经覆盖 brushsides、boxbrushes、leafs、leafbrushes、brushes。本轮请另外导出以下地址的交叉引用表，格式沿用“地址、Direction、Type、调用函数、指令文本”：

- planes：`0x68842C`、`0x688430`、`0x688434`；
- nodes：`0x688438`、`0x68843C`、`0x688440`；
- collision 特殊 leaf：`0x688450`、`0x688454`；
- cmodels：`0x688464`、`0x688468`、`0x68846C`。

对每组三元组，优先再提供一个“写入/构建数组”的函数，而不只是读取函数。目标是证明：

- 第一个字段是构建前或规范数量；
- 第二个字段是数组指针；
- 第三个字段是构建完成后的镜像/校验数量；
- 数组元素的真实步长与代码中的地址运算一致。

### D. cmodel 结构补充

请围绕 `sub_14C3F0` 的 `dword_688468 + 0x24` 补充：

1. `dword_688468` 指向的数组元素步长；
2. `+0x24` 字段是否明确为 `headnode`；
3. cmodel 数量 `0x688464/0x68846C` 的写入来源；
4. 如果 IDA 能恢复结构，请导出首个至少 `0x28` bytes 的字段布局；无需猜测未被代码使用的后续字段。

## 需要人工随日志一并说明的信息

这些信息无法仅从 DLL 日志可靠推断，请在回传时简单注明：

1. 两张地图的准确名称，以及是否在同一次游戏进程内换图；
2. 每张地图实际测试过的表面类型（普通墙、地板、天花板、斜面、疑似 displacement/模型/实体）；
3. 是否始终无法穿越，是否出现碰撞变化、短暂卡入墙体、崩溃或换图异常；
4. 哪一次放置对应哪类表面；若某处 `worldContents=0` 或不符合直觉，最好附地点说明或截图；
5. 完整的 `portal_l4d2_traversal.log`，不要只截取 `ready=true` 行，因为需要比较同图稳定性和换图失效顺序。

## 本轮完成后的决策

- 若修正版在两张地图全部 `ready=true`，则结合已经完成的 planes、nodes、cmodels IDA 证据完成 Stage A 的 Windows 布局门禁，下一步进入“纯 BSP 查询”的 TDD：先实现只读 `PointLeafNum` 和 brush 定位，仍不写碰撞。
- 若运行时成立但 IDA 证据不足，则继续补静态证据，不进入写入。
- 若任何运行时不变量失败，则优先修正 offset、步长或不变量；禁止放宽校验来获得 `ready=true`。
