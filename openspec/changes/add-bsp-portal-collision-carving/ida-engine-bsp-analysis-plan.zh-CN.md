# 使用 IDA 分析 Windows `engine.dll` BSP 碰撞数据方案

## 1. 目标

本方案用于在目标 Windows x86 L4D2 版本中完成两项工作：

1. 定位全局 `CCollisionBSPData g_BSPData` 对象，或能够稳定计算其地址的代码引用。
2. 验证 Linux SourceMod gamedata 中的字段偏移和运行时元素尺寸是否适用于 Windows。

最终产出必须是可验证的证据链，而不是一个“看起来能用”的绝对地址：

```text
EngineTraceClient003 vtable 槽位
→ 目标 engine.dll 函数 RVA
→ 函数中的 BSP 字段访问指令
→ 字段绝对地址
→ 反推 g_BSPData 基址
→ 多字段一致性验证
→ 多地图运行时只读验证
```

本阶段禁止修改 brush contents。

## 2. 已知证据

### 2.1 Linux 字段布局候选

SourceMod 插件通过 `@g_BSPData` 取得全局对象地址，然后按下列字节偏移读取字段：

| 偏移 | 字段 | 预期类型 |
|---:|---|---|
| `0` | `map_rootnode` | `CNode*` |
| `104` | `map_brushsides` | `CBrushSide*` |
| `108` | `numbrushsides` | `int` |
| `116` | `map_boxbrushes` | `CBoxBrush*` |
| `120` | `numboxbrushes` | `int` |
| `152` | `map_leafs` | `CLeaf*` |
| `156` | `numleafs` | `int` |
| `172` | `map_leafbrushes` | `uint16_t*` |
| `176` | `numleafbrushes` | `int` |
| `196` | `map_brushes` | `CBrush*` |
| `200` | `numbrushes` | `int` |

候选运行时元素尺寸：

| 类型 | 候选尺寸 |
|---|---:|
| `CNode` | `12` |
| `CLeaf` | `16` |
| `CBrush` | `8` |
| `CBrushSide` | `8` |
| `CBoxBrush` | `48` |

这些值只能作为 Windows 逆向时的结构假设，不能直接写入生产代码。

### 2.2 当前工程的稳定入口

工程已经从 `engine.dll` 取得 `EngineTraceClient003`，并正常 Hook `GetLeafContainingPoint`。L4D2 当前接口布局中的关键槽位候选为：

| 槽位 | 方法 |
|---:|---|
| `0` | `GetPointContents` |
| `1` | `GetPointContents_WorldOnly` |
| `5` | `TraceRay` |
| `14` | `GetBrushesInAABB` |
| `16` | `GetBrushInfo` |
| `17` | `PointOutsideWorld` |
| `18` | `GetLeafContainingPoint` |

新增诊断会输出各槽位的运行时地址、相对 `engine.dll` 的 RVA，以及地址是否位于模块范围内。IDA 应优先使用实机日志中的 RVA，不依赖字符串搜索或猜测函数边界。

## 3. 分析输入

### 3.1 目标二进制

必须分析与实机运行完全相同的文件：

```text
J:\Games\Left 4 Dead 2 v2.2.4.3\bin\engine.dll
```

记录：

- 文件大小
- PE 时间戳
- SHA-256
- ImageBase
- `.text`、`.rdata`、`.data` 区段范围
- 实机日志中的加载基址和 `SizeOfImage`

禁止使用另一套 L4D2 安装目录中的同名 DLL 替代。

### 3.2 运行时诊断日志

从 `[PortalBsp][EngineTrace]` 日志提取：

```text
moduleBase
moduleSize
interface
vtable
slot
method name
runtime address
RVA
insideEngine
```

所有目标槽位必须满足 `insideEngine=true`。如果槽位 18 不能继续正常返回 leaf，先修正接口布局，不进入 BSP 分析。

## 4. IDA 工程准备

1. 以 PE 方式加载 `engine.dll`，处理器选择 32-bit x86。
2. 允许 IDA 完成自动分析、函数识别、导入和重定位处理。
3. 确认 IDA ImageBase 与 PE Optional Header 一致。
4. 建议建立独立 IDB/I64，不修改目标 DLL。
5. 根据日志 RVA 创建并命名函数：

```text
EngineTrace_GetPointContents
EngineTrace_GetPointContentsWorldOnly
EngineTrace_TraceRay
EngineTrace_GetBrushesInAABB
EngineTrace_GetBrushInfo
EngineTrace_PointOutsideWorld
EngineTrace_GetLeafContainingPoint
```

如果某个 RVA 落在 thunk：

- 跟随无条件 `jmp`；
- 记录 thunk RVA 和实际实现 RVA；
- 后续签名建立在实际实现或稳定 thunk 上；
- 不把 thunk 内的导入跳转表地址误认为 BSP 数据地址。

## 5. 首选分析：`GetBrushInfo`

`GetBrushInfo(int brushIndex, ..., int* contentsOut)` 是最强锚点，因为它必须完成 brush 索引验证、brush 数组定位和 contents 读取。

### 5.1 需要识别的语义

预期伪代码：

```cpp
if (brushIndex < 0 || brushIndex >= g_BSPData.numbrushes)
    return false;

CBrush* brush = &g_BSPData.map_brushes[brushIndex];
*contentsOut = brush->contents;
```

在反汇编中重点寻找：

- `brushIndex` 与一个全局 `int` 比较；
- 从一个全局地址加载 brush 数组指针；
- `brushIndex * 8`，或等价的移位/LEA；
- 读取元素偏移 `0` 作为 contents；
- 读取元素偏移 `4`、`6` 作为 side 数量和首 side 索引；
- 遍历 brush side/plane 并填充输出平面数组。

### 5.2 反推候选基址

若得到两个指令操作数地址：

```text
field_map_brushes
field_numbrushes
```

按 Linux 候选布局分别反推：

```text
candidateA = field_map_brushes - 196
candidateB = field_numbrushes - 200
```

只有 `candidateA == candidateB` 才能把它记录为高可信 `g_BSPData` 候选。

必须记录每个操作数来自哪条指令、指令 RVA、数据交叉引用和反推计算过程。

### 5.3 易混淆项

必须区分：

```text
&g_BSPData                  全局对象地址
&g_BSPData.map_brushes      字段地址
g_BSPData.map_brushes       字段中保存的堆数组地址
&map_brushes[index]         某个运行时 brush 地址
```

IDA 中绝对内存操作数通常先给出“字段地址”，而不是字段中保存的数组地址。

## 6. 第二分析：`GetLeafContainingPoint`

该方法必须从 BSP 根节点开始遍历并返回 leaf。

### 6.1 需要识别的语义

预期行为：

```text
nodeIndex = 0
读取 node plane
计算 dot(normal, point) - dist
根据符号选择 child[0] 或 child[1]
负 child 使用按位取反得到 leafIndex
```

在反汇编中寻找：

- `CNode` 步长 `12` 的乘法/LEA；
- plane 指针位于 node `+0`；
- child 位于 node `+4` 和 `+8`；
- plane normal 三个 float 和 `dist`；
- 负 node index 检查和 `not`；
- 对 `map_rootnode` 字段或 `g_BSPData` 基址的引用。

### 6.2 与候选基址交叉验证

如果 `map_rootnode` 候选偏移为 `0`：

```text
&g_BSPData.map_rootnode == candidateBase
```

需要判断函数使用的是：

- 字段地址；
- 字段中保存的 root node 数组指针；
- 或把 `&g_BSPData` 传给内部 `CM_PointLeafnum` 类函数。

跟进一层直接调用，直到找到实际 node 遍历或明确的数据引用。

## 7. 第三分析：`GetBrushesInAABB`

该方法可能同时访问 node、leaf、leafbrush、brush 和 contents，适合验证剩余字段是否属于同一个全局对象。

重点寻找：

- `CLeaf` 步长 `16`；
- leaf 中 `firstLeafBrush` 和 `numLeafBrushes` 的 16-bit 读取；
- leafbrush 数组的 16-bit brush 索引；
- `CBrush` 步长 `8`；
- contents mask 过滤；
- 与 `candidateBase + 152`、`+172`、`+196` 对应的数据引用。

若优化器将部分访问移入内部辅助函数，应沿直接调用链分别命名，例如：

```text
CM_BoxLeafnums
CM_EnumerateLeafBrushes
CM_GetBrushesInAABB_Internal
```

名称可以是推断名，但文档必须标注“推断”，不能冒充原始符号。

## 8. 其他辅助入口

### `GetPointContents_WorldOnly`

适合查找 `map_brushes`、brush sides 和 contents mask 的实际使用，但调用链可能更深。

### `TraceRay`

覆盖范围最大，适合作最终交叉验证，不适合作为最先分析的入口。

### 地图加载/释放函数

对候选 `g_BSPData` 查看写交叉引用，可以找到：

- BSP lump 加载；
- 数组分配；
- 数量赋值；
- 换图清理或清零。

如果同一函数连续写入 `base+196` 和 `base+200`，其可信度高于只读调用点。

## 9. Windows 字段布局验证表

每个字段都要形成如下记录：

| 字段 | Linux 候选偏移 | Windows 访问指令 RVA | 操作数地址 | 反推 base | 可信度 |
|---|---:|---:|---:|---:|---|
| `map_rootnode` | `0` | 待填 | 待填 | 待填 | 待验证 |
| `map_brushsides` | `104` | 待填 | 待填 | 待填 | 待验证 |
| `numbrushsides` | `108` | 待填 | 待填 | 待填 | 待验证 |
| `map_boxbrushes` | `116` | 待填 | 待填 | 待填 | 待验证 |
| `numboxbrushes` | `120` | 待填 | 待填 | 待填 | 待验证 |
| `map_leafs` | `152` | 待填 | 待填 | 待填 | 待验证 |
| `numleafs` | `156` | 待填 | 待填 | 待填 | 待验证 |
| `map_leafbrushes` | `172` | 待填 | 待填 | 待填 | 待验证 |
| `numleafbrushes` | `176` | 待填 | 待填 | 待填 | 待验证 |
| `map_brushes` | `196` | 待填 | 待填 | 待填 | 待验证 |
| `numbrushes` | `200` | 待填 | 待填 | 待填 | 待验证 |

可信度建议：

- **低：** 只根据 Linux 偏移或相似常量猜测。
- **中：** 一个 Windows 函数中发现符合语义的数据引用。
- **高：** 两个独立字段反推同一 base，并在不同函数中交叉引用。
- **已验证：** 实机只读数据、引擎接口结果和多地图测试全部一致。

## 10. 签名设计

不得把静态绝对地址直接保存为 offset，因为 ASLR 和游戏更新会改变地址。

首选签名目标是 `GetBrushInfo` 中同时体现以下语义的短代码段：

```text
读取/比较 numbrushes
读取 map_brushes
按 brushIndex * 8 定位元素
读取 contents
```

绝对地址四字节必须使用通配符。运行时流程：

```text
扫描稳定代码
→ 解码 map_brushes 字段地址
→ 解码 numbrushes 字段地址
→ 分别减去已验证字段偏移
→ 两个 base 必须相等
→ 执行完整只读结构验证
```

如果编译器只在不同函数中暴露两个字段，可以使用两个独立签名；两个结果一致仍是启用条件。

签名记录必须包含：

- 完整字节模式与通配符；
- 匹配所在方法和 RVA；
- 从匹配起点到地址操作数的字节偏移；
- 操作数是绝对字段地址、间接指针还是相对位移；
- 目标 DLL SHA-256；
- 至少一个防误匹配的语义检查。

## 11. 实机只读验收

IDA 得到候选后，先实现只读验证，不实现写接口。

### 11.1 地址与数量验证

- `g_BSPData` 字段地址应位于 `engine.dll` 可读数据区。
- 数组指针应指向当前进程中已提交、可读的内存。
- 数量必须为合理正值，并设置保守上限。
- 指针加 `count * elementSize` 不得溢出。
- 换图时对象字段保持可解析；数组指针和数量允许变化。

### 11.2 Leaf 双重验证

用自定义只读 BSP 遍历计算：

```cpp
customLeaf = PointLeafNum(point);
engineLeaf = I::EngineTrace->GetLeafContainingPoint(point);
```

对玩家眼睛、玩家脚下、两扇门位置和多个世界坐标比较，结果必须一致。

### 11.3 Brush 双重验证

同一 brush index 的自定义 contents 读取必须与 `IEngineTrace::GetBrushInfo` 输出一致。该验证同时证明：

- `map_brushes` 字段正确；
- `numbrushes` 字段正确；
- `CBrush` 步长正确；
- contents 位于元素偏移 `0`。

### 11.4 多地图验证

至少选择两张 BSP 结构差异明显的地图。每张地图都记录：

- 所有数组地址和数量；
- 自定义 leaf 与引擎 leaf 比较结果；
- 多个 brush contents 与引擎接口比较结果；
- 地图关闭前后的代际与失效行为。

## 12. 停止条件

遇到任一情况必须停止进入写入阶段：

- 关键槽位地址不在 `engine.dll`；
- 两个字段反推的 base 不一致；
- 任一数量或指针不合理；
- 自定义 leaf 与引擎 leaf 不一致；
- 自定义 brush contents 与 `GetBrushInfo` 不一致；
- 只有单地图偶然成功；
- 签名在目标 DLL 中匹配多处且无法通过语义消歧；
- 需要靠放宽越界或地址校验才能继续。

## 13. 最终交付清单

- [ ] 目标 `engine.dll` 文件元数据和 SHA-256
- [ ] EngineTrace 关键槽位的运行时地址和 RVA
- [ ] IDA 数据库中的目标函数命名
- [ ] `GetBrushInfo` 反汇编/伪代码与字段访问证据
- [ ] `GetLeafContainingPoint` 调用链和 root node 证据
- [ ] `GetBrushesInAABB` 的 leaf/leafbrush/brush 证据
- [ ] 完整 Windows 字段布局验证表
- [ ] 可运行的代码签名及操作数解码规则
- [ ] 多字段反推同一 `g_BSPData` 的计算
- [ ] 多地图只读一致性日志
- [ ] 仍未确认的字段、结构尺寸和风险清单

完成以上内容后，才能在后续实现中把 Windows 布局从“候选”提升为“已验证”，并进入可恢复 brush contents 写入阶段。
