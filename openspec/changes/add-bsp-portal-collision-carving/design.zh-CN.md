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
