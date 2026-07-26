# 传送瞬间相机接续不连续诊断

## 现象

启用 portal-aware near clip 后，墙体条带与 viewmodel 裁剪已消失，但缓慢穿越时仍能感觉到 Teleport 前后画面位置不连续。高速穿越的速度继承基本连续。

## 根因假设

入口最后一帧的传送门画面使用“入口相机经门对变换后的出口眼位”，并仅沿出口法线做约 1 unit 的渲染偏移。物理 Teleport 当前还会为 hull clearance 沿出口法线推送玩家：已测 `FullHull` 从原始出口深度 `-1.5` 推到 `+18`，形成 `19.5` units 的位置差；`PlaneEpsilon` 推到 `+2`，形成 `3.5` units 的位置差。因此最后一帧门内画面与 Teleport 后第一帧主视图并非来自同一观察点。

## 本轮最小验证

- 增加 `portal_bsp_clearance exact`：完成入口到出口变换后不施加任何出口 push。
- 保持 near clip 修复、跨面预测触发、Teleport、速度与视角变换不变，避免同时改变多个变量。
- 增加 `[PortalContinuity]` 文件日志，记录精确映射眼位、约 1 unit 的传送门渲染眼位、物理眼位及两组误差。
- 保留 `ExitingPortal` 空间重武装锁：玩家到达出口前方 16 units 或离开孔径前不得再次入门。
- 空间解锁后重新开始 0.20 秒冷却，并在策略层拒绝 `CommittingTeleport`/`ExitingPortal` 期间的新入口，防止零 push 导致双门反复传送。

## 边界

本轮不修复高度差、地面嵌入、传送门放置限制、brush 约束、blocker 或跨面触发时机。`ExactTransform` 是因果诊断模式；若物理眼位仍位于墙体视觉几何之后而产生新遮挡，需要依据实测再决定最终接续策略。

## 实现侧验证

- 先建立缺失 `ExactTransform`、缺失连续性日志分类和缺失解锁后冷却计算的 RED 测试，再完成最小实现使其转绿。
- 11 个 `tests/run_*.cmd` 独立测试脚本全部通过。
- Debug x86 解决方案构建通过，0 个错误；保留 `convar.cpp` 中 5 个既有 C4355 警告。
- 游戏内画面连续性与防反复传送仍需本轮人工实测验收。

## 第十一轮证据与修正

第十一轮已经确认黑天空消失，但慢速和高速穿越仍会在 Teleport 前看到入口墙体或墙后内容。日志显示问题窗口发生在相机越过入口平面之后、物理移动命令提交 Teleport 之前：慢速样本约持续 27–53 ms，高速样本同样可跨越多个渲染帧。因此，根因不是 `Teleport` API 或出口半空间再次错误，而是项目缺少官方客户端在 `CalcPortalView` 中执行的 Teleport 前相机交接。

本轮以单因子方式补入该交接：`CalcPlayerView` 得到原始本地眼位后，只有在 `IntersectingPortal`、入口深度为负且眼位仍在孔径内时，才通过现有门对矩阵变换渲染眼位与角度。玩家实体位置、速度、Teleport 提交、出口落点、near clip、exit guard 和防重入逻辑均不改变。`portal_visual_entryhandoff 0|1` 用于独立 A/B；生效帧写入 `[PortalEntryViewHandoff]` 文件日志。插值历史和轻微顿挫不属于本轮修复范围。
