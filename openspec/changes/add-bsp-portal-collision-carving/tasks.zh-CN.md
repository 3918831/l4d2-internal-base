# BSP 传送门碰撞挖空实施计划

**目标：** 在 L4D2 单人/本地服务器中，临时移除承载有效传送门对的 BSP brush contents，使本地玩家的原始移动可以穿过墙体，再由现有传送门系统完成传送。

**验收标准：** Windows BSP 数据得到验证；放置传送门时能解析承载 brush；写入可恢复且具备事务性；阶段一不使用附近检测或 blocker；可以关闭旧绕过以证明因果关系；所有传送门、地图和 DLL 生命周期都能恢复碰撞；后续阶段在不替换核心的前提下增加附近检测与可选 blocker。

**架构：** 在 `src/Portal` 下增加经过验证的 BSP 数据读取器和由传送门所有权驱动的碰撞挖空管理器。复用现有放置证据、`PortalTransform`、`PortalTransitionSimulator`、`PortalPlayerTeleport`、日志、特征扫描与本地服务器生命周期。开发默认使用纯视觉基线；旧碰撞绕过只保留为显式对照模式。

**技术栈：** C++17、Windows x86 L4D2 内部结构、engine 特征扫描、BSP 碰撞结构、现有 MSBuild x86 解决方案和独立 C++ 测试。

---

## 0. Windows 布局证据与诊断实验

- [x] 0.0 增加 `VisualOnlyBaseline` 物理模式及单元测试：保留传送门视觉，屏蔽旧状态机、受控 noclip、碰撞 trace 清除、移动推进、传送提交和临时 MoveType 诊断写入，并输出明确模式日志。
- [x] 0.1 记录目标 L4D2 版本和模块版本，并在 `src/Util/Logger/PortalFileLog.h` 增加聚焦的 `PortalBsp` 日志类别。
- [x] 0.2 在 `engine.dll` 中定位 Windows x86 `g_BSPData` 引用；记录候选指令、解码地址、所在函数和签名稳定性依据。
- [x] 0.2a 增加只读运行时真值探针：记录地图、`numleafs`、由 `GetBrushInfo` 边界推导的精确 `numbrushes`、brush contents 样本和传送门表面点的 leaf/contents；加入边界查找单元测试。
- [x] 0.2b 在至少两张地图采集运行时真值，并与 IDA 中三个 EngineTrace 锚点的字段访问逐项比对。
- [x] 0.2c 修正 Windows `GetBrushInfo` 的一字节布尔返回 ABI；增加只读指令解码、候选 BSP 基址、规范/镜像计数及数组指针交叉验证日志。
- [x] 0.3 在独立的 `src/Util/Offsets/PortalBspOffset.h/.cpp` 中保存由 `GetBrushInfo` 语义操作数解出的只读候选地址并记录来源；不启用写入，也不改动既有非 UTF-8 offset 文件。
- [x] 0.4 新建 `src/Portal/PortalBspTypes.h`，定义最小定宽 BSP 布局，并逐项标记证据等级；第三轮 IDA 与双地图运行时证据已确认 plane/node 步长和 node 固定 `+6` box-hull 分配关系。
- [x] 0.5 新建 `src/Portal/PortalBspData.h/.cpp` 只读骨架，并加入 `src/l4d2_base.vcxproj` 与 `.filters`。
- [x] 0.6 实现所有必需数组指针、逻辑/分配数量关系与完整分配跨度的受检读取；普通表要求二者相等，nodes 要求分配数量等于逻辑数量加 6；拒绝空指针、非正数量、不合理上限、关系不符、地址运算溢出和不可读范围。
- [x] 0.7 增加开发期状态探针：输出 base、布局偏移、数组地址、数量、完整跨度、地图名、地图代际、根节点/leaf/cmodel 不变量和 `destructiveWrites=false`；地图关闭时清空缓存。
- [x] 0.8 使用 `MSBuild.exe src/l4d2_base.sln /p:Configuration=Debug /p:Platform=x86 /m` 构建；结果为零警告、零编译和链接错误。
- [x] 0.9 至少在两张地图运行探针，保存数量和指针在换图时一致变化的证据；已在同一游戏进程内验证 `ssv1` 与 `c1m2_streets`，换图失效、地图代际、数组地址和计数均一致变化。
- [x] 0.10 门禁：Windows base 和所有使用字段的布局未得到证明前，不得进入写入阶段；第四轮在同一进程内验证 `ssv1` 和 `c1m2_streets` 共 19 次 snapshot 全部 `ready=true`，Stage A 布局门禁通过。过程、布局、边界与复验规范见 `stage-a-ccollisionbspdata-retrospective.zh-CN.md`。

## 1. 纯 BSP 查询实现

- [ ] 1.1 新建 `tests/PortalBspQueryTests.cpp`，为正子节点、负子节点、负 child 解码 leaf、非法 leaf 编写合成测试；先确认因查询函数不存在而失败。
- [ ] 1.2 把不依赖引擎内存的查询逻辑放入 `src/Portal/PortalBspQuery.h/.cpp`，保证单元测试无需实时地址。
- [ ] 1.3 实现等价 `PointLeafNum` 的受检 node/plane 遍历，并让 node/leaf 测试通过。
- [ ] 1.4 为凸 brush 编写失败测试：内部点、越过一个平面的外部点、空 plane、零 side、side 范围溢出。
- [ ] 1.5 实现凸 brush 内点判断并使测试通过。
- [ ] 1.6 为 box brush 编写中心点、边界面、边界外和非法索引测试。
- [ ] 1.7 实现 box brush 内点判断并使测试通过。
- [ ] 1.8 为 leafbrush 枚举、`MASK_PLAYERSOLID` 过滤、重复候选和内部采样顺序 `{2, 4, 8, 1, 16, 0.5}` 编写失败测试。
- [ ] 1.9 实现 `FindBrushForSurfacePoint`，返回 leaf、brush、选中偏移和原始 contents 等诊断元数据。
- [ ] 1.10 参照现有独立测试方式增加 `tests/run_portal_bsp_query_tests.cmd`；预期全部 BSP 查询测试通过。
- [ ] 1.11 把纯查询逻辑接入 `CPortalBspData` 的受检实时内存访问。
- [ ] 1.12 运行 Debug x86 构建及全部现有 `tests/run_*.cmd`；预期无回归。

## 2. 传送门放置绑定（只读）

- [ ] 2.1 扩展 `src/Portal/client/weapon_portalgun.cpp` 的成功放置路径，在传送门有效时保留或复用世界命中位置和表面法线。
- [ ] 2.2 新建 `src/Portal/PortalBspCollisionCarver.h/.cpp`，由 `CPortalBspCollisionCarver` 持有 `PortalBrushBinding`；除非生命周期身份确有需要，不把 brush 字段塞进渲染职责。
- [ ] 2.3 成功放置时把蓝门/橙门所有权绑定到解析出的 brush，并记录完整解析结果。
- [ ] 2.4 重放置时先解析新 brush，再释放旧绑定；解析失败则保留安全的现有穿越方案，不修改任一 brush。
- [ ] 2.5 传送门关闭、系统关闭或地图失效时清理只读绑定。
- [ ] 2.6 增加诊断状态输出：两门活跃状态、解析状态、brush、原始 contents 和地图代际。
- [ ] 2.7 在简单墙面、地板、天花板和斜面上，把结果与 SourcePawn 算法对照；只记录不支持情况，不立即增加补丁。
- [ ] 2.8 门禁：选定墙面测试中必须能重复解析正确 brush，之后才能允许任何写入代码启用。

## 3. 可恢复修改核心

- [ ] 3.1 为单所有者、不同 brush 的传送门对、同一 brush 双所有者、重复激活幂等性和原始 contents 保存编写失败测试。
- [ ] 3.2 为预期 contents 过期、索引越界、地图代际不匹配、第二次写入失败回滚、重复恢复和所有者释放顺序编写失败测试。
- [ ] 3.3 为 carver 增加可注入的 brush 访问接口；测试使用假存储，生产实现使用 `CPortalBspData`。
- [ ] 3.4 实现写入前比较并写成 `CONTENTS_EMPTY`；绝不能用挖空后读到的值覆盖已保存的原始值。
- [ ] 3.5 实现唯一 brush 事务和蓝门/橙门所有者位掩码。
- [ ] 3.6 实现统一的幂等 `RestoreAll(reason)`，恢复前验证地图代际和预期当前值。
- [ ] 3.7 任一目标验证或写入失败时，回滚已完成的写入。
- [ ] 3.8 运行修改单元测试；预期成功、回滚、共享所有者和幂等场景全部通过。
- [ ] 3.9 生产写入必须受明确的开发开关控制，首次受控实机运行前默认关闭。
- [ ] 3.10 记录所有尝试、成功、拒绝、回滚和恢复事件，日志使用与地址无关的标识和值。

## 4. 阶段一——无约束整块 brush 实验

- [ ] 4.1 增加阶段一策略：两扇门活跃、两个绑定有效且开发开关开启，即启用成对挖空；不得检查本地玩家距离或附近状态。
- [ ] 4.2 在有效传送门对建立后接入激活，保持事务顺序，禁止半挖空状态。
- [ ] 4.3 在传送门重放置释放、关闭完成/失效、`PortalShutdown`、关卡关闭和 DLL 卸载前接入恢复。
- [ ] 4.4 地图关闭时先在 BSP 指针有效期间恢复，再使地图代际失效/递增。
- [ ] 4.5 增加基线、BSP+旧路径、仅 BSP 因果测试、强制 BSP 失败兜底四种诊断模式。
- [ ] 4.6 增加穿过目标传送门位置的客户端和本地服务端 hull trace，对比修改前、修改后、恢复后的 fraction、`startsolid`、`allsolid`、终点和法线。
- [ ] 4.7 构建 Debug x86 并运行全部单元测试；预期零错误且全部通过。
- [ ] 4.8 使用位于不同 brush 的蓝门/橙门测试 BSP+旧路径，验证修改值与恢复值完全一致。
- [ ] 4.9 因果测试时关闭旧碰撞结果清除和受控 noclip，但保留状态机、坐标变换、传送和预测同步。
- [ ] 4.10 验证本地玩家能依靠 BSP 挖空完成接近、跨越、传送和离开。
- [ ] 4.11 按要求故意从孔径外的其他位置穿过已挖空 brush，记录无约束行为。
- [ ] 4.12 测试两扇门位于同一 brush，验证只有一次写入和一次最终恢复。
- [ ] 4.13 挖空期间分别重放置蓝门/橙门、关闭一扇门、重载章节、换图和正常卸载 DLL，验证不存在丢失的原始 contents。
- [ ] 4.14 强制制造地址、绑定和写入失败，验证旧穿越兜底仍可用且没有残留的部分修改。
- [ ] 4.15 阶段一门禁：形成决策报告，包含因果穿越证据、恢复证据、无约束异常和是否需要刷新引擎碰撞缓存。

## 5. 阶段二——可配置附近生命周期

- [ ] 5.1 新建 `tests/PortalBspCarveActivationTests.cpp`，使用现有 `PortalTransform` 基底约定测试局部左右、上下、前后深度边界。
- [ ] 5.2 为进入/离开迟滞、任一传送门激活、两门无效拒绝和边界反复抖动编写失败测试。
- [ ] 5.3 编写失败测试，证明 `ApproachingPortal`、`IntersectingPortal`、`CommittingTeleport`、`ExitingPortal` 即使附近采样为假也保持挖空。
- [ ] 5.4 实现 `PortalCarveActivationConfig`，初始参数为 `lateralMargin=24`、`verticalMargin=24`、`enterDepth=96`、`leaveDepth=144`，强制 `leaveDepth > enterDepth`。
- [ ] 5.5 复用本地玩家锚点和传送门局部变换实现 `IsPlayerNearPortalForCarving`，不得使用世界空间球形距离。
- [ ] 5.6 把纯迟滞激活策略与 brush 写入分离实现。
- [ ] 5.7 接入现有逐指令传送门更新，同时保留阶段一诊断模式。
- [ ] 5.8 穿越或离开阶段必须拒绝恢复，并用限频日志解释每次延迟恢复。
- [ ] 5.9 增加保守的探针丢失恢复：保持挖空，直到状态回到安全阶段或满足明确的安全恢复条件。
- [ ] 5.10 运行全部纯测试和 Debug x86 构建；预期全部通过。
- [ ] 5.11 实机根据玩家锚点、局部坐标、阶段、激活决策和恢复决策日志调节阈值。
- [ ] 5.12 验证远离传送门时始终使用原始 BSP 碰撞、接近只激活一次、阈值抖动不反复切换、离开只恢复一次。
- [ ] 5.13 阶段二门禁：与阶段一比较异常频率和穿越可靠性；保留阶段一模式用于回归诊断。

## 6. 阶段三——可选孔径 blocker 约束

- [ ] 6.1 审阅阶段一和阶段二证据，明确决定是否仍需要 blocker；如果附近检测已足够，则跳过阶段三。
- [ ] 6.2 如有需要，复用现有 `IServerTools` 做读写诊断实验：创建一个 `env_physics_blocker`，设置边界/keyvalue，生成、移动/旋转并安全删除。
- [ ] 6.3 记录目标版本是否支持旋转阻挡体、影响哪些玩家碰撞 mask，以及正确的实体删除生命周期。
- [ ] 6.4 为受支持平面墙体分解成孔径周围左、右、上、下 blocker 体积编写纯几何测试。
- [ ] 6.5 明确阶段三支持的表面约束；不满足约束的 brush 必须拒绝，不能盲目近似。
- [ ] 6.6 实现 `CPortalApertureBlockerSet` 事务创建：全部成功，或者删除已创建 blocker 并恢复 brush。
- [ ] 6.7 blocker 生命周期必须绑定到同一套传送门所有者、地图代际、激活、恢复、重放置和关闭路径。
- [ ] 6.8 验证本地玩家只能穿过孔径，周围经过测试的墙体区域仍保持阻挡。
- [ ] 6.9 强制部分 blocker 创建失败和重放置失败，验证没有孤儿 blocker 或挖空 brush 残留。
- [ ] 6.10 阶段三门禁：只对已证明的表面类型启用约束；其他情况保留仅附近检测的兜底。

## 7. 加固与交付

- [ ] 7.1 运行完整独立测试套件和 Debug x86 解决方案构建。
- [ ] 7.2 至少在两张地图重复最终选定模式的人工测试矩阵，并保存聚焦的 `portal_l4d2_traversal.log` 证据。
- [ ] 7.3 审计所有传送门、地图和 DLL 退出路径是否覆盖 `RestoreAll()`，并验证其幂等性。
- [ ] 7.4 审计本功能没有拦截或宣称支持范围外单位及远程服务器行为。
- [ ] 7.5 记录开发开关、状态输出、支持范围、brush 粒度限制、恢复步骤以及如何切回旧穿越路径。
- [ ] 7.6 根据 `specs/portal-bsp-collision-carving/spec.zh-CN.md` 的每个场景执行最终规格符合性检查。
- [ ] 7.7 本变更不删除旧碰撞基础设施；只有 BSP-only 长期验证稳定后，另行提出清理提案。
