# L4D2 传送门穿越阶段性实现总结

日期：2026-06-29

## 阶段目标

本阶段目标是在 L4D2 注入式 DLL 中，以 Portal SDK 2013 的官方穿越逻辑为主参考，实现单人 localserver 场景下的玩家穿越主干体验：

- 玩家可以从地面持续走向传送门，并嵌入门洞，而不是像普通墙体一样被挡住。
- 玩家在入口门洞内可以继续推进，跨过入口 plane 后被传送到出口。
- 传送后位置、速度、视角尽量在同一帧完成同步，避免墙体闪屏、视角先跳或速度突降。
- 暂时只优先处理本地玩家穿越，不扩展其他实体。
- 不使用修改 MoveType 为 noclip 的旧方案，实际传送仍使用 server 侧 `CBaseEntity::Teleport`，即当前验证可用的 vtable `[118]`。

当前实测结果：玩家已经可以嵌入墙壁门洞并完成蓝门到橙门穿越。最后一轮修复后，用户确认基础功能完成，闪屏消失。

## 官方方案参考

Portal SDK 2013 的核心思路不是简单地在最后一刻瞬移玩家，而是在玩家接触传送门时持续维护一个 portal-aware movement 环境：

- 门洞区域有 portal-aware 的碰撞豁免。
- 玩家进入门洞时，movement trace、ground trace、position test 都需要理解 portal aperture。
- 官方侧存在 `CPortalSimulator` / portal environment / touching entity 相关状态，用于记录实体与传送门的交互过程。
- 当实体跨过入口 plane 后，位置、角度、速度通过入口到出口的变换矩阵统一转换。
- 传送不是单点行为，而是运动、碰撞、相机和实体状态共同参与的连续过程。

L4D2 注入式 DLL 无法直接拥有 Portal 的完整类体系和 server/client 源码级改造能力，因此本阶段采用了更窄的运行时 hook 方案：在已确认的 movement hook 点中拦截和补偿玩家 CMoveData，再由独立 simulator 决策穿越时机。

## 已验证接口和 Hook 资产

### 已确认可用

- server `TracePlayerBBox`：使用 server.dll 签名 hook，已验证参数正确。此前尝试 vtable `[14]` 会出现参数错乱和 ESP 问题，已放弃。
- client `TracePlayerBBox[16]`：由 CGameMovement vtable 布局偏移验证可用。
- server `PlayerMove[18]`：可 hook。
- server `WalkMove[28]`：可 hook，是当前门洞嵌入和速度补偿的关键点。
- server `FullWalkMove[30]`：可 hook，主要用于观察调用链。
- server `TryPlayerMove[38]`：可 hook，但不是当前地面门洞阻塞的最终解决点。
- server `StepMove[64]`：可 hook/验证，但当前主干没有依赖它完成穿越。
- server `CategorizePosition(void)`：签名和 vtable `[52]` 匹配，已验证可 hook。
- server `StayOnGround`：通过已知 return address 附近 prologue 定位并 hook，已验证会参与地面路径；单独跳过它不足以让玩家进门，但它是地面穿越诊断的重要资产。
- client `PlayerMove[18]`、`WalkMove[28]`、`FullWalkMove[30]`：可 hook。
- server local player `Teleport(vtable[118])`：用于实际提交玩家 teleport。

### 当前仍未完整掌握

- client `CategorizePosition`：尚未纳入主干。
- server/client `TestPlayerPosition`：签名/调用约定仍未完全确认。
- 更完整的 `CMoveData`、`CGameMovement` 私有成员布局：目前只使用已经验证过的字段和 hook 点。
- 官方 Portal 中完整的 portal environment / collision rules / touching entity 状态，在 L4D2 内仍未等价复刻。

## 当前实现结构

### PortalCollisionBridge

`PortalCollisionBridge` 负责在 movement trace 中判定是否允许门洞区域碰撞豁免。它以 simulator phase、入口侧、玩家是否在 aperture 内、trace 类型等条件约束豁免范围。

当前作用：

- 允许玩家 hull 在门洞区域通过原墙体 collision。
- 区分 horizontal move、step/down probe、zero-length position test 等 trace 形态。
- 记录 frame diagnostics，供 `WalkMove` 后处理使用。

这部分解决的是“碰撞检测别把玩家直接挡在墙外”，但它本身不足以让地面玩家顺滑进门，因为 L4D2 的 `WalkMove` 会在原始 movement 结果里重新夹回/清空速度。

### PortalTransitionSimulator

`PortalTransitionSimulator` 维护当前本地玩家的穿越状态：

- `Idle`
- `ApproachingPortal`
- `IntersectingPortal`
- `CommittingTeleport`
- `ExitingPortal`
- `Cooldown`

它负责：

- 根据玩家 origin/eye/center/feet 到门平面的距离判断进入阶段。
- 根据 aperture 内外和速度方向判断是否正在进入传送门。
- 在接近跨过入口 plane 时预测 crossing。
- 构造入口到出口的 transform matrix。
- 计算出口新 origin、angles、velocity。
- 通过 `PortalPlayerTeleport::Commit` 使用 server `Teleport[118]` 提交真实传送。
- 缓存 committed movement，用于后续 `WalkMove` 同步 CMoveData。

关键新增能力：

- `TryCommitMovementCrossing`：在 `WalkMoveNudge` 推进后，如果预测已经跨过入口 plane，则立即提交 teleport。
- `TryGetCommittedMovementForCommand`：提供最近一次 teleport 后的 origin、velocity、angles，供 movement hook 在同一 command 或后一 command 同步。
- 延迟本地视角应用：避免先改 `SetViewAngles`、后改 origin 导致“视角已经在出口，人物还在入口”的两帧闪屏。

### PortalPlayerTeleport

`PortalPlayerTeleport` 封装 server local player 查找和 `Teleport(vtable[118])` 调用。

查找 server player 的路径：

- 优先通过 `CServerTools->GetIServerEntity(clientLocal)` 获取 server entity。
- 失败时使用 edict / `IServerUnknown` fallback。

实际提交：

```cpp
using FnTeleport = void(__thiscall*)(void*, const Vector*, const QAngle*, const Vector*);
teleport(serverPlayer, &origin, &angles, &velocity);
```

### CCSGameMovement Hook

当前最关键的逻辑在 `WalkMove` detour 后处理：

1. 记录原始 `CMoveData` origin/velocity。
2. 调用原始 `WalkMove`。
3. 记录原始结果。
4. 在 `ApproachingPortal` 阶段采样完整入口速度向量。
5. 如果有 committed movement，优先同步 origin/velocity/angles。
6. 如果没有 committed movement，则尝试 `WalkMoveNudge`：
   - 使用碰撞桥记录的 horizontal trace 结果。
   - 按 portal normal 推进玩家到门洞内。
   - 用采样的完整速度向量恢复速度，而不是只恢复法线速度。
   - 若推进后已经跨 plane，立即调用 simulator commit teleport。
7. 如果处于出口阶段且速度被原始 movement 清掉，则尝试恢复出口速度。

server/client `WalkMove` 都执行类似逻辑，以减少预测和实际 server 状态之间的错位。

## 关键问题与解决过程

### 1. 地面玩家无法嵌入门洞

现象：玩家站在地面持续向门移动时，仍像撞普通墙一样停住；跳跃进入时反而能嵌入。

诊断结论：

- `TracePlayerBBox` 的门洞豁免能被命中，但只靠 trace fraction 改写不足以改变最终 movement 结果。
- 地面路径中 `WalkMove` / ground 相关逻辑会把最终 origin/velocity 重新约束到墙前。
- `StayOnGround` 有参与，但单独跳过不足以解决。

解决：

- 在 `WalkMove` 原函数返回后，对 portal bridge phase 的玩家执行 `WalkMoveNudge`。
- 该逻辑将 CMoveData origin 沿入口 normal 推入门洞，并恢复合理的入口速度。
- 实测后，玩家可以走入门洞并卡进墙体/门后模型，说明主阻塞点被突破。

### 2. 穿越后速度卡顿

现象：穿越时速度从 220 突降，或斜向/倒退进门时速度不连续。

诊断结论：

- 原始 `WalkMove` 在门洞交界处会把速度清零或压低。
- 早期补偿只恢复入口法线方向速度，切向速度会丢失。
- 快速/斜向穿越要求保留完整速度向量，而不是写死 220。

解决：

- 在 `ApproachingPortal` 阶段采样完整 `velocityAfterOriginal`。
- `WalkMoveNudge` 使用完整速度向量作为目标速度。
- 法线速度仍有下限保护，但切向速度保留。
- 传送后 velocity 通过 portal transform 转换到出口方向。

### 3. 传送时墙体闪屏

早期现象：穿越时会看到部分墙体，说明传送位置和渲染/预测状态有一帧错位。

解决：

- simulator 在接近 plane 时预测 crossing，而不是等待 center 已经完全穿过。
- `TryCommitMovementCrossing` 在 WalkMove 推进后立即判断是否跨 plane。
- committed movement 缓存后，由后续 `WalkMove` 同步 CMoveData，减少 server/client 预测偏差。

### 4. 视角先变、位置后变导致的新闪屏

最新现象：逐帧观察显示，玩家还嵌在蓝门时，视角已经变成橙门方向，持续约两帧。

诊断结论：

- `TryCommitTeleport` 内先执行了 `EngineClient->SetViewAngles(newAngles)`。
- 但 client 的 CMoveData origin/velocity 是稍后在 `CommittedMoveSync` 中才同步。
- 因此画面出现“入口位置 + 出口视角”的错配帧。

解决：

- `TryCommitTeleport` 不再立刻调用 `SetViewAngles`。
- new angles 缓存到 committed movement。
- `CommittedMoveSync` 写入 origin/velocity 后，同步调用 `SetViewAngles`。

实测结果：用户确认闪屏消失。

## 当前方案与官方方案的差异

当前方案仍是注入式近似复刻，而不是源码级完整 Portal movement：

- 没有完整复刻官方 portal environment / touching entity / collision rules。
- 没有对所有实体提供 portal-aware movement，只处理本地玩家。
- 没有全量替换 `CGameMovement::TracePlayerBBox/TestPlayerPosition/CategorizePosition` 的内部语义。
- `WalkMoveNudge` 仍是对 L4D2 movement 结果的后处理修正，不是官方式完整 movement solver。
- 依赖已知 vtable index、函数签名和 server/client CGameMovement 布局，游戏版本变化可能需要重新验证。

但当前方案已经证明：

- L4D2 注入式 DLL 可以在不使用 noclip MoveType 的情况下实现玩家门洞嵌入。
- 可以用 `Teleport[118]` 与 CMoveData 同步实现无明显闪屏的基础传送。
- 关键在于把碰撞豁免、WalkMove 结果修正、teleport commit、viewangles 应用收束到一致的 movement 时序内。

## 日志与诊断资产

当前 `PortalFileLog` 会捕获关键日志到 `D:\portal_l4d2_trace.log`，包含：

- `PortalTraversalFrame`
- `PortalWalkMoveFrame`
- `WalkMoveNudge`
- `CommittedMoveSync`
- `PortalTeleport`
- `PortalBridge`

这些日志已经被证明对定位帧级错位非常有效。提交前可以保留，但后续进入稳定版时建议降低频率或受 cvar 控制。

## 当前风险

- `WalkMoveNudge` 仍可能在复杂地形、移动平台、楼梯、水中、低血量速度变化等场景出现边缘问题。
- 当前只验证本地玩家，其他实体穿越尚未纳入。
- 出口侧如果紧贴实体或动态物件，仍可能出现卡入模型的情况。
- `StayOnGround` 通过 prologue 反推定位，虽然实测有效，但仍属于版本敏感资产。
- 速度恢复依赖采样窗口，极端输入切换可能还需要更多保护。

## 下一步建议

1. 清理日志噪声，把高频帧日志改成调试开关或只在 portal phase 内限频输出。
2. 将 `WalkMoveNudge` 从“后处理硬推”逐步升级为更正式的 movement result 合成：
   - 使用 intended end / accepted trace / portal plane projection 计算目标位移。
   - 更严格地区分入口推进、跨 plane 提交、出口释放。
3. 增加出口空间检查，避免传送后卡进门后模型或实体。
4. 扩展到非玩家实体前，先抽象 `PortalPlayerTeleport` 为更通用的 `PortalEntityTeleport`。
5. 继续补全 `TestPlayerPosition`、client `CategorizePosition` 等接口资产，但不建议在当前已经跑通的主干里盲目加入。
6. 添加 cvar 控制：
   - 是否启用 traversal debug log。
   - 是否启用 WalkMoveNudge。
   - 是否启用 predicted crossing。
   - 是否启用 delayed view angle sync。

## 提交建议

本阶段可以作为一次“重大进展”提交，建议提交主题：

```text
Implement player portal traversal movement bridge
```

提交范围建议包含：

- movement hook 与 CMoveData 同步逻辑。
- portal collision bridge。
- portal transition simulator。
- server Teleport 封装。
- transition decision helper。
- project 文件中新增源文件引用。
- 本文档。

不建议把无关 brainstorm 文档、空 diff 的行尾变化、与当前穿越主线无关的实验文件混入同一次提交。
