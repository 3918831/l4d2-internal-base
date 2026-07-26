# 传送门近裁剪面遮罩退化问题诊断

## 现象

玩家缓慢贴近传送门时，可以在传送发生前定格一条横跨屏幕的墙体贴图带；转动视角时，条带的位置与角度随视角变化，部分第一人称武器模型也会被遮挡。`FullHull` 与 `PlaneEpsilon` 两种出口清离方式均可复现。

## 复现证据

- 第七轮日志共有 32 次 Teleport 提交，两种 clearance 模式各 16 次。
- `FullHull` 下可在入口深度约 `6.472` 连续停留 178 个视觉探针采样；`PlaneEpsilon` 下可在约 `6.250` 连续停留 134 个采样。
- 上述采样均处于 `IntersectingPortal` 且 Teleport 尚未提交，而主视图 `zNear=7.0`。
- 两种 clearance 只影响 Teleport 后的出口位置，因此无法解释 Teleport 前完全相同的画面异常。

## 根因判断

当前渲染模式使用传送门模型本身写入 stencil，再在 stencil 区域绘制全屏传送门纹理。当观察相机距入口平面的距离进入主视图近裁剪范围时，作为遮罩的传送门几何体被近裁剪面截断或退化，原墙面因深度/遮罩覆盖不完整重新暴露，形成随视角变化的条状画面。

这是渲染遮罩和主相机近裁剪面的交互问题，不是 Teleport、预测同步或出口 hull push 的直接结果。出口 push 仍可能造成另一类传送瞬间的不连续，应在本问题完成 A/B 验证后独立处理。

## 参考实现佐证

- Portal SDK 2013 在相机近距离接近传送门时使用专门的 near-plane render-fix mesh，并严格恢复 stencil/depth 状态；视觉眼点跨越和玩家中心物理传送也是两个独立时机。
- GMod 实现对防止重复传送的主要启发是出口侧空间重武装；其 noclip/custom movement 不适用于当前 BSP 挖空主线，也没有解决此遮罩退化问题。

## 本轮最小修复与边界

增加可运行时开关的 portal-aware 主视图近裁剪缩短实验：仅在 BSP 挖空已激活、相机位于当前入口孔径内、处于接近/相交阶段且入口正侧深度进入近裁剪危险区时，将当前帧主视图 `zNear` 降至 `1.0`。不修改 `zNearViewmodel`，不修改出口 clearance、高度差、放置限制、brush 策略或 blocker。

该实验用于直接验证根因。若条带仍存在，下一步应实现官方风格的屏幕空间 near-plane render-fix mesh，并审计 stencil/depth 的绘制与恢复顺序，而不是继续调出口 push。

## 验收方式

同一对传送门、同一接近路线下，分别执行 `portal_visual_nearclip 0` 与 `portal_visual_nearclip 1`，缓慢进入并尝试定格异常画面。启用后日志应出现仅写文件的 `[PortalNearClipFix]`，且视觉探针中的 `zNear` 在危险区降为 `1.0`。游戏内主观验收在下一轮进行。

## 实现侧验证

- `PortalTransitionDecision` 失败测试先确认新策略 API 缺失，最小实现后通过。
- 日志策略测试确认 `[PortalNearClipFix]` 会写入聚焦日志并从控制台抑制。
- 全部 11 组本地测试通过。
- Debug x86 完整编译通过：0 错误；5 个既有 `convar.cpp` C4355 警告。

实现侧验证不能代替游戏画面验收，本问题在 A/B 实测前仍视为“待验证修复”。

## 第十二轮实测更新

- 实测指令虽未追加执行 `portal_bsp_status`，但前置五条控制指令均由日志确认生效；缺少 status 只影响摘要可见性，不影响实验配置。
- 本轮记录 14 次成功 Teleport 事务和 21 帧入口相机交接，没有拒绝事务，也没有黑天空回归。
- 慢速和高速穿越仍只出现条状墙体，且 viewmodel 同时缺失一部分。这与第七轮可稳定定格的 near-plane 遮罩退化同源，说明官方式入口相机交接修复了“眼位越过入口后仍看墙后”的时间窗，却不能修复写 stencil 的门模型本身被 near plane 截断。
- 因此继续调整 Teleport、出口 push 或 BSP 不再是本问题的正确单因子；下一实验应只修门面 stencil mask。

## 第十三轮候选修复

保留传送门实体相对墙面既有约 1.0 单位的前移，因为该偏移承担门模型与墙面的防 Z-fighting 职责。本轮不移动门实体，也不修改边框偏移。主视图靠近实际门面时，仅在 portal model 写 stencil 的阶段临时采用小于门面深度的极近投影，并关闭墙深度对该遮罩的拒绝；写入完成后立即恢复投影和深度状态。世界主视图 `zNear`、RTT、Teleport、玩家位置、BSP contents 和 viewmodel 路径均不变。

该路径由 `portal_visual_maskrepair <0|1>` 控制，默认开启；`[PortalMaskRepair]` 只写入聚焦日志而不刷控制台。判定限制为非递归主视图、有效门对、相机距实际门面不超过 64 单位、门面处于相机正侧且深度不超过 `zNear + 1`。实现复用工程内已验证的 `IMatRenderContext` 与现有门模型绘制路径，对齐 Portal SDK render-fix 的功能不变量，而不直接引入 ABI 尚未验证的官方 `IMesh`。

## 第十三轮实现侧验证

- 先增加失败测试，证明主视图近面遮罩判定、极近投影计算和运行时开关尚不存在；最小实现后测试通过。
- 日志策略测试先因 `[PortalMaskRepair]` 未登记为文件诊断而失败，补齐策略后通过，确认控制台不会持续刷新增日志。
- Debug x86 完整 Rebuild 生成了新的 DLL/启动器，随后增量校验明确返回 `exit=0`；全部 11 组本地测试通过。独立 `PortalTransform` 测试仍输出两条既有中文代码页 C4819 警告。游戏内是否消除条带仍以人工实测为准。

## 第十三轮实测否定与根因重定向

- 测试状态确认 `ExactTransform`、near-clip、入口相机交接与 `PortalMaskRepair` 全部生效；BSP 挖空有效，旧 collision bypass、movement mutation 与 noclip 均关闭。
- `[PortalMaskRepair]` 在入口眼位正侧深度从约 `1.9867` 下降至 `0.2446` 的全过程持续执行，但测试者仍能在 Teleport 前稳定停留，并让视野被承载墙面填满。
- 侧视截图保留蓝色门框，正常主世界位于门框外，错误墙面位于门框内部。这比“主视图 stencil 边缘少写了一条”更符合 RTT 远端内容或完整深度合成错误。
- 因此普通门模型的极近投影/无深度拒绝实验已被证伪。`PortalMaskRepair` 的渲染状态覆盖、运行时命令、状态字段、测试和日志策略均从有效实现中删除。

## 第十四轮官方远端视图单因子修复

当前 RTT 路径曾在精确入口到出口变换后把相机沿出口法线额外外推 `1.0`，同时把出口裁剪平面设为 `dot(normal, exitOrigin) + 1.0`。Portal SDK 2013 的对应实现保持精确变换相机，并使用 `dot(normal, exitOrigin - normal * 0.5)`。两者的裁剪平面相差 `1.5` 单位；在本问题的亚 2 单位眼位深度下足以直接暴露出口承载墙。

本轮移除 RTT 相机外推，并把所有远端裁剪平面统一为官方后退 `0.5` 单位的公式。此前用于解决黑天空的出口安全 PVS 原点继续保留，但不再通过移动相机改变透视。门实体、边框、Teleport、BSP、near clip、入口交接、出口守卫、高度差和放置限制均不改变。新增限频且仅写文件的 `[PortalOfficialRemoteView]`，用于记录入口眼位深度、远端眼位深度、裁剪距离和零相机外推。

如果实机仍能在门框内部稳定看到完整墙面，下一步不再回到 near-plane 阈值，而是实现 Portal SDK 的完整 stencil hole、stencil 内清深度、生成式 render-fix mesh 与 post-stencil 深度恢复链。

## 第十五轮实现：官方近裁剪代理（墙体优先）

第十四轮日志把关键稳定样本固定为 `entryEyeDepth=0.6339`、`zNear=1.0`。门模型位于相机近裁剪范围内，而承载墙体位于门后约 0.5 单位，仍能写入主世界颜色；因此“门遮罩消失、墙仍存在”是当前条带和整面墙画面的直接原因。

本轮在现有 `DrawModelExecute` stencil `ALWAYS/REPLACE` 阶段追加 Portal SDK 同类的主视图代理：

- 相机 `zNear + 0.05` 处生成 80×80 初始四边形；
- 使用十二个放大 1.1 倍的门孔边界平面及门正面平面裁剪；
- 使用引擎当前 view/projection 矩阵投影，并把 NDC 深度固定为 `0.00001`；
- 使用现有 `IMatRenderContext::GetDynamicMesh` 绘制三角带；
- 仅主视图执行，递归 RTT 不执行；
- 不新增 Hook、offset 或 IDA 信息，不修改 BSP、Teleport、门放置偏移、深度清理和雾。

新增纯几何回归测试覆盖已知 `0.634 < 1.0` 样本、仍在近裁剪面外的空代理以及无效坐标系拒绝；新增 `[PortalRenderFix]` 限频文件日志和 `nearPlaneRenderFix=OfficialStencilProxyOnly` 状态字段。Debug x86 已完成编译验证；墙体画面是否消失仍以第十五轮游戏内测试为准。

## 第十五轮实测：不规则巨型多边形的根因

第十五轮中，原本规则的墙体条带变成了随视角大幅变化的三角形、梯形和不规则墙面块，部分画面在玩家和视角静止时仍会局部闪烁；高速视频抓帧也保留了大型错误多边形。与此同时，日志中的代理顶点数始终是合理的 3～5，`[PortalRenderFix]` 持续执行且没有引擎错误。因此该现象不是门模型与墙面的普通 Z-fighting，也不是需要立即补深度或雾，而是新动态网格的索引 ABI 使用错误。

Portal SDK 的 `IndexDesc_t::m_nIndexSize` 并不是字节步长：它是有效索引流的 0/1 元素增量。官方 `CIndexBuilder` 按 `unsigned short` 元素推进，并把 `m_nFirstVertex + localIndex` 写入索引。旧实现却按字节地址加上 `stripIndex * indexSize` 后写 16 位值；当 `indexSize=1` 时，相邻写入互相重叠，还遗漏了 `firstVertex`。例如意图写入 `0,1,2` 时，第一个 16 位值可能被重叠成 `0x0100`，进而引用动态缓冲区中的陈旧顶点并形成横跨屏幕的墙体纹理三角形。

修复保持单因子：

- 按 16 位元素写入 `indices[i]`，不再进行字节指针运算；
- 每个索引值加上描述符的 `firstVertex`；
- 只接受官方有效的 `indexSize=1`；
- 在任何写入前验证指针、容量、非负首顶点和 16 位索引上限；
- 增加非零首顶点、哨兵内存、异常步进、容量不足和溢出回归测试；
- `[PortalRenderFix]` 新增首顶点、首索引、步进及首末写入值诊断。

本轮不修改代理几何、stencil/depth 状态、深度清理、雾、RTT、Teleport、BSP 或门放置偏移，也不需要新 Hook、offset 或 IDA 信息。下一轮实机首先验证巨型不规则多边形和静止闪烁是否消失；只有索引正确后仍存在规则墙体遮挡，才继续评估官方的局部清深度与雾恢复链。

## 第十六轮验收：墙体遮罩问题关闭

第十六轮人工实测确认：

- 慢速和高速双向穿越都未再出现黑天空。
- 未再出现条状墙体。
- 未再出现门后承载墙画面。
- 未再出现第十五轮的巨型三角形、梯形或静止局部闪烁。
- 当前只剩较轻的主观视觉连续性问题，表现已不再是墙体遮罩错误。

日志包含 23 次成功 `PortalTeleportCommit`（蓝→橙 12 次、橙→蓝 11 次）和 20 条 `[PortalRenderFix]`。所有 render-fix 记录均满足 `indexSize=1`、`firstIndexValue=firstVertex` 与连续末索引，没有真实 error、crash、事务回滚或穿越拒绝。

关停证据同时确认 BSP 恢复完整：brush 6 和 brush 4 都从 `0x00000000` 恢复到原始 `0x00000001`；恢复前 hull trace 为开放的 `fraction=1.0`，恢复后为重新命中墙体的 `fraction=0.375`。

因此本问题在当前单人/localserver Phase 1 范围内标记为已解决。局部清深度和雾修复不再作为默认下一步；残余轻微不连续转入独立的新会话，优先研究 Portal SDK 客户端 `PlayerPortalled`、眼位/眼角插值历史及 viewmodel 交接。
