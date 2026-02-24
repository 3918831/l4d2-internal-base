# 传送门动画系统重构文档

## 概述

本文档记录了传送门动画系统的架构重构，将原本分散的打开/关闭动画逻辑统一为单一状态机管理模式。

**重构日期**: 2026-02-24

---

## 问题背景

### 原有架构问题

在重构前，传送门动画系统存在以下架构缺陷：

| 问题 | 描述 | 影响 |
|------|------|------|
| **逻辑分散** | 打开动画逻辑嵌入在 `DrawModelExecute` 中，关闭动画使用独立函数 | 难以统一管理 |
| **状态竞争** | `DrawModelExecute` 每帧无条件设置 `bIsActive = true`，与关闭动画的 `bIsActive = false` 冲突 | 关闭动画抽搐 |
| **重复触发** | `Reload::Detour` 在换弹期间每帧被调用，导致关闭动画重复启动 | 动画异常 |
| **状态混乱** | 使用 `isAnimating` + `bIsClosing` 组合来区分动画类型 | 代码复杂度高 |

### 症状表现

1. 单独创建橙门后按 R 键关闭，传送门会抽搐约 1 秒
2. 控制台大量打印 `Pistol::Reload::Detour is called.`
3. 关闭动画无法正常完成

---

## 解决方案

### 核心设计：统一动画状态机

引入明确的动画状态枚举 `EPortalAnimState`，替代原有的 `isAnimating` + `bIsClosing` 组合。

```
┌─────────────────────────────────────────────────────────────────┐
│                      传送门动画状态机                            │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│   [IDLE] ──(创建传送门)──▶ [OPENING] ──(动画完成)──▶ [OPEN]    │
│     ▲                          │                      │        │
│     │                          ▼                      │        │
│     │                     (scale: 0→1)                │        │
│     │                                                 │        │
│     │                      (按R键)                    ▼        │
│     │                                            [CLOSING]     │
│     │                                                 │        │
│     │                                             (scale: 1→0) │
│     │                                                 │        │
│     └───────────────────(动画完成)──────────────▶ [CLOSED]    │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### 状态定义

```cpp
enum EPortalAnimState
{
    PORTAL_ANIM_IDLE = 0,      // 空闲，无动画
    PORTAL_ANIM_OPENING,       // 正在打开 (scale: 0→1)
    PORTAL_ANIM_OPEN,          // 完全打开 (scale = 1.0)
    PORTAL_ANIM_CLOSING,       // 正在关闭 (scale: 1→0)
    PORTAL_ANIM_CLOSED,        // 已关闭 (scale = 0.0)
};
```

---

## 代码变更

### 1. 新增状态枚举 (`L4D2_Portal.h`)

```cpp
// 替代 isAnimating + bIsClosing 组合
enum EPortalAnimState
{
    PORTAL_ANIM_IDLE = 0,
    PORTAL_ANIM_OPENING,
    PORTAL_ANIM_OPEN,
    PORTAL_ANIM_CLOSING,
    PORTAL_ANIM_CLOSED,
};

// PortalInfo_t 结构体新增字段
struct PortalInfo_t
{
    // ... 原有字段 ...

    EPortalAnimState animState = PORTAL_ANIM_IDLE;  // 新增：统一动画状态
};
```

### 2. 新增管理函数 (`L4D2_Portal.h`)

```cpp
class L4D2_Portal {
public:
    // 触发打开动画（从 FirePortal 调用）
    void StartPortalOpenAnimation(PortalInfo_t* pPortal, const Vector& newPosition);

    // 触发关闭动画（从 Reload::Detour 调用）
    void StartPortalCloseAnimation(PortalInfo_t* pPortal);

    // 检查是否可以开始新动画
    bool CanStartAnimation(const PortalInfo_t* pPortal) const;
};
```

### 3. 打开动画触发 (`L4D2_Portal.cpp`)

```cpp
void L4D2_Portal::StartPortalOpenAnimation(PortalInfo_t* pPortal, const Vector& newPosition)
{
    if (!pPortal || !I::EngineClient) return;

    // 如果正在关闭，不允许打开
    if (pPortal->animState == PORTAL_ANIM_CLOSING) return;

    pPortal->origin = newPosition;
    pPortal->lastOrigin = newPosition;
    pPortal->animState = PORTAL_ANIM_OPENING;
    pPortal->isAnimating = true;
    pPortal->bIsActive = true;
    pPortal->bIsClosing = false;
    pPortal->currentScale = 0.0f;
    pPortal->animStartTime = I::EngineClient->OBSOLETE_Time();
}
```

### 4. 关闭动画触发 (`L4D2_Portal.cpp`)

```cpp
void L4D2_Portal::StartPortalCloseAnimation(PortalInfo_t* pPortal)
{
    // 关键修复：检查是否已在关闭状态，防止重复触发
    if (!pPortal || !pPortal->bIsActive ||
        pPortal->currentScale <= 0.0f ||
        pPortal->animState == PORTAL_ANIM_CLOSING) {
        return;
    }

    pPortal->animState = PORTAL_ANIM_CLOSING;
    pPortal->bIsClosing = true;
    pPortal->isAnimating = true;
    pPortal->closeAnimStartTime = I::EngineClient->OBSOLETE_Time();
}
```

### 5. 统一动画更新 (`L4D2_Portal.cpp`)

```cpp
bool L4D2_Portal::UpdatePortalScaleAnimation(PortalInfo_t* pPortal, const Vector& currentPos, C_BaseAnimating* pEntity)
{
    // ... 前置检查 ...

    switch (pPortal->animState) {
        case PORTAL_ANIM_CLOSING:
        {
            // 关闭动画: 1.0 → 0.0 (线性)
            float flElapsedTime = flCurrentTime - pPortal->closeAnimStartTime;
            float t = flElapsedTime / pPortal->closeAnimDuration;
            float scale = 1.0f - t;

            if (scale < 0.0f) scale = 0.0f;
            pPortal->currentScale = scale;

            // 动画完成
            if (t >= 1.0f) {
                pPortal->currentScale = 0.0f;
                pPortal->animState = PORTAL_ANIM_CLOSED;
                pPortal->bIsActive = false;
                pPortal->bIsClosing = false;
            }
            break;
        }

        case PORTAL_ANIM_OPENING:
        {
            // 打开动画: 0.0 → 1.0 (可配置曲线)
            float flElapsedTime = flCurrentTime - pPortal->animStartTime;
            float t = flElapsedTime / pPortal->animDuration;

            // 使用 ScaleCurves 计算缩放值
            pPortal->currentScale = ScaleCurves::Calculate(pPortal->openAnimType, t);

            if (t >= 1.0f) {
                pPortal->currentScale = 1.0f;
                pPortal->animState = PORTAL_ANIM_OPEN;
                pPortal->isAnimating = false;
            }
            break;
        }

        case PORTAL_ANIM_IDLE:
        default:
            // 兼容旧逻辑：通过位置变化检测触发打开动画
            // ...
            break;
    }

    // 应用缩放到实体
    // ...
}
```

### 6. 简化 DrawModelExecute (`ModelRender.cpp`)

```cpp
// 修改前：每帧无条件设置 bIsActive
if (isBluePortal) {
    G::G_L4D2Portal.g_BluePortal.bIsActive = true;  // 问题代码
    G::G_L4D2Portal.UpdatePortalScaleAnimation(...);
}

// 修改后：不再管理 bIsActive，完全由动画系统控制
if (isBluePortal) {
    G::G_L4D2Portal.UpdatePortalScaleAnimation(&G::G_L4D2Portal.g_BluePortal, pInfo.origin, pEntity);
    // bIsActive 由动画系统自动管理
}
```

### 7. FirePortal 调用打开动画 (`weapon_portalgun.cpp`)

```cpp
void CWeaponPortalgun::FirePortal(bool bPortal2, ...)
{
    // ... 创建传送门 ...

    // 触发打开动画（统一管理 bIsActive 和缩放动画）
    G::G_L4D2Portal.StartPortalOpenAnimation(&portalInfo, vFinalPosition);

    // ...
}
```

---

## 修复的关键 Bug

### Bug 1: 关闭动画重复触发

**原因**: `Reload::Detour` 在换弹期间每帧被调用，`StartPortalCloseAnimation` 没有检查是否已在关闭状态。

**修复**: 添加 `animState == PORTAL_ANIM_CLOSING` 检查。

```cpp
// 修复前
if (!pPortal || !pPortal->bIsActive || pPortal->currentScale <= 0.0f) {
    return;
}

// 修复后
if (!pPortal || !pPortal->bIsActive ||
    pPortal->currentScale <= 0.0f ||
    pPortal->animState == PORTAL_ANIM_CLOSING) {  // 新增检查
    return;
}
```

### Bug 2: 状态竞争导致抽搐

**原因**: `DrawModelExecute` 每帧设置 `bIsActive = true`，与关闭动画的 `bIsActive = false` 冲突。

**修复**: 移除 `DrawModelExecute` 中的 `bIsActive` 设置，完全由动画状态机管理。

---

## 文件变更清单

| 文件 | 变更类型 | 说明 |
|------|---------|------|
| `src/Portal/L4D2_Portal.h` | 修改 | 新增 `EPortalAnimState` 枚举，添加新函数声明 |
| `src/Portal/L4D2_Portal.cpp` | 修改 | 实现统一动画管理，重构 `UpdatePortalScaleAnimation` |
| `src/Hooks/ModelRender/ModelRender.cpp` | 修改 | 移除 `bIsActive` 设置 |
| `src/Portal/client/weapon_portalgun.cpp` | 修改 | 调用 `StartPortalOpenAnimation` |

---

## 动画参数配置

编辑 `src/Portal/L4D2_Portal.h` 中的 `PortalInfo_t` 结构体：

```cpp
struct PortalInfo_t
{
    // 打开动画配置
    EScaleAnimationType openAnimType = SCALE_ELASTIC;  // 打开动画曲线类型
    float animDuration = 0.5f;                         // 打开动画时长（秒）

    // 关闭动画配置
    float closeAnimDuration = 0.3f;                    // 关闭动画时长（秒）

    // ... 其他字段 ...
};
```

### 可用的打开动画曲线

| 曲线类型 | 效果 |
|---------|------|
| `SCALE_LINEAR` | 线性匀速 |
| `SCALE_EASE_IN` | 缓入（慢→快） |
| `SCALE_EASE_OUT` | 缓出（快→慢） |
| `SCALE_EASE_IN_OUT` | 缓入缓出 |
| `SCALE_ELASTIC` | 弹性回弹（默认） |

### 关闭动画

关闭动画固定使用线性曲线，确保视觉上平滑消失。

---

## 测试验证

### 测试场景

| 场景 | 预期结果 | 状态 |
|------|---------|------|
| 创建蓝门 | 看到弹性打开动画 (0→1) | ✅ |
| 创建橙门 | 看到弹性打开动画 (0→1) | ✅ |
| 只有一扇门时按 R | 只有该门执行关闭动画 (1→0) | ✅ |
| 两扇门时按 R | 两门同时关闭 | ✅ |
| 关闭动画进行中开新门 | 被阻止 | ✅ |
| 换弹期间动画不抽搐 | 平滑关闭 | ✅ |

---

## 架构优势

1. **单一职责**: 动画状态完全由 `L4D2_Portal` 类管理
2. **状态明确**: 使用枚举而非布尔组合，状态转换清晰
3. **易于扩展**: 添加新动画状态只需扩展枚举和 switch
4. **防止竞争**: 状态检查防止重复触发
5. **易于调试**: 状态机模式便于追踪问题

---

## 后续优化建议

1. **状态转换日志**: 添加状态转换时的调试日志
2. **动画队列**: 支持动画队列，允许排队等待
3. **事件回调**: 动画完成时触发回调函数
4. **配置热更新**: 支持运行时修改动画参数

---

*文档版本: 1.0*
*最后更新: 2026-02-24*
