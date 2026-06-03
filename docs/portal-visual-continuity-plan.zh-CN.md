# 传送门视觉连续性实施方案

**功能:** 基于当前本地物理穿越稳定版，继续改善传送前后的视角连续性。

**目标:** 保留已经验证稳定的本地传送门物理穿越链路，在不重新引入卡墙、穿墙异常或崩溃风险的前提下，减少 teleport 当帧附近的相机突变。

**验收标准:**
- 玩家在 localserver 中穿越传送门时不崩溃。
- 玩家可以进入传送门、中心点穿过门平面、通过 server-side entity teleport 到出口，并顺利离开出口门。
- 入口墙面闪烁帧保持消除。
- 传送后的第一帧不再明显像“已经向出口前方多走了一截”。
- 新增视觉连续性机制可以被开关或禁用，方便 Debug。
- 控制台日志能明确区分物理穿越阶段、视觉补偿阶段和 fallback 行为。

**架构:** 保留当前混合式穿越实现：高层状态机参考 `PortalGun_Gmod_gmpublisher` 的 `InPortal` 思路，包括进入门内状态、跨平面判定和出口恢复；底层落地使用本工程已验证可行的 L4D2 Trace bypass、server-side vtable teleport 和 assisted embedding。下一阶段新增的是 teleport 前后的视觉连续性层，而不是继续通过压低玩家物理出口位置来硬凑视觉效果。

**技术栈:** C++17、现有 PortalTransform 数学、项目已有 Source/L4D2 hook、当前 Logger、通过 `IServerTools`/server entity vtable 获取 server-side local player 并执行 teleport。

---

## 当前基线

当前分支已经具备一版稳定的物理穿越基线：

- 玩家有意向传送门移动，并处于有效门洞范围内时，进入 `InPortal` 状态。
- 普通 hull 碰撞阻止玩家中心点抵达门平面时，通过 assisted embedding 小步推进玩家。
- teleport 触发条件是玩家中心点穿过门平面，而不是 hull 刚碰到墙面。
- 如果 assisted embedding 预测下一步会跨过门平面，会在同一帧直接 teleport，避免先渲染一帧入口墙面。
- 使用 server-side vtable teleport，因为 client entity vtable teleport 曾触发调用约定/ESP 错误。
- 出口 clearance 已恢复为稳定值：`finalExitEyeD ~= 32`，用于降低卡墙概率。

这一版不是纯照搬 GMod Lua 方案，而是混合方案：

- **参考部分:** `PortalGun_Gmod_gmpublisher` 的状态机思想，包括 `InPortal`、临时 noclip、门洞约束、跨平面后转换、离开出口后恢复。
- **适配部分:** 由于本工程没有 GMod Lua API，且当前 `CMoveData::m_vecAbsOrigin` 不可靠，所以实际实现改为 Trace bbox bypass、server-side vtable teleport、小步推进和中心点 crossing 检测。

## 当前问题

降低出口 clearance 可以让传送后的视角更贴近出口门面，看起来更连续；但实测证明 clearance 太低时容易卡墙。

保持稳定 clearance 可以避免卡墙，但 teleport 后的第一帧会感觉玩家已经离出口门较远，也就是“往前多了一点”。

因此，下一阶段不应该继续把玩家物理位置作为唯一调参手段。更合理的方向是：物理位置保持稳定，视觉上做短暂相机/视图补偿。

## 不做的内容

- 暂不实现实体克隆。
- 暂不实现完整递归视觉穿越或玩家身体分割渲染。
- 不依赖当前明显异常的 `CMoveData::m_vecAbsOrigin`。
- 除非确认更安全的 engine API，否则不替换已验证的 server-side teleport 路径。
- 不再把降低出口 clearance 作为主要解决方案。

## 方案方向

### 阶段 3A: Teleport 当帧诊断

**涉及文件:**
- 修改: `src/Portal/PortalTransition.h`
- 修改: `src/Portal/PortalTransition.cpp`
- 可选修改: 选定最终视图 hook 后，修改 `src/Hooks/RenderView/` 或 `src/Hooks/ClientMode/` 下的相关文件

**实现内容:**
- 新增一个小型 `VisualTransitionState`，记录：
  - 入口 portal side
  - 出口 portal side
  - teleport 前 eye position
  - crossing 预测 eye position
  - 物理 teleport 后 eye position
  - 转换前后的 view angles
  - 开始时间和过期时间
- 每次成功 teleport 时打印物理出口 offset 和期望视觉 offset。

**验证方式:**
- Debug x86 编译通过。
- 游戏内每次成功穿越时，控制台出现一条视觉过渡状态日志。
- 这一阶段不改变实际视觉行为，只确认数据链路。

### 阶段 3B: 单帧视图位置补偿

**涉及文件:**
- 修改: `src/Portal/PortalTransition.h`
- 修改: `src/Portal/PortalTransition.cpp`
- 修改: 选定的 view setup hook，优先调查 `src/Hooks/RenderView/` 或 `src/Hooks/ClientMode/`

**实现内容:**
- 玩家物理 teleport 仍然落在稳定出口 clearance，例如 `finalExitEyeD ~= 32`。
- teleport 后第一个渲染帧，对相机位置做视觉补偿：沿出口 normal 的反方向轻微拉回，让视觉上更接近门面。
- 初始建议参数：
  - 物理出口 clearance: `32`
  - 视觉目标 clearance: `10` 到 `16`
  - 补偿持续时间: 1 个渲染帧，或最多 `0.03s`
- 补偿必须 clamp，不能把相机拉到出口门平面后方。
- 加本地常量或调试 cvar，用于禁用视觉补偿。

**验证方式:**
- Debug x86 编译并复制 DLL。
- 游戏内反复测试蓝门到橙门、橙门到蓝门。
- 预期：
  - 不会卡墙。
  - 没有入口墙面闪烁帧。
  - teleport 后第一帧更接近门面，视觉上不再像多走了一截。
- 日志能看到视觉补偿被应用并过期。

### 阶段 3C: 短时间平滑过渡

**涉及文件:**
- 同阶段 3B

**实现内容:**
- 如果单帧补偿仍然显得突兀，把视觉 offset 从目标视觉 clearance 平滑插值回物理 clearance。
- 初始持续时间建议 `0.05s` 到 `0.10s`。
- 先使用线性插值；只有线性看起来不自然时再考虑 easing。
- 物理玩家位置保持不变，只调整渲染视图。

**验证方式:**
- 对比单帧补偿和短平滑补偿。
- 游戏内验收标准：
  - 没有明显闪帧。
  - 没有明显“多往前走一截”的感觉。
  - 相机不会穿进出口墙面。
  - 玩家控制没有延迟感。

### 阶段 3D: 出口门洞碰撞宽限复核

**涉及文件:**
- 修改: `src/Portal/PortalTransition.cpp`

**实现内容:**
- 复核当前 `ExitingPortal` 状态下的出口门洞 trace bypass。
- 如果它能防止边缘卡墙，就保留。
- 如果它允许玩家穿过出口周围非门洞墙体，就收窄条件。

**验证方式:**
- 在平面墙、靠近角落的墙、不同高度/朝向的门上测试。
- 确认玩家无法从门洞旁边穿过普通墙面。

## 风险和待确认问题

- 需要确认哪个 hook 能稳定修改最终本地相机 origin，而不是被后续预测或渲染流程覆盖。
- 如果 view hook 执行时机早于 teleport 状态更新，可能需要提前记录视觉状态，或换到更晚的渲染 hook。
- 武器 viewmodel 是否要跟随视觉补偿需要实测；如果只移动世界相机而 viewmodel 不动，可能出现轻微违和。
- 如果 L4D2 prediction 在我们的 hook 后又重写 view origin，需要把补偿移动到更靠后的渲染流程。

## 编码前审阅清单

- 确认最终本地相机 origin 由哪个 view hook 决定。
- 确认 weapon viewmodel 是否需要同步视觉 offset。
- 选择初始视觉目标 clearance：`16` 更保守，`10` 更接近之前贴门测试效果。
- 决定第一版使用单帧补偿，还是直接做 `0.05s` 左右短平滑。

## 建议的第一轮编码

1. 新增 `VisualTransitionState` 和日志，不改变行为。
2. 编译并确认游戏内穿越行为不变。
3. 在本地常量开关保护下，实现单帧 view compensation。
4. 编译复制 DLL，进行游戏内实测。
5. 如果单帧补偿仍突兀，再扩展为短时间平滑。

