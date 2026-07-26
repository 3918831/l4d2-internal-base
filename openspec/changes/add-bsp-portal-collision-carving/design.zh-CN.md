## 背景

当前传送门视觉路径已经比较成熟。物理穿越由 `CPortalTransitionSimulator` 跟踪接近、相交、提交和离开阶段；`PortalTransform` 负责传送门局部几何与入口到出口的变换；`PortalPlayerTeleport` 负责权威的本地服务端传送；`CPortalCollisionBridge` 与受控 noclip 则负责绕过客户端和服务端移动中的墙体碰撞。

`Reference Code/sourcemod-wallhack/` 中的 SourcePawn 参考实现展示了另一条路径：定位 `g_BSPData`，遍历 BSP node 找到 leaf，从 leaf 引用的 brush 中寻找 solid brush，判断一个点是否处于凸 brush 或 box brush 内，保存原始 `contents`，然后写入 `CONTENTS_EMPTY`。其 gamedata 只支持 Linux，因此符号和偏移只能作为算法证据，不能直接用于 Windows。

本变更只面向单人/本地服务器中的本地幸存者。其他实体受到的影响只作为诊断观察，不属于验收范围。

## 目标与非目标

**目标：**

- 让原始本地玩家移动 trace 把承载传送门的世界 brush 视为可穿过通道。
- 尽量复用项目已有的传送门放置、坐标变换、穿越状态、传送、日志、接口、特征扫描和生命周期设施。
- 所有 BSP 写入都必须可恢复、幂等、限定在当前地图代际内，并且可诊断。
- 阶段一不增加附近检测，以便测量整块 brush 被移除后的原始行为。
- 阶段二和阶段三只在核心实现之上增加附近控制和可选孔径约束，不推倒重写阶段一。
- 保留与现有碰撞桥和 noclip 实现的受控对照能力。

**非目标：**

- NPC 或 AI 导航支持。
- 子弹、近战、投掷物、物理道具、普通感染者或特殊感染者的传送门行为。
- 远程服务器支持或多人独立碰撞。
- 替换传送门视觉、坐标变换、传送提交、预测同步或过渡状态。
- 修改磁盘上的 `.bsp` 文件。
- 在阶段一或阶段二重建任意 brush 的完整几何形状。

## 架构

### 组件

#### `PortalBspTypes`

定义经过验证的 Windows x86 定宽视图和算法使用的常量。在完整布局得到证明前，不应直接把整块引擎内存强转为完整 C++ 类型；所有访问都通过受检地址运算和定宽读写完成。

最小逻辑布局：

```cpp
struct PortalBspLayout {
    std::ptrdiff_t mapRootNode;
    std::ptrdiff_t mapBrushSides;
    std::ptrdiff_t numBrushSides;
    std::ptrdiff_t mapBoxBrushes;
    std::ptrdiff_t numBoxBrushes;
    std::ptrdiff_t mapLeafs;
    std::ptrdiff_t numLeafs;
    std::ptrdiff_t mapLeafBrushes;
    std::ptrdiff_t numLeafBrushes;
    std::ptrdiff_t mapBrushes;
    std::ptrdiff_t numBrushes;
};
```

Windows 目标版本的 IDA 与双地图运行时证据已经确认 `CNode=12`、`CPlane=20`、`CLeaf=16`、`CBrush=8`、`CBrushSide=8`、`CBoxBrush=48`。数组三元组的首字段表示逻辑数量，末字段表示实际分配数量；普通表二者相等，但 nodes 的分配数量固定为逻辑数量加 6。这 6 个额外节点来自引擎构造的 box hull，因此 nodes 的完整可读跨度必须使用 `(逻辑数量 + 6) * 12`，遍历地图 BSP 时仍以逻辑数量作为索引上限。该差异属于已验证的结构语义，不能通过放宽通用计数校验来掩盖。

#### `CPortalBspData`

负责保存已定位的 `g_BSPData` 地址、验证后的布局和当前地图代际。只有验证通过后才开放查询：

```cpp
bool InitializeForMap();
void InvalidateForMapChange();
bool IsReady() const;
std::optional<int> FindBrushForSurfacePoint(
    const Vector& hitPosition,
    const Vector& hitNormal,
    unsigned int requiredMask) const;
std::optional<int> ReadBrushContents(int brushIndex) const;
bool WriteBrushContents(int brushIndex, int expectedCurrent, int replacement);
```

`WriteBrushContents` 必须先比较当前值与 `expectedCurrent`，如果数据陈旧或已被外部修改，必须拒绝写入。

#### `CPortalBspCollisionCarver`

负责传送门到 brush 的绑定，以及 brush 修改状态：

```cpp
enum class PortalBrushOwner { Blue, Orange };

struct PortalBrushBinding {
    int brushIndex = -1;
    int originalContents = CONTENTS_EMPTY;
    uint32_t mapGeneration = 0;
    bool resolved = false;
};

struct ModifiedPortalBrush {
    int brushIndex = -1;
    int originalContents = CONTENTS_EMPTY;
    uint8_t ownerMask = 0;
    uint32_t mapGeneration = 0;
    bool modified = false;
};
```

主要接口：

```cpp
bool BindPortalBrush(PortalBrushOwner owner,
                     const Vector& hitPosition,
                     const Vector& hitNormal);
void UnbindPortalBrush(PortalBrushOwner owner);
bool ActivatePairCarving();
void RestoreAll(const char* reason);
void UpdatePhase2(const C_TerrorPlayer* player,
                  const PortalTransitionContext& context);
```

所有者位掩码用于处理两扇门位于同一 brush 的情况。重新绑定一扇门时，只释放该门的所有权，不能提前恢复另一扇门仍在使用的 brush。

#### 阶段二激活策略

阶段二增加纯决策函数，不把距离阈值直接写进内存修改逻辑：

```cpp
struct PortalCarveActivationConfig {
    float lateralMargin = 24.0f;
    float verticalMargin = 24.0f;
    float enterDepth = 96.0f;
    float leaveDepth = 144.0f;
};

bool IsPlayerNearPortalForCarving(...);
bool ShouldKeepCarvingActive(bool nearBlue,
                             bool nearOrange,
                             PortalTransitionPhase phase,
                             bool currentlyActive);
```

本地玩家锚点和传送门局部基底必须复用 `PortalTransform`。`leaveDepth` 必须大于 `enterDepth`，形成迟滞。只要状态处于 `ApproachingPortal` 到 `ExitingPortal` 的穿越区间，即使附近采样暂时为假，也强制保持挖空。

#### 阶段三 blocker 约束

阶段三是否实施由前两阶段证据决定。可选的 `CPortalApertureBlockerSet` 复用现有 `IServerTools::CreateEntityByName`、`SetKeyValue` 和 `DispatchSpawn`，在孔径周围创建四个或更多 `env_physics_blocker`。实施前必须通过诊断实验确认目标版本中的边界、旋转、碰撞 mask、生成/删除生命周期和本地玩家行为。

### 运行流程

#### 地图初始化

```text
关卡初始化
→ 定位 Windows g_BSPData
→ 读取数组指针和数量
→ 执行结构合理性校验
→ 增加地图代际
→ 启用只读 brush 查询
```

任何失败都不得影响现有穿越方案。

#### 传送门放置

```text
现有放置射线成功
→ 复用世界命中位置和表面法线
→ 按参考偏移序列向表面内部采样
→ 定位 leaf 和候选 leafbrush
→ 要求候选 contents 与 MASK_PLAYERSOLID 相交
→ 绑定到蓝门或橙门
→ 记录 brush、leaf、contents、采样偏移和地图代际
```

如果原始放置 trace 数据可用，就不能只根据最终渲染用传送门原点猜测 brush。

#### 阶段一修改生命周期

```text
两扇门活跃且绑定有效
→ 原子验证两个绑定
→ 每个唯一 brush 只保存一次原始 contents
→ 写入 CONTENTS_EMPTY
→ 不检查玩家距离，持续保持挖空

传送门重放置/关闭、地图结束、DLL 卸载或验证失败
→ 恢复当前地图代际中仍有效的所有 brush
→ 清理相应绑定和修改状态
```

阶段一明确不使用附近检测、不创建 blocker，目的就是完整记录整块 brush 挖空的实际影响。

#### 阶段二修改生命周期

```text
两扇门有效，且玩家进入任一传送门局部激活范围
→ 激活成对挖空

玩家仍在附近，或穿越阶段仍活跃
→ 保持挖空

玩家离开较大的迟滞范围，且状态回到安全的 Idle/Cooldown
→ 恢复碰撞
```

只要仍处于跨越或离开阶段，就必须拒绝恢复。宁可延迟恢复，也不能在玩家碰撞包围盒仍位于墙内时恢复墙体。

## 设计决策

### 决策一：只修改已加载的 BSP 碰撞数据

不修改磁盘 `.bsp`。引擎已经使用内存中的碰撞表示，内存修改速度快、可恢复，并天然限定在当前地图生命周期内。

### 决策二：阶段一不设置附近约束

首次实验必须直接测量移除完整承载 brush 的后果。附近检测可能掩盖生命周期或几何问题，也会让我们难以判断 BSP 挖空本身是否真正生效。

### 决策三：保留现有穿越状态和传送权威

BSP 挖空只解决墙体阻挡。孔径相交、跨平面、坐标/速度变换、出口安全推出、冷却和预测同步继续由现有系统处理。

### 决策四：放置传送门时解析 brush

放置阶段拥有最可靠的表面证据。只解析一次既可避免逐帧遍历 BSP，也能在修改发生前拒绝无效放置。

### 决策五：把 brush 写入视为事务

激活前验证所有唯一目标；任一写入失败时立即恢复本次已经写入的目标。恢复时检查地图代际、索引范围、地址范围和预期当前值。

### 决策六：使用纯视觉基线隔离物理穿越因果关系

使用以下诊断模式矩阵：

| 模式 | BSP 挖空 | 旧碰撞桥/noclip | 用途 |
|---|---:|---:|---|
| 纯视觉基线（默认） | 关闭 | 关闭 | 确认门后视觉正常且玩家完全被原墙阻挡 |
| 写入验证 | 开启 | 开启 | 证明修改和恢复安全 |
| 因果测试 | 开启 | 关闭 | 证明仅 BSP 即可穿墙 |
| 显式旧方案对照 | 关闭或强制失败 | 开启 | 仅用于人工选择的回归对照 |

在因果测试可重复成功前不删除旧代码，但任何失败都必须回到纯视觉基线，不得自动回退到旧穿越路径。

### 决策七：阶段二使用传送门局部空间和迟滞

球形距离与传送门孔径不匹配，也可能隔着无关墙体激活。扩展后的传送门局部棱柱可以分别调节左右、上下、进入深度和离开深度。

### 决策八：用精确变换诊断隔离物理出口 push

`ExactTransform` 在完成入口到出口的变换后不施加任何出口法线方向的清离 push。它是阶段一的显式因果诊断模式，暂时不是最终的清离策略。在该模式下，现有 `ExitingPortal` 阶段继续充当空间重武装锁，直到玩家到达出口平面前方 16 单位或离开孔径。只有空间锁解除后才开始 0.20 秒的时间冷却，因此缓慢离开时不会在玩家仍与出口门重叠期间耗尽冷却。提交和离开阶段也会在策略层拒绝新的穿越入口。聚焦日志用于比较精确映射眼位、传送门渲染眼位近似值和 Teleport 后物理眼位。

### 决策九：保持精确物理落点，单独修复 Teleport 后主视图交接

第九轮证据表明，精确映射眼位与 Teleport 后物理眼位之间的误差为零，但主视图最初几帧仍可能使用墙后可见性 leaf。移动玩家会重新引入 `ExactTransform` 已消除的画面位移。因此，有界的出口交接保持物理位置不变，把经过验证的出口前方安全可见性原点追加到主视图现有 PVS 原点，并在 `ExitingPortal` 和紧接着的 `Cooldown` 交接期间继续使用缩短后的世界近裁剪。该守卫仅作用于 `ExactTransform`、传送门孔径内、有界平面距离和非递归主视图，并提供运行时 A/B 开关及仅写文件诊断。

### 决策十：先对齐官方跨面半空间，再引入官方相机交接

Portal SDK 证据确认官方服务端同样通过玩家 `Teleport` 完成传送，因此 API 本身不是首要嫌疑。权威穿越只会在玩家中心进入入口门后半空间后提交；经过 180 度变换，该点应落在出口门前半空间。普通路径在 `Touch` 内立即提交，高速路径则保留扫掠式兜底。阶段一据此把预测目标修正为入口平面后方的微小负深度，拒绝任何原始变换出口深度为负的事务，在首次有效采样已经位于门后时于同一次更新内提交，并检测连续命令锚点线段与零平面孔径的交点。

官方客户端还会在物理玩家中心跨面之前变换主相机，并在实体穿越时修复插值历史、眼位插值、viewmodel 朝向和可见性历史。如果黑天空或看到入口门背后的闪帧消失后仍有轻微顿挫，这些机制是优先的后续方向。本轮故意不把它们混入半空间修复，以便游戏内测试能判断主要缺陷究竟是否来自提交时机和提交位置。

### 决策十一：不移动玩家，在 Teleport 前完成入口相机交接

第十一轮证据已经把残留的“看到墙后”现象与已修正的 Teleport 半空间问题分离开来。缓慢穿越时，渲染眼位越过入口平面后仍可能等待约 27–53 ms 才由移动命令提交 Teleport；高速穿越也存在同类区间，并会看到已经清空碰撞的承载 brush 后方内容。Portal SDK 的 `C_Portal_Player::CalcPortalView` 给出了对应的客户端行为：当眼位位于传送门平面后方且玩家处于门洞中时，客户端会在权威玩家传送完成前，先把眼位和视角经链接传送门变换。

因此，阶段一在本地 `CalcPlayerView` 之后复用现有入口到出口矩阵，并严格限制为：状态处于 `IntersectingPortal`、原始眼位深度已为负、眼位仍在已跟踪入口孔径内、BSP 挖空活跃且门对有效。该操作只改变渲染相机；物理与预测 Teleport、移动、速度、精确出口落点、near clip、出口可见性守卫、防重入以及全部 BSP 行为均保持不变。运行时 `portal_visual_entryhandoff` 开关和仅写文件的 `[PortalEntryViewHandoff]` 证据保留受控 A/B 路径。官方插值历史修复及轻微顿挫问题明确延后处理。

### 决策十二：普通门模型遮罩 workaround 已被实测证伪

第十二轮实测仍可在 Teleport 前稳定定格条状墙体，同时 viewmodel 也被截断；日志确认相机交接指令有效、黑天空没有回归。因此问题已进一步收敛到主视图中用于写 stencil 的传送门模型被 near plane 截断，而不是 Teleport 提交、BSP 碰撞或出口落点。Portal SDK 2013 同样保持普通主视图近裁剪，并用专门的 render-fix 几何与受控 stencil/depth 状态补足近距离门面。

第十三轮证伪了普通门模型 workaround。`[PortalMaskRepair]` 在入口眼位正侧深度从约 1.99 降至 0.24 单位的过程中持续生效，但测试者仍能在可见蓝色门框内部稳定定格完整承载墙画面。由此可知，降低同一门模型的近裁剪投影并绕过深度拒绝，并不等价于 Portal SDK 依据相机生成的 render-fix mesh。相关渲染状态覆盖、运行时命令、状态字段、测试和诊断全部移除，不把已证伪实现作为休眠 workaround 保留。

### 决策十三：在移植官方 render-fix mesh 前，先对齐远端 RTT 相机与裁剪平面

侧视截图中蓝色门框仍存在，但门框内部被墙面填满，而且异常在 Teleport 前即可稳定停留。当前远端视图代码与 Portal SDK 有两项耦合的坐标差异：变换后的 RTT 相机被沿出口法线额外外推 1 单位；自定义裁剪平面使用 `dot(normal, exitOrigin) + 1`。官方保持精确变换相机，并使用 `dot(normal, exitOrigin - normal * 0.5)`。在本轮观测到的 0.2–2.0 单位眼位深度下，1.0 单位相机误差和 1.5 单位裁剪平面位移是一级误差，不是可忽略的 epsilon。

下一轮单因子实现因此删除远端相机外推，并在所有渲染模式中采用官方后退 0.5 单位的裁剪平面。出口前方已经验证有效的 PVS 原点保持不变，并与相机原点分离，从而在不改变透视的情况下保留此前的黑天空修复。门实体和边框放置本轮不变；用户已经明确这些偏移可以调整，但验证当前根因并不要求同时修改它们。限频且仅写文件的 `[PortalOfficialRemoteView]` 日志记录源眼位深度、变换后出口深度、裁剪距离和零相机外推。若完整墙面仍存在，下一步直接实施官方 stencil hole／局部清深度／生成式 render-fix／恢复深度链，不再调整 near-plane 阈值。

### 决策十四：先移植官方近裁剪代理，再决定是否补深度和雾

第十四轮虽然降低了问题概率，但没有消除可稳定停留的墙面画面。关键样本同时满足 `entryEyeDepth=0.6339` 与主视图 `zNear=1.0`：普通门模型平面已经进入近裁剪区，而承载墙体大约还在其后 0.5 单位，仍能被主世界渲染。这一几何关系同时解释极慢停留和高速条带，不需要假定 Teleport 或远端相机仍然错误。

因此本轮只移植 Portal SDK 的主视图生成式代理：在相机 `zNear + 0.05` 处建立四边形，用十二个放大 1.1 倍的孔径平面及门正面平面裁剪，在 CPU 上投影到 NDC `z=0.00001`，并在现有 stencil 状态为 `ALWAYS/REPLACE` 时绘制。唯一入口仍是现有 `DrawModelExecute` Hook。工程已经声明 `IMatRenderContext::GetDynamicMesh`，所以只需增加局部、最小的 Windows x86 网格 ABI 适配层；不新增引擎 Hook、接口定位、签名或 offset。stencil 内局部清深度、雾与 post-stencil 修复延后，先由游戏内实测判断墙体遮罩是否已经解决。

### 决策十五：改变渲染架构前，先修正动态网格索引 ABI

第十五轮异常从规则墙体条带变成随视角改变的大型不规则三角形和梯形，甚至在相机静止时局部闪烁。聚焦日志显示代理持续生成，三至五个顶点的多边形数量合理，且引擎没有报错。这组证据不符合普通 Z-fighting，更符合代理三角形索引损坏。

局部 ABI 适配器原先在 `reinterpret_cast<byte*>(indices) + stripIndex * indexSize` 处写入 16 位索引，并且只写 `stripIndex`。Portal SDK 中 `IndexDesc_t::m_nIndexSize` 表示有效状态下的 0/1 元素增量；`CIndexBuilder` 按该增量推进 `unsigned short*`，写入值则为 `m_nFirstVertex + localIndex`。当实机描述符的有效值为 `1` 时，旧代码按字节推进会让相邻 16 位写入重叠，原本的 `0,1,2` 可能形成 `256` 等越界索引，GPU 随后读取动态缓冲区的陈旧顶点并构成横跨屏幕的巨型多边形。

本轮因此只修改索引生成：仅接受官方有效增量 `1`，按 `indices[i]` 写入完整 16 位元素，加上 `firstVertex`，并在首次写入前完成容量和 16 位范围校验；其他描述符全部安全失败。纯测试使用非零首顶点和哨兵存储验证索引值、边界及无部分写入。限频 `[PortalRenderFix]` 日志新增 `firstVertex`、`firstIndex`、`indexSize` 与首末索引值。代理几何、stencil/depth 状态、雾、RTT、Teleport、BSP 和传送门放置都保持不变；不需要新增 Hook、接口、签名、offset 或 IDA 工作。

### 决策十六：验收墙体遮罩结果，残余连续性独立后续处理

第十六轮完成了 23 次双向成功 Teleport，包含慢速和高速穿越；没有出现黑天空、墙体条带、承载墙画面或第十五轮的屏幕级错误多边形。20 条限频 render-fix 记录全部满足 `indexSize=1`、`firstIndexValue=firstVertex`，末索引也连续有效。关停时两个 brush 都从 `CONTENTS_EMPTY` 恢复到原始 `0x00000001`；对应客户端 hull trace 从恢复前开放的 `fraction=1.0` 变为恢复后阻挡的 `fraction=0.375`。

因此，在当前单人/localserver Phase 1 基线范围内，主视图墙体遮罩问题验收为已解决。仍有较轻的主观视觉不连续，但本次不再把新的相机或插值实验混入已经验收的碰撞/渲染结果。后续从 Portal SDK 客户端的 `PlayerPortalled`、眼位插值、眼角 latch、viewmodel facing 和可见性历史着手，并先建立 transaction 级诊断再修改状态。由于第十六轮没有黑天空或遮罩异常证据，depth/fog 修复继续延后。

## 安全不变量

- `g_BSPData`、必需数量和指针未通过校验时不得写入。
- brush 索引越界时不得写入。
- 首次写入前保存原始 contents；后续不得用已经清零后的值覆盖原始值。
- 两扇门共用 brush 时只写一次，只有两个所有者都释放后才恢复。
- 地图代际不匹配时禁止解引用旧 brush 地址。
- 地图关闭时先恢复，再使地址失效。
- DLL 关闭统一调用幂等的 `RestoreAll()`。
- 第二个目标写入失败时必须回滚第一个目标。
- 重复激活和重复恢复都必须是空操作。
- 逐帧日志可以限频，但修改和恢复事件不得被抑制。

## 验证策略

Stage A 的 Windows x86 定位、完整布局、第四轮双地图验收和版本复验流程已经收口在 `stage-a-ccollisionbspdata-retrospective.zh-CN.md`。Stage A 通过只授权后续只读 BSP 查询；在传送门承载 brush 的只读绑定门禁和可恢复事务测试通过前，仍不得启用生产 BSP 写入。

### 确定性测试

使用不依赖实际引擎内存的合成 node/leaf/brush 数据验证：

- BSP 正负子节点遍历和 leaf 解码
- 凸 brush 平面内点判断
- box brush 边界判断
- 损坏数量、索引、指针和 side 范围的拒绝行为
- 命中平面内部偏移采样选择
- 共用 brush 的所有者/引用行为
- 事务回滚和幂等恢复
- 阶段二局部范围和迟滞决策
- 穿越阶段阻止过早恢复

### 实机诊断

聚焦日志必须包含：

- `g_BSPData` 地址及定位签名来源
- 各字段偏移、数组地址和数量
- 传送门颜色、命中点/法线、leaf、brush、原始 contents
- 每次修改的旧值、新值和原因
- 每次恢复的预期值、当前值、恢复值和原因
- 修改前、修改后、恢复后的客户端与服务端玩家 hull trace

### 阶段一人工测试矩阵

- 简单矩形 brush 墙面
- 蓝门/橙门位于不同 brush
- 蓝门/橙门位于同一 brush
- 挖空期间重新放置任一传送门
- 挖空期间关闭任一传送门
- 挖空期间章节重启或换图
- 挖空期间卸载 DLL
- 故意从孔径以外位置穿过已移除 brush，记录无约束副作用
- 在地板、天花板和斜面放置，只记录行为，不声明支持
- 关闭旧碰撞绕过后执行 BSP 因果测试

## 风险与权衡

| 风险 | 缓解措施 |
|---|---|
| Linux 参考偏移不适用于 Windows | 独立定位和验证 Windows 布局，未经证明绝不照搬偏移 |
| 承载 brush 远大于可见墙面 | 阶段一完整测量；阶段二缩短时间窗口；阶段三可选空间约束 |
| 旧碰撞桥掩盖 BSP 是否生效 | 使用关闭旧绕过的因果测试模式 |
| 地图内存失效后才恢复 | 在关卡关闭的地址有效阶段恢复，并使用地图代际保护 |
| 两门共用 brush | 唯一 brush 事务和所有者位掩码 |
| 重放置导致原始 contents 丢失 | 原始值只保存一次，最后一个所有者释放前一直保留 |
| 引擎缓存碰撞状态 | 比较写入前后的即时客户端/服务端 trace；不生效则停止移动集成 |
| 整块 brush 移除影响其他对象 | 阶段一只记录；这些对象不属于当前验收范围 |

## 由诊断实验回答的问题

以下问题不阻塞 SDD，但实施时必须用证据回答：

1. 目标版本中哪个 Windows x86 签名能可靠定位 `g_BSPData`？
2. SourcePawn 使用的哪些结构尺寸和偏移与 Windows 版本一致？
3. 客户端预测和本地服务端玩家 hull trace 是否立即观察到同一份修改？
4. 修改 brush contents 后是否需要刷新碰撞缓存？
5. 阶段三中，L4D2 的 `env_physics_blocker` 是否支持所需旋转边界和玩家碰撞？

## 发布与兜底

- 初期通过开发配置或控制台开关控制新系统。
- BSP 数据不可用或 brush 解析失败时，默认保持纯视觉且不可穿越；旧穿越路径只能由显式诊断模式开启。
- 只有两扇活跃传送门都绑定有效时才允许成对激活，禁止半激活。
- 开发期间提供 `status`、`resolve`、`activate`、`restore` 等诊断入口或日志标记。
- 只有前一阶段验收门禁通过后，才启用阶段二或阶段三。
